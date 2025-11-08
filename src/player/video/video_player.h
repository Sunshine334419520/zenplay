#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

#include "player/common/common_def.h"
#include "player/common/error.h"
#include "player/common/player_state_manager.h"
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
    int max_frame_queue_size = 30;  // 最大帧队列大小（匹配解码节流阈值）
    bool drop_frames = true;        // 允许丢帧以维持同步
  };

  /**
   * @brief 帧时间戳信息 (使用通用的 MediaTimestamp)
   */
  using FrameTimestamp = MediaTimestamp;

  VideoPlayer(PlayerStateManager* state_manager,
              AVSyncController* sync_controller = nullptr);
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
   * @brief Seek 前的准备：清空状态、缓存、渲染器资源
   *
   * 职责：
   * - 暂停渲染（Pause）
   * - 等待队列真正变空（WaitForQueueEmpty）
   * - 清空帧队列（ClearFrames）
   * - 通知 renderer 清空缓存（renderer_->ClearCaches）
   *
   * 调用后：
   * - 所有帧队列已清空
   * - 所有渲染缓存已清空
   * - 准备好进行 Demuxer Seek
   */
  void PreSeek();

  /**
   * @brief Seek 后的初始化：根据目标状态恢复播放
   *
   * 职责：
   * - 如果目标状态是 Playing，则调用 Resume()
   * - 如果目标状态是 Paused，保持暂停
   * - 准备好接收新位置的帧
   *
   * @param target_state 目标播放状态
   * 调用前：Demuxer、Decoder 已完成 Seek
   * 调用后：准备好接收新位置的帧
   */
  void PostSeek(PlayerStateManager::PlayerState target_state);

  /**
   * @brief 推送视频帧到播放队列
   * @param frame 视频帧
   * @param timestamp 时间戳信息
   * @return 成功返回true
   */
  bool PushFrame(AVFramePtr frame, const FrameTimestamp& timestamp);

  /**
   * @brief 推送视频帧（具有背压的阻塞版本，但可中断）
   *
   * 当队列接近满时会自动阻塞，但如果以下情况发生会立即返回：
   * - ShouldStop() 变为 true（程序关闭）
   * - ShouldPause() 变为 true（用户暂停）
   * - timeout_ms 超时
   *
   * 这确保了 Seek、Pause、Stop 等操作永远不会被阻塞。
   *
   * @param frame 视频帧指针
   * @param timestamp 时间戳信息
   * @param max_wait_ms 最大等待时间（毫秒）
   *   - 0: 无限等待（但会响应 ShouldStop/ShouldPause）
   *   - >0: 有限等待，推荐 500ms
   * @return true 推送成功
   *         false 超时、停止、暂停
   */
  bool PushFrameBlocking(AVFramePtr frame,
                         const FrameTimestamp& timestamp,
                         int max_wait_ms = 0);

  /**
   * @brief 清空视频帧队列
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
   * @brief 视频帧信息 (使用通用的 MediaFrame)
   */
  using VideoFrame = MediaFrame;

  bool WaitForQueueBelow(size_t threshold, int timeout_ms);
  size_t GetMaxQueueSize() const;

  /**
   * @brief 视频渲染线程主函数
   */
  void VideoRenderThread();

  /**
   * @brief 内部：等待队列有空间（可被打断）
   *
   * 等待过程中会检查以下条件，任何一个满足就立即返回：
   * - 队列空间足够
   * - ShouldStop() 为 true
   * - ShouldPause() 为 true
   * - 超时
   *
   * @param lock 已持有的 frame_queue_mutex_ 锁
   * @param timeout_ms 最大等待时间（毫秒）
   *   - 0: 无限等待（直到条件满足或被中断）
   *   - >0: 有限等待
   *   - <0: 无等待（立即检查）
   * @return true 队列有空间且系统未停止/暂停
   *         false 超时、停止、暂停
   */
  bool WaitForQueueSpace_Locked(std::unique_lock<std::mutex>& lock,
                                int timeout_ms);

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
  /**
   * @brief 计算有效播放时长（排除暂停时间）
   * @param current_time 当前时间
   * @return 有效播放时长（毫秒）
   */
  double GetEffectiveElapsedTime(
      std::chrono::steady_clock::time_point current_time) const;

  // 渲染器和同步控制器
  Renderer* renderer_;
  PlayerStateManager* state_manager_;     // 状态管理器
  AVSyncController* av_sync_controller_;  // 外部管理的同步控制器

  // 配置
  VideoConfig config_;

  // 视频帧队列 (使用通用的 MediaFrame)
  mutable std::mutex frame_queue_mutex_;
  std::queue<std::unique_ptr<MediaFrame>> frame_queue_;
  std::condition_variable frame_available_;  // 通知消费者：有帧可用
  std::condition_variable frame_consumed_;   // 通知生产者：有空间可用

  // 渲染线程
  std::unique_ptr<std::thread> render_thread_;

  // 播放时间管理
  std::chrono::steady_clock::time_point play_start_time_;  // 播放开始时间

  // 背压日志记录时间（避免日志过多）
  std::chrono::steady_clock::time_point last_throttle_log_time_;
};

}  // namespace zenplay
