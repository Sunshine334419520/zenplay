#include "video_player.h"

#include <algorithm>
#include <cmath>

#include "player/common/log_manager.h"
#include "player/stats/statistics_manager.h"

namespace zenplay {

VideoPlayer::VideoPlayer(PlayerStateManager* state_manager,
                         AVSyncController* sync_controller)
    : state_manager_(state_manager), av_sync_controller_(sync_controller) {}

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
  MODULE_INFO(LOG_MODULE_VIDEO, "VideoPlayer Start called");

  // 记录播放开始时间
  play_start_time_ = std::chrono::steady_clock::now();
  {
    std::lock_guard<std::mutex> lock(pause_mutex_);
    accumulated_pause_duration_ = std::chrono::steady_clock::duration::zero();
    pause_start_time_ = play_start_time_;
  }

  // 启动视频渲染线程
  render_thread_ =
      std::make_unique<std::thread>(&VideoPlayer::VideoRenderThread, this);

  MODULE_INFO(LOG_MODULE_VIDEO, "VideoPlayer started");
  return true;
}

void VideoPlayer::Stop() {
  MODULE_INFO(LOG_MODULE_VIDEO, "VideoPlayer Stop called");

  // Handle accumulated pause duration if paused
  if (state_manager_->GetState() == PlayerStateManager::PlayerState::kPaused) {
    auto stop_time = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(pause_mutex_);
    accumulated_pause_duration_ += stop_time - pause_start_time_;
  }

  // 通知可能在等待的线程
  frame_available_.notify_all();

  // 等待渲染线程结束
  if (render_thread_ && render_thread_->joinable()) {
    render_thread_->join();
    render_thread_.reset();
  }

  // 清空队列
  ClearFrames();

  {
    std::lock_guard<std::mutex> lock(pause_mutex_);
    accumulated_pause_duration_ = std::chrono::steady_clock::duration::zero();
    pause_start_time_ = std::chrono::steady_clock::time_point{};
  }

  MODULE_INFO(LOG_MODULE_VIDEO, "VideoPlayer stopped");
}

void VideoPlayer::Pause() {
  std::lock_guard<std::mutex> lock(pause_mutex_);
  pause_start_time_ = std::chrono::steady_clock::now();
  MODULE_INFO(LOG_MODULE_VIDEO, "VideoPlayer paused");
}

void VideoPlayer::Resume() {
  auto resume_time = std::chrono::steady_clock::now();
  {
    std::lock_guard<std::mutex> lock(pause_mutex_);
    accumulated_pause_duration_ += resume_time - pause_start_time_;
  }
  MODULE_INFO(LOG_MODULE_VIDEO, "VideoPlayer resumed");

  frame_available_.notify_all();
}

bool VideoPlayer::PushFrame(AVFramePtr frame, const FrameTimestamp& timestamp) {
  if (!frame || state_manager_->ShouldStop()) {
    return false;
  }

  std::lock_guard<std::mutex> lock(frame_queue_mutex_);

  // 检查队列大小，避免内存过度使用和延迟积累
  if (frame_queue_.size() >=
      static_cast<size_t>(config_.max_frame_queue_size)) {
    if (config_.drop_frames) {
      // 丢弃最老的帧以保持低延迟
      frame_queue_.pop();
      // 使用 StatisticsManager 统计丢帧
      STATS_UPDATE_RENDER(true, false, true, 0.0);
      MODULE_DEBUG(LOG_MODULE_VIDEO,
                   "Dropped old frame, queue was full at {} frames",
                   config_.max_frame_queue_size);
    } else {
      MODULE_DEBUG(LOG_MODULE_VIDEO, "Queue full, rejecting frame");
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

void VideoPlayer::ResetTimestamps() {
  std::lock_guard<std::mutex> lock(pause_mutex_);

  // 重置播放时间
  play_start_time_ = std::chrono::steady_clock::now();

  // 重置暂停累计时间
  accumulated_pause_duration_ = std::chrono::steady_clock::duration::zero();

  MODULE_INFO(LOG_MODULE_VIDEO, "VideoPlayer timestamps reset");
}

bool VideoPlayer::IsPlaying() const {
  auto state = state_manager_->GetState();
  return state == PlayerStateManager::PlayerState::kPlaying ||
         state == PlayerStateManager::PlayerState::kPaused;
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

  while (!state_manager_->ShouldStop()) {
    // 检查暂停状态
    if (state_manager_->ShouldPause()) {
      state_manager_->WaitForResume();
      last_render_time = std::chrono::steady_clock::now();
      continue;
    }

    // 获取待渲染的帧
    std::unique_ptr<VideoFrame> video_frame;
    {
      std::unique_lock<std::mutex> lock(frame_queue_mutex_);
      frame_available_.wait(lock, [this] {
        return !frame_queue_.empty() || state_manager_->ShouldStop();
      });

      if (state_manager_->ShouldStop()) {
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

    // 渲染帧
    auto render_start = std::chrono::steady_clock::now();
    if (renderer_) {
      // RenderFrame is expected to handle presenting internally when needed
      renderer_->RenderFrame(video_frame->frame.get());
    }
    auto render_end = std::chrono::steady_clock::now();

    // 更新视频时钟到同步控制器（传递原始PTS，由AVSyncController负责归一化）
    double video_pts_ms = video_frame->timestamp.ToMilliseconds();

    if (av_sync_controller_) {
      // 传递原始PTS，由AVSyncController统一归一化
      av_sync_controller_->UpdateVideoClock(video_pts_ms, render_end);

      // 📊 定期输出同步调试信息（每30帧）
      static int log_counter = 0;
      if (++log_counter % 30 == 0) {
        double master_clock_ms =
            av_sync_controller_->GetMasterClock(render_end);
        double normalized_pts =
            av_sync_controller_->NormalizeVideoPTS(video_pts_ms);
        double sync_offset = normalized_pts - master_clock_ms;

        MODULE_DEBUG(LOG_MODULE_VIDEO,
                     "AV Sync: video_pts={:.2f}ms, audio_clock={:.2f}ms, "
                     "offset={:.2f}ms, queue={}",
                     normalized_pts, master_clock_ms, sync_offset,
                     GetQueueSize());
      }
    }

    // 计算音视频同步偏移（用于统计）
    double sync_offset = CalculateAVSync(video_pts_ms);

    // 更新统计信息
    double render_time_ms =
        std::chrono::duration<double, std::milli>(render_end - render_start)
            .count();
    UpdateStats(false, render_time_ms, sync_offset);

    last_render_time = current_time;
  }
}

double VideoPlayer::GetEffectiveElapsedTime(
    std::chrono::steady_clock::time_point current_time) const {
  // 计算从播放开始到现在的总时长
  auto elapsed_time = current_time - play_start_time_;

  // 获取累计暂停时长的快照（线程安全）
  std::chrono::steady_clock::duration paused_duration_snapshot;
  std::chrono::steady_clock::time_point pause_start_snapshot;
  {
    std::lock_guard<std::mutex> lock(pause_mutex_);
    paused_duration_snapshot = accumulated_pause_duration_;
    pause_start_snapshot = pause_start_time_;
  }

  // 如果当前正在暂停，需要加上本次暂停到现在的时间
  if (state_manager_->GetState() == PlayerStateManager::PlayerState::kPaused &&
      pause_start_snapshot.time_since_epoch().count() != 0) {
    paused_duration_snapshot += current_time - pause_start_snapshot;
  }

  // 有效播放时长 = 总时长 - 暂停时长
  auto effective_elapsed = elapsed_time - paused_duration_snapshot;
  if (effective_elapsed.count() < 0) {
    effective_elapsed = std::chrono::steady_clock::duration::zero();
  }

  return std::chrono::duration<double, std::milli>(effective_elapsed).count();
}

std::chrono::steady_clock::time_point VideoPlayer::CalculateFrameDisplayTime(
    const VideoFrame& frame_info) {
  double video_pts_ms = frame_info.timestamp.ToMilliseconds();
  auto current_time = std::chrono::steady_clock::now();

  // 步骤1：检查PTS是否有效
  if (video_pts_ms < 0) {
    // 无效时间戳：使用接收时间 + 固定帧间隔
    double frame_duration_ms = 1000.0 / config_.target_fps;
    return frame_info.receive_time +
           std::chrono::milliseconds(static_cast<int64_t>(frame_duration_ms));
  }

  // 步骤2：检查是否有同步控制器
  if (!av_sync_controller_) {
    // 仅视频播放模式：基于播放时长计算
    double elapsed_ms = GetEffectiveElapsedTime(current_time);
    double delay_ms = video_pts_ms - elapsed_ms;
    delay_ms = std::clamp(delay_ms, -500.0, 500.0);

    return current_time +
           std::chrono::milliseconds(static_cast<int64_t>(delay_ms));
  }

  // 步骤3：使用AVSyncController计算视频延迟
  // 注意：CalculateVideoDelay需要归一化后的PTS
  double normalized_pts_ms =
      av_sync_controller_->NormalizeVideoPTS(video_pts_ms);
  double delay_ms =
      av_sync_controller_->CalculateVideoDelay(normalized_pts_ms, current_time);

  // 步骤4：计算目标显示时间点
  auto target_time =
      current_time + std::chrono::milliseconds(static_cast<int64_t>(delay_ms));

  return target_time;
}

bool VideoPlayer::ShouldDropFrame(
    const VideoFrame& frame_info,
    std::chrono::steady_clock::time_point current_time) {
  // 对于无效时间戳的帧，永远不要丢弃
  double video_pts_ms = frame_info.timestamp.ToMilliseconds();
  if (video_pts_ms < 0) {
    return false;
  }

  // 如果没有同步控制器，使用简单的延迟检测
  if (!av_sync_controller_) {
    auto target_display_time = CalculateFrameDisplayTime(frame_info);
    auto delay = std::chrono::duration<double, std::milli>(current_time -
                                                           target_display_time)
                     .count();

    // 延迟超过5帧时间才丢帧
    double frame_duration_ms = 1000.0 / config_.target_fps;
    bool should_drop = delay > (frame_duration_ms * 5.0);

    if (should_drop) {
      MODULE_DEBUG(LOG_MODULE_VIDEO,
                   "Frame drop (no sync): PTS={:.2f}ms, delay={:.2f}ms, "
                   "threshold={:.2f}ms",
                   video_pts_ms, delay, frame_duration_ms * 5.0);
    }
    return should_drop;
  }

  // 使用AVSyncController判断是否需要丢帧
  // 注意：ShouldDropVideoFrame需要归一化后的PTS
  double normalized_pts_ms =
      av_sync_controller_->NormalizeVideoPTS(video_pts_ms);
  bool should_drop = av_sync_controller_->ShouldDropVideoFrame(
      normalized_pts_ms, current_time);

  if (should_drop) {
    // 计算同步偏移用于日志
    double master_clock_ms = av_sync_controller_->GetMasterClock(current_time);
    double sync_offset = normalized_pts_ms - master_clock_ms;

    MODULE_DEBUG(LOG_MODULE_VIDEO,
                 "Frame drop: PTS={:.2f}ms, master_clock={:.2f}ms, "
                 "sync_offset={:.2f}ms",
                 normalized_pts_ms, master_clock_ms, sync_offset);
  }

  return should_drop;
}

double VideoPlayer::CalculateAVSync(double video_pts_ms) {
  if (av_sync_controller_) {
    auto current_time = std::chrono::steady_clock::now();
    double master_clock_ms = av_sync_controller_->GetMasterClock(current_time);

    // 由AVSyncController归一化视频PTS
    double normalized_pts_ms = av_sync_controller_->NormalizeVideoPTS(
        static_cast<int64_t>(video_pts_ms));

    // 返回音视频时钟差值：正值表示视频超前，负值表示音频超前
    return normalized_pts_ms - master_clock_ms;
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
