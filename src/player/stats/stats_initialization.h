/**
 * @file stats_initialization.h
 * @brief ZenPlay统计系统集成指南和推荐实现
 */

#pragma once

#include "player/stats/statistics_manager.h"

namespace zenplay {
namespace stats {

/**
 * @brief 统计系统集成的推荐配置
 */
inline StatsConfig CreateRecommendedConfig() {
  StatsConfig config;

  // 基础设置
  config.enabled = true;
  config.report_interval = std::chrono::seconds(5);  // 5秒报告间隔

  // 日志设置
  config.auto_logging = true;                       // 自动记录统计日志
  config.separate_log_file = true;                  // 使用独立日志文件
  config.log_file_path = "logs/zenplay_stats.log";  // 统计日志路径

  // 高级功能
  config.enable_bottleneck_detection = true;  // 启用瓶颈检测

  return config;
}

/**
 * @brief 在main.cpp中的推荐初始化代码
 *
 * 应该在以下位置调用：
 * 1. 日志系统初始化之后
 * 2. Loki消息循环初始化之前
 * 3. 主窗口创建之前
 */
inline bool InitializeStatsSystem() {
  try {
    auto config = CreateRecommendedConfig();
    StatisticsManager::Initialize(config);
    StatisticsManager::SetGlobalEnabled(true);

    MODULE_INFO(
        LOG_MODULE_STATS,
        "Statistics system initialized with config: "
        "enabled=true, interval={}s, auto_logging=true, separate_log=true",
        config.report_interval.count());
    return true;
  } catch (const std::exception& e) {
    ZENPLAY_ERROR("Failed to initialize statistics system: {}", e.what());
    return false;
  }
}

/**
 * @brief 在main.cpp中的推荐关闭代码
 *
 * 应该在以下位置调用：
 * 1. app.exec()执行完成之后
 * 2. 日志系统关闭之前
 */
inline void ShutdownStatsSystem() {
  try {
    if (auto* stats = StatisticsManager::GetInstance()) {
      // 生成最终统计报告
      std::string final_report = stats->GenerateReport();
      MODULE_INFO(LOG_MODULE_STATS, "Final Statistics Report:\n{}",
                  final_report);
    }

    StatisticsManager::Shutdown();
    MODULE_INFO(LOG_MODULE_STATS, "Statistics system shutdown completed");
  } catch (const std::exception& e) {
    ZENPLAY_ERROR("Error during statistics system shutdown: {}", e.what());
  }
}

}  // namespace stats
}  // namespace zenplay
