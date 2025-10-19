#include "audio_player.h"

#include <algorithm>
#include <chrono>
#include <cstring>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

#include "player/common/log_manager.h"
#include "player/common/timer_util.h"
#include "player/stats/statistics_manager.h"

namespace zenplay {

AudioPlayer::AudioPlayer(PlayerStateManager* state_manager,
                         AVSyncController* sync_controller)
    : state_manager_(state_manager),
      sync_controller_(sync_controller),
      swr_context_(nullptr),
      resampled_data_(nullptr),
      resampled_data_size_(0),
      max_resampled_samples_(0),
      buffer_read_pos_(0),
      src_sample_rate_(0),
      src_channels_(0),
      src_format_(AV_SAMPLE_FMT_NONE),
      format_initialized_(false),
      last_fill_had_real_data_(false) {}

AudioPlayer::~AudioPlayer() {
  Cleanup();
}

bool AudioPlayer::Init(const AudioConfig& config) {
  config_ = config;
  target_sample_rate_ = config.target_sample_rate;  // 保存目标采样率用于PTS计算

  // 配置音频输出规格
  output_spec_.sample_rate = config_.target_sample_rate;
  output_spec_.channels = config_.target_channels;
  output_spec_.bits_per_sample = config_.target_bits_per_sample;
  output_spec_.buffer_size = config_.buffer_size;
  output_spec_.format = config_.target_format;

  // 创建音频输出设备
  audio_output_ = AudioOutput::Create();
  if (!audio_output_) {
    MODULE_ERROR(LOG_MODULE_AUDIO, "Failed to create audio output device");
    return false;
  }

  // 初始化音频输出设备
  zenplay::AudioOutputCallback callback = &AudioPlayer::AudioOutputCallback;
  MODULE_INFO(LOG_MODULE_AUDIO, "Setting up audio callback, this={}",
              (void*)this);

  if (!audio_output_->Init(output_spec_, callback, this)) {
    MODULE_ERROR(LOG_MODULE_AUDIO, "Failed to initialize audio output device");
    return false;
  }

  // 预分配内部缓冲区
  int buffer_size_bytes = config_.buffer_size * config_.target_channels *
                          (config_.target_bits_per_sample / 8);
  internal_buffer_.resize(buffer_size_bytes * 4);  // 4x缓冲以避免underrun

  MODULE_INFO(LOG_MODULE_AUDIO,
              "Audio player initialized: {}Hz, {} channels, {} bits",
              config_.target_sample_rate, config_.target_channels,
              config_.target_bits_per_sample);

  return true;
}

bool AudioPlayer::Start() {
  MODULE_INFO(LOG_MODULE_AUDIO, "AudioPlayer Start called");

  if (!audio_output_->Start()) {
    MODULE_ERROR(LOG_MODULE_AUDIO, "Failed to start audio output");
    return false;
  }

  MODULE_INFO(LOG_MODULE_AUDIO, "Audio playback started");
  return true;
}

void AudioPlayer::Stop() {
  MODULE_INFO(LOG_MODULE_AUDIO, "AudioPlayer Stop called");

  // ✅ 唤醒所有等待的解码线程
  frame_consumed_.notify_all();

  // 停止音频输出
  if (audio_output_) {
    audio_output_->Stop();
  }

  // 清空队列
  ClearFrames();

  MODULE_INFO(LOG_MODULE_AUDIO, "Audio playback stopped");
}

void AudioPlayer::Pause() {
  if (audio_output_) {
    audio_output_->Pause();
  }
  MODULE_INFO(LOG_MODULE_AUDIO, "AudioPlayer paused");
}

void AudioPlayer::Resume() {
  if (audio_output_) {
    audio_output_->Resume();
  }
  MODULE_INFO(LOG_MODULE_AUDIO, "AudioPlayer resumed");
}

void AudioPlayer::SetVolume(float volume) {
  if (audio_output_) {
    audio_output_->SetVolume(volume);
  }
}

float AudioPlayer::GetVolume() const {
  if (audio_output_) {
    return audio_output_->GetVolume();
  }
  return 0.0f;
}

bool AudioPlayer::PushFrame(AVFramePtr frame, const FrameTimestamp& timestamp) {
  if (!frame || state_manager_->ShouldStop()) {
    MODULE_DEBUG(LOG_MODULE_AUDIO,
                 "PushFrame rejected: frame={}, should_stop={}",
                 (void*)frame.get(), state_manager_->ShouldStop());
    return false;
  }

  std::lock_guard<std::mutex> lock(frame_queue_mutex_);

  // 检查队列大小，避免内存过度使用
  if (frame_queue_.size() >= MAX_QUEUE_SIZE) {
    // 丢弃最老的帧
    frame_queue_.pop();
    MODULE_WARN(LOG_MODULE_AUDIO, "Audio queue full, dropping oldest frame");
  }

  // ✅ 创建 MediaFrame 并入队 (与 VideoPlayer 保持一致)
  auto media_frame = std::make_unique<MediaFrame>(std::move(frame), timestamp);
  frame_queue_.push(std::move(media_frame));

  MODULE_DEBUG(LOG_MODULE_AUDIO,
               "Audio frame pushed: pts={:.3f}s, queue_size={}",
               timestamp.ToSeconds(), frame_queue_.size());

  return true;
}

bool AudioPlayer::PushFrameTimeout(AVFramePtr frame,
                                   const FrameTimestamp& timestamp,
                                   int timeout_ms) {
  if (!frame || state_manager_->ShouldStop()) {
    return false;
  }

  std::unique_lock<std::mutex> lock(frame_queue_mutex_);

  // ✅ 等待队列有空间（使用条件变量替代 sleep 轮询）
  if (timeout_ms > 0) {
    bool success = frame_consumed_.wait_for(
        lock, std::chrono::milliseconds(timeout_ms), [this] {
          return state_manager_->ShouldStop() ||
                 frame_queue_.size() < MAX_QUEUE_SIZE;
        });

    if (!success || state_manager_->ShouldStop()) {
      return false;  // 超时或停止
    }
  } else {
    // timeout_ms == 0，非阻塞模式
    if (frame_queue_.size() >= MAX_QUEUE_SIZE) {
      return false;
    }
  }

  auto media_frame = std::make_unique<MediaFrame>(std::move(frame), timestamp);
  frame_queue_.push(std::move(media_frame));

  MODULE_DEBUG(LOG_MODULE_AUDIO,
               "Audio frame pushed (timeout): pts={:.3f}s, queue_size={}",
               timestamp.ToSeconds(), frame_queue_.size());

  return true;
}

void AudioPlayer::ClearFrames() {
  std::lock_guard<std::mutex> lock(frame_queue_mutex_);
  std::queue<std::unique_ptr<MediaFrame>> empty_queue;
  frame_queue_.swap(empty_queue);

  // ✅ 清空内部缓冲区，避免 Seek 后播放旧数据
  internal_buffer_.clear();
  buffer_read_pos_ = 0;

  // 重置PTS跟踪状态
  {
    std::lock_guard<std::mutex> pts_lock(pts_mutex_);
    current_base_pts_seconds_ = 0.0;
    samples_played_since_base_ = 0;
  }

  // ✅ 清空后通知等待的生产者：现在有大量空间了
  frame_consumed_.notify_all();

  MODULE_DEBUG(LOG_MODULE_AUDIO, "Audio frames and internal buffer cleared");
}

void AudioPlayer::Flush() {
  // 1. 清空硬件层缓冲区
  if (audio_output_) {
    audio_output_->Flush();
  }

  MODULE_INFO(LOG_MODULE_AUDIO, "AudioPlayer flushed (hardware only)");
}

void AudioPlayer::ResetTimestamps() {
  std::lock_guard<std::mutex> lock(pts_mutex_);

  // 重置PTS基准和采样计数
  current_base_pts_seconds_ = 0.0;
  samples_played_since_base_ = 0;

  MODULE_INFO(LOG_MODULE_AUDIO, "AudioPlayer timestamps reset");
}

double AudioPlayer::GetCurrentPlaybackPTS() const {
  std::lock_guard<std::mutex> lock(pts_mutex_);

  if (current_base_pts_seconds_ < 0) {
    return -1.0;  // 尚未开始播放
  }

  // 根据已播放的采样数计算经过的时间
  double elapsed_seconds =
      static_cast<double>(samples_played_since_base_) / target_sample_rate_;

  double current_pts_seconds = current_base_pts_seconds_ + elapsed_seconds;

  return current_pts_seconds * 1000.0;  // 转换为毫秒
}

bool AudioPlayer::IsPlaying() const {
  auto state = state_manager_->GetState();
  return state == PlayerStateManager::PlayerState::kPlaying ||
         state == PlayerStateManager::PlayerState::kPaused;
}

size_t AudioPlayer::GetQueueSize() const {
  std::lock_guard<std::mutex> lock(frame_queue_mutex_);
  return frame_queue_.size();
}

void AudioPlayer::Cleanup() {
  Stop();

  // 清理重采样器
  if (swr_context_) {
    swr_free(&swr_context_);
    swr_context_ = nullptr;
  }

  // 清理重采样缓冲区
  if (resampled_data_) {
    av_freep(&resampled_data_[0]);
    av_freep(&resampled_data_);
    resampled_data_size_ = 0;
    max_resampled_samples_ = 0;
  }

  // 清理音频输出
  if (audio_output_) {
    audio_output_->Cleanup();
    audio_output_.reset();
  }
}

int AudioPlayer::AudioOutputCallback(void* user_data,
                                     uint8_t* buffer,
                                     int buffer_size) {
  static int call_count = 0;
  if (++call_count % 100 == 0) {
    MODULE_DEBUG(LOG_MODULE_AUDIO,
                 "AudioOutputCallback called {} times, buffer_size={}",
                 call_count, buffer_size);
  }

  AudioPlayer* player = static_cast<AudioPlayer*>(user_data);

  TIMER_START(audio_render);
  int bytes_filled = player->FillAudioBuffer(buffer, buffer_size);
  auto render_time_ms = TIMER_END_MS(audio_render);

  // ✅ 只有真正渲染了音频数据才更新统计（不包括静音填充）
  bool audio_rendered = player->last_fill_had_real_data_;
  STATS_UPDATE_RENDER(false, audio_rendered, false, render_time_ms);

  // ✅ 查询当前播放位置的 PTS (基于采样数精确计算)
  double current_pts_ms = player->GetCurrentPlaybackPTS();

  if (bytes_filled > 0 && current_pts_ms >= 0 && player->sync_controller_) {
    auto current_time = std::chrono::steady_clock::now();

    // ✅ 传递精确计算的当前 PTS
    player->sync_controller_->UpdateAudioClock(current_pts_ms, current_time);
  }

  return bytes_filled;
}

bool AudioPlayer::InitializeResampler(const AVFrame* frame) {
  if (format_initialized_) {
    return true;  // 已经初始化
  }

  src_sample_rate_ = frame->sample_rate;
  src_channels_ = frame->ch_layout.nb_channels;
  src_format_ = static_cast<AVSampleFormat>(frame->format);

  MODULE_INFO(LOG_MODULE_AUDIO,
              "Initializing resampler: {}Hz -> {}Hz, {} -> {} channels, format "
              "{} -> {}",
              src_sample_rate_, config_.target_sample_rate, src_channels_,
              config_.target_channels, av_get_sample_fmt_name(src_format_),
              av_get_sample_fmt_name(config_.target_format));

  // 分配重采样上下文
  swr_context_ = swr_alloc();
  if (!swr_context_) {
    MODULE_ERROR(LOG_MODULE_AUDIO, "Failed to allocate resampler context");
    return false;
  }

  // 设置重采样参数
  AVChannelLayout src_ch_layout, dst_ch_layout;
  av_channel_layout_default(&src_ch_layout, src_channels_);
  av_channel_layout_default(&dst_ch_layout, config_.target_channels);

  av_opt_set_chlayout(swr_context_, "in_chlayout", &src_ch_layout, 0);
  av_opt_set_int(swr_context_, "in_sample_rate", src_sample_rate_, 0);
  av_opt_set_sample_fmt(swr_context_, "in_sample_fmt", src_format_, 0);

  av_opt_set_chlayout(swr_context_, "out_chlayout", &dst_ch_layout, 0);
  av_opt_set_int(swr_context_, "out_sample_rate", config_.target_sample_rate,
                 0);
  av_opt_set_sample_fmt(swr_context_, "out_sample_fmt", config_.target_format,
                        0);

  // 初始化重采样器
  if (swr_init(swr_context_) < 0) {
    MODULE_ERROR(LOG_MODULE_AUDIO, "Failed to initialize resampler");
    swr_free(&swr_context_);
    return false;
  }

  // 分配输出缓冲区
  max_resampled_samples_ = static_cast<int>(
      av_rescale_rnd(config_.buffer_size, config_.target_sample_rate,
                     src_sample_rate_, AV_ROUND_UP));

  int ret = av_samples_alloc_array_and_samples(
      &resampled_data_, &resampled_data_size_, config_.target_channels,
      max_resampled_samples_, config_.target_format, 0);

  if (ret < 0) {
    MODULE_ERROR(LOG_MODULE_AUDIO, "Failed to allocate resampled data buffer");
    swr_free(&swr_context_);
    return false;
  }

  format_initialized_ = true;

  av_channel_layout_uninit(&src_ch_layout);
  av_channel_layout_uninit(&dst_ch_layout);

  return true;
}

int AudioPlayer::ResampleFrame(const AVFrame* frame,
                               uint8_t** output_buffer,
                               int max_output_samples) {
  if (!swr_context_) {
    return -1;
  }

  int output_samples =
      swr_convert(swr_context_, output_buffer, max_output_samples,
                  (const uint8_t**)frame->data, frame->nb_samples);

  if (output_samples < 0) {
    MODULE_ERROR(LOG_MODULE_AUDIO, "Error during resampling");
    return -1;
  }

  return output_samples;
}

int AudioPlayer::FillAudioBuffer(uint8_t* buffer, int buffer_size) {
  if (state_manager_->ShouldStop() || state_manager_->ShouldPause()) {
    // 播放静音
    memset(buffer, 0, buffer_size);
    return buffer_size;
  }

  int bytes_filled = 0;
  int bytes_per_sample =
      config_.target_channels * (config_.target_bits_per_sample / 8);

  // ✅ 记录是否从队列中实际获取了音频数据
  bool has_real_audio_data = false;

  // ✅ 检查是否需要更新 base_pts (只在 buffer 开始且没有剩余数据时)
  bool need_update_base_pts =
      (internal_buffer_.empty() && buffer_read_pos_ == 0);

  while (bytes_filled < buffer_size) {
    // 首先尝试从内部缓冲区读取
    if (buffer_read_pos_ < internal_buffer_.size()) {
      int available_bytes =
          static_cast<int>(internal_buffer_.size() - buffer_read_pos_);
      int bytes_to_copy = std::min(buffer_size - bytes_filled, available_bytes);

      memcpy(buffer + bytes_filled, internal_buffer_.data() + buffer_read_pos_,
             bytes_to_copy);
      bytes_filled += bytes_to_copy;
      buffer_read_pos_ += bytes_to_copy;

      // ✅ 累积已播放的采样数
      {
        std::lock_guard<std::mutex> lock(pts_mutex_);
        int samples_copied = bytes_to_copy / bytes_per_sample;
        samples_played_since_base_ += samples_copied;
      }

      if (buffer_read_pos_ >= internal_buffer_.size()) {
        internal_buffer_.clear();  // 清空已消费的数据
        buffer_read_pos_ = 0;
      }

      // ✅ 内部缓冲区有数据说明之前获取过真实音频
      has_real_audio_data = true;

      continue;
    }

    // 内部缓冲区为空，尝试获取新的音频帧
    std::unique_lock<std::mutex> lock(frame_queue_mutex_);
    if (frame_queue_.empty()) {
      MODULE_DEBUG(LOG_MODULE_AUDIO, "Audio queue empty, filled {} bytes",
                   bytes_filled);
      break;  // 没有更多数据
    }

    size_t queue_size_before = frame_queue_.size();
    std::unique_ptr<MediaFrame> media_frame = std::move(frame_queue_.front());
    frame_queue_.pop();

    // ✅ 通知生产者：队列有空间了
    frame_consumed_.notify_one();

    lock.unlock();

    if (!media_frame || !media_frame->frame) {
      MODULE_WARN(LOG_MODULE_AUDIO, "Got null frame from queue");
      break;  // EOF
    }

    // ✅ 只在需要时更新基准 PTS (避免多帧填充时重复重置)
    {
      std::lock_guard<std::mutex> pts_lock(pts_mutex_);

      if (need_update_base_pts) {
        // 只在 buffer 开始时设置 base_pts (一次 FillAudioBuffer 调用只设置一次)
        current_base_pts_seconds_ = media_frame->timestamp.ToSeconds();
        samples_played_since_base_ = 0;
        need_update_base_pts = false;  // 标记已更新,避免重复更新
      }
    }

    AVFrame* frame = media_frame->frame.get();

    // ✅ 标记从队列获取了真实音频帧
    has_real_audio_data = true;

    // 初始化重采样器(如果需要)
    if (!format_initialized_) {
      if (!InitializeResampler(frame)) {
        MODULE_ERROR(LOG_MODULE_AUDIO, "Failed to initialize resampler");
        break;
      }
    }

    // 重采样音频数据
    int resampled_samples =
        ResampleFrame(frame, resampled_data_, max_resampled_samples_);
    if (resampled_samples <= 0) {
      MODULE_WARN(LOG_MODULE_AUDIO, "Resample failed, samples={}",
                  resampled_samples);
      continue;
    }

    // 计算重采样后的数据大小
    int resampled_bytes = resampled_samples * bytes_per_sample;

    // 将重采样后的数据添加到内部缓冲区
    size_t old_size = internal_buffer_.size();
    internal_buffer_.resize(old_size + resampled_bytes);
    memcpy(internal_buffer_.data() + old_size, resampled_data_[0],
           resampled_bytes);
  }

  // 如果没有足够的数据，用静音填充
  if (bytes_filled < buffer_size) {
    memset(buffer + bytes_filled, 0, buffer_size - bytes_filled);
    if (!has_real_audio_data) {
      MODULE_DEBUG(LOG_MODULE_AUDIO,
                   "Filled with silence, no audio data available");
    }
    bytes_filled = buffer_size;
  }

  // ✅ 返回是否有真实音频数据的标志（通过修改返回值的语义）
  // 但为了兼容性，我们需要在调用处判断
  // 这里我们将 has_real_audio_data 存储到成员变量
  last_fill_had_real_data_ = has_real_audio_data;

  return bytes_filled;
}

}  // namespace zenplay
