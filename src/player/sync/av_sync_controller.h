#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>

namespace zenplay {

/**
 * @brief 音视频同步控制器
 *
 * 负责管理音频和视频的时钟同步，实现以下同步策略：
 * 1. 以音频为主时钟（音频播放更稳定）
 * 2. 视频根据音频时钟调整显示时间
 * 3. 处理时钟漂移和同步误差
 */
class AVSyncController {
 public:
  /**
   * @brief 同步模式
   */
  enum class SyncMode {
    AUDIO_MASTER,    // 以音频为主时钟（推荐）
    VIDEO_MASTER,    // 以视频为主时钟
    EXTERNAL_MASTER  // 外部时钟（如系统时钟）
  };

  /**
   * @brief 同步统计信息
   */
  struct SyncStats {
    double audio_clock_ms = 0.0;     // 音频时钟
    double video_clock_ms = 0.0;     // 视频时钟
    double sync_offset_ms = 0.0;     // 同步偏移
    double avg_sync_error_ms = 0.0;  // 平均同步误差
    double max_sync_error_ms = 0.0;  // 最大同步误差
    int64_t sync_corrections = 0;    // 同步校正次数

    // 同步质量评估
    bool is_in_sync() const {
      return std::abs(sync_offset_ms) < 40.0;  // 40ms以内认为同步
    }

    const char* sync_quality() const {
      double abs_offset = std::abs(sync_offset_ms);
      if (abs_offset < 20.0) {
        return "Excellent";
      }
      if (abs_offset < 40.0) {
        return "Good";
      }
      if (abs_offset < 80.0) {
        return "Fair";
      }
      return "Poor";
    }
  };

  AVSyncController();
  ~AVSyncController() = default;

  /**
   * @brief 设置同步模式
   */
  void SetSyncMode(SyncMode mode);

  /**
   * @brief 重置同步状态（Stop 或非 Seek 场景）
   */
  void Reset();

  /**
   * @brief 重置同步状态到指定位置（Seek 专用）
   * @param target_pts_ms Seek 的目标位置（毫秒）
   */
  void ResetForSeek(int64_t target_pts_ms);

  /**
   * @brief 获取当前同步模式
   */
  SyncMode GetSyncMode() const;

  /**
   * @brief 更新音频时钟
   * @param audio_pts_ms 音频PTS毫秒数
   * @param system_time 系统时间戳
   */
  void UpdateAudioClock(double audio_pts_ms,
                        std::chrono::steady_clock::time_point system_time);

  /**
   * @brief 更新视频时钟
   * @param video_pts_ms 视频PTS毫秒数
   * @param system_time 系统时间戳
   */
  void UpdateVideoClock(double video_pts_ms,
                        std::chrono::steady_clock::time_point system_time);

  /**
   * @brief 获取主时钟时间
   * @param current_time 当前系统时间
   * @return 主时钟毫秒数
   */
  double GetMasterClock(
      std::chrono::steady_clock::time_point current_time) const;

  /**
   * @brief 计算视频帧显示延迟
   * @param video_pts_ms 视频帧PTS毫秒数
   * @param current_time 当前系统时间
   * @return 建议的显示延迟毫秒数（正数表示延迟，负数表示提前）
   */
  double CalculateVideoDelay(
      double video_pts_ms,
      std::chrono::steady_clock::time_point current_time) const;

  /**
   * @brief 计算音频缓冲调整
   * @param audio_pts_ms 音频PTS毫秒数
   * @param current_time 当前系统时间
   * @return 建议的音频调整毫秒数
   */
  double CalculateAudioAdjustment(
      double audio_pts_ms,
      std::chrono::steady_clock::time_point current_time) const;

  /**
   * @brief 检查是否需要丢帧
   * @param video_pts_ms 视频帧PTS
   * @param current_time 当前时间
   * @return true表示应该丢帧
   */
  bool ShouldDropVideoFrame(
      double video_pts_ms,
      std::chrono::steady_clock::time_point current_time) const;

  /**
   * @brief 检查是否需要重复帧
   * @param video_pts_ms 视频帧PTS
   * @param current_time 当前时间
   * @return true表示应该重复显示帧
   */
  bool ShouldRepeatVideoFrame(
      double video_pts_ms,
      std::chrono::steady_clock::time_point current_time) const;

  /**
   * @brief 获取同步统计信息
   */
  SyncStats GetSyncStats() const;

  /**
   * @brief 设置同步参数
   */
  struct SyncParams {
    double max_video_delay_ms = 100.0;        // 最大视频延迟
    double max_video_speedup_ms = 100.0;      // 最大视频加速
    double sync_threshold_ms = 40.0;          // 同步阈值
    double drop_frame_threshold_ms = 80.0;    // 丢帧阈值
    double repeat_frame_threshold_ms = 20.0;  // 重复帧阈值
    bool enable_frame_drop = true;            // 启用丢帧
    bool enable_frame_repeat = true;          // 启用重复帧
  };

  void SetSyncParams(const SyncParams& params);

 private:
  /**
   * @brief 更新同步统计
   */
  void UpdateSyncStats();

  /**
   * @brief 时钟信息
   */
  struct ClockInfo {
    std::atomic<double> pts_ms{0.0};                    // PTS时间戳
    std::chrono::steady_clock::time_point system_time;  // 对应的系统时间
    std::atomic<double> drift{0.0};                     // 时钟漂移

    // 获取当前时钟值
    double GetCurrentTime(std::chrono::steady_clock::time_point now) const {
      auto elapsed_ms =
          std::chrono::duration<double, std::milli>(now - system_time).count();
      return pts_ms.load() + elapsed_ms + drift.load();
    }
  };

  SyncMode sync_mode_;
  SyncParams sync_params_;

  // 时钟信息
  mutable std::mutex clock_mutex_;
  ClockInfo audio_clock_;
  ClockInfo video_clock_;
  ClockInfo external_clock_;
  bool audio_start_initialized_{false};
  double audio_start_pts_ms_{0.0};
  bool video_start_initialized_{false};
  double video_start_pts_ms_{0.0};

  // 同步统计
  mutable std::mutex stats_mutex_;
  SyncStats stats_;

  // 同步历史（用于计算平均误差）
  static const size_t SYNC_HISTORY_SIZE = 100;
  std::vector<double> sync_error_history_;
  size_t sync_history_index_;

  // 播放开始时间
  std::chrono::steady_clock::time_point play_start_time_;
  bool is_initialized_;
};

}  // namespace zenplay
