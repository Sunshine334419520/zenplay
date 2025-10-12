#include "audio_player.h"

#include <algorithm>
#include <chrono>
#include <cstring>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

#include "../common/log_manager.h"
#include "../common/timer_util.h"
#include "../stats/statistics_manager.h"

namespace zenplay {

AudioPlayer::AudioPlayer(PlayerStateManager* state_manager,
                         AVSyncController* sync_controller)
    : state_manager_(state_manager),
      sync_controller_(sync_controller),
      base_audio_pts_(0.0),
      total_samples_played_(0),
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

  // 通知可能在等待的线程
  frame_available_.notify_all();

  // 停止音频输出
  if (audio_output_) {
    audio_output_->Stop();
  }

  // 清空队列
  ClearFrames();

  // 重置PTS跟踪状态
  {
    std::lock_guard<std::mutex> pts_lock(pts_mutex_);
    base_audio_pts_ = 0.0;
    total_samples_played_ = 0;
  }

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
  frame_available_.notify_all();
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

bool AudioPlayer::PushFrame(AVFramePtr frame) {
  if (!frame || state_manager_->ShouldStop()) {
    MODULE_DEBUG(LOG_MODULE_AUDIO,
                 "PushFrame rejected: frame={}, should_stop={}",
                 (void*)frame.get(), state_manager_->ShouldStop());
    return false;
  }

  // ✅ 设置基础PTS（第一帧），用于音频时钟计算
  // 现在队列足够大(150帧)，不会丢帧，所以第一帧就是真正播放的第一帧
  if (!base_pts_initialized_ && frame->pts != AV_NOPTS_VALUE) {
    std::lock_guard<std::mutex> pts_lock(pts_mutex_);
    if (!base_pts_initialized_) {  // 双重检查
      base_audio_pts_ =
          static_cast<double>(frame->pts) * av_q2d(audio_time_base_);
      base_pts_initialized_ = true;
      MODULE_INFO(
          LOG_MODULE_AUDIO,
          "Audio base PTS set to: {:.3f}s (raw_pts={}, time_base={}/{})",
          base_audio_pts_, frame->pts, audio_time_base_.num,
          audio_time_base_.den);
    }
  }

  std::lock_guard<std::mutex> lock(frame_queue_mutex_);

  // 检查队列大小，避免内存过度使用
  if (frame_queue_.size() >= MAX_QUEUE_SIZE) {
    // 丢弃最老的帧
    frame_queue_.pop();
    MODULE_WARN(LOG_MODULE_AUDIO, "Audio queue full, dropping oldest frame");
  }

  frame_queue_.push(std::move(frame));
  frame_available_.notify_one();
  return true;
}

void AudioPlayer::ClearFrames() {
  std::lock_guard<std::mutex> lock(frame_queue_mutex_);
  std::queue<AVFramePtr> empty_queue;
  frame_queue_.swap(empty_queue);
  buffer_read_pos_ = 0;

  // 重置PTS跟踪状态
  {
    std::lock_guard<std::mutex> pts_lock(pts_mutex_);
    base_audio_pts_ = 0.0;
    total_samples_played_ = 0;
    base_pts_initialized_ = false;
    audio_started_ = false;
  }
}

void AudioPlayer::ResetTimestamps() {
  std::lock_guard<std::mutex> lock(pts_mutex_);

  // 重置 PTS 基准
  base_audio_pts_ = 0.0;
  total_samples_played_ = 0;
  base_pts_initialized_ = false;
  audio_started_ = false;

  MODULE_INFO(LOG_MODULE_AUDIO, "AudioPlayer timestamps reset");
}

void AudioPlayer::SetTimeBase(AVRational time_base) {
  std::lock_guard<std::mutex> lock(pts_mutex_);
  audio_time_base_ = time_base;
  MODULE_INFO(LOG_MODULE_AUDIO, "Audio time_base set to: {}/{}", time_base.num,
              time_base.den);
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

  // 更新音频时钟
  if (bytes_filled > 0 && player->sync_controller_) {
    std::lock_guard<std::mutex> pts_lock(player->pts_mutex_);

    auto current_time = std::chrono::steady_clock::now();

    // 第一次回调时记录开始时间
    if (!player->audio_started_) {
      player->audio_start_time_ = current_time;
      player->audio_started_ = true;
      MODULE_INFO(LOG_MODULE_AUDIO, "Audio clock started: base_pts={:.3f}s",
                  player->base_audio_pts_);
    }

    // ✅ 修复：使用真实播放时间计算音频时钟
    auto elapsed_time = current_time - player->audio_start_time_;
    double elapsed_seconds =
        std::chrono::duration<double>(elapsed_time).count();

    // 音频时钟 = 基准 PTS + 实际播放时间
    double current_audio_clock = player->base_audio_pts_ + elapsed_seconds;

    // 📊 定期输出调试信息
    static int clock_log_counter = 0;
    if (++clock_log_counter % 100 == 0) {
      MODULE_DEBUG(LOG_MODULE_AUDIO,
                   "Audio clock: base={:.3f}s, elapsed={:.3f}s, clock={:.3f}s "
                   "({:.2f}ms)",
                   player->base_audio_pts_, elapsed_seconds,
                   current_audio_clock, current_audio_clock * 1000.0);
    }

    // 更新同步控制器的音频时钟（转换为毫秒）
    player->sync_controller_->UpdateAudioClock(current_audio_clock * 1000.0,
                                               current_time);
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
    AVFramePtr frame = std::move(frame_queue_.front());
    frame_queue_.pop();
    lock.unlock();

    // MODULE_DEBUG(
    //     LOG_MODULE_AUDIO,
    //     "Consumed audio frame from queue (size {} -> {}), pts={},
    //     samples={}", queue_size_before, queue_size_before - 1, frame->pts,
    //     frame->nb_samples);

    if (!frame) {
      MODULE_WARN(LOG_MODULE_AUDIO, "Got null frame from queue");
      break;  // EOF
    }

    // ✅ 标记从队列获取了真实音频帧
    has_real_audio_data = true;

    // 初始化重采样器(如果需要)
    if (!format_initialized_) {
      if (!InitializeResampler(frame.get())) {
        MODULE_ERROR(LOG_MODULE_AUDIO, "Failed to initialize resampler");
        break;
      }
    }

    // 重采样音频数据
    int resampled_samples =
        ResampleFrame(frame.get(), resampled_data_, max_resampled_samples_);
    if (resampled_samples <= 0) {
      MODULE_WARN(LOG_MODULE_AUDIO, "Resample failed, samples={}",
                  resampled_samples);
      continue;
    }

    // MODULE_DEBUG(LOG_MODULE_AUDIO, "Resampled {} samples",
    // resampled_samples);

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
