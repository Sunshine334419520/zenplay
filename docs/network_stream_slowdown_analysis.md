# 网络流读包速率下降问题深度分析

## 📋 问题描述

您的 Demuxer 在打开网络流时，**前 2 秒读包速率正常，之后速率急剧下降**。而本地文件播放无此问题。

---

## 🔍 根本原因分析

### 1. **缺乏网络缓冲和预读机制**（主要原因）

当前 `demuxer.cpp` 的 `ReadPacket()` 实现是**被动同步读取**：

```cpp
Result<AVPacket*> Demuxer::ReadPacket() {
  AVPacket* packet = av_packet_alloc();
  int ret = av_read_frame(format_context_, packet);  // 🔴 阻塞调用
  // ...直接返回
}
```

**问题**：
- 每次读包都直接调用 `av_read_frame()`，这是一个**网络 I/O 操作**
- FFmpeg 的网络读取涉及多层缓冲，但初始配置不足
- **前 2 秒是系统缓冲命中**（TCP 窗口、OS 缓冲有数据）
- **之后速率下降是因为**：
  1. **缓冲耗尽**（已消费的数据比网络接收新数据快）
  2. **网络抖动**（新包到达延迟）
  3. **无重试机制**（网络超时后无预读）

---

### 2. **FFmpeg 网络选项配置不足**

您的当前配置：

```cpp
if (url.find("rtsp://") == 0 || url.find("rtmp://") == 0) {
    av_dict_set(&options, "rtsp_transport", "tcp", 0);
    av_dict_set(&options, "timeout", "5000000", 0);  // 5 秒
}
```

**缺陷分析**：

| 问题 | 影响 | 解决方案 |
|------|------|--------|
| 无 `max_delay` 设置 | 低延迟模式不工作 | 加入 `max_delay` |
| 无 `buffer_size` 设置 | 缓冲不足 | 设置适当的 `buffer_size` |
| 无 `reconnect` 和 `reconnect_delay_max` | 网络中断不重试 | 添加自动重连 |
| 只针对 RTSP/RTMP，HTTP/HTTPS 忽略 | HTTP 流无优化 | 所有网络协议应用 |
| `timeout` 设置为 5 秒太长 | 初始连接快，但超时响应慢 | 改为 1-2 秒 |

---

### 3. **BlockingQueue 阻塞反馈问题**

`playback_controller.cpp` 中的 DemuxTask：

```cpp
void PlaybackController::DemuxTask() {
  while (!state_manager_->ShouldStop()) {
    // ... 
    auto packet_result = demuxer_->ReadPacket();  // 如果队列满，这里阻塞？
    
    if (packet->stream_index == demuxer_->active_video_stream_index()) {
      if (!video_packet_queue_.Push(packet)) {  // 可能阻塞
        // ...
      }
    }
  }
}
```

**问题链**：
1. 如果 `video_packet_queue_` 满了，`Push()` 会阻塞
2. DemuxTask 阻塞 → 不读新包 → 队列一直满
3. 解码器等待新包 → 但读包线程已阻塞 → **死锁风险**
4. 实际表现：读包速率忽快忽慢，跟队列容量相关

---

### 4. **缺乏异步预读缓冲**

网络流播放的业界标准实现（mpv, VLC）都使用：

```
网络接收线程 (高速) → 内存缓冲池 → 应用层读取 (按需)
```

当前实现：

```
应用层 (DemuxTask) → 直接调用 FFmpeg av_read_frame()
                  ↓
            等待网络 I/O (慢)
```

---

## 🔧 完整解决方案

### **方案 1: 优化 FFmpeg 网络选项（立即见效）**

修改 `demuxer.cpp` 的 `Open()` 方法：

```cpp
Result<void> Demuxer::Open(const std::string& url) {
  if (format_context_) {
    Close();
  }

  AVDictionary* options = nullptr;

  // ✅ 通用网络选项（所有网络协议）
  av_dict_set(&options, "reconnect", "1", 0);                    // 自动重连
  av_dict_set(&options, "reconnect_delay_max", "5", 0);         // 最大重连延迟 5s
  av_dict_set(&options, "reconnect_streamed", "1", 0);          // 流媒体重连
  
  // ✅ HTTP/HTTPS 优化
  if (url.find("http://") == 0 || url.find("https://") == 0) {
    av_dict_set(&options, "buffer_size", "10485760", 0);        // 10MB 缓冲
    av_dict_set(&options, "max_delay", "5000000", 0);           // 5秒最大延迟
    av_dict_set(&options, "user_agent", "ZenPlay/1.0", 0);
  }
  // ✅ RTSP/RTMP 优化
  else if (url.find("rtsp://") == 0 || url.find("rtmp://") == 0) {
    av_dict_set(&options, "rtsp_transport", "tcp", 0);          // TCP 更可靠
    av_dict_set(&options, "buffer_size", "5242880", 0);         // 5MB 缓冲
    av_dict_set(&options, "max_delay", "5000000", 0);           // 5秒
    av_dict_set(&options, "timeout", "2000000", 0);             // 2秒超时（改短）
  }
  // ✅ UDP 协议（低延迟直播）
  else if (url.find("udp://") == 0) {
    av_dict_set(&options, "buffer_size", "1048576", 0);         // 1MB 缓冲
    av_dict_set(&options, "timeout", "1000000", 0);             // 1秒超时
  }

  int ret = avformat_open_input(&format_context_, url.c_str(), nullptr, &options);
  if (ret < 0) {
    av_dict_free(&options);
    avformat_free_context(format_context_);
    format_context_ = nullptr;
    return FFmpegErrorToResult(ret, "Open input: " + url);
  }

  av_dict_free(&options);

  ret = avformat_find_stream_info(format_context_, nullptr);
  if (ret < 0) {
    Close();
    return FFmpegErrorToResult(ret, "Find stream info: " + url);
  }

  probeStreams();
  return Result<void>::Ok();
}
```

**预期改进**：
- ✅ 缓冲从接收端补充
- ✅ 自动处理网络中断
- ✅ 读包速率稳定

---

### **方案 2: 添加异步预读线程（终极方案）**

扩展 `demuxer.h`：

```cpp
#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <queue>

#include "player/common/error.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/dict.h>
#include <libavutil/opt.h>
}

namespace zenplay {

class Demuxer {
 public:
  // 预读配置
  struct PrefetchConfig {
    size_t buffer_size_mb = 10;        // 预读缓冲大小
    size_t min_refill_size_mb = 2;     // 低于此值触发预读
    bool enable_prefetch = true;       // 是否启用预读
  };

  Demuxer();
  ~Demuxer();

  Result<void> Open(const std::string& url);
  Result<void> Open(const std::string& url, const PrefetchConfig& config);
  void Close();

  Result<AVPacket*> ReadPacket();

  bool Seek(int64_t timestamp, bool backward = false);

  AVDictionary* GetMetadata() const;
  int64_t GetDuration() const;

  int active_video_stream_index() const { return active_video_stream_index_; }
  int active_audio_stream_index() const { return active_audio_stream_index_; }

  AVStream* findStreamByIndex(int index) const;

  // 获取缓冲统计信息
  size_t GetPrefetchBufferSize() const;
  double GetPrefetchBufferHealth() const;  // 0-100%

 private:
  void probeStreams();
  void PrefetchThread();  // 异步预读线程

  AVFormatContext* format_context_;
  std::vector<int> video_streams_;
  std::vector<int> audio_streams_;

  int active_video_stream_index_ = -1;
  int active_audio_stream_index_ = -1;

  // 预读相关
  PrefetchConfig prefetch_config_;
  std::queue<AVPacket*> prefetch_queue_;
  std::mutex prefetch_mutex_;
  std::thread prefetch_thread_;
  std::atomic<bool> prefetch_running_{false};
  std::atomic<size_t> buffered_bytes_{0};

  static std::once_flag init_once_flag_;
};

}  // namespace zenplay
```

完整实现见下面的代码补丁。

---

### **方案 3: 优化队列策略**

在 `playback_controller.h` 中：

```cpp
// 旧配置
BlockingQueue<AVPacket*> video_packet_queue_{16};  // 太小
AudioPacketQueue<AVPacket*> audio_packet_queue_{32};

// 新配置（网络流优化）
BlockingQueue<AVPacket*> video_packet_queue_{64};  // 增大以容纳网络抖动
BlockingQueue<AVPacket*> audio_packet_queue_{96};  // 音频队列更大

// 或者配置化
Result<void> PlaybackController::InitializeQueues(bool is_network_stream) {
  if (is_network_stream) {
    // 网络流：更大缓冲应对抖动
    video_packet_queue_ = BlockingQueue<AVPacket*>(64);
    audio_packet_queue_ = BlockingQueue<AVPacket*>(96);
  } else {
    // 本地流：紧凑配置
    video_packet_queue_ = BlockingQueue<AVPacket*>(16);
    audio_packet_queue_ = BlockingQueue<AVPacket*>(32);
  }
  return Result<void>::Ok();
}
```

---

## 📊 对比本地文件 vs 网络流

| 维度 | 本地文件 | 网络流 |
|------|--------|-------|
| I/O 延迟 | 1-5ms | 10-100ms（网络抖动） |
| 可用缓冲 | 大（OS 页缓存） | 小（需显式配置） |
| 超时情况 | 无 | 可能发生 |
| 读包速率 | 恒定（受限于解码速度） | **波动**（受网络影响） |
| **需要预读** | 否 | **是** |

---

## 🎯 实现步骤

### **立即实施（5 分钟）**
1. ✅ 修改 `demuxer.cpp::Open()` - 应用方案 1
2. ✅ 重新编译和测试

### **中期优化（1-2 小时）**
3. ✅ 实现方案 2 - 异步预读线程
4. ✅ 添加统计信息（缓冲健康度）
5. ✅ 测试各种网络条件

### **长期完善（可选）**
6. 配置化队列大小
7. 自适应缓冲算法
8. 网络质量检测

---

## 🧪 验证方法

在您的测试代码中：

```cpp
// 测试网络流
std::string url = "http://example.com/video.mp4";  // 或 RTSP/RTMP

demuxer->Open(url);

// 记录前 2 秒和后续的读包速率
for (int i = 0; i < 300; i++) {  // 30 秒
  auto start = std::chrono::steady_clock::now();
  
  auto result = demuxer->ReadPacket();
  AVPacket* packet = result.Value();
  
  auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start).count();
  
  if (i % 30 == 0) {
    std::cout << "ReadPacket took: " << elapsed_ms << "ms at t=" << (i/30) << "s\n";
  }
  
  if (packet) av_packet_free(&packet);
}
```

**预期**：
- 优化前：0-2s 平稳（~1ms），2s+ 剧增（50-200ms）
- 优化后：全程平稳（2-5ms）

---

## 📚 FFmpeg 7.0 官方文档参考

### 相关 AVOption

```
libavformat/avformat.h:
- AVFormatContext::max_delay        // 最大缓冲延迟 
- AVFormatContext::probesize         // 探测字节数
- AVFormatContext::max_analyze_duration  // 探测时间

AVDictionary 常用网络选项：
- "buffer_size"         // 协议级缓冲大小（字节）
- "max_delay"           // 最大缓冲延迟（微秒）
- "timeout"             // 网络超时（微秒）
- "reconnect"           // 自动重连（0/1）
- "reconnect_delay_max" // 最大重连延迟（秒）
```

参考：`libavformat/protocols.texi`

---

## ⚠️ 注意事项

1. **缓冲 vs 延迟的权衡**：
   - 缓冲大 → 抗抖动能力强，但延迟大
   - 缓冲小 → 低延迟，但网络抖动时卡顿
   - **直播**：缓冲 1-5MB
   - **流媒体**：缓冲 5-10MB

2. **内存使用**：
   - 10MB 缓冲 + 64 个 packet 队列 ≈ 15-20MB
   - 确保目标设备有足够内存

3. **网络协议区别**：
   - **HTTP/HTTPS**: 天生支持缓冲（TCP 窗口）
   - **RTSP/RTMP**: 需要显式缓冲
   - **UDP**: 无缓冲，易丢包，需要应用层处理

---

## 🔍 诊断命令

用 ffprobe 检查您的网络流：

```bash
# 显示详细的网络缓冲信息
ffprobe -v debug "http://example.com/video.mp4" 2>&1 | grep -i "buffer\|delay\|timeout"

# 测试读取速度
time ffmpeg -i "http://example.com/video.mp4" -c copy -t 30 -f null -
```

