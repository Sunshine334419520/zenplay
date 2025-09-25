# 🏗️ ZenPlay Statistics System 设计文档

## 📋 **1. 设计目标**

### **1.1 初期目标**
- 识别性能瓶颈：读取、解码、渲染各环节的帧率统计
- 问题定位：明确是哪个环节导致播放问题
- 日志记录：将统计数据输出到日志系统便于调试

### **1.2 长期愿景**
- 类似WebRTC Stats的丰富功能
- 实时性能监控
- 质量分析和优化建议

## 📊 **2. 统计指标设计**

### **2.1 核心流水线指标**

```cpp
namespace zenplay {
namespace stats {

// 媒体流水线统计
struct PipelineStats {
    // === 读取层统计 (Demuxer) ===
    struct DemuxStats {
        uint64_t packets_read_total = 0;         // 总读取包数
        uint64_t packets_read_video = 0;         // 视频包数
        uint64_t packets_read_audio = 0;         // 音频包数
        double read_rate_pps = 0.0;              // 读取速率(包/秒)
        uint64_t bytes_read_total = 0;           // 总读取字节数
        double read_rate_bps = 0.0;              // 读取比特率(字节/秒)
        uint64_t read_errors = 0;                // 读取错误次数
        double avg_read_time_ms = 0.0;           // 平均读取耗时(毫秒)
    } demux;

    // === 解码层统计 (Video/Audio Decoder) ===
    struct DecodeStats {
        uint64_t frames_input = 0;               // 输入解码帧数
        uint64_t frames_decoded = 0;             // 成功解码帧数  
        uint64_t decode_errors = 0;              // 解码错误次数
        double decode_rate_fps = 0.0;            // 解码帧率(帧/秒)
        double avg_decode_time_ms = 0.0;         // 平均解码耗时(毫秒)
        uint32_t queue_size = 0;                 // 当前队列大小
        uint32_t queue_max_size = 30;            // 队列最大容量
        double queue_usage_percent = 0.0;        // 队列使用率(%)
    };
    DecodeStats video_decode;
    DecodeStats audio_decode;

    // === 渲染层统计 (Renderer) ===
    struct RenderStats {
        uint64_t frames_received = 0;            // 接收到的帧数
        uint64_t frames_rendered = 0;            // 实际渲染帧数
        uint64_t frames_dropped = 0;             // 丢弃帧数  
        double render_rate_fps = 0.0;            // 渲染帧率(帧/秒)
        double avg_render_time_ms = 0.0;         // 平均渲染耗时(毫秒)
        double frame_drop_rate = 0.0;            // 丢帧率(%)
    };
    RenderStats video_render;
    RenderStats audio_render;
};

// === 同步与质量统计 ===
struct SyncQualityStats {
    double av_sync_offset_ms = 0.0;              // 音视频同步偏移(毫秒)
    double avg_sync_offset_ms = 0.0;             // 平均同步偏移
    double sync_jitter_ms = 0.0;                 // 同步抖动
    uint64_t sync_corrections = 0;               // 同步修正次数
    bool is_in_sync = true;                      // 是否同步正常
    const char* sync_quality = "Good";           // 同步质量评级
};

// === 系统资源统计 ===
struct SystemResourceStats {
    double cpu_usage_percent = 0.0;             // CPU使用率(%)
    uint64_t memory_usage_mb = 0;                // 内存使用(MB)
    uint64_t gpu_memory_mb = 0;                  // GPU内存使用(MB)
    uint32_t thread_count = 0;                   // 活跃线程数
};

// === 网络统计 (适用于网络流) ===
struct NetworkStats {
    double download_rate_kbps = 0.0;             // 下载速率(kbps)
    uint64_t bytes_downloaded = 0;               // 已下载字节数
    uint32_t buffer_health_percent = 100;       // 缓冲健康度(%)
    uint64_t network_errors = 0;                // 网络错误次数
    double rtt_ms = 0.0;                         // 往返延迟(毫秒)
};

} // namespace stats
} // namespace zenplay
```

### **2.2 问题诊断指标**

```cpp
// 性能瓶颈检测
struct PerformanceBottleneck {
    enum class BottleneckType {
        None = 0,
        DemuxSlow,          // 读取慢
        VideoDecodeSlow,    // 视频解码慢
        AudioDecodeSlow,    // 音频解码慢
        VideoRenderSlow,    // 视频渲染慢
        AudioRenderSlow,    // 音频渲染慢
        SyncIssue,          // 同步问题
        MemoryPressure,     // 内存压力
        NetworkSlow         // 网络慢
    };
    
    BottleneckType primary_bottleneck = BottleneckType::None;
    std::vector<BottleneckType> secondary_bottlenecks;
    double severity_score = 0.0;        // 严重程度(0-10)
    std::string description;             // 问题描述
    std::string recommendation;          // 优化建议
};
```

## 🏗️ **3. 架构设计**

### **3.1 统计管理器 (StatisticsManager)**

```cpp
class StatisticsManager {
public:
    StatisticsManager();
    ~StatisticsManager();

    // === 统计数据更新接口 ===
    void UpdateDemuxStats(uint32_t packets_read, uint64_t bytes_read, 
                         double read_time_ms, bool is_video);
    void UpdateDecodeStats(bool is_video, bool decode_success, 
                          double decode_time_ms, uint32_t queue_size);
    void UpdateRenderStats(bool is_video, bool frame_rendered, 
                          bool frame_dropped, double render_time_ms);
    void UpdateSyncStats(double av_offset_ms, bool is_in_sync);
    void UpdateSystemStats(double cpu_percent, uint64_t memory_mb);
    void UpdateNetworkStats(double download_kbps, uint64_t bytes_total);

    // === 统计数据获取接口 ===
    PipelineStats GetPipelineStats() const;
    SyncQualityStats GetSyncStats() const;
    SystemResourceStats GetSystemStats() const;
    NetworkStats GetNetworkStats() const;
    
    // === 问题诊断接口 ===
    PerformanceBottleneck AnalyzeBottlenecks() const;
    std::string GenerateReport() const;
    
    // === 配置接口 ===
    void SetReportInterval(std::chrono::milliseconds interval);
    void EnableAutoLogging(bool enable);
    void SetLogLevel(LogLevel level);

private:
    void CalculateRates();               // 计算各种速率
    void DetectBottlenecks();            // 检测性能瓶颈
    void LogStatistics();                // 输出统计日志
    void ResetCounters();                // 重置计数器

    mutable std::mutex stats_mutex_;
    PipelineStats pipeline_stats_;
    SyncQualityStats sync_stats_;
    SystemResourceStats system_stats_;
    NetworkStats network_stats_;
    
    // 计算相关
    std::chrono::steady_clock::time_point last_report_time_;
    std::chrono::milliseconds report_interval_{1000};  // 1秒报告间隔
    bool auto_logging_enabled_{true};
    LogLevel log_level_{LogLevel::INFO};
    
    std::unique_ptr<std::thread> report_thread_;
    std::atomic<bool> should_stop_{false};
};
```

### **3.2 组件集成点**

```cpp
// === Demuxer集成 ===
class Demuxer {
    void NotifyPacketRead(AVPacket* packet, double read_time_ms) {
        if (stats_manager_) {
            bool is_video = (packet->stream_index == video_stream_index_);
            stats_manager_->UpdateDemuxStats(1, packet->size, read_time_ms, is_video);
        }
    }
    
private:
    StatisticsManager* stats_manager_ = nullptr;
};

// === VideoDecoder集成 ===
class VideoDecoder {
    bool DecodeFrame(AVPacket* packet, std::vector<AVFramePtr>* frames) {
        auto start_time = std::chrono::high_resolution_clock::now();
        bool success = /* 实际解码逻辑 */;
        auto end_time = std::chrono::high_resolution_clock::now();
        
        double decode_time = std::chrono::duration<double, std::milli>(
            end_time - start_time).count();
            
        if (stats_manager_) {
            stats_manager_->UpdateDecodeStats(true, success, decode_time, 
                                             frame_queue_size_);
        }
        return success;
    }
};

// === VideoPlayer集成 ===
class VideoPlayer {
    void VideoRenderThread() {
        while (!should_stop_) {
            auto render_start = std::chrono::high_resolution_clock::now();
            bool frame_rendered = RenderFrame();
            bool frame_dropped = ShouldDropFrame();
            auto render_end = std::chrono::high_resolution_clock::now();
            
            double render_time = std::chrono::duration<double, std::milli>(
                render_end - render_start).count();
                
            if (stats_manager_) {
                stats_manager_->UpdateRenderStats(true, frame_rendered, 
                                                 frame_dropped, render_time);
            }
        }
    }
};
```

## 📝 **4. 日志输出格式**

### **4.1 定期统计报告**

```
[STATS] ===== ZenPlay Performance Report (Interval: 1000ms) =====
[STATS] Pipeline Stats:
[STATS]   Demux    -> Read: 150 pkts/s (1.2MB/s), Errors: 0, AvgTime: 2.3ms
[STATS]   VideoDec -> Input: 30fps, Decoded: 30fps, Queue: 15/30 (50%), AvgTime: 8.5ms
[STATS]   AudioDec -> Input: 43fps, Decoded: 43fps, Queue: 8/20 (40%), AvgTime: 1.2ms  
[STATS]   VideoRnd -> Received: 30fps, Rendered: 29fps, Dropped: 1 (3.3%), AvgTime: 12.1ms
[STATS]   AudioRnd -> Received: 43fps, Rendered: 43fps, Dropped: 0 (0%), AvgTime: 0.8ms
[STATS] Sync Stats:
[STATS]   AV Offset: +12.3ms, Jitter: 8.7ms, Quality: Good, InSync: Yes
[STATS] System Stats:  
[STATS]   CPU: 45.2%, Memory: 128MB, GPU: 256MB, Threads: 8
[STATS] Bottleneck Analysis: Primary=None, Severity=0.0
[STATS] ===============================================================
```

### **4.2 问题告警日志**

```
[WARN] Performance Bottleneck Detected!
[WARN] Primary: VideoDecodeSlow (Severity: 7.2/10)
[WARN] Description: Video decode rate (18fps) significantly below target (30fps)
[WARN] Recommendation: Consider enabling hardware decode or reducing video quality
[WARN] Secondary Issues: MemoryPressure (Memory usage: 89%)
```

## 🔧 **5. 实现步骤**

### **5.1 第一阶段：基础统计**
1. ✅ 创建基础数据结构
2. ✅ 实现StatisticsManager核心功能
3. ✅ 在关键组件添加统计点
4. ✅ 实现基础日志输出

### **5.2 第二阶段：智能分析**
1. ⏳ 实现性能瓶颈检测算法
2. ⏳ 添加问题诊断和建议系统
3. ⏳ 实现自适应报告频率

### **5.3 第三阶段：高级功能**
1. ⏳ 历史数据存储和趋势分析
2. ⏳ 实时性能监控界面
3. ⏳ WebRTC风格的详细指标

## 📁 **6. 文件结构**

```
src/player/stats/
├── statistics_manager.h           # 统计管理器接口
├── statistics_manager.cpp         # 统计管理器实现
├── stats_types.h                  # 统计数据结构定义
├── performance_analyzer.h         # 性能分析器
├── performance_analyzer.cpp       # 瓶颈检测算法实现
└── stats_reporter.h              # 统计报告生成器
```

## 🎯 **7. 使用示例**

```cpp
// 在PlaybackController中集成
class PlaybackController {
public:
    PlaybackController(...) {
        stats_manager_ = std::make_unique<StatisticsManager>();
        stats_manager_->SetReportInterval(std::chrono::seconds(5));
        stats_manager_->EnableAutoLogging(true);
    }
    
    void Start() {
        // 将stats_manager传递给各个组件
        video_player_->SetStatsManager(stats_manager_.get());
        audio_player_->SetStatsManager(stats_manager_.get());
        demuxer_->SetStatsManager(stats_manager_.get());
    }
    
    std::string GetPerformanceReport() {
        return stats_manager_->GenerateReport();
    }

private:
    std::unique_ptr<StatisticsManager> stats_manager_;
};
```

## 📈 **8. 性能瓶颈识别规则**

### **8.1 瓶颈检测阈值**

| 组件 | 指标 | 正常范围 | 警告阈值 | 严重阈值 |
|------|------|----------|----------|----------|
| Demux | 读取速率 | >目标fps×1.2 | <目标fps×1.1 | <目标fps |
| VideoDecode | 解码帧率 | ≥目标fps | <目标fps×0.9 | <目标fps×0.8 |
| AudioDecode | 解码帧率 | ≥音频帧率 | <音频帧率×0.95 | <音频帧率×0.9 |
| VideoRender | 渲染帧率 | ≥目标fps | <目标fps×0.9 | <目标fps×0.8 |
| VideoRender | 丢帧率 | <5% | 5-15% | >15% |
| Sync | AV偏移 | ±40ms | ±100ms | ±200ms |
| System | CPU使用率 | <70% | 70-90% | >90% |
| System | 内存使用 | <80% | 80-95% | >95% |

### **8.2 瓶颈严重程度计算**

```cpp
double CalculateSeverity(double actual, double expected, double threshold) {
    if (actual >= expected) return 0.0;  // 正常
    
    double deviation = (expected - actual) / expected;
    if (deviation < threshold) return deviation * 5.0;      // 轻微 (0-5)
    else if (deviation < threshold * 2) return 5.0 + (deviation - threshold) * 10.0; // 中等 (5-7.5)
    else return 7.5 + std::min(2.5, (deviation - threshold * 2) * 5.0);  // 严重 (7.5-10)
}
```

## 🔄 **9. 数据流图**

```
┌─────────────┐    ┌──────────────┐    ┌─────────────┐
│   Demuxer   │───▶│ VideoDecoder │───▶│ VideoPlayer │
│             │    │              │    │             │
│ packets/s   │    │ decode fps   │    │ render fps  │
│ bytes/s     │    │ queue size   │    │ drop rate   │
│ read time   │    │ decode time  │    │ render time │
└──────┬──────┘    └──────┬───────┘    └──────┬──────┘
       │                  │                   │
       ▼                  ▼                   ▼
┌─────────────────────────────────────────────────────┐
│            StatisticsManager                        │
│                                                     │
│ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐    │
│ │ Aggregation │ │   Analysis  │ │  Reporting  │    │
│ │   - Rates   │ │ - Bottlenck │ │ - Logging   │    │
│ │   - Queues  │ │ - Severity  │ │ - Alerts    │    │
│ │   - Timings │ │ - Recommend │ │ - Export    │    │
│ └─────────────┘ └─────────────┘ └─────────────┘    │
└─────────────────────────────────────────────────────┘
                       │
                       ▼
              ┌─────────────────┐
              │  Logger/Console │
              │     Output      │
              └─────────────────┘
```

## 🎪 **10. 扩展性设计**

### **10.1 插件式统计收集器**

```cpp
class StatsCollector {
public:
    virtual ~StatsCollector() = default;
    virtual void Collect() = 0;
    virtual std::string GetName() const = 0;
};

class CPUStatsCollector : public StatsCollector {
    void Collect() override;
    std::string GetName() const override { return "CPU"; }
};

class NetworkStatsCollector : public StatsCollector {
    void Collect() override;
    std::string GetName() const override { return "Network"; }
};
```

### **10.2 自定义导出格式**

```cpp
class StatsExporter {
public:
    virtual ~StatsExporter() = default;
    virtual void Export(const AllStats& stats) = 0;
};

class JSONExporter : public StatsExporter {
    void Export(const AllStats& stats) override;
};

class CSVExporter : public StatsExporter {
    void Export(const AllStats& stats) override;
};
```

---

这个设计提供了：
- ✅ **完整的性能监控** - 覆盖整个媒体流水线
- ✅ **问题定位能力** - 明确识别瓶颈环节  
- ✅ **可扩展架构** - 便于后续功能扩展
- ✅ **实用的日志输出** - 便于调试和优化
- ✅ **智能分析** - 自动检测问题并给出建议
- ✅ **未来拓展** - 支持WebRTC风格的丰富统计功能

该系统将成为ZenPlay播放器性能优化和问题诊断的强大工具。