#include "audio_player.h"

#include <algorithm>
#include <chrono>
#include <cstring>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

#include "../common/log_manager.h"

namespace zenplay {

AudioPlayer::AudioPlayer(AVSyncController* sync_controller)
    : sync_controller_(sync_controller),
      base_audio_pts_(0.0),
      total_samples_played_(0),
      swr_context_(nullptr),
      resampled_data_(nullptr),
      resampled_data_size_(0),
      max_resampled_samples_(0),
      is_playing_(false),
      is_paused_(false),
      should_stop_(false),
      buffer_read_pos_(0),
      src_sample_rate_(0),
      src_channels_(0),
      src_format_(AV_SAMPLE_FMT_NONE),
      format_initialized_(false) {}

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
  if (!audio_output_->Init(output_spec_, AudioOutputCallback, this)) {
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
  if (is_playing_.load()) {
    return true;
  }

  should_stop_ = false;
  is_paused_ = false;

  if (!audio_output_->Start()) {
    MODULE_ERROR(LOG_MODULE_AUDIO, "Failed to start audio output");
    return false;
  }

  is_playing_ = true;
  MODULE_INFO(LOG_MODULE_AUDIO, "Audio playback started");
  return true;
}

void AudioPlayer::Stop() {
  if (!is_playing_.load()) {
    return;
  }

  should_stop_ = true;
  is_playing_ = false;

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
  is_paused_ = true;
  if (audio_output_) {
    audio_output_->Pause();
  }
}

void AudioPlayer::Resume() {
  is_paused_ = false;
  if (audio_output_) {
    audio_output_->Resume();
  }
  frame_available_.notify_all();
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
  if (!frame || should_stop_.load()) {
    return false;
  }

  // 设置基础PTS，用于音频时钟计算
  if (base_audio_pts_ == 0.0 && frame->pts != AV_NOPTS_VALUE) {
    std::lock_guard<std::mutex> pts_lock(pts_mutex_);
    if (base_audio_pts_ == 0.0) {  // 双重检查
      base_audio_pts_ = frame->pts;
      total_samples_played_ = 0;
    }
  }

  std::lock_guard<std::mutex> lock(frame_queue_mutex_);

  // 检查队列大小，避免内存过度使用
  if (frame_queue_.size() >= MAX_QUEUE_SIZE) {
    // 丢弃最老的帧
    frame_queue_.pop();
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
  }
}

bool AudioPlayer::IsPlaying() const {
  return is_playing_.load();
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
  AudioPlayer* player = static_cast<AudioPlayer*>(user_data);
  int bytes_filled = player->FillAudioBuffer(buffer, buffer_size);

  // 更新音频时钟
  if (bytes_filled > 0 && player->sync_controller_) {
    int bytes_per_sample =
        player->config_.target_channels *
        av_get_bytes_per_sample(player->config_.target_format);
    int samples_filled = bytes_filled / bytes_per_sample;

    // 更新已播放的总采样数
    {
      std::lock_guard<std::mutex> pts_lock(player->pts_mutex_);
      player->total_samples_played_ += samples_filled;

      // 计算当前音频时钟
      double samples_per_second = player->config_.target_sample_rate;
      double current_audio_clock =
          player->base_audio_pts_ +
          (player->total_samples_played_ / samples_per_second);

      // 更新同步控制器的音频时钟（转换为毫秒）
      player->sync_controller_->UpdateAudioClock(
          current_audio_clock * 1000.0, std::chrono::steady_clock::now());
    }
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
  max_resampled_samples_ =
      av_rescale_rnd(config_.buffer_size, config_.target_sample_rate,
                     src_sample_rate_, AV_ROUND_UP);

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
  if (should_stop_.load() || is_paused_.load()) {
    // 播放静音
    memset(buffer, 0, buffer_size);
    return buffer_size;
  }

  int bytes_filled = 0;
  int bytes_per_sample =
      config_.target_channels * (config_.target_bits_per_sample / 8);

  while (bytes_filled < buffer_size) {
    // 首先尝试从内部缓冲区读取
    if (buffer_read_pos_ < internal_buffer_.size()) {
      int available_bytes = internal_buffer_.size() - buffer_read_pos_;
      int bytes_to_copy = std::min(buffer_size - bytes_filled, available_bytes);

      memcpy(buffer + bytes_filled, internal_buffer_.data() + buffer_read_pos_,
             bytes_to_copy);
      bytes_filled += bytes_to_copy;
      buffer_read_pos_ += bytes_to_copy;

      if (buffer_read_pos_ >= internal_buffer_.size()) {
        buffer_read_pos_ = 0;  // 重置缓冲区位置
      }

      continue;
    }

    // 内部缓冲区为空，尝试获取新的音频帧
    std::unique_lock<std::mutex> lock(frame_queue_mutex_);
    if (frame_queue_.empty()) {
      break;  // 没有更多数据
    }

    AVFramePtr frame = std::move(frame_queue_.front());
    frame_queue_.pop();
    lock.unlock();

    if (!frame) {
      break;  // EOF
    }

    // 初始化重采样器(如果需要)
    if (!format_initialized_) {
      if (!InitializeResampler(frame.get())) {
        break;
      }
    }

    // 重采样音频数据
    int resampled_samples =
        ResampleFrame(frame.get(), resampled_data_, max_resampled_samples_);
    if (resampled_samples <= 0) {
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
    bytes_filled = buffer_size;
  }

  return bytes_filled;
}

}  // namespace zenplay
