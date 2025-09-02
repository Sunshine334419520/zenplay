#include "video_player.h"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace zenplay {

VideoPlayer::VideoPlayer()
    : is_playing_(false),
      is_paused_(false),
      should_stop_(false),
      audio_clock_ms_(0.0),
      video_clock_ms_(0.0),
      sync_offset_ms_(0.0),
      frames_since_last_stats_(0) {}

VideoPlayer::~VideoPlayer() {
  Cleanup();
}

bool VideoPlayer::Init(std::shared_ptr<Renderer> renderer,
                       const VideoConfig& config) {
  renderer_ = renderer;
  config_ = config;

  if (!renderer_) {
    std::cerr << "VideoPlayer: Invalid renderer" << std::endl;
    return false;
  }

  // 初始化统计信息
  stats_ = PlaybackStats{};
  last_stats_update_ = std::chrono::steady_clock::now();

  std::cout << "VideoPlayer initialized: target_fps=" << config_.target_fps
            << ", max_queue_size=" << config_.max_frame_queue_size << std::endl;

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
  std::cout << "VideoPlayer started" << std::endl;
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

  std::cout << "VideoPlayer stopped" << std::endl;
}

void VideoPlayer::Pause() {
  is_paused_ = true;
  std::cout << "VideoPlayer paused" << std::endl;
}

void VideoPlayer::Resume() {
  is_paused_ = false;
  pause_cv_.notify_all();
  frame_available_.notify_all();
  std::cout << "VideoPlayer resumed" << std::endl;
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
      std::lock_guard<std::mutex> stats_lock(stats_mutex_);
      stats_.frames_dropped++;
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

void VideoPlayer::SetAudioClock(double audio_clock_ms) {
  audio_clock_ms_.store(audio_clock_ms);
}

double VideoPlayer::GetVideoClock() const {
  return video_clock_ms_.load();
}

bool VideoPlayer::IsPlaying() const {
  return is_playing_.load();
}

size_t VideoPlayer::GetQueueSize() const {
  std::lock_guard<std::mutex> lock(frame_queue_mutex_);
  return frame_queue_.size();
}

VideoPlayer::PlaybackStats VideoPlayer::GetStats() const {
  std::lock_guard<std::mutex> lock(stats_mutex_);
  return stats_;
}

void VideoPlayer::Cleanup() {
  Stop();
  renderer_.reset();
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
      UpdateStats(true, 0.0);  // 记录丢帧
      continue;
    }

    // 等待到合适的显示时间
    if (target_display_time > current_time) {
      std::this_thread::sleep_until(target_display_time);
    }

    // 渲染帧
    auto render_start = std::chrono::steady_clock::now();
    if (renderer_) {
      renderer_->RenderFrame(video_frame->frame.get());
      renderer_->Present();
    }
    auto render_end = std::chrono::steady_clock::now();

    // 更新视频时钟
    double video_pts_ms = video_frame->timestamp.ToMilliseconds();
    video_clock_ms_.store(video_pts_ms);

    // 计算音视频同步偏移
    double sync_offset = CalculateAVSync(video_pts_ms);
    sync_offset_ms_.store(sync_offset);

    // 更新统计信息
    double render_time_ms =
        std::chrono::duration<double, std::milli>(render_end - render_start)
            .count();
    UpdateStats(false, render_time_ms);

    last_render_time = current_time;
  }
}

std::chrono::steady_clock::time_point VideoPlayer::CalculateFrameDisplayTime(
    const VideoFrame& frame_info) {
  double frame_pts_ms = frame_info.timestamp.ToMilliseconds();

  // 如果有音频时钟参考，进行同步
  double audio_clock_ms = audio_clock_ms_.load();
  if (audio_clock_ms > 0.0) {
    // 音视频同步模式：根据音频时钟调整显示时间
    auto elapsed_since_start =
        std::chrono::steady_clock::now() - play_start_time_;
    double elapsed_ms =
        std::chrono::duration<double, std::milli>(elapsed_since_start).count();

    // 计算理想的显示时间
    double target_delay_ms = frame_pts_ms - audio_clock_ms;

    // 限制同步偏移范围，避免过度延迟或提前
    target_delay_ms = std::max(-100.0, std::min(100.0, target_delay_ms));

    auto target_time =
        play_start_time_ + std::chrono::milliseconds(static_cast<int64_t>(
                               frame_pts_ms + target_delay_ms));
    return target_time;
  } else {
    // 仅视频播放模式：根据帧率计算显示时间
    double frame_duration_ms = 1000.0 / config_.target_fps;
    auto target_time =
        frame_info.receive_time +
        std::chrono::milliseconds(static_cast<int64_t>(frame_duration_ms));
    return target_time;
  }
}

bool VideoPlayer::ShouldDropFrame(
    const VideoFrame& frame_info,
    std::chrono::steady_clock::time_point current_time) {
  // 计算帧的延迟
  auto target_display_time = CalculateFrameDisplayTime(frame_info);
  auto delay = std::chrono::duration<double, std::milli>(current_time -
                                                         target_display_time)
                   .count();

  // 如果延迟超过两帧时间，考虑丢帧
  double frame_duration_ms = 1000.0 / config_.target_fps;
  return delay > (frame_duration_ms * 2.0);
}

double VideoPlayer::CalculateAVSync(double video_pts_ms) {
  double audio_clock_ms = audio_clock_ms_.load();
  if (audio_clock_ms <= 0.0) {
    return 0.0;  // 没有音频参考
  }

  // 返回音视频时钟差值：正值表示视频超前，负值表示音频超前
  return video_pts_ms - audio_clock_ms;
}

void VideoPlayer::UpdateStats(bool frame_dropped, double render_time_ms) {
  std::lock_guard<std::mutex> lock(stats_mutex_);

  if (frame_dropped) {
    stats_.frames_dropped++;
  } else {
    stats_.frames_rendered++;
    stats_.render_time_ms =
        (stats_.render_time_ms * 0.9) + (render_time_ms * 0.1);  // 滑动平均
  }

  frames_since_last_stats_++;

  // 每秒更新一次FPS统计
  auto now = std::chrono::steady_clock::now();
  auto elapsed =
      std::chrono::duration<double>(now - last_stats_update_).count();
  if (elapsed >= 1.0) {
    stats_.average_fps = frames_since_last_stats_ / elapsed;
    stats_.sync_offset_ms = sync_offset_ms_.load();

    last_stats_update_ = now;
    frames_since_last_stats_ = 0;
  }
}

}  // namespace zenplay
