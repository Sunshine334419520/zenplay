#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

#include "loki/src/callback.h"
#include "loki/src/threading/loki_thread.h"
#include "player/codec/decode.h"
#include "player/common/thread_safe_queue.h"
#include "player/sync/av_sync_controller.h"

extern "C" {
#include <libavformat/avformat.h>
}

namespace zenplay {

class Demuxer;
class VideoDecoder;
class AudioDecoder;
class Renderer;
class VideoPlayer;
class AudioPlayer;
class PlayerStateManager;

/**
 * @brief 播放控制器 - 统一协调音视频播放和同步
 *
 * 职责：
 * 1. 管理解封装和解码线程
 * 2. 协调AudioPlayer和VideoPlayer
 * 3. 控制AVSyncController进行音视频同步
 * 4. 提供统一的播放控制接口
 */

// 播放控制器，管理所有播放线程
class PlaybackController {
 public:
  PlaybackController(std::shared_ptr<PlayerStateManager> state_manager,
                     Demuxer* demuxer,
                     VideoDecoder* video_decoder,
                     AudioDecoder* audio_decoder,
                     Renderer* renderer);
  ~PlaybackController();

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
   * @brief 跳转到指定时间
   * @param timestamp_ms 目标时间戳(毫秒)
   * @return 成功返回true
   */
  bool Seek(int64_t timestamp_ms);

  /**
   * @brief 设置音量
   * @param volume 音量值(0.0-1.0)
   */
  void SetVolume(float volume);

  /**
   * @brief 获取音量
   */
  float GetVolume() const;

  /**
   * @brief 获取当前播放时间
   * @return 当前播放时间（毫秒）
   */
  int64_t GetCurrentTime() const;

 private:
  // 解封装任务 - 在专门的工作线程执行
  void DemuxTask();

  // 视频解码任务 - 在专门的解码线程执行
  void VideoDecodeTask();

  // 音频解码任务 - 在专门的解码线程执行
  void AudioDecodeTask();

  // 同步控制任务 - 定期更新时钟同步
  void SyncControlTask();

  // 停止所有线程
  void StopAllThreads();

 private:
  // 组件引用
  Demuxer* demuxer_;
  VideoDecoder* video_decoder_;
  AudioDecoder* audio_decoder_;
  Renderer* renderer_;

  // 播放器组件
  std::unique_ptr<AudioPlayer> audio_player_;
  std::unique_ptr<VideoPlayer> video_player_;
  std::unique_ptr<AVSyncController> av_sync_controller_;

  // 状态管理器（共享）
  std::shared_ptr<PlayerStateManager> state_manager_;

  // 数据队列
  ThreadSafeQueue<AVPacket*> video_packet_queue_;
  ThreadSafeQueue<AVPacket*> audio_packet_queue_;

  // 解码线程（使用std::thread，因为需要持续运行）
  std::unique_ptr<std::thread> demux_thread_;
  std::unique_ptr<std::thread> video_decode_thread_;
  std::unique_ptr<std::thread> audio_decode_thread_;
  std::unique_ptr<std::thread> sync_control_thread_;
};

}  // namespace zenplay
