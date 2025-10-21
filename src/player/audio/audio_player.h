#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

extern "C" {
#include <libavutil/frame.h>
}

#include "player/audio/audio_output.h"
#include "player/audio/resampled_audio_frame.h"
#include "player/common/blocking_queue.h"
#include "player/common/common_def.h"
#include "player/common/error.h"
#include "player/common/player_state_manager.h"
#include "player/sync/av_sync_controller.h"

namespace zenplay {

/**
 * @brief 音频播放器（重构版 - 职责简化）
 *
 * 核心职责：
 * 1. 管理播放队列（ResampledAudioFrame）
 * 2. 控制音频输出设备（AudioOutput）
 * 3. 跟踪播放时钟（PTS 管理）
 * 4. 音视频同步（与 AVSyncController 协作）
 *
 * 不再负责：
 * - ❌ 重采样逻辑（已移至 AudioResampler）
 * - ❌ SwrContext 管理（已移至 AudioResampler）
 *
 * 调用流程：
 * PlaybackController::AudioDecodeTask
 *   → AudioResampler::Resample (解码线程)
 *   → AudioPlayer::PushFrame (推入播放队列)
 *   → AudioPlayer::FillAudioBuffer (音频回调，仅 memcpy)
 */
class AudioPlayer {
 public:
  /**
   * @brief 音频播放器配置
   */
  struct AudioConfig {
    int target_sample_rate = 44100;                    // 目标采样率
    int target_channels = 2;                           // 目标声道数
    AVSampleFormat target_format = AV_SAMPLE_FMT_S16;  // 目标采样格式
    int target_bits_per_sample = 16;                   // 目标位深度
    int buffer_size = 1024;                            // 缓冲区大小
  };

  /**
   * @brief 帧时间戳信息 (使用通用的 MediaTimestamp)
   */
  using FrameTimestamp = MediaTimestamp;

  AudioPlayer(PlayerStateManager* state_manager,
              AVSyncController* sync_controller = nullptr);
  ~AudioPlayer();

  /**
   * @brief 初始化音频播放器
   * @param config 音频配置
   * @return Result<void> 成功返回Ok，失败返回错误码
   */
  Result<void> Init(const AudioConfig& config = AudioConfig{});

  /**
   * @brief 开始播放
   * @return Result<void> 成功返回Ok，失败返回错误码
   */
  Result<void> Start();

  /**
   * @brief 停止播放
   */
  void Stop();

  /**
   * @brief 暂停播放
   */
  void Pause();

  /**
   * @brief 恢复播放
   */
  void Resume();

  /**
   * @brief 设置音量
   * @param volume 音量值 (0.0 - 1.0)
   */
  void SetVolume(float volume);

  /**
   * @brief 获取音量
   */
  float GetVolume() const;

  /**
   * @brief 推送重采样后的帧到播放队列
   * @param frame 重采样后的音频帧
   * @return 成功返回true
   *
   * @note 由 PlaybackController::AudioDecodeTask 调用
   * @note 使用 BlockingQueue，队列满时会阻塞
   */
  bool PushFrame(ResampledAudioFrame frame);

  /**
   * @brief 推送重采样后的帧（带超时）
   * @param frame 重采样后的音频帧
   * @param timeout_ms 超时时间（毫秒）
   * @return 成功返回true，超时或停止返回false
   */
  bool PushFrameTimeout(ResampledAudioFrame frame, int timeout_ms = 100);

  /**
   * @brief 清空音频帧队列
   */
  void ClearFrames();

  /**
   * @brief 清空音频缓冲区（软件 + 硬件）
   *
   * 用于 Seek 场景，清空：
   * 1. 软件层：frame_queue_、internal_buffer_
   * 2. 硬件层：WASAPI 缓冲区
   *
   * @note 必须在 Pause 状态下调用
   */
  void Flush();

  /**
   * @brief 重置时间戳状态（Seek 后调用）
   */
  void ResetTimestamps();

  /**
   * @brief 检查是否正在播放
   */
  bool IsPlaying() const;

  /**
   * @brief 获取队列中的帧数
   */
  size_t GetQueueSize() const;

  /**
   * @brief 清理资源
   */
  void Cleanup();

 private:
  /**
   * @brief 音频输出回调函数
   * @param user_data 用户数据(AudioPlayer实例)
   * @param buffer 输出缓冲区
   * @param buffer_size 缓冲区大小
   * @return 实际填充的字节数
   */
  static int AudioOutputCallback(void* user_data,
                                 uint8_t* buffer,
                                 int buffer_size);

  /**
   * @brief 填充音频输出缓冲区（在音频回调中调用）
   * @param buffer 输出缓冲区
   * @param buffer_size 缓冲区大小（字节）
   * @return 实际填充的字节数
   *
   * @note 此函数仅做 memcpy，不执行任何计算密集操作
   * @note 目标延迟：<0.1ms
   */
  int FillAudioBuffer(uint8_t* buffer, int buffer_size);

  /**
   * @brief 获取当前播放位置的 PTS (毫秒)
   * @return PTS 毫秒数,如果无效返回 -1.0
   */
  double GetCurrentPlaybackPTS() const;

 private:
  // 音频输出设备
  std::unique_ptr<AudioOutput> audio_output_;

  // 音频配置
  AudioConfig config_;
  AudioOutput::AudioSpec output_spec_;

  // 状态管理和音视频同步控制器
  PlayerStateManager* state_manager_;
  AVSyncController* sync_controller_;

  // PTS跟踪 (基于采样数的精确计算)
  mutable std::mutex pts_mutex_;
  double current_base_pts_seconds_{0.0};  // 当前基准 PTS (秒)
  size_t samples_played_since_base_{0};   // 从基准开始已播放的采样数
  int target_sample_rate_{44100};         // 目标采样率

  // ========== 播放队列（AudioResampler 生产，音频回调消费） ==========

  /**
   * @brief 播放队列（重采样后的 PCM 帧）
   * - 生产者：PlaybackController::AudioDecodeTask（通过 AudioResampler）
   * - 消费者：AudioPlayer::FillAudioBuffer（音频回调）
   * - 容量：50帧 @ 44.1kHz/1024样本 ≈ 1.2秒缓冲
   * - 流控：BlockingQueue 自动阻塞，匹配解码速度和播放速度
   */
  BlockingQueue<ResampledAudioFrame> frame_queue_{50};

  // ========== 音频回调相关 ==========

  // 当前正在消费的帧（部分消费支持）
  ResampledAudioFrame current_playback_frame_;
  size_t current_frame_offset_ = 0;  // 当前帧的读取偏移（字节）

  // 音频渲染状态跟踪
  bool last_fill_had_real_data_;  // 上次 FillAudioBuffer 是否有真实音频数据
};

}  // namespace zenplay
