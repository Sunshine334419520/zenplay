#pragma once

#include <atomic>
#include <chrono>
#include <string>
#include <vector>

namespace zenplay {
namespace stats {

// 媒体流水线统计
struct PipelineStats {
  // === 读取层统计 (Demuxer) ===
  struct DemuxStats {
    std::atomic<uint64_t> packets_read_total{0};  // 总读取包数
    std::atomic<uint64_t> packets_read_video{0};  // 视频包数
    std::atomic<uint64_t> packets_read_audio{0};  // 音频包数
    std::atomic<double> read_rate_pps{0.0};       // 读取速率(包/秒)
    std::atomic<uint64_t> bytes_read_total{0};    // 总读取字节数
    std::atomic<double> read_rate_bps{0.0};       // 读取比特率(字节/秒)
    std::atomic<uint64_t> read_errors{0};         // 读取错误次数
    std::atomic<double> avg_read_time_ms{0.0};    // 平均读取耗时(毫秒)

    // 内部计算用
    std::atomic<uint64_t> packets_in_interval{0};  // 区间内包数
    std::atomic<uint64_t> bytes_in_interval{0};    // 区间内字节数
    std::atomic<double> total_read_time_ms{0.0};   // 总读取时间
  } demux;

  // === 解码层统计 (Video/Audio Decoder) ===
  struct DecodeStats {
    std::atomic<uint64_t> frames_input{0};         // 输入解码帧数
    std::atomic<uint64_t> frames_decoded{0};       // 成功解码帧数
    std::atomic<uint64_t> decode_errors{0};        // 解码错误次数
    std::atomic<double> decode_rate_fps{0.0};      // 解码帧率(帧/秒)
    std::atomic<double> avg_decode_time_ms{0.0};   // 平均解码耗时(毫秒)
    std::atomic<uint32_t> queue_size{0};           // 当前队列大小
    uint32_t queue_max_size = 30;                  // 队列最大容量
    std::atomic<double> queue_usage_percent{0.0};  // 队列使用率(%)

    // 内部计算用
    std::atomic<uint64_t> frames_in_interval{0};    // 区间内解码帧数
    std::atomic<double> total_decode_time_ms{0.0};  // 总解码时间
  };
  DecodeStats video_decode;
  DecodeStats audio_decode;

  // === 渲染层统计 (Renderer) ===
  struct RenderStats {
    std::atomic<uint64_t> frames_received{0};     // 接收到的帧数
    std::atomic<uint64_t> frames_rendered{0};     // 实际渲染帧数
    std::atomic<uint64_t> frames_dropped{0};      // 丢弃帧数
    std::atomic<double> render_rate_fps{0.0};     // 渲染帧率(帧/秒)
    std::atomic<double> avg_render_time_ms{0.0};  // 平均渲染耗时(毫秒)
    std::atomic<double> frame_drop_rate{0.0};     // 丢帧率(%)

    // 内部计算用
    std::atomic<uint64_t> frames_rendered_in_interval{0};  // 区间内渲染帧数
    std::atomic<uint64_t> frames_received_in_interval{0};  // 区间内接收帧数
    std::atomic<double> total_render_time_ms{0.0};         // 总渲染时间
  };
  RenderStats video_render;
  RenderStats audio_render;
};

// === 同步与质量统计 ===
struct SyncQualityStats {
  std::atomic<double> av_sync_offset_ms{0.0};   // 音视频同步偏移(毫秒)
  std::atomic<double> avg_sync_offset_ms{0.0};  // 平均同步偏移
  std::atomic<double> sync_jitter_ms{0.0};      // 同步抖动
  std::atomic<uint64_t> sync_corrections{0};    // 同步修正次数
  std::atomic<bool> is_in_sync{true};           // 是否同步正常
  std::string sync_quality = "Good";            // 同步质量评级

  // 内部计算用
  std::atomic<double> offset_sum{0.0};       // 偏移累计
  std::atomic<uint64_t> offset_count{0};     // 偏移采样数
  std::atomic<double> offset_variance{0.0};  // 偏移方差
};

// === 系统资源统计 ===
struct SystemResourceStats {
  std::atomic<double> cpu_usage_percent{0.0};  // CPU使用率(%)
  std::atomic<uint64_t> memory_usage_mb{0};    // 内存使用(MB)
  std::atomic<uint64_t> gpu_memory_mb{0};      // GPU内存使用(MB)
  std::atomic<uint32_t> thread_count{0};       // 活跃线程数
};

// === 网络统计 (适用于网络流) ===
struct NetworkStats {
  std::atomic<double> download_rate_kbps{0.0};       // 下载速率(kbps)
  std::atomic<uint64_t> bytes_downloaded{0};         // 已下载字节数
  std::atomic<uint32_t> buffer_health_percent{100};  // 缓冲健康度(%)
  std::atomic<uint64_t> network_errors{0};           // 网络错误次数
  std::atomic<double> rtt_ms{0.0};                   // 往返延迟(毫秒)

  // 内部计算用
  std::atomic<uint64_t> bytes_in_interval{0};  // 区间内下载字节
};

// 性能瓶颈检测
struct PerformanceBottleneck {
  enum class BottleneckType {
    None = 0,
    DemuxSlow,        // 读取慢
    VideoDecodeSlow,  // 视频解码慢
    AudioDecodeSlow,  // 音频解码慢
    VideoRenderSlow,  // 视频渲染慢
    AudioRenderSlow,  // 音频渲染慢
    SyncIssue,        // 同步问题
    MemoryPressure,   // 内存压力
    NetworkSlow       // 网络慢
  };

  BottleneckType primary_bottleneck = BottleneckType::None;
  std::vector<BottleneckType> secondary_bottlenecks;
  double severity_score = 0.0;  // 严重程度(0-10)
  std::string description;      // 问题描述
  std::string recommendation;   // 优化建议
};

// 统计配置
struct StatsConfig {
  bool enabled = true;                                   // 全局开关
  std::chrono::milliseconds report_interval{5000};       // 报告间隔(5秒)
  bool auto_logging = true;                              // 自动日志输出
  bool separate_log_file = true;                         // 是否使用单独日志文件
  std::string log_file_path = "logs/zenplay_stats.log";  // 统计日志文件路径
  bool enable_bottleneck_detection = true;               // 是否开启瓶颈检测
  double target_video_fps = 30.0;                        // 目标视频帧率
  double target_audio_sample_rate = 44100.0;             // 目标音频采样率
};

}  // namespace stats
}  // namespace zenplay
