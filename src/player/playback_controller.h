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

/**
 * @brief 播放控制器 - 统一协调音视频播放和同步
 *
 * 职责：
 * 1. 管理解封装和解码线程
 * 2. 协调AudioPlayer和VideoPlayer
 * 3. 控制AVSyncController进行音视频同步
 * 4. 提供统一的播放控制接口
 */

// 媒体数据队列，线程安全
template <typename T>
class ThreadSafeQueue {
 public:
  void Push(T item) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(std::move(item));
    condition_.notify_one();
  }

  bool Pop(T& item,
           std::chrono::milliseconds timeout = std::chrono::milliseconds(100)) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (condition_.wait_for(lock, timeout,
                            [this] { return !queue_.empty() || stop_; })) {
      if (!queue_.empty()) {
        item = std::move(queue_.front());
        queue_.pop();
        return true;
      }
    }
    return false;
  }

  void Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::queue<T> empty;
    queue_.swap(empty);
  }

  void Stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
    condition_.notify_all();
  }

  size_t Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
  }

 private:
  mutable std::mutex mutex_;
  std::queue<T> queue_;
  std::condition_variable condition_;
  std::atomic<bool> stop_{false};
};

// 播放控制器，管理所有播放线程
class PlaybackController {
 public:
  PlaybackController(Demuxer* demuxer,
                     VideoDecoder* video_decoder,
                     AudioDecoder* audio_decoder,
                     std::shared_ptr<Renderer> renderer);
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
   * @brief 获取播放统计信息
   */
  struct PlaybackStats {
    // 音频统计
    size_t audio_queue_size = 0;
    float audio_volume = 0.0f;

    // 视频统计
    size_t video_queue_size = 0;
    double video_fps = 0.0;
    int64_t video_frames_rendered = 0;
    int64_t video_frames_dropped = 0;

    // 同步统计
    double sync_offset_ms = 0.0;
    const char* sync_quality = "Unknown";
    bool is_in_sync = false;
  };
  PlaybackStats GetStats() const;

  // 获取播放状态
  bool IsPlaying() const { return is_playing_.load(); }
  bool IsPaused() const { return is_paused_.load(); }

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
  std::shared_ptr<Renderer> renderer_;

  // 播放器组件
  std::unique_ptr<AudioPlayer> audio_player_;
  std::unique_ptr<VideoPlayer> video_player_;
  std::unique_ptr<AVSyncController> av_sync_controller_;

  // 数据队列
  ThreadSafeQueue<AVPacket*> video_packet_queue_;
  ThreadSafeQueue<AVPacket*> audio_packet_queue_;

  // 线程控制
  std::atomic<bool> is_playing_{false};
  std::atomic<bool> is_paused_{false};
  std::atomic<bool> should_stop_{false};

  // 解码线程（使用std::thread，因为需要持续运行）
  std::unique_ptr<std::thread> demux_thread_;
  std::unique_ptr<std::thread> video_decode_thread_;
  std::unique_ptr<std::thread> audio_decode_thread_;
  std::unique_ptr<std::thread> sync_control_thread_;

  // 同步相关
  mutable std::mutex state_mutex_;
  std::condition_variable pause_cv_;
};

}  // namespace zenplay
