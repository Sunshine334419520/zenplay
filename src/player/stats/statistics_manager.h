#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>

#include "player/common/log_manager.h"
#include "player/common/timer.h"
#include "stats_types.h"

namespace zenplay {
namespace stats {

class StatisticsManager {
 public:
  explicit StatisticsManager(const StatsConfig& config = StatsConfig{});
  ~StatisticsManager();

  // === 全局控制接口 ===
  static void SetGlobalEnabled(bool enabled);
  static bool IsGlobalEnabled();
  static StatisticsManager* GetInstance();
  static void Initialize(const StatsConfig& config = StatsConfig{});
  static void Shutdown();

  // === 统计数据更新接口 ===
  void UpdateDemuxStats(uint32_t packets_read,
                        uint64_t bytes_read,
                        double read_time_ms,
                        bool is_video);
  void UpdateDecodeStats(bool is_video,
                         bool decode_success,
                         double decode_time_ms,
                         uint32_t queue_size);
  void UpdateRenderStats(bool is_video,
                         bool frame_rendered,
                         bool frame_dropped,
                         double render_time_ms);
  void UpdateSyncStats(double av_offset_ms, bool is_in_sync);
  void UpdateSystemStats(double cpu_percent, uint64_t memory_mb);
  void UpdateNetworkStats(double download_kbps, uint64_t bytes_downloaded);

  // === 统计数据获取接口 ===
  const PipelineStats& GetPipelineStats() const;
  const SyncQualityStats& GetSyncStats() const;
  const SystemResourceStats& GetSystemStats() const;
  const NetworkStats& GetNetworkStats() const;

  // === 问题诊断接口 ===
  PerformanceBottleneck AnalyzeBottlenecks() const;
  std::string GenerateReport() const;

  // === 配置接口 ===
  void SetReportInterval(std::chrono::milliseconds interval);
  void EnableAutoLogging(bool enable);
  void SetConfig(const StatsConfig& config);
  StatsConfig GetConfig() const;

  // === 控制接口 ===
  void Start();
  void Stop();
  void Reset();

 private:
  void CalculateRates();         // 计算各种速率
  void DetectBottlenecks();      // 检测性能瓶颈
  void LogStatistics();          // 输出统计日志
  void ResetCounters();          // 重置区间计数器
  void OnReportTimer();          // Timer回调函数
  void InitializeStatsLogger();  // 初始化统计日志记录器

  // 全局控制
  static std::atomic<bool> global_enabled_;
  static std::unique_ptr<StatisticsManager> instance_;
  static std::mutex instance_mutex_;

  // 实例配置和状态
  StatsConfig config_;
  std::atomic<bool> running_{false};

  // 统计数据
  mutable std::mutex stats_mutex_;
  PipelineStats pipeline_stats_;
  SyncQualityStats sync_stats_;
  SystemResourceStats system_stats_;
  NetworkStats network_stats_;
  PerformanceBottleneck last_bottleneck_;

  // 时间管理
  std::chrono::steady_clock::time_point last_report_time_;
  std::chrono::steady_clock::time_point start_time_;

  // Timer管理
  std::unique_ptr<Timer> report_timer_;

  // 日志管理
  std::shared_ptr<spdlog::logger> stats_logger_;
};

}  // namespace stats
}  // namespace zenplay

// 便捷宏定义 - 只在全局开关打开时才执行
#define STATS_UPDATE_DEMUX(packets, bytes, time_ms, is_video)           \
  do {                                                                  \
    if (zenplay::stats::StatisticsManager::IsGlobalEnabled()) {         \
      auto* manager = zenplay::stats::StatisticsManager::GetInstance(); \
      if (manager)                                                      \
        manager->UpdateDemuxStats(packets, bytes, time_ms, is_video);   \
    }                                                                   \
  } while (0)

#define STATS_UPDATE_DECODE(is_video, success, time_ms, queue_size)         \
  do {                                                                      \
    if (zenplay::stats::StatisticsManager::IsGlobalEnabled()) {             \
      auto* manager = zenplay::stats::StatisticsManager::GetInstance();     \
      if (manager)                                                          \
        manager->UpdateDecodeStats(is_video, success, time_ms, queue_size); \
    }                                                                       \
  } while (0)

#define STATS_UPDATE_RENDER(is_video, rendered, dropped, time_ms)         \
  do {                                                                    \
    if (zenplay::stats::StatisticsManager::IsGlobalEnabled()) {           \
      auto* manager = zenplay::stats::StatisticsManager::GetInstance();   \
      if (manager)                                                        \
        manager->UpdateRenderStats(is_video, rendered, dropped, time_ms); \
    }                                                                     \
  } while (0)

#define STATS_UPDATE_SYNC(offset_ms, in_sync)                           \
  do {                                                                  \
    if (zenplay::stats::StatisticsManager::IsGlobalEnabled()) {         \
      auto* manager = zenplay::stats::StatisticsManager::GetInstance(); \
      if (manager)                                                      \
        manager->UpdateSyncStats(offset_ms, in_sync);                   \
    }                                                                   \
  } while (0)

#define STATS_UPDATE_SYSTEM(cpu_percent, memory_mb)                     \
  do {                                                                  \
    if (zenplay::stats::StatisticsManager::IsGlobalEnabled()) {         \
      auto* manager = zenplay::stats::StatisticsManager::GetInstance(); \
      if (manager)                                                      \
        manager->UpdateSystemStats(cpu_percent, memory_mb);             \
    }                                                                   \
  } while (0)

#define STATS_UPDATE_NETWORK(download_kbps, bytes_total)                \
  do {                                                                  \
    if (zenplay::stats::StatisticsManager::IsGlobalEnabled()) {         \
      auto* manager = zenplay::stats::StatisticsManager::GetInstance(); \
      if (manager)                                                      \
        manager->UpdateNetworkStats(download_kbps, bytes_total);        \
    }                                                                   \
  } while (0)
