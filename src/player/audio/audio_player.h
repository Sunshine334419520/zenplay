#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

#include "../sync/av_sync_controller.h"
#include "audio_output.h"
#include "player/common/player_state_manager.h"

extern "C" {
#include <libavutil/frame.h>
#include <libswresample/swresample.h>
}

#include "player/common/common_def.h"

namespace zenplay {

/**
 * @brief 音频播放器
 *
 * 负责从音频帧队列中取出解码后的音频数据，
 * 进行格式转换和重采样，然后通过AudioOutput播放
 * 同时作为音视频同步的主时钟源
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

  AudioPlayer(PlayerStateManager* state_manager,
              AVSyncController* sync_controller = nullptr);
  ~AudioPlayer();

  /**
   * @brief 初始化音频播放器
   * @param config 音频配置
   * @return 成功返回true
   */
  bool Init(const AudioConfig& config = AudioConfig{});

  /**
   * @brief 开始播放
   * @return 成功返回true
   */
  bool Start();

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
   * @brief 推送音频帧到播放队列
   * @param frame 音频帧
   * @return 成功返回true
   */
  bool PushFrame(AVFramePtr frame);

  /**
   * @brief 清空音频帧队列
   */
  void ClearFrames();

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
   * @brief 初始化重采样器
   * @param frame 第一个音频帧，用于获取源格式信息
   * @return 成功返回true
   */
  bool InitializeResampler(const AVFrame* frame);

  /**
   * @brief 重采样音频帧
   * @param frame 源音频帧
   * @param output_buffer 输出缓冲区
   * @param max_output_samples 最大输出采样数
   * @return 实际输出的采样数，-1表示错误
   */
  int ResampleFrame(const AVFrame* frame,
                    uint8_t** output_buffer,
                    int max_output_samples);

  /**
   * @brief 填充音频输出缓冲区
   * @param buffer 输出缓冲区
   * @param buffer_size 缓冲区大小
   * @return 实际填充的字节数
   */
  int FillAudioBuffer(uint8_t* buffer, int buffer_size);

 private:
  // 音频输出设备
  std::unique_ptr<AudioOutput> audio_output_;

  // 音频配置
  AudioConfig config_;
  AudioOutput::AudioSpec output_spec_;

  // 状态管理和音视频同步控制器
  PlayerStateManager* state_manager_;
  AVSyncController* sync_controller_;

  // PTS跟踪
  double base_audio_pts_;
  size_t total_samples_played_;
  std::mutex pts_mutex_;

  // 重采样器
  SwrContext* swr_context_;
  uint8_t** resampled_data_;
  int resampled_data_size_;
  int max_resampled_samples_;

  // 音频帧队列
  mutable std::mutex frame_queue_mutex_;
  std::queue<AVFramePtr> frame_queue_;
  std::condition_variable frame_available_;
  static const size_t MAX_QUEUE_SIZE = 50;

  // 内部缓冲区
  std::vector<uint8_t> internal_buffer_;
  size_t buffer_read_pos_;

  // 源音频格式信息(从第一帧获取)
  int src_sample_rate_;
  int src_channels_;
  AVSampleFormat src_format_;
  bool format_initialized_;

  // 音频渲染状态跟踪
  bool last_fill_had_real_data_;  // 上次 FillAudioBuffer 是否有真实音频数据
};

}  // namespace zenplay
