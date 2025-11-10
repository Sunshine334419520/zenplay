# FFmpeg 7.0 网络流优化 - 官方选项参考

## 📚 官方文档位置

FFmpeg 7.0 的网络相关文档：
- `libavformat/protocols.texi` - 协议级选项
- `libavformat/utils.c` - 通用选项处理
- `libavformat/network.c` - 网络层实现

---

## 🔧 关键 AVDictionary 选项详解

### 全局网络选项

#### 1. `buffer_size` - 协议缓冲区大小
```cpp
av_dict_set(&options, "buffer_size", "10485760", 0);  // 10MB

// FFmpeg 内部行为：
// - TCP 接收缓冲：提升 SO_RCVBUF
// - 在 libavformat 中维护二级缓冲
// - 缓冲越大 → 网络波动容错越好，但初始延迟越大

// 推荐值
HTTP/HTTPS:  10MB  (流媒体服务器 HLS/DASH)
RTSP:         5MB  (实时流)
RTMP:         5MB  (实时流)
UDP:          1MB  (低延迟直播)
```

#### 2. `max_delay` - 最大缓冲延迟
```cpp
av_dict_set(&options, "max_delay", "5000000", 0);  // 5 秒（微秒）

// FFmpeg 内部行为：
// - format_context_->max_delay = av_dict_get(options, "max_delay")
// - 限制 avformat_find_stream_info() 的探测时间
// - 不是实际的播放延迟，是探测延迟

// 推荐值
流媒体 (HLS/DASH):  5-10 秒
RTSP/RTMP:          3-5 秒
直播:               1-2 秒
```

#### 3. `timeout` - 网络超时
```cpp
av_dict_set(&options, "timeout", "2000000", 0);  // 2 秒（微秒）

// 作用范围
- TCP 连接建立超时
- 单个数据包读取超时
- DNS 查询超时（某些协议）

// 注意：不是总超时，而是单次 I/O 操作超时
// 如果一个 av_read_frame() 在 2 秒内没有收到数据，则返回超时

// 推荐值
连接超时:    5-10 秒  (首次建立连接)
读取超时:    1-2 秒   (单次数据包)
```

#### 4. `reconnect` - 自动重连
```cpp
av_dict_set(&options, "reconnect", "1", 0);
av_dict_set(&options, "reconnect_delay_max", "5", 0);

// 功能
- 连接中断时自动重新连接
- 逐级退避：1s → 2s → 4s → ... → reconnect_delay_max
- 适用于 HTTP, RTSP, RTMP

// 案例：网络临时中断 500ms
- 不设置 reconnect: 播放停止，需手动重新打开
- 设置 reconnect: 自动重连，用户无感知
```

#### 5. `reconnect_streamed` - 流媒体重连
```cpp
av_dict_set(&options, "reconnect_streamed", "1", 0);

// 差异
reconnect:            对普通 HTTP 文件有效
reconnect_streamed:   对流媒体(HLS/DASH) 有效

// 使用场景
- 直播流中断恢复
- HLS/DASH Segment 下载失败重试
```

---

## 🌐 协议特定选项

### HTTP/HTTPS 特定

```cpp
// User-Agent 设置
av_dict_set(&options, "user_agent", "ZenPlay/1.0", 0);

// HTTP 特定
av_dict_set(&options, "headers", "Referer: http://example.com\r\n", 0);
av_dict_set(&options, "follow_redirects", "1", 0);  // 跟随 302/301 重定向
av_dict_set(&options, "multiple_requests", "1", 0); // 持久连接

// 范例：完整配置
AVDictionary* opts = nullptr;
av_dict_set(&opts, "buffer_size", "10485760", 0);     // 10MB
av_dict_set(&opts, "max_delay", "5000000", 0);        // 5s
av_dict_set(&opts, "timeout", "2000000", 0);          // 2s
av_dict_set(&opts, "reconnect", "1", 0);
av_dict_set(&opts, "user_agent", "ZenPlay/1.0", 0);
avformat_open_input(&fmt_ctx, url.c_str(), nullptr, &opts);
av_dict_free(&opts);
```

### RTSP 特定

```cpp
// RTSP 传输协议
av_dict_set(&options, "rtsp_transport", "tcp", 0);    // TCP (可靠)
// av_dict_set(&options, "rtsp_transport", "udp", 0); // UDP (低延迟)

// RTSP 连接参数
av_dict_set(&options, "buffer_size", "5242880", 0);   // 5MB
av_dict_set(&options, "max_delay", "5000000", 0);     // 5s

// 标准 RTSP 完整配置
AVDictionary* opts = nullptr;
av_dict_set(&opts, "rtsp_transport", "tcp", 0);
av_dict_set(&opts, "buffer_size", "5242880", 0);
av_dict_set(&opts, "max_delay", "5000000", 0);
av_dict_set(&opts, "timeout", "2000000", 0);
av_dict_set(&opts, "reconnect", "1", 0);
avformat_open_input(&fmt_ctx, url.c_str(), nullptr, &opts);
av_dict_free(&opts);
```

### RTMP 特定

```cpp
av_dict_set(&options, "buffer_size", "5242880", 0);   // 5MB
av_dict_set(&options, "timeout", "2000000", 0);
av_dict_set(&options, "rtmp_live", "live", 0);        // live, recorded, unknown

// 完整配置
AVDictionary* opts = nullptr;
av_dict_set(&opts, "buffer_size", "5242880", 0);
av_dict_set(&opts, "rtmp_live", "live", 0);
av_dict_set(&opts, "timeout", "2000000", 0);
av_dict_set(&opts, "reconnect", "1", 0);
avformat_open_input(&fmt_ctx, url.c_str(), nullptr, &opts);
av_dict_free(&opts);
```

### UDP/RTP 特定

```cpp
// 低延迟直播
av_dict_set(&options, "buffer_size", "1048576", 0);   // 1MB (最小)
av_dict_set(&options, "timeout", "1000000", 0);       // 1s (快速失败)

// UDP 单播模式
av_dict_set(&options, "listen", "0", 0);

// UDP 多播模式
av_dict_set(&options, "reuse", "1", 0);  // 允许端口复用
av_dict_set(&options, "fifo_size", "50", 0);  // 接收队列
```

---

## 📊 配置场景参考表

### 场景 A：稳定的 HLS/DASH 流媒体

```cpp
AVDictionary* opts = nullptr;
av_dict_set(&opts, "buffer_size", "10485760", 0);      // 10MB
av_dict_set(&opts, "max_delay", "10000000", 0);        // 10s 探测
av_dict_set(&opts, "timeout", "2000000", 0);           // 2s 单次超时
av_dict_set(&opts, "reconnect", "1", 0);
av_dict_set(&opts, "reconnect_delay_max", "10", 0);    // 最多延迟 10s
av_dict_set(&opts, "user_agent", "ZenPlay/1.0", 0);
av_dict_set(&opts, "follow_redirects", "1", 0);
av_dict_set(&opts, "multiple_requests", "1", 0);

avformat_open_input(&fmt_ctx, url.c_str(), nullptr, &opts);
av_dict_free(&opts);
```

**特点**：
- 大缓冲容纳 Segment 切换
- 长探测时间确保元数据完整
- 自动重连应对网络中断

---

### 场景 B：实时 RTSP 直播

```cpp
AVDictionary* opts = nullptr;
av_dict_set(&opts, "rtsp_transport", "tcp", 0);        // 可靠传输
av_dict_set(&opts, "buffer_size", "2097152", 0);       // 2MB
av_dict_set(&opts, "max_delay", "3000000", 0);         // 3s 探测
av_dict_set(&opts, "timeout", "3000000", 0);           // 3s 单次超时
av_dict_set(&opts, "reconnect", "1", 0);

avformat_open_input(&fmt_ctx, url.c_str(), nullptr, &opts);
av_dict_free(&opts);
```

**特点**：
- 平衡延迟和可靠性
- TCP 确保不丢包
- 缓冲 2-3MB 应对网络波动

---

### 场景 C：低延迟直播（UDP）

```cpp
AVDictionary* opts = nullptr;
av_dict_set(&opts, "buffer_size", "1048576", 0);       // 1MB
av_dict_set(&opts, "timeout", "1000000", 0);           // 1s 快速失败
av_dict_set(&opts, "reuse", "1", 0);                   // 端口复用
av_dict_set(&opts, "fifo_size", "50", 0);              // 接收队列

avformat_open_input(&fmt_ctx, url.c_str(), nullptr, &opts);
av_dict_free(&opts);
```

**特点**：
- 最小缓冲，毫秒级延迟
- UDP 丢包是代价
- 适合体育直播、实时互动

---

## 🔍 诊断和监控

### 查看 FFmpeg 内部的实际配置

```cpp
// 打开后检查实际应用的选项
void PrintFormatContextOptions(AVFormatContext* fmt_ctx) {
  AVDictionary* opts = nullptr;
  AVDictionaryEntry* entry = nullptr;
  
  while ((entry = av_dict_get(fmt_ctx->metadata, "", entry, AV_DICT_IGNORE_SUFFIX))) {
    printf("Metadata: %s = %s\n", entry->key, entry->value);
  }
}
```

### 使用 ffprobe 诊断

```bash
# 显示打开网络流时的 FFmpeg 日志
ffprobe -v debug "http://example.com/video.mp4" 2>&1 | grep -i "buffer\|delay\|timeout\|reconnect"

# 显示 TCP 连接信息
ffprobe -v trace "rtsp://example.com/stream" 2>&1 | head -50

# 使用自定义选项测试（通过 -option 传递）
ffplay -buffer_size 10485760 -max_delay 5000000 "http://example.com/video.mp4"
```

### 性能监控代码

```cpp
#include <chrono>
#include <iostream>

void MonitorNetworkPerformance(Demuxer* demuxer) {
  auto start = std::chrono::steady_clock::now();
  int packets = 0;
  int64_t total_bytes = 0;
  
  for (int i = 0; i < 1000; i++) {
    auto read_start = std::chrono::steady_clock::now();
    auto result = demuxer->ReadPacket();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - read_start);
    
    if (result.IsOk() && result.Value()) {
      packets++;
      total_bytes += result.Value()->size;
      av_packet_free(&result.Value());
    }
    
    if (i % 100 == 0) {
      auto total_elapsed = std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::steady_clock::now() - start);
      
      printf("Packets: %d, Bytes: %ld MB, Time: %ld s, "
             "Rate: %.2f Mbps, Latency: %ld us\n",
             packets, total_bytes / (1024*1024), total_elapsed.count(),
             (total_bytes * 8.0) / (1024*1024.0*total_elapsed.count()),
             elapsed.count());
    }
  }
}
```

---

## 💡 常见问题和解决方案

### Q1: 为什么前 2 秒快，后面慢？

**根本原因**：
- 前 2 秒：TCP 接收缓冲(64-256KB) 有数据，直接读取
- 之后：缓冲耗尽，需要等待新数据到达（网络延迟 10-100ms）

**解决方案**：
```cpp
av_dict_set(&opts, "buffer_size", "10485760", 0);  // 增加应用层缓冲
av_dict_set(&opts, "reconnect", "1", 0);            // 容错网络波动
```

---

### Q2: timeout 设置多少合适？

**规则**：
```
timeout = 网络往返时间(RTT) × 3 + 冗余

本地网络(LAN):      1-2 秒
广域网(Internet):   2-5 秒
跨域(CDN):          5-10 秒
卫星/4G:            10-30 秒
```

**实现**：
```cpp
if (IsLocalNetwork(url)) {
  av_dict_set(&opts, "timeout", "1000000", 0);    // 1s
} else if (IsInternetStream(url)) {
  av_dict_set(&opts, "timeout", "2000000", 0);    // 2s
} else {
  av_dict_set(&opts, "timeout", "5000000", 0);    // 5s（保险）
}
```

---

### Q3: buffer_size 和 max_delay 区别？

| 选项 | 作用层级 | 影响 | 可调性 |
|------|---------|------|--------|
| `buffer_size` | 协议层 | I/O 缓冲 | 可在播放中调整 |
| `max_delay` | 格式层 | 探测延迟 | 仅在 Open 时有效 |

实例：
```cpp
// max_delay 影响 avformat_find_stream_info 的探测时间
av_dict_set(&opts, "max_delay", "5000000", 0);  // 这行在 Open 前设置
avformat_open_input(&fmt_ctx, url.c_str(), nullptr, &opts);
// ↓ max_delay 在此时生效

// buffer_size 影响之后的每次 av_read_frame
while (av_read_frame(fmt_ctx, pkt) >= 0) {
  // ↑ buffer_size 在此时生效
}
```

---

## 🚀 最佳实践总结

### ✅ DO（推荐）

```cpp
// 1. 针对不同协议应用不同配置
if (IsHttp(url)) ApplyHttpConfig(opts);
else if (IsRtsp(url)) ApplyRtspConfig(opts);
else if (IsUdp(url)) ApplyUdpConfig(opts);

// 2. 始终设置 timeout 和 reconnect
av_dict_set(&opts, "timeout", "2000000", 0);
av_dict_set(&opts, "reconnect", "1", 0);

// 3. 监控缓冲状态
double buffer_health = GetPrefetchBufferHealth();
if (buffer_health < 20%) LOG_WARN("Buffer running low");

// 4. Seek 后重置缓冲
av_seek_frame(fmt_ctx, -1, timestamp, 0);
ClearPrefetchBuffer();
```

### ❌ DON'T（避免）

```cpp
// 1. ❌ 盲目设置过大的缓冲
av_dict_set(&opts, "buffer_size", "1073741824", 0);  // 1GB - OOM 风险

// 2. ❌ 忽略网络流和本地文件的差异
// 相同配置用于 HTTP 和本地文件 → 浪费内存

// 3. ❌ 在播放中动态修改 max_delay
// max_delay 仅在 Open 时有效，中途改变无效

// 4. ❌ 不处理 timeout 错误
if (av_read_frame(fmt_ctx, pkt) < 0) {
  // ❌ 直接退出
  // ✅ 应该重试或记录日志
}
```

---

## 📝 参考

- FFmpeg 官方文档: https://ffmpeg.org/ffmpeg-protocols.html
- 协议文档: libavformat/protocols.texi (FFmpeg 源码)
- 网络层: libavformat/network.c (FFmpeg 源码)
- 常见问题: https://trac.ffmpeg.org/wiki/StreamingGuide

