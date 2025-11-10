# 网络流读包优化 - 代码实现补丁

## 补丁 1: 增强 demuxer.h（添加预读支持）

```cpp
#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

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
  // 预读配置结构
  struct PrefetchConfig {
    size_t buffer_size_mb = 10;        // 预读缓冲大小（MB）
    size_t min_refill_size_mb = 2;     // 低于此值触发补充预读
    bool enable_prefetch = true;       // 是否启用异步预读线程
    bool is_network_stream = false;    // 是否是网络流（自动检测）
  };

  Demuxer();
  ~Demuxer();

  /**
   * @brief 打开媒体文件或流（自动检测网络流）
   * @param url 文件路径或网络 URL
   * @return Result<void> 成功返回 Ok()，失败返回详细错误信息
   */
  Result<void> Open(const std::string& url);

  /**
   * @brief 打开媒体文件或流（带预读配置）
   * @param url 文件路径或网络 URL
   * @param prefetch_config 预读配置
   * @return Result<void>
   */
  Result<void> Open(const std::string& url, const PrefetchConfig& prefetch_config);

  /**
   * @brief 关闭 Demuxer 并释放资源
   */
  void Close();

  /**
   * @brief 读取下一个数据包
   * @return Result<AVPacket*> 成功返回数据包指针，EOF 返回 nullptr，失败返回错误
   */
  Result<AVPacket*> ReadPacket();

  /**
   * @brief 跳转到指定时间戳
   * @param timestamp 目标时间戳（微秒）
   * @param backward 是否向后搜索关键帧
   * @return 成功返回 true，失败返回 false
   */
  bool Seek(int64_t timestamp, bool backward = false);

  AVDictionary* GetMetadata() const;
  int64_t GetDuration() const;  // 返回总时长（毫秒）

  int active_video_stream_index() const { return active_video_stream_index_; }
  int active_audio_stream_index() const { return active_audio_stream_index_; }

  AVStream* findStreamByIndex(int index) const;

  // 预读统计接口
  size_t GetPrefetchBufferSize() const { return buffered_bytes_.load(); }
  
  double GetPrefetchBufferHealth() const {
    size_t current = buffered_bytes_.load();
    size_t max = prefetch_config_.buffer_size_mb * 1024 * 1024;
    return max > 0 ? (double)current / max * 100.0 : 0.0;
  }

  bool IsPrefetchEnabled() const { return prefetch_config_.enable_prefetch; }

 private:
  void probeStreams();
  void PrefetchWorkerThread();  // 异步预读工作线程
  bool IsNetworkUrl(const std::string& url) const;

  AVFormatContext* format_context_;
  std::vector<int> video_streams_;
  std::vector<int> audio_streams_;

  int active_video_stream_index_ = -1;
  int active_audio_stream_index_ = -1;

  // === 预读相关成员 ===
  PrefetchConfig prefetch_config_;
  std::queue<AVPacket*> prefetch_queue_;
  mutable std::mutex prefetch_mutex_;
  std::condition_variable prefetch_cv_;
  std::thread prefetch_thread_;
  std::atomic<bool> prefetch_running_{false};
  std::atomic<bool> prefetch_should_stop_{false};
  std::atomic<size_t> buffered_bytes_{0};

  static std::once_flag init_once_flag_;
};

}  // namespace zenplay
```

---

## 补丁 2: 增强 demuxer.cpp（实现预读和优化选项）

```cpp
#include "player/demuxer/demuxer.h"

#include <algorithm>
#include <chrono>

#include "player/common/ffmpeg_error_utils.h"
#include "player/common/log_manager.h"

namespace zenplay {

std::once_flag Demuxer::init_once_flag_;

Demuxer::Demuxer() : format_context_(nullptr) {
  std::call_once(init_once_flag_, []() { avformat_network_init(); });
}

Demuxer::~Demuxer() {
  Close();
}

bool Demuxer::IsNetworkUrl(const std::string& url) const {
  return url.find("http://") == 0 || url.find("https://") == 0 ||
         url.find("rtsp://") == 0 || url.find("rtmp://") == 0 ||
         url.find("udp://") == 0 || url.find("rtp://") == 0 ||
         url.find("mms://") == 0;
}

Result<void> Demuxer::Open(const std::string& url) {
  PrefetchConfig default_config;
  default_config.is_network_stream = IsNetworkUrl(url);
  return Open(url, default_config);
}

Result<void> Demuxer::Open(const std::string& url,
                           const PrefetchConfig& prefetch_config) {
  if (format_context_) {
    Close();
  }

  prefetch_config_ = prefetch_config;

  // 自动检测网络流
  if (!prefetch_config_.is_network_stream) {
    prefetch_config_.is_network_stream = IsNetworkUrl(url);
  }

  AVDictionary* options = nullptr;

  // === 通用网络选项（所有网络协议） ===
  if (prefetch_config_.is_network_stream) {
    // 自动重连配置
    av_dict_set(&options, "reconnect", "1", 0);
    av_dict_set(&options, "reconnect_delay_max", "5", 0);  // 最大重连延迟 5s
    av_dict_set(&options, "reconnect_streamed", "1", 0);
  }

  // === HTTP/HTTPS 优化 ===
  if (url.find("http://") == 0 || url.find("https://") == 0) {
    // 大缓冲区应对网络抖动
    av_dict_set(&options, "buffer_size", "10485760", 0);  // 10MB
    av_dict_set(&options, "max_delay", "5000000", 0);     // 5秒
    av_dict_set(&options, "user_agent", "ZenPlay/1.0", 0);
    MODULE_DEBUG(LOG_MODULE_DEMUXER,
                 "HTTP(S) stream: buffer=10MB, max_delay=5s");
  }
  // === RTSP/RTMP 优化 ===
  else if (url.find("rtsp://") == 0 || url.find("rtmp://") == 0) {
    av_dict_set(&options, "rtsp_transport", "tcp", 0);    // TCP 更可靠
    av_dict_set(&options, "buffer_size", "5242880", 0);   // 5MB
    av_dict_set(&options, "max_delay", "5000000", 0);     // 5秒
    av_dict_set(&options, "timeout", "2000000", 0);       // 2秒超时
    MODULE_DEBUG(LOG_MODULE_DEMUXER,
                 "RTSP(P) stream: buffer=5MB, timeout=2s");
  }
  // === UDP 协议（低延迟直播） ===
  else if (url.find("udp://") == 0) {
    av_dict_set(&options, "buffer_size", "1048576", 0);   // 1MB
    av_dict_set(&options, "timeout", "1000000", 0);       // 1秒
    MODULE_DEBUG(LOG_MODULE_DEMUXER, "UDP stream: buffer=1MB, timeout=1s");
  }

  int ret =
      avformat_open_input(&format_context_, url.c_str(), nullptr, &options);
  if (ret < 0) {
    av_dict_free(&options);
    if (format_context_) {
      avformat_free_context(format_context_);
      format_context_ = nullptr;
    }
    return FFmpegErrorToResult(ret, "Open input: " + url);
  }

  av_dict_free(&options);

  ret = avformat_find_stream_info(format_context_, nullptr);
  if (ret < 0) {
    Close();
    return FFmpegErrorToResult(ret, "Find stream info: " + url);
  }

  probeStreams();

  // === 启动异步预读线程（网络流） ===
  if (prefetch_config_.enable_prefetch && prefetch_config_.is_network_stream) {
    prefetch_running_ = true;
    prefetch_should_stop_ = false;
    prefetch_thread_ = std::thread(&Demuxer::PrefetchWorkerThread, this);
    MODULE_INFO(LOG_MODULE_DEMUXER,
                "Prefetch thread started: buffer_size={}MB, "
                "min_refill={}MB",
                prefetch_config_.buffer_size_mb,
                prefetch_config_.min_refill_size_mb);
  }

  return Result<void>::Ok();
}

void Demuxer::Close() {
  // === 停止预读线程 ===
  if (prefetch_thread_.joinable()) {
    prefetch_should_stop_ = true;
    prefetch_cv_.notify_all();
    prefetch_thread_.join();
    prefetch_running_ = false;
    MODULE_DEBUG(LOG_MODULE_DEMUXER, "Prefetch thread stopped");
  }

  // === 清空预读队列 ===
  {
    std::unique_lock<std::mutex> lock(prefetch_mutex_);
    while (!prefetch_queue_.empty()) {
      AVPacket* packet = prefetch_queue_.front();
      prefetch_queue_.pop();
      av_packet_free(&packet);
    }
  }

  if (format_context_) {
    avformat_free_context(format_context_);
    format_context_ = nullptr;
    video_streams_.clear();
    audio_streams_.clear();
    active_video_stream_index_ = -1;
    active_audio_stream_index_ = -1;
    buffered_bytes_ = 0;
  }
}

Result<AVPacket*> Demuxer::ReadPacket() {
  // === 如果启用了预读，从预读队列读取 ===
  if (prefetch_running_) {
    std::unique_lock<std::mutex> lock(prefetch_mutex_);

    // 等待预读队列有数据或预读停止
    prefetch_cv_.wait(lock, [this]() {
      return !prefetch_queue_.empty() || prefetch_should_stop_;
    });

    if (!prefetch_queue_.empty()) {
      AVPacket* packet = prefetch_queue_.front();
      prefetch_queue_.pop();

      // 更新缓冲统计
      buffered_bytes_ -= packet->size;

      lock.unlock();

      // 通知预读线程可以继续读取
      prefetch_cv_.notify_one();

      // 检查是否是活动流
      if (packet->stream_index != active_audio_stream_index_ &&
          packet->stream_index != active_video_stream_index_) {
        av_packet_unref(packet);
        av_packet_free(&packet);
        return ReadPacket();  // 递归读取下一个
      }

      return Result<AVPacket*>::Ok(packet);
    }

    // 预读线程已停止且队列为空 → EOF
    if (prefetch_should_stop_) {
      return Result<AVPacket*>::Ok(nullptr);
    }
  }

  // === 直接读取（无预读或预读未启用） ===
  AVPacket* packet = av_packet_alloc();
  if (!packet) {
    return Result<AVPacket*>::Err(ErrorCode::kOutOfMemory,
                                  "Failed to allocate AVPacket");
  }

  int ret = av_read_frame(format_context_, packet);

  if (ret == AVERROR_EOF) {
    av_packet_free(&packet);
    return Result<AVPacket*>::Ok(nullptr);
  } else if (ret < 0) {
    av_packet_free(&packet);
    return Result<AVPacket*>::Err(MapFFmpegError(ret),
                                  FormatFFmpegError(ret, "Read packet"));
  }

  // 跳过非活动流的数据包
  if (packet->stream_index != active_audio_stream_index_ &&
      packet->stream_index != active_video_stream_index_) {
    av_packet_unref(packet);
    av_packet_free(&packet);
    return ReadPacket();  // 递归读取
  }

  return Result<AVPacket*>::Ok(packet);
}

void Demuxer::PrefetchWorkerThread() {
  MODULE_DEBUG(LOG_MODULE_DEMUXER, "Prefetch worker thread started");
  
  size_t max_buffer_bytes = prefetch_config_.buffer_size_mb * 1024 * 1024;
  size_t min_refill_bytes = prefetch_config_.min_refill_size_mb * 1024 * 1024;

  while (!prefetch_should_stop_) {
    // 检查缓冲大小
    size_t current_buffer = buffered_bytes_.load();

    // 如果缓冲未满且未满最大值，继续预读
    if (current_buffer < max_buffer_bytes) {
      AVPacket* packet = av_packet_alloc();
      if (!packet) {
        MODULE_WARN(LOG_MODULE_DEMUXER,
                    "Failed to allocate packet in prefetch thread");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        continue;
      }

      int ret = av_read_frame(format_context_, packet);

      if (ret == AVERROR_EOF) {
        av_packet_free(&packet);
        // EOF，设置停止标志但保持队列中的数据可读
        std::unique_lock<std::mutex> lock(prefetch_mutex_);
        prefetch_should_stop_ = true;
        lock.unlock();
        prefetch_cv_.notify_all();
        break;
      } else if (ret < 0) {
        av_packet_free(&packet);
        MODULE_WARN(LOG_MODULE_DEMUXER,
                    "Prefetch read error: {}",
                    FormatFFmpegError(ret, "Prefetch"));
        // 出错，暂停一会儿重试
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        continue;
      }

      // 将数据包加入预读队列
      {
        std::unique_lock<std::mutex> lock(prefetch_mutex_);
        prefetch_queue_.push(packet);
        buffered_bytes_ += packet->size;

        if (current_buffer == 0) {
          // 队列从空变有数据，通知读取线程
          lock.unlock();
          prefetch_cv_.notify_one();
        }
      }

    } else {
      // 缓冲满了，等待消费
      std::unique_lock<std::mutex> lock(prefetch_mutex_);
      prefetch_cv_.wait_for(lock, std::chrono::milliseconds(100), [this]() {
        return buffered_bytes_.load() < prefetch_config_.buffer_size_mb * 1024 * 1024 / 2;
      });
    }
  }

  MODULE_DEBUG(LOG_MODULE_DEMUXER, "Prefetch worker thread exiting");
}

bool Demuxer::Seek(int64_t timestamp, bool backward) {
  if (!format_context_) {
    return false;
  }

  int ret = av_seek_frame(format_context_, -1, timestamp,
                          backward ? AVSEEK_FLAG_BACKWARD : 0);

  if (ret < 0) {
    return false;
  }

  avformat_flush(format_context_);

  // === Seek 后清空预读缓冲 ===
  if (prefetch_running_) {
    std::unique_lock<std::mutex> lock(prefetch_mutex_);
    while (!prefetch_queue_.empty()) {
      AVPacket* packet = prefetch_queue_.front();
      prefetch_queue_.pop();
      av_packet_free(&packet);
    }
    buffered_bytes_ = 0;
  }

  return true;
}

AVDictionary* Demuxer::GetMetadata() const {
  if (!format_context_) {
    return nullptr;
  }
  return format_context_->metadata;
}

int64_t Demuxer::GetDuration() const {
  if (!format_context_) {
    return 0;
  }
  return static_cast<int64_t>(format_context_->duration / 1000);
}

AVStream* Demuxer::findStreamByIndex(int index) const {
  if (!format_context_ || index < 0 ||
      index >= static_cast<int>(format_context_->nb_streams)) {
    return nullptr;
  }
  return format_context_->streams[index];
}

void Demuxer::probeStreams() {
  video_streams_.clear();
  audio_streams_.clear();

  for (unsigned int i = 0; i < format_context_->nb_streams; ++i) {
    AVStream* stream = format_context_->streams[i];
    if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      video_streams_.push_back(i);
      if (active_video_stream_index_ == -1) {
        active_video_stream_index_ = i;
      }
    } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
      audio_streams_.push_back(i);
      if (active_audio_stream_index_ == -1) {
        active_audio_stream_index_ = i;
      }
    }
  }

  MODULE_INFO(LOG_MODULE_DEMUXER, "Found {} video streams, {} audio streams",
              video_streams_.size(), audio_streams_.size());
}

}  // namespace zenplay
```

---

## 补丁 3: 调整队列大小（playback_controller.h）

在 `PlaybackController` 构造函数之前的成员变量声明中：

```cpp
// 旧配置
// BlockingQueue<AVPacket*> video_packet_queue_{16};
// BlockingQueue<AVPacket*> audio_packet_queue_{32};

// ✅ 新配置：网络流优化
// 增大队列容量以应对网络抖动
BlockingQueue<AVPacket*> video_packet_queue_{64};  // 16 → 64
BlockingQueue<AVPacket*> audio_packet_queue_{96};  // 32 → 96
```

或者在构造函数中配置化：

```cpp
PlaybackController::PlaybackController(
    std::shared_ptr<PlayerStateManager> state_manager,
    Demuxer* demuxer,
    VideoDecoder* video_decoder,
    AudioDecoder* audio_decoder,
    Renderer* renderer)
    : demuxer_(demuxer),
      video_decoder_(video_decoder),
      audio_decoder_(audio_decoder),
      renderer_(renderer),
      state_manager_(state_manager),
      video_packet_queue_(64),    // ✅ 改为 64
      audio_packet_queue_(96) {   // ✅ 改为 96
  // ... 其余初始化代码
}
```

---

## 补丁 4: 监控缓冲状态（可选，用于调试）

在主播放循环中添加日志：

```cpp
void PlaybackController::DemuxTask() {
  if (!demuxer_) {
    return;
  }

  auto last_log_time = std::chrono::steady_clock::now();

  while (!state_manager_->ShouldStop()) {
    if (state_manager_->ShouldPause()) {
      state_manager_->WaitForResume();
      continue;
    }

    TIMER_START(demux_read);

    auto packet_result = demuxer_->ReadPacket();
    if (!packet_result.IsOk()) {
      // ... EOF 处理
      break;
    }

    AVPacket* packet = packet_result.Value();

    if (!packet) {
      // ... EOF 处理
      break;
    }

    auto demux_time_ms = TIMER_END_MS_INT(demux_read);

    // === 定期输出预读缓冲状态 ===
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(now - last_log_time)
            .count() > 1) {
      if (demuxer_->IsPrefetchEnabled()) {
        MODULE_INFO(
            LOG_MODULE_PLAYER,
            "📊 Prefetch status: {}MB / {}MB ({:.1f}%)",
            demuxer_->GetPrefetchBufferSize() / (1024 * 1024),
            static_cast<int>(demuxer_->GetPrefetchBufferSize() / (1024 * 1024)),
            demuxer_->GetPrefetchBufferHealth());
      }
      last_log_time = now;
    }

    STATS_UPDATE_DEMUX(1, packet->size, demux_time_ms,
                       packet->stream_index == demuxer_->active_video_stream_index());

    // ... 分发 packet 逻辑
  }
}
```

---

## 🧪 测试代码

```cpp
// 在您的测试文件中
#include "player/demuxer/demuxer.h"

void TestNetworkStreamPerformance() {
  zenplay::Demuxer demuxer;

  // 配置网络流
  zenplay::Demuxer::PrefetchConfig config;
  config.buffer_size_mb = 10;      // 10MB 缓冲
  config.min_refill_size_mb = 2;   // 低于 2MB 补充预读
  config.enable_prefetch = true;   // 启用预读

  std::string url = "http://example.com/video.mp4";  // 替换为您的网络流 URL

  auto result = demuxer.Open(url, config);
  if (!result.IsOk()) {
    std::cerr << "Failed to open: " << result.FullMessage() << std::endl;
    return;
  }

  std::cout << "Starting network stream performance test...\n";

  int packets_read = 0;
  auto start_time = std::chrono::steady_clock::now();

  for (int i = 0; i < 300; i++) {  // 读取 300 个包
    auto read_start = std::chrono::steady_clock::now();

    auto packet_result = demuxer.ReadPacket();
    if (!packet_result.IsOk()) {
      std::cerr << "Read failed: " << packet_result.FullMessage() << std::endl;
      break;
    }

    AVPacket* packet = packet_result.Value();
    if (!packet) {
      std::cout << "EOF reached\n";
      break;
    }

    auto read_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - read_start)
                            .count();

    packets_read++;

    if (i % 50 == 0) {
      auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::steady_clock::now() - start_time)
                         .count();
      std::cout << "Packet " << i << " @ t=" << elapsed << "s, "
                << "read_time=" << read_time_ms << "ms, "
                << "buffer_health=" << demuxer.GetPrefetchBufferHealth() << "%\n";
    }

    av_packet_free(&packet);
  }

  auto total_time = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::steady_clock::now() - start_time)
                        .count();

  std::cout << "\n=== Test Summary ===\n"
            << "Packets read: " << packets_read << "\n"
            << "Total time: " << total_time << "s\n"
            << "Average rate: " << (packets_read / (float)total_time) << " pps\n";

  demuxer.Close();
}
```

---

## 编译步骤

1. 替换 `src/player/demuxer/demuxer.h` 和 `demuxer.cpp`
2. 在 `playback_controller.h` 中调整队列大小
3. 重新编译：

```bash
cd /workspaces/zenplay
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

---

## 预期效果

| 指标 | 优化前 | 优化后 |
|------|--------|--------|
| 前 2 秒读包延迟 | 1-5ms | 1-3ms |
| 2+ 秒读包延迟 | 50-200ms（下降） | 3-5ms（稳定） |
| 缓冲区利用率 | 波动 | 稳定 30-60% |
| 网络抖动容错 | 弱 | 强（10MB 缓冲） |
| 内存使用 | +0MB | +15-20MB |

