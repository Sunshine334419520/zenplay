#include "av_sync_controller.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace zenplay {

AVSyncController::AVSyncController()
    : sync_mode_(SyncMode::AUDIO_MASTER),
      sync_history_index_(0),
      is_initialized_(false),
      is_paused_(false),
      accumulated_pause_duration_(std::chrono::steady_clock::duration::zero()) {
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

double AVSyncController::NormalizeAudioPTS(double raw_pts_ms) {
  // 必须在clock_mutex_保护下调用
  if (raw_pts_ms < 0.0) {
    return raw_pts_ms;  // 无效PTS，直接返回
  }

  if (!audio_start_initialized_) {
    audio_start_initialized_ = true;
    audio_start_pts_ms_ = raw_pts_ms;
    // 第一帧归一化为0
    return 0.0;
  }

  // 后续帧相对于第一帧的偏移
  return raw_pts_ms - audio_start_pts_ms_;
}

double AVSyncController::NormalizeVideoPTS(double raw_pts_ms) {
  // 必须在clock_mutex_保护下调用
  if (raw_pts_ms < 0.0) {
    return raw_pts_ms;  // 无效PTS，直接返回
  }

  if (!video_start_initialized_) {
    video_start_initialized_ = true;
    video_start_pts_ms_ = raw_pts_ms;
    // 第一帧归一化为0
    return 0.0;
  }

  // 后续帧相对于第一帧的偏移
  return raw_pts_ms - video_start_pts_ms_;
}

void AVSyncController::UpdateAudioClock(
    double audio_pts_ms,
    std::chrono::steady_clock::time_point system_time) {
  std::lock_guard<std::mutex> lock(clock_mutex_);

  if (!is_initialized_) {
    play_start_time_ = system_time;
    is_initialized_ = true;
  }

  // 归一化PTS：将原始PTS转换为从0开始的相对时间
  double normalized_pts = NormalizeAudioPTS(audio_pts_ms);

  // 计算时钟漂移（Drift）
  // Drift是音频硬件时钟与系统时钟之间的偏差
  if (audio_clock_.system_time.time_since_epoch().count() > 0) {
    // 根据上次更新的时钟，推算当前应该的PTS
    // 注意：由于Resume()会调整system_time，这里的elapsed已经排除了暂停时间
    double expected_pts = audio_clock_.GetCurrentTime(system_time);

    // 计算实际PTS与推算PTS的差异
    double drift = normalized_pts - expected_pts;

    // 慢速调整drift（系数0.1），避免时钟突然跳变
    audio_clock_.drift = drift * 0.1;
  }

  // 更新音频时钟
  audio_clock_.pts_ms = normalized_pts;
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

  // 归一化PTS：将原始PTS转换为从0开始的相对时间
  double normalized_pts = NormalizeVideoPTS(video_pts_ms);

  // 计算时钟漂移（Drift）
  if (video_clock_.system_time.time_since_epoch().count() > 0) {
    // 根据上次更新的时钟，推算当前应该的PTS
    // 注意：由于Resume()会调整system_time，这里的elapsed已经排除了暂停时间
    double expected_pts = video_clock_.GetCurrentTime(system_time);

    // 计算实际PTS与推算PTS的差异
    double drift = normalized_pts - expected_pts;

    // 慢速调整drift（系数0.1），避免时钟突然跳变
    video_clock_.drift = drift * 0.1;
  }

  // 更新视频时钟
  video_clock_.pts_ms = normalized_pts;
  video_clock_.system_time = system_time;

  UpdateSyncStats();
}

double AVSyncController::GetMasterClock(
    std::chrono::steady_clock::time_point current_time) const {
  std::lock_guard<std::mutex> lock(clock_mutex_);

  switch (sync_mode_) {
    case SyncMode::AUDIO_MASTER:
      // 以音频为主时钟：音频连续播放不能停顿，最稳定
      return audio_clock_.GetCurrentTime(current_time);

    case SyncMode::VIDEO_MASTER:
      // 以视频为主时钟：用于音频同步到视频的特殊场景
      // 注意：仅用于有音频+视频且需要视频优先的情况
      // 如果只有视频（无音频），应该使用 EXTERNAL_MASTER
      return video_clock_.GetCurrentTime(current_time);

    case SyncMode::EXTERNAL_MASTER: {
      // 以系统时钟为主：用于无音频场景和测试调试
      // 注意：由于Resume()会调整play_start_time_，这里的elapsed已经排除了暂停时间
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
  std::lock_guard<std::mutex> lock(clock_mutex_);

  // 步骤1：归一化视频PTS
  // 注意：这里调用NormalizeVideoPTS是安全的，因为：
  // 1. UpdateVideoClock 总是在此之前调用，已经初始化了基准
  // 2. NormalizeVideoPTS 在基准初始化后是纯函数（无副作用）
  double normalized_video_pts =
      const_cast<AVSyncController*>(this)->NormalizeVideoPTS(video_pts_ms);

  // 步骤2：获取主时钟（通常是音频时钟）
  // GetMasterClock返回的是归一化后的时钟值（第一帧为0）
  double master_clock_ms = GetMasterClock(current_time);

  // 步骤3：计算同步差值
  // sync_diff > 0: 视频超前，需要延迟显示
  // sync_diff < 0: 视频落后，需要加速显示（甚至丢帧）
  double sync_diff = normalized_video_pts - master_clock_ms;

  // 步骤4：限制延迟范围，避免极端情况
  // 最大延迟：max_video_delay_ms（默认100ms）
  // 最大加速：-max_video_speedup_ms（默认-100ms）
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

void AVSyncController::Reset() {
  {
    std::lock_guard<std::mutex> lock(clock_mutex_);

    auto now = std::chrono::steady_clock::now();

    // 完全重置所有时钟
    audio_clock_.pts_ms.store(0.0);
    audio_clock_.system_time = now;
    audio_clock_.drift = 0.0;

    video_clock_.pts_ms.store(0.0);
    video_clock_.system_time = now;
    video_clock_.drift = 0.0;

    external_clock_.pts_ms.store(0.0);
    external_clock_.system_time = now;
    external_clock_.drift = 0.0;

    play_start_time_ = now;

    // 完全重置，清空所有起始 PTS 基准
    audio_start_initialized_ = false;
    audio_start_pts_ms_ = 0.0;
    video_start_initialized_ = false;
    video_start_pts_ms_ = 0.0;
  }

  {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    // 清空统计历史
    std::fill(sync_error_history_.begin(), sync_error_history_.end(), 0.0);
    sync_history_index_ = 0;
    sync_corrections_ = 0;
  }

  // 重置暂停状态
  {
    std::lock_guard<std::mutex> lock(pause_mutex_);
    is_paused_ = false;
    pause_start_time_ = std::chrono::steady_clock::time_point{};
    accumulated_pause_duration_ = std::chrono::steady_clock::duration::zero();
  }

  MODULE_INFO(LOG_MODULE_SYNC, "AVSyncController reset (Stop scenario)");
}

void AVSyncController::Pause() {
  std::lock_guard<std::mutex> clock_lock(clock_mutex_);
  std::lock_guard<std::mutex> pause_lock(pause_mutex_);

  if (is_paused_) {
    MODULE_WARN(LOG_MODULE_SYNC, "AVSyncController already paused");
    return;
  }

  is_paused_ = true;
  pause_start_time_ = std::chrono::steady_clock::now();

  MODULE_INFO(LOG_MODULE_SYNC, "AVSyncController paused");
}

void AVSyncController::Resume() {
  std::lock_guard<std::mutex> clock_lock(clock_mutex_);
  std::lock_guard<std::mutex> pause_lock(pause_mutex_);

  if (!is_paused_) {
    MODULE_WARN(LOG_MODULE_SYNC, "AVSyncController not paused, cannot resume");
    return;
  }

  auto resume_time = std::chrono::steady_clock::now();

  // 计算本次暂停时长
  auto this_pause_duration = resume_time - pause_start_time_;
  accumulated_pause_duration_ += this_pause_duration;

  // ⚠️ 关键优化：直接调整所有时钟的 system_time，
  // 这样 GetCurrentTime() 计算时就不需要减去暂停时长了！
  audio_clock_.system_time += this_pause_duration;
  video_clock_.system_time += this_pause_duration;
  external_clock_.system_time += this_pause_duration;
  play_start_time_ += this_pause_duration;

  auto total_paused_ms =
      std::chrono::duration<double, std::milli>(accumulated_pause_duration_)
          .count();

  MODULE_INFO(
      LOG_MODULE_SYNC,
      "AVSyncController resumed, this pause: {:.2f}ms, total paused: {:.2f}ms",
      std::chrono::duration<double, std::milli>(this_pause_duration).count(),
      total_paused_ms);

  is_paused_ = false;
  pause_start_time_ = std::chrono::steady_clock::time_point{};
}

void AVSyncController::ResetForSeek(int64_t target_pts_ms) {
  {
    std::lock_guard<std::mutex> lock(clock_mutex_);

    auto now = std::chrono::steady_clock::now();
    double target_ms = static_cast<double>(target_pts_ms);

    // ✅ 关键：设置时钟为目标位置，这样 GetCurrentTime() 就会返回正确值
    audio_clock_.pts_ms.store(target_ms);
    audio_clock_.system_time = now;
    audio_clock_.drift = 0.0;

    video_clock_.pts_ms.store(target_ms);
    video_clock_.system_time = now;
    video_clock_.drift = 0.0;

    external_clock_.pts_ms.store(target_ms);
    external_clock_.system_time = now;
    external_clock_.drift = 0.0;

    // ✅ 更新 play_start_time_，使其偏移到目标位置
    // EXTERNAL_MASTER 模式下：now - play_start_time_ = target_ms
    // 所以：play_start_time_ = now - target_ms
    play_start_time_ = now - std::chrono::milliseconds(target_pts_ms);

    // ✅ 保持起始 PTS 基准不变，因为我们仍然使用相同的 normalization base
    // 如果 audio_start_pts_ms_ = 1000ms，target = 5000ms
    // 那么下一帧 PTS 如果是 6000ms，normalized = 6000 - 1000 = 5000ms（正确）
  }

  {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    // Seek 时清空统计，但不影响时钟
    std::fill(sync_error_history_.begin(), sync_error_history_.end(), 0.0);
    sync_history_index_ = 0;
    sync_corrections_ = 0;
  }
}

void AVSyncController::SetSyncParams(const SyncParams& params) {
  sync_params_ = params;
}

void AVSyncController::UpdateSyncStats() {
  std::lock_guard<std::mutex> lock(stats_mutex_);

  // 获取时钟数据
  double audio_clock_ms = audio_clock_.pts_ms.load();
  double video_clock_ms = video_clock_.pts_ms.load();
  double sync_offset_ms = video_clock_ms - audio_clock_ms;

  // 更新同步误差历史
  sync_error_history_[sync_history_index_] = std::abs(sync_offset_ms);
  sync_history_index_ = (sync_history_index_ + 1) % SYNC_HISTORY_SIZE;

  // 计算平均和最大误差
  double avg_sync_error_ms = std::accumulate(sync_error_history_.begin(),
                                             sync_error_history_.end(), 0.0) /
                             SYNC_HISTORY_SIZE;
  double max_sync_error_ms =
      *std::max_element(sync_error_history_.begin(), sync_error_history_.end());

  // 检查是否需要同步校正
  if (std::abs(sync_offset_ms) > sync_params_.sync_threshold_ms) {
    sync_corrections_++;
  }

  // 更新到 StatisticsManager
  STATS_UPDATE_SYNC(audio_clock_ms, video_clock_ms, sync_offset_ms,
                    avg_sync_error_ms, max_sync_error_ms, sync_corrections_);
}

}  // namespace zenplay
