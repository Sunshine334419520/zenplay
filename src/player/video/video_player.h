#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

#include "player/common/common_def.h"
#include "player/sync/av_sync_controller.h"
#include "player/video/render/renderer.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/rational.h>
}

namespace zenplay {

/**
 * @brief 视频播放器
 *
 * 负责从视频帧队列中取出解码后的视频帧，
 * 进行时间戳管理和同步，然后通过Renderer渲染显示
 */
class VideoPlayer {
 public:
  /**
   * @brief 视频播放器配置
   */
  struct VideoConfig {
    double target_fps = 30.0;       // 目标帧率
    bool vsync_enabled = true;      // 垂直同步
    int max_frame_queue_size = 30;  // 最大帧队列大小
    bool drop_frames = false;       // 是否允许丢帧以维持同步
  };

  /**
   * @brief 帧时间戳信息
   */
  struct FrameTimestamp {
    int64_t pts = AV_NOPTS_VALUE;      // 显示时间戳
    int64_t dts = AV_NOPTS_VALUE;      // 解码时间戳
    AVRational time_base{1, 1000000};  // 时间基准

    // 转换为毫秒
    double ToMilliseconds() const {
      if (pts == AV_NOPTS_VALUE || pts < 0) {
        return -1.0;  // 返回-1表示无效时间戳，而不是0.0
      }
      return pts * av_q2d(time_base) * 1000.0;
    }
  };

  VideoPlayer(AVSyncController* sync_controller = nullptr);
  ~VideoPlayer();

  /**
   * @brief 初始化视频播放器
   * @param renderer 渲染器实例
   * @param config 视频配置
   * @return 成功返回true
   */
  bool Init(Renderer* renderer, const VideoConfig& config = VideoConfig{});

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
   * @brief 推送视频帧到播放队列
   * @param frame 视频帧
   * @param timestamp 时间戳信息
   * @return 成功返回true
   */
  bool PushFrame(AVFramePtr frame, const FrameTimestamp& timestamp);

  /**
   * @brief 清空视频帧队列
   */
  void ClearFrames();

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
   * @brief 视频帧信息
   */
  struct VideoFrame {
    AVFramePtr frame;
    FrameTimestamp timestamp;
    std::chrono::steady_clock::time_point receive_time;

    VideoFrame(AVFramePtr f, const FrameTimestamp& ts)
        : frame(std::move(f)),
          timestamp(ts),
          receive_time(std::chrono::steady_clock::now()) {}
  };

  /**
   * @brief 视频渲染线程主函数
   */
  void VideoRenderThread();

  /**
   * @brief 计算帧显示时间
   * @param frame_info 帧信息
   * @return 应该显示的时间点
   */
  std::chrono::steady_clock::time_point CalculateFrameDisplayTime(
      const VideoFrame& frame_info);

  /**
   * @brief 检查是否需要丢帧
   * @param frame_info 帧信息
   * @param current_time 当前时间
   * @return true表示应该丢帧
   */
  bool ShouldDropFrame(const VideoFrame& frame_info,
                       std::chrono::steady_clock::time_point current_time);

  /**
   * @brief 音视频同步计算
   * @param video_pts_ms 视频PTS毫秒数
   * @return 同步偏移量，正数表示视频超前，负数表示音频超前
   */
  double CalculateAVSync(double video_pts_ms);

  /**
   * @brief 更新播放统计
   */
  void UpdateStats(bool frame_dropped,
                   double render_time_ms,
                   double sync_offset_ms = 0.0);

 private:
  // 渲染器和同步控制器
  Renderer* renderer_;
  AVSyncController* av_sync_controller_;  // 外部管理的同步控制器

  // 配置
  VideoConfig config_;

  // 视频帧队列
  mutable std::mutex frame_queue_mutex_;
  std::queue<std::unique_ptr<VideoFrame>> frame_queue_;
  std::condition_variable frame_available_;

  // 渲染线程
  std::unique_ptr<std::thread> render_thread_;
  std::atomic<bool> is_playing_;
  std::atomic<bool> is_paused_;
  std::atomic<bool> should_stop_;

  // 同步相关
  mutable std::mutex sync_mutex_;
  std::condition_variable pause_cv_;

  // 播放时间管理
  std::chrono::steady_clock::time_point play_start_time_;  // 播放开始时间
};

}  // namespace zenplay
