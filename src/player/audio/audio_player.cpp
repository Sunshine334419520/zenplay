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

  MODULE_INFO(LOG_MODULE_AUDIO,
              "Audio player initialized: {}Hz, {} channels, {} bits",
              config_.target_sample_rate, config_.target_channels,
              config_.target_bits_per_sample);

  return true;
}

bool AudioPlayer::Start() {
  MODULE_INFO(LOG_MODULE_AUDIO, "AudioPlayer Start called");

  // ✅ 重置队列（清除stopped标志，允许Push/Pop）
  frame_queue_.Reset();

  if (!audio_output_->Start()) {
    MODULE_ERROR(LOG_MODULE_AUDIO, "Failed to start audio output");
    return false;
  }

  MODULE_INFO(LOG_MODULE_AUDIO, "Audio playback started");
  return true;
}

void AudioPlayer::Stop() {
  MODULE_INFO(LOG_MODULE_AUDIO, "AudioPlayer Stop called");

  // ✅ 停止队列（会唤醒所有阻塞的Push/Pop）
  frame_queue_.Stop();

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

bool AudioPlayer::PushFrame(ResampledAudioFrame frame) {
  if (state_manager_->ShouldStop()) {
    return false;
  }

  // ✅ 推入播放队列（BlockingQueue自动流控）
  return frame_queue_.Push(std::move(frame));
}

bool AudioPlayer::PushFrameTimeout(ResampledAudioFrame frame, int timeout_ms) {
  if (state_manager_->ShouldStop()) {
    return false;
  }

  // ✅ 带超时的推送
  if (timeout_ms > 0) {
    return frame_queue_.PushTimeout(std::move(frame), timeout_ms);
  } else {
    return frame_queue_.TryPush(std::move(frame));
  }
}

void AudioPlayer::ClearFrames() {
  // ✅ 清空播放队列
  frame_queue_.Clear([](ResampledAudioFrame& frame) {
    frame.Clear();  // 释放PCM数据
  });

  // ✅ 清空当前播放帧
  current_playback_frame_.Clear();
  current_frame_offset_ = 0;

  // ✅ 重置PTS跟踪状态
  {
    std::lock_guard<std::mutex> lock(pts_mutex_);
    current_base_pts_seconds_ = 0.0;
    samples_played_since_base_ = 0;
  }

  MODULE_INFO(LOG_MODULE_AUDIO, "Audio queue and buffer cleared");
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
  // ✅ 返回播放队列大小（重采样后的帧数）
  // 这是音频回调实际消费的队列
  return frame_queue_.Size();
}

void AudioPlayer::Cleanup() {
  Stop();

  // ✅ 停止队列（唤醒所有阻塞的线程）
  frame_queue_.Stop();

  // 清理音频输出
  if (audio_output_) {
    audio_output_->Cleanup();
    audio_output_.reset();
  }

  MODULE_INFO(LOG_MODULE_AUDIO, "AudioPlayer cleanup complete");
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

int AudioPlayer::FillAudioBuffer(uint8_t* buffer, int buffer_size) {
  // ✅ 新架构：音频回调仅负责 memcpy，不做任何计算密集操作
  // 目标延迟：<0.1ms

  if (state_manager_->ShouldStop() || state_manager_->ShouldPause()) {
    memset(buffer, 0, buffer_size);
    last_fill_had_real_data_ = false;
    return buffer_size;
  }

  int bytes_filled = 0;
  int bytes_per_sample =
      config_.target_channels * (config_.target_bits_per_sample / 8);
  bool has_real_audio_data = false;
  bool need_update_base_pts =
      current_frame_offset_ == 0 && current_playback_frame_.IsEmpty();

  while (bytes_filled < buffer_size) {
    // ✅ Step 1: 消费当前帧（部分消费支持）
    if (!current_playback_frame_.IsEmpty() &&
        current_frame_offset_ < current_playback_frame_.GetDataSize()) {
      size_t available =
          current_playback_frame_.GetDataSize() - current_frame_offset_;
      size_t to_copy = std::min<size_t>(buffer_size - bytes_filled, available);

      memcpy(buffer + bytes_filled,
             current_playback_frame_.pcm_data.data() + current_frame_offset_,
             to_copy);

      bytes_filled += to_copy;
      current_frame_offset_ += to_copy;
      has_real_audio_data = true;

      // ✅ 更新已播放采样数
      {
        std::lock_guard<std::mutex> lock(pts_mutex_);
        int samples_played = to_copy / bytes_per_sample;
        samples_played_since_base_ += samples_played;
      }

      // ✅ 当前帧已消费完毕，清理
      if (current_frame_offset_ >= current_playback_frame_.GetDataSize()) {
        current_playback_frame_.Clear();
        current_frame_offset_ = 0;
      }

      continue;
    }

    // ✅ Step 2: 从播放队列获取新的重采样帧（非阻塞）
    ResampledAudioFrame new_frame;
    if (!frame_queue_.TryPop(new_frame)) {
      // 队列空了，没有更多数据
      MODULE_DEBUG(LOG_MODULE_AUDIO, "Frame queue empty, filled {} bytes",
                   bytes_filled);
      break;
    }

    // ✅ Step 3: 更新基准PTS（仅在填充开始时）
    if (need_update_base_pts) {
      std::lock_guard<std::mutex> lock(pts_mutex_);
      current_base_pts_seconds_ = new_frame.pts_ms / 1000.0;
      samples_played_since_base_ = 0;
      need_update_base_pts = false;
    }

    // ✅ Step 4: 设置为当前帧并继续消费
    current_playback_frame_ = std::move(new_frame);
    current_frame_offset_ = 0;
    has_real_audio_data = true;
  }

  // ✅ Step 5: 不足部分填充静音
  if (bytes_filled < buffer_size) {
    memset(buffer + bytes_filled, 0, buffer_size - bytes_filled);
    if (!has_real_audio_data) {
      MODULE_DEBUG(LOG_MODULE_AUDIO, "No audio data, filling with silence");
    }
    bytes_filled = buffer_size;
  }

  last_fill_had_real_data_ = has_real_audio_data;
  return bytes_filled;
}

}  // namespace zenplay
