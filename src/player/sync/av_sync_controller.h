#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>

#include "player/stats/statistics_manager.h"

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
   *
   * 定义音视频同步的主时钟来源。主时钟决定播放进度，其他流同步到主时钟。
   *
   * 选择原则：
   * - 有音频 → 用 AUDIO_MASTER（音频硬件稳定，音频卡顿体验差）
   * - 无音频 → 用 EXTERNAL_MASTER（系统时钟简单可靠）
   * - VIDEO_MASTER 仅用于特殊场景（视频逐帧分析等，音频可能卡顿）
   */
  enum class SyncMode {
    /**
     * 音频主时钟模式（推荐）
     *
     * 适用场景：
     * - 音频 + 视频（标准播放）
     * - 只有音频（音乐、播客等）
     *
     * 原理：
     * - 音频时钟由硬件驱动，最稳定
     * - 视频根据音频时钟调整显示时间
     * - 视频通过丢帧/重复帧适应音频
     *
     * 优势：
     * - 音频播放流畅，无卡顿
     * - 音画同步准确
     * - 用户体验最佳
     */
    AUDIO_MASTER,

    /**
     * 视频主时钟模式（特殊场景）
     *
     * 适用场景：
     * - 视频演示/教学（画面更重要）
     * - 视频逐帧分析工具
     *
     * 原理：
     * - 视频时钟作为基准
     * - 音频同步到视频（需要音频重采样）
     *
     * 缺点：
     * - 音频可能产生卡顿
     * - 用户体验较差
     * - 不推荐用于纯视频（应该用 EXTERNAL_MASTER）
     *
     * 注意：当前实现可能未完全支持音频重采样
     */
    VIDEO_MASTER,

    /**
     * 外部时钟/系统时钟模式
     *
     * 适用场景：
     * - 只有视频（GIF、静默视频等）
     * - 测试和调试
     *
     * 原理：
     * - 使用系统时钟（play_start_time_）作为基准
     * - 计算公式：master_clock = now - play_start_time_
     * - 视频 PTS 与 master_clock 比较，决定显示时间
     *
     * 优势：
     * - 简单直观，易于调试
     * - 无音频时的最佳选择
     * - 时钟行为可预测
     */
    EXTERNAL_MASTER
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
   * @brief 暂停同步控制器
   *
   * 暂停时会记录暂停开始时间，停止时钟推算。
   * 这样在暂停期间，GetMasterClock() 返回的时钟值不会增长。
   *
   * @note 必须在暂停音视频播放时调用
   * @thread_safety 线程安全
   */
  void Pause();

  /**
   * @brief 恢复同步控制器
   *
   * 恢复时会累计暂停时长，更新所有时钟的system_time，
   * 确保时钟推算排除暂停时间。
   *
   * @note 必须在恢复音视频播放时调用
   * @thread_safety 线程安全
   */
  void Resume();

  /**
   * @brief 获取当前同步模式
   */
  SyncMode GetSyncMode() const;

  /**
   * @brief 更新音频时钟
   *
   * 由AudioPlayer定期调用，更新音频播放进度。
   * 该函数会：
   * 1. 归一化PTS（第一帧设为0，后续帧相对于第一帧）
   * 2. 计算时钟漂移（drift）并慢速调整
   * 3. 更新同步统计
   *
   * @param audio_pts_ms 音频PTS毫秒数（**原始值，函数内部会归一化**）
   * @param system_time 系统时间戳
   *
   * @note 调用频率：约每0.5-1秒更新一次（AudioPlayer每100次callback）
   * @thread_safety 线程安全，使用clock_mutex_保护
   */
  void UpdateAudioClock(double audio_pts_ms,
                        std::chrono::steady_clock::time_point system_time);

  /**
   * @brief 更新视频时钟
   *
   * 由VideoRenderer每次渲染帧时调用，更新视频播放进度。
   * 该函数会：
   * 1. 归一化PTS（第一帧设为0，后续帧相对于第一帧）
   * 2. 计算时钟漂移（drift）并慢速调整
   * 3. 更新同步统计
   *
   * @param video_pts_ms 视频PTS毫秒数（**原始值，函数内部会归一化**）
   * @param system_time 系统时间戳
   *
   * @note 调用频率：每帧渲染时（30fps约33ms一次）
   * @thread_safety 线程安全，使用clock_mutex_保护
   */
  void UpdateVideoClock(double video_pts_ms,
                        std::chrono::steady_clock::time_point system_time);

  /**
   * @brief 获取主时钟时间
   *
   * 根据当前同步模式，返回主时钟的当前值。
   * - AUDIO_MASTER: 返回音频时钟（默认，最常用）
   * - VIDEO_MASTER: 返回视频时钟
   * - EXTERNAL_MASTER: 返回系统时钟
   *
   * @param current_time 当前系统时间
   * @return 主时钟毫秒数（**已归一化，第一帧为0**）
   *
   * @note 该函数会推算当前时钟值，即使上次更新是在之前
   * @thread_safety 线程安全，使用clock_mutex_保护
   */
  double GetMasterClock(
      std::chrono::steady_clock::time_point current_time) const;

  /**
   * @brief 计算视频帧显示延迟
   *
   * 根据视频帧PTS和主时钟，计算该帧应该延迟多久显示。
   * 这是音视频同步的核心算法。
   *
   * @param video_pts_ms
   * 视频帧PTS毫秒数（**传入原始PTS即可，函数内部会自动归一化**）
   *                     支持传入原始PTS或已归一化的PTS，函数会自动处理
   * @param current_time 当前系统时间
   * @return 建议的显示延迟毫秒数
   *         > 0: 视频超前，需要延迟显示
   *         = 0: 同步良好，立即显示
   *         < 0: 视频落后，需要加速显示（可能丢帧）
   *
   * @note 返回值被限制在[-max_video_speedup_ms, +max_video_delay_ms]范围内
   * @thread_safety 线程安全，使用clock_mutex_保护
   *
   * @implementation_note
   *    内部实现：
   *    1. 调用 NormalizeVideoPTS(video_pts_ms) 归一化PTS
   *    2. 获取主时钟 GetMasterClock()使用相同的归一化基准！
   *          推荐做法：都使用归一化后的PTS
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

  /**
   * @brief 归一化音频PTS
   *
   * 将原始PTS转换为从0开始的相对时间，便于同步计算
   *
   * @param raw_pts_ms 原始音频PTS（毫秒）
   * @return 归一化后的PTS（毫秒），第一帧为0
   *
   * @note 第一次调用时会记录起始PTS作为基准
   * @note 线程安全，需要在clock_mutex_保护下调用
   */
  double NormalizeAudioPTS(double raw_pts_ms);

  /**
   * @brief 归一化视频PTS
   *
   * 将原始PTS转换为从0开始的相对时间，便于同步计算
   *
   * @param raw_pts_ms 原始视频PTS（毫秒）
   * @return 归一化后的PTS（毫秒），第一帧为0
   *
   * @note 第一次调用时会记录起始PTS作为基准
   * @note 线程安全，需要在clock_mutex_保护下调用
   */
  double NormalizeVideoPTS(double raw_pts_ms);

 private:
  /**
   * @brief 更新同步统计到 StatisticsManager
   */
  void UpdateSyncStats();

  /**
   * @brief 时钟信息
   *
   * 用于记录音频/视频时钟的状态，支持时钟推算
   */
  struct ClockInfo {
    std::atomic<double> pts_ms{0.0};  // 上次更新时的PTS时间戳（毫秒）
    std::chrono::steady_clock::time_point system_time;  // 上次更新时的系统时间
    std::atomic<double> drift{0.0};                     // 时钟漂移（毫秒）

    /**
     * @brief 获取当前时钟值（通过推算）
     *
     * 原理：clock(now) = pts + (now - system_time) + drift
     *
     * @param now 当前系统时间
     * @return 推算出的当前时钟值（毫秒）
     *
     * 示例：
     *   上次更新时：pts_ms = 1000ms, system_time = T0
     *   现在查询时：now = T0 + 500ms
     *   计算：elapsed = 500ms
     *         current_time = 1000 + 500 + drift = 1500ms + drift
     *
     * 暂停处理：
     *   Resume() 时会调整 system_time += pause_duration
     *   这样 elapsed = now - (system_time + pause_duration)
     *          = now - system_time - pause_duration
     *   自动排除了暂停时间，无需额外计算！
     */
    double GetCurrentTime(std::chrono::steady_clock::time_point now) const {
      auto elapsed_ms =
          std::chrono::duration<double, std::milli>(now - system_time).count();
      return pts_ms.load() + elapsed_ms + drift.load();
    }
  };

  SyncMode sync_mode_;
  SyncParams sync_params_;

  // === 时钟管理 ===
  mutable std::mutex clock_mutex_;
  ClockInfo audio_clock_;     // 音频时钟
  ClockInfo video_clock_;     // 视频时钟
  ClockInfo external_clock_;  // 外部时钟（系统时钟）

  // === PTS归一化基准 ===
  // 用于将原始PTS转换为从0开始的相对时间
  bool audio_start_initialized_{false};  // 是否已记录音频起始PTS
  double audio_start_pts_ms_{0.0};  // 音频第一帧的原始PTS（作为归一化基准）
  bool video_start_initialized_{false};  // 是否已记录视频起始PTS
  double video_start_pts_ms_{0.0};  // 视频第一帧的原始PTS（作为归一化基准）

  // === 同步统计 ===
  mutable std::mutex stats_mutex_;
  static const size_t SYNC_HISTORY_SIZE = 100;
  std::vector<double> sync_error_history_;  // 同步误差历史（用于计算平均值）
  size_t sync_history_index_;               // 当前历史索引
  int64_t sync_corrections_{0};             // 同步校正次数

  // === 播放状态 ===
  std::chrono::steady_clock::time_point
      play_start_time_;  // 播放开始时间（用于外部时钟模式）
  bool is_initialized_;  // 是否已初始化

  // === 暂停状态管理 ===
  mutable std::mutex pause_mutex_;  // 保护暂停状态的互斥锁
  bool is_paused_{false};           // 是否处于暂停状态
  std::chrono::steady_clock::time_point
      pause_start_time_;  // 本次暂停的开始时间
  std::chrono::steady_clock::duration
      accumulated_pause_duration_;  // 累计暂停时长（用于日志统计）
};

} // namespace zenplay
