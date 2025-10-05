#include "av_sync_controller.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace zenplay {

AVSyncController::AVSyncController()
    : sync_mode_(SyncMode::AUDIO_MASTER),
      sync_history_index_(0),
      is_initialized_(false) {
  sync_error_history_.resize(SYNC_HISTORY_SIZE, 0.0);
  Reset();
}

void AVSyncController::SetSyncMode(SyncMode mode) {
  std::lock_guard<std::mutex> lock(clock_mutex_);
  sync_mode_ = mode;
}

AVSyncController::SyncMode AVSyncController::GetSyncMode() const {
  return sync_mode_;
}

void AVSyncController::UpdateAudioClock(
    double audio_pts_ms,
    std::chrono::steady_clock::time_point system_time) {
  std::lock_guard<std::mutex> lock(clock_mutex_);

  if (!is_initialized_) {
    play_start_time_ = system_time;
    is_initialized_ = true;
  }

  double normalized_audio_pts = audio_pts_ms;
  if (audio_pts_ms >= 0.0) {
    if (!audio_start_initialized_) {
      audio_start_initialized_ = true;
      audio_start_pts_ms_ = audio_pts_ms;
    }
    normalized_audio_pts = audio_pts_ms - audio_start_pts_ms_;
  }

  // 计算时钟漂移
  if (audio_clock_.system_time.time_since_epoch().count() > 0) {
    auto expected_pts = audio_clock_.GetCurrentTime(system_time);
    double drift = normalized_audio_pts - expected_pts;
    audio_clock_.drift = drift * 0.1;  // 慢速调整漂移
  }

  audio_clock_.pts_ms = normalized_audio_pts;
  audio_clock_.system_time = system_time;

  UpdateSyncStats();
}

void AVSyncController::UpdateVideoClock(
    double video_pts_ms,
    std::chrono::steady_clock::time_point system_time) {
  std::lock_guard<std::mutex> lock(clock_mutex_);

  if (!is_initialized_) {
    play_start_time_ = system_time;
    is_initialized_ = true;
  }

  double normalized_video_pts = video_pts_ms;
  if (video_pts_ms >= 0.0) {
    if (!video_start_initialized_) {
      video_start_initialized_ = true;
      video_start_pts_ms_ = video_pts_ms;
    }
    normalized_video_pts = video_pts_ms - video_start_pts_ms_;
  }

  // 计算时钟漂移
  if (video_clock_.system_time.time_since_epoch().count() > 0) {
    auto expected_pts = video_clock_.GetCurrentTime(system_time);
    double drift = normalized_video_pts - expected_pts;
    video_clock_.drift = drift * 0.1;  // 慢速调整漂移
  }

  video_clock_.pts_ms = normalized_video_pts;
  video_clock_.system_time = system_time;

  UpdateSyncStats();
}

double AVSyncController::GetMasterClock(
    std::chrono::steady_clock::time_point current_time) const {
  std::lock_guard<std::mutex> lock(clock_mutex_);

  switch (sync_mode_) {
    case SyncMode::AUDIO_MASTER:
      return audio_clock_.GetCurrentTime(current_time);

    case SyncMode::VIDEO_MASTER:
      return video_clock_.GetCurrentTime(current_time);

    case SyncMode::EXTERNAL_MASTER: {
      // 使用系统时钟
      auto elapsed_ms = std::chrono::duration<double, std::milli>(
                            current_time - play_start_time_)
                            .count();
      return elapsed_ms;
    }
  }

  return 0.0;
}

double AVSyncController::CalculateVideoDelay(
    double video_pts_ms,
    std::chrono::steady_clock::time_point current_time) const {
  double master_clock_ms = GetMasterClock(current_time);
  double sync_diff = video_pts_ms - master_clock_ms;

  // 限制延迟范围
  sync_diff = std::max(-sync_params_.max_video_speedup_ms,
                       std::min(sync_params_.max_video_delay_ms, sync_diff));

  return sync_diff;
}

double AVSyncController::CalculateAudioAdjustment(
    double audio_pts_ms,
    std::chrono::steady_clock::time_point current_time) const {
  if (sync_mode_ != SyncMode::VIDEO_MASTER) {
    return 0.0;  // 只有在视频为主时钟时才调整音频
  }

  double master_clock_ms = GetMasterClock(current_time);
  double sync_diff = audio_pts_ms - master_clock_ms;

  // 限制音频调整范围
  return std::max(-50.0, std::min(50.0, sync_diff));
}

bool AVSyncController::ShouldDropVideoFrame(
    double video_pts_ms,
    std::chrono::steady_clock::time_point current_time) const {
  if (!sync_params_.enable_frame_drop) {
    return false;
  }

  double delay = CalculateVideoDelay(video_pts_ms, current_time);
  return delay < -sync_params_.drop_frame_threshold_ms;
}

bool AVSyncController::ShouldRepeatVideoFrame(
    double video_pts_ms,
    std::chrono::steady_clock::time_point current_time) const {
  if (!sync_params_.enable_frame_repeat) {
    return false;
  }

  double delay = CalculateVideoDelay(video_pts_ms, current_time);
  return delay > sync_params_.repeat_frame_threshold_ms;
}

AVSyncController::SyncStats AVSyncController::GetSyncStats() const {
  std::lock_guard<std::mutex> lock(stats_mutex_);
  return stats_;
}

void AVSyncController::Reset() {
  {
    std::lock_guard<std::mutex> lock(clock_mutex_);
    audio_clock_.pts_ms.store(0.0);
    audio_clock_.system_time = {};
    audio_clock_.drift = 0.0;

    video_clock_.pts_ms.store(0.0);
    video_clock_.system_time = {};
    video_clock_.drift = 0.0;

    external_clock_.pts_ms.store(0.0);
    external_clock_.system_time = {};
    external_clock_.drift = 0.0;

    is_initialized_ = false;
    audio_start_initialized_ = false;
    audio_start_pts_ms_ = 0.0;
    video_start_initialized_ = false;
    video_start_pts_ms_ = 0.0;
  }

  {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_ = SyncStats{};
    std::fill(sync_error_history_.begin(), sync_error_history_.end(), 0.0);
    sync_history_index_ = 0;
  }
}

void AVSyncController::SetSyncParams(const SyncParams& params) {
  sync_params_ = params;
}

void AVSyncController::UpdateSyncStats() {
  std::lock_guard<std::mutex> lock(stats_mutex_);

  stats_.audio_clock_ms = audio_clock_.pts_ms.load();
  stats_.video_clock_ms = video_clock_.pts_ms.load();
  stats_.sync_offset_ms = stats_.video_clock_ms - stats_.audio_clock_ms;

  // 更新同步误差历史
  sync_error_history_[sync_history_index_] = std::abs(stats_.sync_offset_ms);
  sync_history_index_ = (sync_history_index_ + 1) % SYNC_HISTORY_SIZE;

  // 计算平均和最大误差
  stats_.avg_sync_error_ms = std::accumulate(sync_error_history_.begin(),
                                             sync_error_history_.end(), 0.0) /
                             SYNC_HISTORY_SIZE;
  stats_.max_sync_error_ms =
      *std::max_element(sync_error_history_.begin(), sync_error_history_.end());

  // 检查是否需要同步校正
  if (std::abs(stats_.sync_offset_ms) > sync_params_.sync_threshold_ms) {
    stats_.sync_corrections++;
  }
}

}  // namespace zenplay
