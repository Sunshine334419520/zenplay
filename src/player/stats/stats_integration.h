/**
 * @file stats_integration.h
 * @brief 统计系统集成示例和用法指南
 *
 * 演示如何在ZenPlay各组件中集成统计系统的采集点
 */

#pragma once

#include "statistics_manager.h"

namespace zenplay {
namespace stats {

/**
 * @class StatsIntegrationGuide
 * @brief 统计系统集成指南和用法示例
 *
 * 提供在各个播放器组件中集成统计采集的具体实现方法
 */
class StatsIntegrationGuide {
 public:
  // ===== Demuxer 集成示例 =====

  /**
   * @brief 在Demuxer::ReadFrame中的集成示例
   * @code
   * bool Demuxer::ReadFrame() {
   *   auto start_time = std::chrono::steady_clock::now();
   *
   *   // 执行解复用操作
   *   AVPacket* packet = av_packet_alloc();
   *   int ret = av_read_frame(format_context_, packet);
   *
   *   auto end_time = std::chrono::steady_clock::now();
   *   double read_time_ms = std::chrono::duration<double, std::milli>(
   *       end_time - start_time).count();
   *
   *   if (ret >= 0) {
   *     // 更新统计信息
   *     STATS_UPDATE_DEMUX(1, packet->size, read_time_ms,
   *                        packet->stream_index == video_stream_index_);
   *
   *     // 推送数据到解码队列
   *     PushToDecodeQueue(packet);
   *     return true;
   *   } else {
   *     // 记录读取错误
   *     STATS_UPDATE_DEMUX_ERROR();
   *     av_packet_free(&packet);
   *     return false;
   *   }
   * }
   * @endcode
   */
  static void DemuxerIntegrationExample();

  // ===== VideoDecoder 集成示例 =====

  /**
   * @brief 在VideoDecoder::DecodeFrame中的集成示例
   * @code
   * bool VideoDecoder::DecodeFrame(AVPacket* packet) {
   *   auto start_time = std::chrono::steady_clock::now();
   *
   *   // 发送包到解码器
   *   int ret = avcodec_send_packet(codec_context_, packet);
   *   if (ret < 0) {
   *     STATS_UPDATE_DECODE(true, false, 0.0, frame_queue_.size());
   *     return false;
   *   }
   *
   *   // 接收解码的帧
   *   AVFrame* frame = av_frame_alloc();
   *   ret = avcodec_receive_frame(codec_context_, frame);
   *
   *   auto end_time = std::chrono::steady_clock::now();
   *   double decode_time_ms = std::chrono::duration<double, std::milli>(
   *       end_time - start_time).count();
   *
   *   if (ret >= 0) {
   *     // 成功解码
   *     frame_queue_.push(std::shared_ptr<AVFrame>(frame, av_frame_free));
   *     STATS_UPDATE_DECODE(true, true, decode_time_ms, frame_queue_.size());
   *     return true;
   *   } else {
   *     // 解码失败
   *     STATS_UPDATE_DECODE(true, false, decode_time_ms, frame_queue_.size());
   *     av_frame_free(&frame);
   *     return false;
   *   }
   * }
   * @endcode
   */
  static void VideoDecoderIntegrationExample();

  // ===== VideoPlayer 渲染集成示例 =====

  /**
   * @brief 在VideoPlayer::RenderFrame中的集成示例
   * @code
   * void VideoPlayer::RenderFrame() {
   *   if (frame_queue_.empty()) {
   *     return;
   *   }
   *
   *   auto frame = frame_queue_.front();
   *   frame_queue_.pop();
   *
   *   // 检查是否需要丢帧
   *   bool should_drop = ShouldDropFrame(frame);
   *   if (should_drop) {
   *     STATS_UPDATE_RENDER(true, false, true, 0.0);
   *     return;
   *   }
   *
   *   auto render_start = std::chrono::steady_clock::now();
   *
   *   // 执行渲染
   *   bool render_success = renderer_->RenderFrame(frame);
   *
   *   auto render_end = std::chrono::steady_clock::now();
   *   double render_time_ms = std::chrono::duration<double, std::milli>(
   *       render_end - render_start).count();
   *
   *   // 更新渲染统计
   *   STATS_UPDATE_RENDER(true, render_success, false, render_time_ms);
   *
   *   // 更新同步统计
   *   if (av_sync_controller_) {
   *     double av_offset = av_sync_controller_->GetAVSyncOffset();
   *     bool in_sync = std::abs(av_offset) <= 40.0;
   *     STATS_UPDATE_SYNC(av_offset, in_sync);
   *   }
   * }
   * @endcode
   */
  static void VideoPlayerIntegrationExample();

  // ===== 主程序初始化示例 =====

  /**
   * @brief 在main()函数中的统计系统初始化示例
   * @code
   * int main(int argc, char* argv[]) {
   *   QApplication app(argc, argv);
   *
   *   // 初始化日志系统
   *   zenplay::LogManager::Initialize();
   *
   *   // 配置统计系统
   *   zenplay::stats::StatsConfig config;
   *   config.enabled = true;
   *   config.report_interval = std::chrono::seconds(5);
   *   config.auto_logging = true;
   *   config.separate_log_file = true;
   *   config.log_file_path = "logs/zenplay_stats.log";
   *   config.enable_bottleneck_detection = true;
   *
   *   // 初始化统计管理器
   *   zenplay::stats::StatisticsManager::Initialize(config);
   *
   *   // 启用全局统计
   *   zenplay::stats::StatisticsManager::SetGlobalEnabled(true);
   *
   *   // 创建主窗口
   *   MainWindow window;
   *   window.show();
   *
   *   int result = app.exec();
   *
   *   // 清理统计系统
   *   zenplay::stats::StatisticsManager::Shutdown();
   *   zenplay::LogManager::Shutdown();
   *
   *   return result;
   * }
   * @endcode
   */
  static void MainApplicationIntegrationExample();

  // ===== 性能监控和调试示例 =====

  /**
   * @brief 性能监控和问题诊断的使用示例
   * @code
   * void PerformanceMonitor::CheckForBottlenecks() {
   *   auto* stats_mgr = zenplay::stats::StatisticsManager::GetInstance();
   *   if (!stats_mgr) return;
   *
   *   // 获取管道统计信息
   *   auto pipeline_stats = stats_mgr->GetPipelineStats();
   *
   *   // 检查解码队列积压
   *   if (pipeline_stats.video_decode.queue_usage_percent.load() > 90.0) {
   *     MODULE_WARN(LOG_MODULE_STATS, "Video decode queue nearly full: {}%",
   *                 pipeline_stats.video_decode.queue_usage_percent.load());
   *   }
   *
   *   // 检查丢帧率
   *   if (pipeline_stats.video_render.frame_drop_rate.load() > 5.0) {
   *     MODULE_WARN(LOG_MODULE_STATS, "High frame drop rate: {}%",
   *                 pipeline_stats.video_render.frame_drop_rate.load());
   *   }
   *
   *   // 检查AV同步
   *   auto sync_stats = stats_mgr->GetSyncStats();
   *   if (std::abs(sync_stats.av_sync_offset_ms.load()) > 100.0) {
   *     MODULE_WARN(LOG_MODULE_STATS, "AV sync drift detected: {} ms",
   *                 sync_stats.av_sync_offset_ms.load());
   *   }
   *
   *   // 分析性能瓶颈
   *   auto bottleneck = stats_mgr->AnalyzeBottlenecks();
   *   if (bottleneck.severity_score > 0.7) {
   *     MODULE_ERROR(LOG_MODULE_STATS, "Performance bottleneck: {}",
   *                  bottleneck.description);
   *   }
   * }
   * @endcode
   */
  static void PerformanceMonitoringExample();

  // ===== UI统计显示示例 =====

  /**
   * @brief 在UI中显示统计信息的示例
   * @code
   * void MainWindow::UpdateStatsDisplay() {
   *   auto* stats_mgr = zenplay::stats::StatisticsManager::GetInstance();
   *   if (!stats_mgr || !stats_display_enabled_) return;
   *
   *   auto pipeline_stats = stats_mgr->GetPipelineStats();
   *   auto sync_stats = stats_mgr->GetSyncStats();
   *
   *   // 更新状态栏信息
   *   QString stats_text = QString("FPS: %1 | Dropped: %2% | AV Sync: %3ms")
   *       .arg(pipeline_stats.video_render.render_rate_fps.load(), 0, 'f', 1)
   *       .arg(pipeline_stats.video_render.frame_drop_rate.load(), 0, 'f', 1)
   *       .arg(sync_stats.av_sync_offset_ms.load(), 0, 'f', 1);
   *
   *   status_label_->setText(stats_text);
   *
   *   // 如果有详细统计窗口
   *   if (stats_window_ && stats_window_->isVisible()) {
   *     QString detailed_report = QString::fromStdString(
   *         stats_mgr->GenerateReport());
   *     stats_window_->UpdateReport(detailed_report);
   *   }
   * }
   * @endcode
   */
  static void UIStatsDisplayExample();

  // ===== 运行时配置调整示例 =====

  /**
   * @brief 运行时动态调整统计配置的示例
   * @code
   * void SettingsDialog::OnStatsConfigChanged() {
   *   auto* stats_mgr = zenplay::stats::StatisticsManager::GetInstance();
   *   if (!stats_mgr) return;
   *
   *   // 动态开启/关闭统计
   *   bool enabled = stats_enable_checkbox_->isChecked();
   *   zenplay::stats::StatisticsManager::SetGlobalEnabled(enabled);
   *
   *   // 调整报告间隔
   *   int interval_seconds = report_interval_spinbox_->value();
   *   stats_mgr->SetReportInterval(std::chrono::seconds(interval_seconds));
   *
   *   // 开启/关闭自动日志
   *   bool auto_logging = auto_logging_checkbox_->isChecked();
   *   stats_mgr->EnableAutoLogging(auto_logging);
   *
   *   MODULE_INFO(LOG_MODULE_STATS, "Statistics configuration updated: "
   *              "enabled={}, interval={}s, auto_logging={}",
   *              enabled, interval_seconds, auto_logging);
   * }
   * @endcode
   */
  static void RuntimeConfigurationExample();
};

/**
 * @brief 统计系统最佳实践建议
 *
 * 1. 性能考虑：
 *    - 使用STATS_*宏进行条件编译，避免不必要的性能开销
 *    - 原子变量操作避免了锁开销，但仍需谨慎使用
 *    - 在高频调用路径中，优先更新计数器，延迟计算派生指标
 *
 * 2. 集成原则：
 *    - 在关键性能点添加统计采集（解复用、解码、渲染）
 *    - 记录成功/失败状态，便于问题诊断
 *    - 包含时间测量，识别性能瓶颈
 *
 * 3. 错误处理：
 *    - 统计系统失败不应影响主要功能
 *    - 使用全局开关快速禁用统计收集
 *    - 提供降级方案（使用通用日志而非专用统计日志）
 *
 * 4. 调试和监控：
 *    - 定期输出统计报告到日志
 *    - 在UI中显示关键指标
 *    - 实现瓶颈自动检测和告警
 *
 * 5. 扩展性：
 *    - 新增统计类型时，同时更新数据结构和宏定义
 *    - 考虑向后兼容性，避免破坏现有API
 *    - 支持运行时配置调整
 */

}  // namespace stats
}  // namespace zenplay
