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

extern "C" {
#include <libavformat/avformat.h>
}

namespace zenplay {

class Demuxer;
class VideoDecoder;
class AudioDecoder;
class Renderer;

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
                     Renderer* renderer);
  ~PlaybackController();

  bool Start();
  void Stop();
  void Pause();
  void Resume();
  bool Seek(int64_t timestamp);

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

  // 渲染任务 - 在UI线程执行
  void RenderTask();

  // 停止所有线程
  void StopAllThreads();

 private:
  // 组件引用
  Demuxer* demuxer_;
  VideoDecoder* video_decoder_;
  AudioDecoder* audio_decoder_;
  Renderer* renderer_;

  // 数据队列
  ThreadSafeQueue<AVPacket*> video_packet_queue_;
  ThreadSafeQueue<AVPacket*> audio_packet_queue_;
  ThreadSafeQueue<AVFramePtr> video_frame_queue_;
  ThreadSafeQueue<AVFramePtr> audio_frame_queue_;

  // 线程控制
  std::atomic<bool> is_playing_{false};
  std::atomic<bool> is_paused_{false};
  std::atomic<bool> should_stop_{false};

  // 解码线程（使用std::thread，因为需要持续运行）
  std::unique_ptr<std::thread> demux_thread_;
  std::unique_ptr<std::thread> video_decode_thread_;
  std::unique_ptr<std::thread> audio_decode_thread_;

  // 同步相关
  mutable std::mutex state_mutex_;
  std::condition_variable pause_cv_;
};

}  // namespace zenplay
