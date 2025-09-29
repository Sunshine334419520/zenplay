#include "video_player.h"

#include <algorithm>
#include <cmath>

#include "player/common/log_manager.h"
#include "player/stats/statistics_manager.h"

namespace zenplay {

VideoPlayer::VideoPlayer(AVSyncController* sync_controller)
    : av_sync_controller_(sync_controller),
      is_playing_(false),
      is_paused_(false),
      should_stop_(false) {}

VideoPlayer::~VideoPlayer() {
  Cleanup();
}

bool VideoPlayer::Init(Renderer* renderer, const VideoConfig& config) {
  renderer_ = renderer;
  config_ = config;

  if (!renderer_) {
    MODULE_ERROR(LOG_MODULE_VIDEO, "VideoPlayer: Invalid renderer");
    return false;
  }

  MODULE_INFO(LOG_MODULE_VIDEO,
              "VideoPlayer initialized: target_fps={}, max_queue_size={}, "
              "drop_frames={}",
              config_.target_fps, config_.max_frame_queue_size,
              config_.drop_frames);

  return true;
}

bool VideoPlayer::Start() {
  if (is_playing_.load()) {
    return true;
  }

  should_stop_ = false;
  is_paused_ = false;

  // 记录播放开始时间
  play_start_time_ = std::chrono::steady_clock::now();

  // 启动视频渲染线程
  render_thread_ =
      std::make_unique<std::thread>(&VideoPlayer::VideoRenderThread, this);

  is_playing_ = true;
  MODULE_INFO(LOG_MODULE_VIDEO, "VideoPlayer started");
  return true;
}

void VideoPlayer::Stop() {
  if (!is_playing_.load()) {
    return;
  }

  should_stop_ = true;
  is_playing_ = false;

  // 通知可能在等待的线程
  frame_available_.notify_all();
  pause_cv_.notify_all();

  // 等待渲染线程结束
  if (render_thread_ && render_thread_->joinable()) {
    render_thread_->join();
    render_thread_.reset();
  }

  // 清空队列
  ClearFrames();

  MODULE_INFO(LOG_MODULE_VIDEO, "VideoPlayer stopped");
}

void VideoPlayer::Pause() {
  is_paused_ = true;
  MODULE_INFO(LOG_MODULE_VIDEO, "VideoPlayer paused");
}

void VideoPlayer::Resume() {
  is_paused_ = false;
  pause_cv_.notify_all();
  frame_available_.notify_all();
  MODULE_INFO(LOG_MODULE_VIDEO, "VideoPlayer resumed");
}

bool VideoPlayer::PushFrame(AVFramePtr frame, const FrameTimestamp& timestamp) {
  if (!frame || should_stop_.load()) {
    return false;
  }

  std::lock_guard<std::mutex> lock(frame_queue_mutex_);

  // 检查队列大小，避免内存过度使用
  if (frame_queue_.size() >=
      static_cast<size_t>(config_.max_frame_queue_size)) {
    if (config_.drop_frames) {
      // 丢弃最老的帧
      frame_queue_.pop();
      // 使用 StatisticsManager 统计丢帧
      STATS_UPDATE_RENDER(true, false, true, 0.0);
    } else {
      return false;  // 队列满，拒绝新帧
    }
  }

  auto video_frame = std::make_unique<VideoFrame>(std::move(frame), timestamp);
  frame_queue_.push(std::move(video_frame));
  frame_available_.notify_one();

  return true;
}

void VideoPlayer::ClearFrames() {
  std::lock_guard<std::mutex> lock(frame_queue_mutex_);
  std::queue<std::unique_ptr<VideoFrame>> empty_queue;
  frame_queue_.swap(empty_queue);
}

bool VideoPlayer::IsPlaying() const {
  return is_playing_.load();
}

size_t VideoPlayer::GetQueueSize() const {
  std::lock_guard<std::mutex> lock(frame_queue_mutex_);
  return frame_queue_.size();
}

void VideoPlayer::Cleanup() {
  Stop();
  renderer_->Cleanup();
}

void VideoPlayer::VideoRenderThread() {
  auto last_render_time = std::chrono::steady_clock::now();

  while (!should_stop_.load()) {
    // 检查暂停状态
    if (is_paused_.load()) {
      std::unique_lock<std::mutex> lock(sync_mutex_);
      pause_cv_.wait(
          lock, [this] { return !is_paused_.load() || should_stop_.load(); });
      last_render_time = std::chrono::steady_clock::now();
      continue;
    }

    // 获取待渲染的帧
    std::unique_ptr<VideoFrame> video_frame;
    {
      std::unique_lock<std::mutex> lock(frame_queue_mutex_);
      frame_available_.wait(lock, [this] {
        return !frame_queue_.empty() || should_stop_.load();
      });

      if (should_stop_.load()) {
        break;
      }

      if (frame_queue_.empty()) {
        continue;
      }

      video_frame = std::move(frame_queue_.front());
      frame_queue_.pop();
    }

    auto current_time = std::chrono::steady_clock::now();

    // 计算帧应该显示的时间
    auto target_display_time = CalculateFrameDisplayTime(*video_frame);

    // 检查是否需要丢帧
    if (config_.drop_frames && ShouldDropFrame(*video_frame, current_time)) {
      double video_pts_ms = video_frame->timestamp.ToMilliseconds();
      double sync_offset = CalculateAVSync(video_pts_ms);
      UpdateStats(true, 0.0, sync_offset);  // 记录丢帧
      continue;
    }

    // 等待到合适的显示时间
    if (target_display_time > current_time) {
      std::this_thread::sleep_until(target_display_time);
    }

    MODULE_DEBUG(LOG_MODULE_VIDEO, "Rendering frame with PTS {} ms",
                 video_frame->timestamp.ToMilliseconds());

    // 渲染帧
    auto render_start = std::chrono::steady_clock::now();
    if (renderer_) {
      // RenderFrame is expected to handle presenting internally when needed
      renderer_->RenderFrame(video_frame->frame.get());
    }
    auto render_end = std::chrono::steady_clock::now();

    // 更新视频时钟到同步控制器
    double video_pts_ms = video_frame->timestamp.ToMilliseconds();
    if (av_sync_controller_) {
      av_sync_controller_->UpdateVideoClock(video_pts_ms, render_end);
    }

    // 计算音视频同步偏移
    double sync_offset = CalculateAVSync(video_pts_ms);

    // 更新统计信息
    double render_time_ms =
        std::chrono::duration<double, std::milli>(render_end - render_start)
            .count();
    UpdateStats(false, render_time_ms, sync_offset);

    last_render_time = current_time;
  }
}

std::chrono::steady_clock::time_point VideoPlayer::CalculateFrameDisplayTime(
    const VideoFrame& frame_info) {
  double video_pts_ms = frame_info.timestamp.ToMilliseconds();
  auto current_time = std::chrono::steady_clock::now();

  // 检查时间戳是否有效
  if (video_pts_ms < 0) {
    // 无效时间戳：使用接收时间 + 帧间隔
    double frame_duration_ms = 1000.0 / config_.target_fps;
    return frame_info.receive_time +
           std::chrono::milliseconds(static_cast<int64_t>(frame_duration_ms));
  }

  // 如果有音视频同步控制器，使用它来计算显示时间
  if (av_sync_controller_) {
    // 更新视频时钟
    av_sync_controller_->UpdateVideoClock(video_pts_ms, current_time);

    // 获取主时钟（通常是音频时钟）
    double master_clock_ms = av_sync_controller_->GetMasterClock(current_time);

    // 计算同步偏移 - 修复：应该基于播放开始时间计算
    double sync_offset_ms = video_pts_ms - master_clock_ms;

    // 限制延迟范围 [-100ms, +100ms]
    sync_offset_ms = std::clamp(sync_offset_ms, -100.0, 100.0);

    // 修复关键Bug：正确的时间计算
    // 不是 current_time + offset，而是 play_start_time + video_pts +
    // sync_adjustment
    auto target_time = play_start_time_ +
                       std::chrono::milliseconds(
                           static_cast<int64_t>(video_pts_ms + sync_offset_ms));
    return target_time;
  } else {
    // 仅视频播放模式：基于播放开始时间 + PTS
    auto target_time =
        play_start_time_ +
        std::chrono::milliseconds(static_cast<int64_t>(video_pts_ms));
    return target_time;
  }
}

bool VideoPlayer::ShouldDropFrame(
    const VideoFrame& frame_info,
    std::chrono::steady_clock::time_point current_time) {
  // 对于无效时间戳的帧，永远不要丢弃
  double video_pts_ms = frame_info.timestamp.ToMilliseconds();
  if (video_pts_ms < 0) {
    return false;
  }

  // 计算帧的延迟
  auto target_display_time = CalculateFrameDisplayTime(frame_info);
  auto delay = std::chrono::duration<double, std::milli>(current_time -
                                                         target_display_time)
                   .count();

  // 如果延迟超过两帧时间，考虑丢帧
  double frame_duration_ms = 1000.0 / config_.target_fps;
  bool should_drop = delay > (frame_duration_ms * 2.0);

  // 添加调试日志
  if (should_drop) {
    MODULE_INFO(LOG_MODULE_VIDEO,
                "Frame drop: PTS={:.2f}ms, delay={:.2f}ms, threshold={:.2f}ms",
                video_pts_ms, delay, frame_duration_ms * 2.0);
  }

  return should_drop;
}

double VideoPlayer::CalculateAVSync(double video_pts_ms) {
  if (av_sync_controller_) {
    auto current_time = std::chrono::steady_clock::now();
    double master_clock_ms = av_sync_controller_->GetMasterClock(current_time);

    // 返回音视频时钟差值：正值表示视频超前，负值表示音频超前
    return video_pts_ms - master_clock_ms;
  } else {
    return 0.0;  // 没有同步控制器
  }
}

void VideoPlayer::UpdateStats(bool frame_dropped,
                              double render_time_ms,
                              double sync_offset_ms) {
  // 使用统一的 StatisticsManager 更新统计
  STATS_UPDATE_RENDER(true, !frame_dropped, frame_dropped, render_time_ms);

  // 更新同步统计
  // if (av_sync_controller_) {
  //   auto sync_stats = av_sync_controller_->GetSyncStats();
  //   STATS_UPDATE_SYNC(sync_offset_ms, sync_stats.is_in_sync());
  // }
}

}  // namespace zenplay
