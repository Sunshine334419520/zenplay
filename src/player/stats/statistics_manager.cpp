#include "statistics_manager.h"

#include <spdlog/sinks/basic_file_sink.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace zenplay {
namespace stats {

// 静态成员初始化
std::atomic<bool> StatisticsManager::global_enabled_{true};
std::unique_ptr<StatisticsManager> StatisticsManager::instance_{nullptr};
std::mutex StatisticsManager::instance_mutex_;

StatisticsManager::StatisticsManager(const StatsConfig& config)
    : config_(config),
      last_report_time_(std::chrono::steady_clock::now()),
      start_time_(std::chrono::steady_clock::now()) {
  InitializeStatsLogger();
}

StatisticsManager::~StatisticsManager() {
  Stop();
}

// === 全局控制接口 ===
void StatisticsManager::SetGlobalEnabled(bool enabled) {
  global_enabled_.store(enabled);
  MODULE_INFO(LOG_MODULE_STATS, "Statistics system global switch: {}",
              enabled ? "ENABLED" : "DISABLED");
}

bool StatisticsManager::IsGlobalEnabled() {
  return global_enabled_.load();
}

StatisticsManager* StatisticsManager::GetInstance() {
  std::lock_guard<std::mutex> lock(instance_mutex_);
  return instance_.get();
}

void StatisticsManager::Initialize(const StatsConfig& config) {
  std::lock_guard<std::mutex> lock(instance_mutex_);
  if (!instance_) {
    instance_ = std::make_unique<StatisticsManager>(config);
    instance_->Start();
    MODULE_INFO(LOG_MODULE_STATS, "Statistics Manager initialized");
  }
}

void StatisticsManager::Shutdown() {
  std::lock_guard<std::mutex> lock(instance_mutex_);
  if (instance_) {
    instance_->Stop();
    instance_.reset();
    MODULE_INFO(LOG_MODULE_STATS, "Statistics Manager shutdown");
  }
}

// === 统计数据更新接口 ===
void StatisticsManager::UpdateDemuxStats(uint32_t packets_read,
                                         uint64_t bytes_read,
                                         double read_time_ms,
                                         bool is_video) {
  if (!global_enabled_.load() || !config_.enabled) {
    return;
  }

  std::lock_guard<std::mutex> lock(stats_mutex_);
  auto& demux = pipeline_stats_.demux;

  demux.packets_read_total.fetch_add(packets_read);
  demux.bytes_read_total.fetch_add(bytes_read);
  demux.packets_in_interval.fetch_add(packets_read);
  demux.bytes_in_interval.fetch_add(bytes_read);
  double old_total_time = demux.total_read_time_ms.load();
  demux.total_read_time_ms.store(old_total_time + read_time_ms);

  if (is_video) {
    demux.packets_read_video.fetch_add(packets_read);
  } else {
    demux.packets_read_audio.fetch_add(packets_read);
  }

  // 更新平均读取时间
  uint64_t total_packets = demux.packets_read_total.load();
  if (total_packets > 0) {
    double new_avg =
        (demux.total_read_time_ms.load() + read_time_ms) / total_packets;
    demux.avg_read_time_ms.store(new_avg);
  }
}

void StatisticsManager::UpdateDecodeStats(bool is_video,
                                          bool decode_success,
                                          double decode_time_ms,
                                          uint32_t queue_size) {
  if (!global_enabled_.load() || !config_.enabled) {
    return;
  }

  std::lock_guard<std::mutex> lock(stats_mutex_);
  auto& decode_stats =
      is_video ? pipeline_stats_.video_decode : pipeline_stats_.audio_decode;

  decode_stats.frames_input.fetch_add(1);
  decode_stats.queue_size.store(queue_size);
  double old_time = decode_stats.total_decode_time_ms.load();
  decode_stats.total_decode_time_ms.store(old_time + decode_time_ms);

  if (decode_success) {
    decode_stats.frames_decoded.fetch_add(1);
    decode_stats.frames_in_interval.fetch_add(1);
  } else {
    decode_stats.decode_errors.fetch_add(1);
  }

  // 更新队列使用率
  if (decode_stats.queue_max_size > 0) {
    decode_stats.queue_usage_percent.store(
        (static_cast<double>(queue_size) / decode_stats.queue_max_size) *
        100.0);
  }

  // 更新平均解码时间
  uint64_t total_frames = decode_stats.frames_decoded.load();
  if (total_frames > 0) {
    decode_stats.avg_decode_time_ms.store(
        decode_stats.total_decode_time_ms.load() / total_frames);
  }
}

void StatisticsManager::UpdateRenderStats(bool is_video,
                                          bool frame_rendered,
                                          bool frame_dropped,
                                          double render_time_ms) {
  if (!global_enabled_.load() || !config_.enabled) {
    return;
  }

  std::lock_guard<std::mutex> lock(stats_mutex_);
  auto& render_stats =
      is_video ? pipeline_stats_.video_render : pipeline_stats_.audio_render;

  render_stats.frames_received.fetch_add(1);
  render_stats.frames_received_in_interval.fetch_add(1);

  if (frame_rendered) {
    render_stats.frames_rendered.fetch_add(1);
    render_stats.frames_rendered_in_interval.fetch_add(1);
    double old_render_time = render_stats.total_render_time_ms.load();
    render_stats.total_render_time_ms.store(old_render_time + render_time_ms);

    // 更新平均渲染时间
    uint64_t total_rendered = render_stats.frames_rendered.load();
    if (total_rendered > 0) {
      render_stats.avg_render_time_ms.store(
          render_stats.total_render_time_ms.load() / total_rendered);
    }
  }

  if (frame_dropped) {
    render_stats.frames_dropped.fetch_add(1);
  }

  // 更新丢帧率
  uint64_t total_received = render_stats.frames_received.load();
  if (total_received > 0) {
    uint64_t dropped = render_stats.frames_dropped.load();
    render_stats.frame_drop_rate.store(
        (static_cast<double>(dropped) / total_received) * 100.0);
  }
}

void StatisticsManager::UpdateSyncStats(double audio_clock_ms,
                                        double video_clock_ms,
                                        double sync_offset_ms,
                                        double avg_sync_error_ms,
                                        double max_sync_error_ms,
                                        int64_t sync_corrections) {
  if (!global_enabled_.load() || !config_.enabled) {
    return;
  }

  std::lock_guard<std::mutex> lock(stats_mutex_);
  auto& sync = sync_stats_;

  // 更新时钟信息
  sync.audio_clock_ms.store(audio_clock_ms);
  sync.video_clock_ms.store(video_clock_ms);

  // 更新同步信息
  sync.av_sync_offset_ms.store(sync_offset_ms);
  sync.avg_sync_offset_ms.store(avg_sync_error_ms);
  sync.max_sync_error_ms.store(max_sync_error_ms);
  sync.sync_corrections.store(sync_corrections);

  // 更新同步状态
  bool in_sync = std::abs(sync_offset_ms) < 40.0;
  sync.is_in_sync.store(in_sync);

  // 更新同步质量评级
  double abs_offset = std::abs(sync_offset_ms);
  if (abs_offset < 20.0) {
    sync.sync_quality = "Excellent";
  } else if (abs_offset < 40.0) {
    sync.sync_quality = "Good";
  } else if (abs_offset < 80.0) {
    sync.sync_quality = "Fair";
  } else {
    sync.sync_quality = "Poor";
  }
}

void StatisticsManager::UpdateSystemStats(double cpu_percent,
                                          uint64_t memory_mb) {
  if (!global_enabled_.load() || !config_.enabled) {
    return;
  }

  std::lock_guard<std::mutex> lock(stats_mutex_);
  system_stats_.cpu_usage_percent.store(cpu_percent);
  system_stats_.memory_usage_mb.store(memory_mb);
}

void StatisticsManager::UpdateNetworkStats(double download_kbps,
                                           uint64_t bytes_downloaded) {
  if (!global_enabled_.load() || !config_.enabled) {
    return;
  }

  std::lock_guard<std::mutex> lock(stats_mutex_);
  network_stats_.download_rate_kbps.store(download_kbps);
  network_stats_.bytes_downloaded.store(bytes_downloaded);
}

// === 统计数据获取接口 ===
const PipelineStats& StatisticsManager::GetPipelineStats() const {
  std::lock_guard<std::mutex> lock(stats_mutex_);
  return pipeline_stats_;
}

const SyncQualityStats& StatisticsManager::GetSyncStats() const {
  std::lock_guard<std::mutex> lock(stats_mutex_);
  return sync_stats_;
}

const SystemResourceStats& StatisticsManager::GetSystemStats() const {
  std::lock_guard<std::mutex> lock(stats_mutex_);
  return system_stats_;
}

const NetworkStats& StatisticsManager::GetNetworkStats() const {
  std::lock_guard<std::mutex> lock(stats_mutex_);
  return network_stats_;
}

// === 问题诊断接口 ===
PerformanceBottleneck StatisticsManager::AnalyzeBottlenecks() const {
  // TODO: 实现瓶颈检测算法
  PerformanceBottleneck bottleneck;
  bottleneck.primary_bottleneck = PerformanceBottleneck::BottleneckType::None;
  bottleneck.severity_score = 0.0;
  bottleneck.description = "No bottlenecks detected";
  bottleneck.recommendation = "System running normally";
  return bottleneck;
}

std::string StatisticsManager::GenerateReport() const {
  std::lock_guard<std::mutex> lock(stats_mutex_);
  std::ostringstream report;

  auto now = std::chrono::steady_clock::now();
  auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_);

  report << "===== ZenPlay Performance Report (Runtime: " << elapsed.count()
         << "ms) =====\n";

  // Pipeline Stats
  report << "Pipeline Stats:\n";

  // Demux
  const auto& demux = pipeline_stats_.demux;
  report << "  Demux    -> Read: " << std::fixed << std::setprecision(1)
         << demux.read_rate_pps.load() << " pkts/s ("
         << (demux.read_rate_bps.load() / 1024.0 / 1024.0) << "MB/s), "
         << "Errors: " << demux.read_errors.load() << ", "
         << "AvgTime: " << demux.avg_read_time_ms.load() << "ms\n";

  // Video Decode
  const auto& vdec = pipeline_stats_.video_decode;
  report << "  VideoDec -> Input: " << vdec.frames_input.load() << ", "
         << "Decoded: " << vdec.decode_rate_fps.load() << "fps, "
         << "Queue: " << vdec.queue_size.load() << "/" << vdec.queue_max_size
         << " (" << std::setprecision(1) << vdec.queue_usage_percent.load()
         << "%), "
         << "AvgTime: " << vdec.avg_decode_time_ms.load() << "ms\n";

  // Audio Decode
  const auto& adec = pipeline_stats_.audio_decode;
  report << "  AudioDec -> Input: " << adec.frames_input.load() << ", "
         << "Decoded: " << adec.decode_rate_fps.load() << "fps, "
         << "Queue: " << adec.queue_size.load() << "/" << adec.queue_max_size
         << " (" << std::setprecision(1) << adec.queue_usage_percent.load()
         << "%), "
         << "AvgTime: " << adec.avg_decode_time_ms.load() << "ms\n";

  // Video Render
  const auto& vrnd = pipeline_stats_.video_render;
  report << "  VideoRnd -> Received: " << vrnd.frames_received.load() << ", "
         << "Rendered: " << vrnd.render_rate_fps.load() << "fps, "
         << "Dropped: " << vrnd.frames_dropped.load() << " ("
         << std::setprecision(1) << vrnd.frame_drop_rate.load() << "%), "
         << "AvgTime: " << vrnd.avg_render_time_ms.load() << "ms\n";

  // Audio Render
  const auto& arnd = pipeline_stats_.audio_render;
  report << "  AudioRnd -> Received: " << arnd.frames_received.load() << ", "
         << "Rendered: " << arnd.render_rate_fps.load() << "fps, "
         << "Dropped: " << arnd.frames_dropped.load() << " ("
         << std::setprecision(1) << arnd.frame_drop_rate.load() << "%), "
         << "AvgTime: " << arnd.avg_render_time_ms.load() << "ms\n";

  // Sync Stats
  const auto& sync = sync_stats_;
  report << "Sync Stats:\n";
  report << "  Clock    -> Audio: " << std::fixed << std::setprecision(2)
         << sync.audio_clock_ms.load() << "ms, "
         << "Video: " << sync.video_clock_ms.load() << "ms\n";
  report << "  AV Sync  -> Offset: " << std::showpos << std::setprecision(2)
         << sync.av_sync_offset_ms.load() << "ms" << std::noshowpos
         << ", Avg: " << sync.avg_sync_offset_ms.load() << "ms"
         << ", Max: " << sync.max_sync_error_ms.load() << "ms\n";
  report << "  Quality  -> " << sync.sync_quality
         << " (InSync: " << (sync.is_in_sync.load() ? "Yes" : "No") << ")"
         << ", Corrections: " << sync.sync_corrections.load() << "\n";

  // System Stats
  const auto& sys = system_stats_;
  report << "System Stats:\n";
  report << "  CPU: " << sys.cpu_usage_percent.load() << "%, "
         << "Memory: " << sys.memory_usage_mb.load() << "MB, "
         << "GPU: " << sys.gpu_memory_mb.load() << "MB, "
         << "Threads: " << sys.thread_count.load() << "\n";

  // Bottleneck Analysis
  auto bottleneck = AnalyzeBottlenecks();
  report << "Bottleneck Analysis: Primary="
         << static_cast<int>(bottleneck.primary_bottleneck)
         << ", Severity=" << bottleneck.severity_score << "\n";

  report << "===============================================================";

  return report.str();
}

// === 配置接口 ===
void StatisticsManager::SetReportInterval(std::chrono::milliseconds interval) {
  std::lock_guard<std::mutex> lock(stats_mutex_);
  config_.report_interval = interval;
}

void StatisticsManager::EnableAutoLogging(bool enable) {
  std::lock_guard<std::mutex> lock(stats_mutex_);
  config_.auto_logging = enable;
}

void StatisticsManager::SetConfig(const StatsConfig& config) {
  std::lock_guard<std::mutex> lock(stats_mutex_);
  config_ = config;
}

StatsConfig StatisticsManager::GetConfig() const {
  std::lock_guard<std::mutex> lock(stats_mutex_);
  return config_;
}

// === 控制接口 ===
void StatisticsManager::Start() {
  if (running_.exchange(true)) {
    return;
  }

  last_report_time_ = std::chrono::steady_clock::now();

  if (config_.auto_logging) {
    // 使用Timer替代手动线程管理
    int interval_ms = static_cast<int>(config_.report_interval.count());
    report_timer_ = TimerFactory::CreateRepeating(
        interval_ms, [this]() { OnReportTimer(); });

    if (report_timer_) {
      report_timer_->Start();
    }
  }

  MODULE_INFO(LOG_MODULE_STATS, "Statistics Manager started");
}

void StatisticsManager::Stop() {
  if (!running_.exchange(false)) {
    return;
  }

  // 停止Timer（自动清理线程资源）
  if (report_timer_) {
    report_timer_->Stop();
    report_timer_.reset();
  }

  // 输出最终报告
  if (config_.auto_logging && stats_logger_) {
    LogStatistics();
  }

  MODULE_INFO(LOG_MODULE_STATS, "Statistics Manager stopped");
}

void StatisticsManager::Reset() {
  std::lock_guard<std::mutex> lock(stats_mutex_);

  // 重置所有统计数据 - 手动重置原子变量
  auto resetDecodeStats = [](auto& stats) {
    stats.frames_input.store(0);
    stats.frames_decoded.store(0);
    stats.frames_in_interval.store(0);
    stats.decode_rate_fps.store(0.0);
    stats.queue_size.store(0);
    stats.queue_usage_percent.store(0.0);
    stats.avg_decode_time_ms.store(0.0);
    stats.total_decode_time_ms.store(0.0);
    stats.decode_errors.store(0);
  };

  auto resetRenderStats = [](auto& stats) {
    stats.frames_received.store(0);
    stats.frames_received_in_interval.store(0);
    stats.frames_rendered.store(0);
    stats.frames_rendered_in_interval.store(0);
    stats.frames_dropped.store(0);
    stats.render_rate_fps.store(0.0);
    stats.frame_drop_rate.store(0.0);
    stats.avg_render_time_ms.store(0.0);
    stats.total_render_time_ms.store(0.0);
  };

  // Reset demux stats
  auto& demux = pipeline_stats_.demux;
  demux.packets_read_total.store(0);
  demux.packets_read_video.store(0);
  demux.packets_read_audio.store(0);
  demux.bytes_read_total.store(0);
  demux.packets_in_interval.store(0);
  demux.bytes_in_interval.store(0);
  demux.read_rate_pps.store(0.0);
  demux.read_rate_bps.store(0.0);
  demux.avg_read_time_ms.store(0.0);
  demux.total_read_time_ms.store(0.0);
  demux.read_errors.store(0);

  // Reset decode stats
  resetDecodeStats(pipeline_stats_.video_decode);
  resetDecodeStats(pipeline_stats_.audio_decode);

  // Reset render stats
  resetRenderStats(pipeline_stats_.video_render);
  resetRenderStats(pipeline_stats_.audio_render);

  // Reset sync stats
  sync_stats_.av_sync_offset_ms.store(0.0);
  sync_stats_.avg_sync_offset_ms.store(0.0);
  sync_stats_.sync_jitter_ms.store(0.0);
  sync_stats_.is_in_sync.store(false);
  sync_stats_.sync_quality = "Unknown";
  sync_stats_.offset_count.store(0);
  sync_stats_.offset_sum.store(0.0);
  sync_stats_.offset_variance.store(0.0);

  // Reset system stats
  system_stats_.cpu_usage_percent.store(0.0);
  system_stats_.memory_usage_mb.store(0);
  system_stats_.gpu_memory_mb.store(0);
  system_stats_.thread_count.store(0);

  // Reset network stats
  network_stats_.download_rate_kbps.store(0.0);
  network_stats_.bytes_downloaded.store(0);
  network_stats_.bytes_in_interval.store(0);

  start_time_ = std::chrono::steady_clock::now();
  last_report_time_ = start_time_;

  MODULE_INFO(LOG_MODULE_STATS, "Statistics data reset");
}

// === 私有方法 ===
void StatisticsManager::CalculateRates() {
  auto current_time = std::chrono::steady_clock::now();
  auto interval_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                         current_time - last_report_time_)
                         .count();

  if (interval_ms <= 0) {
    return;
  }

  double interval_seconds = interval_ms / 1000.0;

  // 计算Demux速率
  auto& demux = pipeline_stats_.demux;
  uint64_t packets_in_interval = demux.packets_in_interval.exchange(0);
  uint64_t bytes_in_interval = demux.bytes_in_interval.exchange(0);

  demux.read_rate_pps.store(packets_in_interval / interval_seconds);
  demux.read_rate_bps.store(bytes_in_interval / interval_seconds);

  // 计算解码速率
  auto& vdec = pipeline_stats_.video_decode;
  uint64_t vframes_in_interval = vdec.frames_in_interval.exchange(0);
  vdec.decode_rate_fps.store(vframes_in_interval / interval_seconds);

  auto& adec = pipeline_stats_.audio_decode;
  uint64_t aframes_in_interval = adec.frames_in_interval.exchange(0);
  adec.decode_rate_fps.store(aframes_in_interval / interval_seconds);

  // 计算渲染速率
  auto& vrnd = pipeline_stats_.video_render;
  uint64_t vrendered_in_interval = vrnd.frames_rendered_in_interval.exchange(0);
  vrnd.render_rate_fps.store(vrendered_in_interval / interval_seconds);

  auto& arnd = pipeline_stats_.audio_render;
  uint64_t arendered_in_interval = arnd.frames_rendered_in_interval.exchange(0);
  arnd.render_rate_fps.store(arendered_in_interval / interval_seconds);

  // 计算网络速率
  auto& net = network_stats_;
  uint64_t net_bytes_in_interval = net.bytes_in_interval.exchange(0);
  net.download_rate_kbps.store((net_bytes_in_interval / interval_seconds) /
                               1024.0);
}

void StatisticsManager::DetectBottlenecks() {
  // TODO: 实现瓶颈检测逻辑
}

void StatisticsManager::LogStatistics() {
  if (!config_.auto_logging) {
    return;
  }

  std::string report = GenerateReport();

  if (stats_logger_) {
    // 使用专门的统计日志记录器
    stats_logger_->info("\n{}", report);
  } else {
    // 回退到通用模块日志
    MODULE_INFO(LOG_MODULE_STATS, "\n{}", report);
  }
}

void StatisticsManager::ResetCounters() {
  // 由CalculateRates()自动处理区间计数器重置
}

void StatisticsManager::OnReportTimer() {
  // Timer回调函数 - 替代原来的ReportingThread
  if (!running_.load()) {
    return;  // 已停止，不处理回调
  }

  CalculateRates();

  if (config_.enable_bottleneck_detection) {
    DetectBottlenecks();
  }

  LogStatistics();

  last_report_time_ = std::chrono::steady_clock::now();
}

void StatisticsManager::InitializeStatsLogger() {
  try {
    if (config_.separate_log_file) {
      stats_logger_ =
          spdlog::basic_logger_mt("stats_logger", config_.log_file_path);
      stats_logger_->set_level(spdlog::level::info);
      stats_logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%f] [STATS] %v");
      MODULE_INFO(LOG_MODULE_STATS, "Statistics logger initialized: {}",
                  config_.log_file_path);
    }
  } catch (const spdlog::spdlog_ex& ex) {
    MODULE_ERROR(LOG_MODULE_STATS, "Failed to initialize stats logger: {}",
                 ex.what());
    stats_logger_ = nullptr;
  }
}

}  // namespace stats
}  // namespace zenplay
