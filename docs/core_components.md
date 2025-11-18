# 🧩 核心组件详解

> **文档版本**: v1.0  
> **最后更新**: 2025-11-18  
> **相关文档**: [整体架构设计](architecture_overview.md) | [状态管理系统](state_management.md)

---

## 📋 文档概览

本文档深入剖析 ZenPlay 的核心组件实现，包括每个关键类的职责、接口设计、内部实现和使用示例。

**包含组件**:
1. **ZenPlayer** - 应用层统一接口
2. **PlaybackController** - 核心协调器
3. **AudioPlayer & VideoPlayer** - 音视频播放器
4. **AVSyncController** - 音视频同步控制器
5. **Demuxer** - 解封装器
6. **Decoder** - 解码器（Video/Audio）
7. **AudioResampler** - 音频重采样器
8. **HWDecoderContext** - 硬件解码上下文
9. **Renderer** - 渲染器接口

**阅读建议**:
- 按顺序阅读可以理解组件之间的依赖关系
- 结合 [整体架构设计](architecture_overview.md) 理解各组件在系统中的位置
- 参考 [状态管理系统](state_management.md) 了解状态转换逻辑

---

## 🎬 1. ZenPlayer - 应用层统一接口

**位置**: Layer 2 (应用层)  
**职责**: 统一对外接口、生命周期管理、资源编排

### 1.1 设计理念

ZenPlayer 是整个播放器的**门面 (Facade)**，隐藏内部复杂性，提供简洁的 API：

```cpp
// 简单的使用示例
ZenPlayer player;
player.Open("video.mp4");
player.SetRenderWindow(window_handle, 1920, 1080);
player.Play();
player.Pause();
player.SeekAsync(30000);  // 跳转到 30 秒
player.Stop();
player.Close();
```

### 1.2 核心接口

```cpp
class ZenPlayer {
 public:
  // ========== 生命周期管理 ==========
  
  /**
   * @brief 打开媒体文件
   * @param url 文件路径或网络 URL
   * @return Result<void> 成功返回 Ok，失败返回详细错误信息
   */
  Result<void> Open(const std::string& url);
  
  /**
   * @brief 关闭播放器，释放所有资源
   */
  void Close();
  
  // ========== 播放控制 ==========
  
  Result<void> Play();     // 开始播放
  Result<void> Pause();    // 暂停播放
  void Stop();             // 停止播放
  void SeekAsync(int64_t timestamp_ms, bool backward = true);  // 异步跳转
  
  // ========== 渲染窗口管理 ==========
  
  Result<void> SetRenderWindow(void* window_handle, int width, int height);
  void OnWindowResize(int width, int height);
  
  // ========== 状态查询 ==========
  
  PlayerStateManager::PlayerState GetState() const;
  int64_t GetDuration() const;
  int64_t GetCurrentPlayTime() const;
  bool IsOpened() const;
  
  // ========== 状态通知 ==========
  
  int RegisterStateChangeCallback(StateChangeCallback callback);
  void UnregisterStateChangeCallback(int callback_id);
  
 private:
  // 内部组件（拥有所有权）
  std::unique_ptr<Demuxer> demuxer_;
  std::unique_ptr<VideoDecoder> video_decoder_;
  std::unique_ptr<AudioDecoder> audio_decoder_;
  std::unique_ptr<Renderer> renderer_;
  std::unique_ptr<PlaybackController> playback_controller_;
  std::unique_ptr<HWDecoderContext> hw_decoder_context_;
  
  // 共享状态管理器
  std::shared_ptr<PlayerStateManager> state_manager_;
  
  bool is_opened_ = false;
};
```

### 1.3 Open() 流程详解

`Open()` 是最复杂的接口，涉及多个组件的初始化，使用 `Result<T>` 链式处理：

```cpp
Result<void> ZenPlayer::Open(const std::string& url) {
  // 如果已打开，先关闭
  if (is_opened_) {
    Close();
  }
  
  state_manager_->TransitionToOpening();
  
  return demuxer_->Open(url)
      // ✅ Step 1: Demuxer 打开成功
      .AndThen([this]() -> Result<void> {
        return InitializeVideoRenderingPipeline();
      })
      // ✅ Step 2: 视频渲染管线初始化（或跳过）
      .AndThen([this]() -> Result<void> {
        return InitializeAudioDecoder();
      })
      // ✅ Step 3: 音频解码器打开（或跳过）
      .AndThen([this]() -> Result<void> {
        // 创建播放控制器
        playback_controller_ = std::make_unique<PlaybackController>(
            state_manager_, demuxer_.get(), video_decoder_.get(),
            audio_decoder_.get(), renderer_.get());
        
        is_opened_ = true;
        state_manager_->TransitionToStopped();
        return Result<void>::Ok();
      })
      // ❌ 任一步骤失败，自动清理
      .MapErr([this](ErrorCode code) -> ErrorCode {
        CleanupResources();
        is_opened_ = false;
        state_manager_->TransitionToError();
        return code;
      });
}
```

**关键流程**:

1. **Demuxer::Open()**: 打开媒体文件，探测流信息
2. **InitializeVideoRenderingPipeline()**: 
   - 使用 `RenderPathSelector` 选择最佳渲染路径
   - 创建 `HWDecoderContext`（如果硬件加速）
   - 创建 `Renderer`（已包装为 `RendererProxy`）
   - 打开 `VideoDecoder`
3. **InitializeAudioDecoder()**: 打开音频解码器
4. **创建 PlaybackController**: 将所有组件传递给协调器
5. **错误处理**: 任一步骤失败，自动调用 `CleanupResources()`

### 1.4 资源清理顺序

```cpp
void ZenPlayer::CleanupResources() {
  // 🧹 按依赖关系的逆序清理资源
  
  // 1. 先停止播放控制器（依赖所有其他资源）
  if (playback_controller_) {
    playback_controller_.reset();
  }
  
  // 2. 关闭解码器（依赖硬件上下文和解封装器）
  if (audio_decoder_ && audio_decoder_->opened()) {
    audio_decoder_->Close();
  }
  if (video_decoder_ && video_decoder_->opened()) {
    video_decoder_->Close();
  }
  
  // 3. 清理硬件解码上下文（在解码器关闭后）
  if (hw_decoder_context_) {
    hw_decoder_context_.reset();
  }
  
  // 4. 最后关闭解封装器（底层资源）
  if (demuxer_) {
    demuxer_->Close();
  }
}
```

**设计原则**: 按依赖关系的**逆序**清理，避免悬空指针。

---

## 🎮 2. PlaybackController - 核心协调器

**位置**: Layer 3 (核心层)  
**职责**: 线程管理、数据流协调、播放控制

### 2.1 设计理念

PlaybackController 是整个播放器的**大脑**，协调所有线程和组件：

```
PlaybackController
    ├── 线程管理: 创建和管理 5+ 个工作线程
    ├── 数据流: 管理 Packet/Frame 队列，实现背压控制
    ├── 播放器协调: 统一控制 AudioPlayer 和 VideoPlayer
    └── 同步控制: 管理 AVSyncController 进行音视频同步
```

### 2.2 核心接口

```cpp
class PlaybackController {
 public:
  PlaybackController(
      std::shared_ptr<PlayerStateManager> state_manager,
      Demuxer* demuxer,
      VideoDecoder* video_decoder,
      AudioDecoder* audio_decoder,
      Renderer* renderer);
  
  ~PlaybackController();
  
  // ========== 播放控制 ==========
  
  Result<void> Start();   // 启动所有线程
  void Stop();            // 停止所有线程
  void Pause();           // 暂停播放
  void Resume();          // 恢复播放
  
  // ========== Seek 控制 ==========
  
  void SeekAsync(int64_t timestamp_ms, bool backward = true);
  
  // ========== 音量控制 ==========
  
  void SetVolume(float volume);
  float GetVolume() const;
  
  // ========== 状态查询 ==========
  
  int64_t GetCurrentTime() const;
  
 private:
  // ========== 线程任务 ==========
  
  void DemuxTask();           // 解封装线程
  void VideoDecodeTask();     // 视频解码线程
  void AudioDecodeTask();     // 音频解码线程
  void SyncControlTask();     // 同步控制线程
  void SeekTask();            // Seek 专用线程
  
  // ========== 内部方法 ==========
  
  void StopAllThreads();      // 停止所有线程并 join
  void ClearAllQueues();      // 清空所有队列
  bool ExecuteSeek(const SeekRequest& request);
  
 private:
  // 组件引用（不拥有所有权）
  Demuxer* demuxer_;
  VideoDecoder* video_decoder_;
  AudioDecoder* audio_decoder_;
  Renderer* renderer_;
  
  // 播放器组件（拥有所有权）
  std::unique_ptr<AudioPlayer> audio_player_;
  std::unique_ptr<VideoPlayer> video_player_;
  std::unique_ptr<AVSyncController> av_sync_controller_;
  std::unique_ptr<AudioResampler> audio_resampler_;
  
  // 共享状态管理器
  std::shared_ptr<PlayerStateManager> state_manager_;
  
  // 数据队列
  BlockingQueue<AVPacket*> video_packet_queue_{64};  // 视频包队列
  BlockingQueue<AVPacket*> audio_packet_queue_{96};  // 音频包队列
  BlockingQueue<SeekRequest> seek_request_queue_{10};  // Seek 请求队列
  
  // 工作线程
  std::unique_ptr<std::thread> demux_thread_;
  std::unique_ptr<std::thread> video_decode_thread_;
  std::unique_ptr<std::thread> audio_decode_thread_;
  std::unique_ptr<std::thread> sync_control_thread_;
  std::unique_ptr<std::thread> seek_thread_;
  
  std::atomic<bool> seeking_{false};
};
```

### 2.3 Start() 流程详解

```cpp
Result<void> PlaybackController::Start() {
  // 1. 重置队列状态
  video_packet_queue_.Reset();
  audio_packet_queue_.Reset();
  seek_request_queue_.Reset();
  
  // 2. 启动解封装线程
  demux_thread_ = std::make_unique<std::thread>(
      &PlaybackController::DemuxTask, this);
  
  // 3. 启动视频解码线程（如果有视频流）
  if (video_decoder_ && video_decoder_->opened()) {
    video_decode_thread_ = std::make_unique<std::thread>(
        &PlaybackController::VideoDecodeTask, this);
  }
  
  // 4. 启动音频解码线程（如果有音频流）
  if (audio_decoder_ && audio_decoder_->opened()) {
    audio_decode_thread_ = std::make_unique<std::thread>(
        &PlaybackController::AudioDecodeTask, this);
  }
  
  // 5. 启动音频播放器
  if (audio_player_) {
    auto result = audio_player_->Start();
    if (!result.IsOk()) {
      MODULE_ERROR(LOG_MODULE_PLAYER, "Failed to start AudioPlayer: {}",
                   result.FullMessage());
    }
  }
  
  // 6. 启动视频播放器
  if (video_player_) {
    auto result = video_player_->Start();
    if (!result.IsOk()) {
      MODULE_ERROR(LOG_MODULE_PLAYER, "Failed to start VideoPlayer: {}",
                   result.FullMessage());
    }
  }
  
  // 7. 启动同步控制线程
  sync_control_thread_ = std::make_unique<std::thread>(
      &PlaybackController::SyncControlTask, this);
  
  // 8. 启动 Seek 专用线程
  seek_thread_ = std::make_unique<std::thread>(
      &PlaybackController::SeekTask, this);
  
  return Result<void>::Ok();
}
```

### 2.4 同步模式选择逻辑

PlaybackController 在构造时智能选择同步模式：

```cpp
PlaybackController::PlaybackController(...) {
  // 根据音视频流的存在情况智能选择同步模式
  bool has_audio = audio_decoder_ && audio_decoder_->opened();
  bool has_video = video_decoder_ && video_decoder_->opened();
  
  if (has_audio && has_video) {
    // 场景 1: 音视频都有 → 使用音频主时钟（标准播放）
    av_sync_controller_->SetSyncMode(AVSyncController::SyncMode::AUDIO_MASTER);
    
  } else if (has_audio && !has_video) {
    // 场景 2: 只有音频 → 使用音频主时钟（音乐播放）
    av_sync_controller_->SetSyncMode(AVSyncController::SyncMode::AUDIO_MASTER);
    
  } else if (!has_audio && has_video) {
    // 场景 3: 只有视频 → 使用外部时钟（GIF、静默视频）
    av_sync_controller_->SetSyncMode(AVSyncController::SyncMode::EXTERNAL_MASTER);
    
  } else {
    // 场景 4: 既无音频也无视频 → 错误情况
    av_sync_controller_->SetSyncMode(AVSyncController::SyncMode::EXTERNAL_MASTER);
  }
}
```

### 2.5 DemuxTask 实现

```cpp
void PlaybackController::DemuxTask() {
  MODULE_INFO(LOG_MODULE_PLAYER, "DemuxTask started");
  
  while (!state_manager_->ShouldStop()) {
    // 1. 检查是否需要暂停
    if (state_manager_->ShouldPause()) {
      state_manager_->WaitForResume();
      continue;
    }
    
    // 2. 从 Demuxer 读取数据包
    TIMER_START(demux_read);
    auto packet_result = demuxer_->ReadPacket();
    TIMER_END(demux_read);
    
    if (!packet_result.IsOk()) {
      // EOF 或错误
      break;
    }
    
    AVPacket* packet = packet_result.Unwrap();
    
    // 3. 根据流类型分发到不同队列
    if (packet->stream_index == demuxer_->active_video_stream_index()) {
      // 视频包 → 视频队列（队列满时阻塞）
      if (!video_packet_queue_.Push(packet, 100)) {
        av_packet_free(&packet);  // 超时，释放包
      }
    } else if (packet->stream_index == demuxer_->active_audio_stream_index()) {
      // 音频包 → 音频队列
      if (!audio_packet_queue_.Push(packet, 100)) {
        av_packet_free(&packet);
      }
    } else {
      // 其他流（字幕等），暂时忽略
      av_packet_free(&packet);
    }
  }
  
  MODULE_INFO(LOG_MODULE_PLAYER, "DemuxTask stopped");
}
```

**关键点**:
- 使用 `BlockingQueue::Push()` 实现背压控制
- 队列满时阻塞，避免内存爆炸
- 响应 `ShouldStop()` 和 `ShouldPause()` 信号

---

## 🔊 3. AudioPlayer - 音频播放器

**位置**: Layer 4 (组件层)  
**职责**: 音频播放队列管理、音频输出控制、播放时钟跟踪

### 3.1 职责简化（重构后）

**重构前** (职责过多):
- ❌ 管理播放队列
- ❌ 控制音频输出设备
- ❌ 跟踪播放时钟
- ❌ **重采样逻辑**（耗时操作）
- ❌ **SwrContext 管理**

**重构后** (职责清晰):
- ✅ 管理播放队列（`ResampledAudioFrame`）
- ✅ 控制音频输出设备（`AudioOutput`）
- ✅ 跟踪播放时钟（PTS 管理）
- ✅ 音视频同步（与 `AVSyncController` 协作）

**重采样移至 `AudioResampler`，在解码线程完成！**

### 3.2 核心接口

```cpp
class AudioPlayer {
 public:
  struct AudioConfig {
    int target_sample_rate = 44100;
    int target_channels = 2;
    AVSampleFormat target_format = AV_SAMPLE_FMT_S16;
    int target_bits_per_sample = 16;
    int buffer_size = 1024;
  };
  
  AudioPlayer(PlayerStateManager* state_manager,
              AVSyncController* sync_controller);
  
  // ========== 生命周期 ==========
  
  Result<void> Init(const AudioConfig& config);
  Result<void> Start();
  void Stop();
  void Pause();
  void Resume();
  
  // ========== Seek 支持 ==========
  
  void PreSeek();   // Seek 前清空缓冲
  void PostSeek(PlayerStateManager::PlayerState target_state);
  
  // ========== 音量控制 ==========
  
  void SetVolume(float volume);
  float GetVolume() const;
  
  // ========== 帧推送 ==========
  
  bool PushFrame(ResampledAudioFrame frame);
  bool PushFrameTimeout(ResampledAudioFrame frame, int timeout_ms);
  
  // ========== 队列管理 ==========
  
  void ClearFrames();
  
 private:
  // 音频回调（由 AudioOutput 调用）
  void FillAudioBuffer(uint8_t* stream, int len);
  
  PlayerStateManager* state_manager_;
  AVSyncController* sync_controller_;
  
  std::unique_ptr<AudioOutput> audio_output_;
  BlockingQueue<ResampledAudioFrame> frame_queue_{30};  // 帧队列
  
  AudioConfig config_;
  std::atomic<float> volume_{1.0f};
};
```

### 3.3 调用流程

```cpp
// AudioDecodeTask (PlaybackController)
while (!should_stop) {
  // 1. 解码音频帧
  auto frame = audio_decoder_->ReceiveFrame();
  
  // 2. ✅ 预重采样（在解码线程完成）
  auto resampled = audio_resampler_->Resample(frame);
  
  // 3. 推送到 AudioPlayer 的队列
  audio_player_->PushFrame(std::move(resampled));
}

// FillAudioBuffer (AudioPlayer，音频回调线程)
void AudioPlayer::FillAudioBuffer(uint8_t* stream, int len) {
  while (len > 0 && !frame_queue_.Empty()) {
    ResampledAudioFrame& frame = frame_queue_.Front();
    
    // ✅ 仅 memcpy（重采样已完成）
    int copy_size = std::min(len, frame.remaining_bytes);
    std::memcpy(stream, frame.data + frame.read_offset, copy_size);
    
    // 更新播放时钟
    double audio_pts = frame.pts_seconds + (frame.read_offset / bytes_per_sec);
    sync_controller_->UpdateAudioClock(audio_pts);
    
    // 更新读取位置
    frame.read_offset += copy_size;
    len -= copy_size;
    stream += copy_size;
    
    // 帧已播放完，弹出队列
    if (frame.read_offset >= frame.data_size) {
      frame_queue_.Pop();
    }
  }
}
```

**关键优化**:
- 音频回调只做 `memcpy`，延迟极低
- 重采样在解码线程完成，有充足时间
- SIMD 优化可以充分利用

---

## 🎥 4. VideoPlayer - 视频播放器

**位置**: Layer 4 (组件层)  
**职责**: 视频帧队列管理、渲染时序控制、丢帧策略

### 4.1 核心接口

```cpp
class VideoPlayer {
 public:
  struct VideoConfig {
    double target_fps = 30.0;
    bool vsync_enabled = true;
    int max_frame_queue_size = 15;
    bool drop_frames = true;  // 允许丢帧
  };
  
  VideoPlayer(PlayerStateManager* state_manager,
              AVSyncController* sync_controller);
  
  // ========== 生命周期 ==========
  
  bool Init(Renderer* renderer, const VideoConfig& config);
  Result<void> Start();
  void Stop();
  void Pause();
  void Resume();
  
  // ========== Seek 支持 ==========
  
  void PreSeek();
  void PostSeek(PlayerStateManager::PlayerState target_state);
  
  // ========== 帧推送 ==========
  
  bool PushFrame(AVFramePtr frame, const FrameTimestamp& timestamp);
  bool PushFrameBlocking(AVFramePtr frame, 
                         const FrameTimestamp& timestamp,
                         int max_wait_ms);
  
  // ========== 队列管理 ==========
  
  void ClearFrames();
  void ResetTimestamps();
  
 private:
  // 渲染线程任务
  void RenderTask();
  
  PlayerStateManager* state_manager_;
  AVSyncController* sync_controller_;
  Renderer* renderer_;
  
  std::unique_ptr<std::thread> render_thread_;
  
  // 帧队列（线程安全）
  std::deque<std::pair<AVFramePtr, FrameTimestamp>> frame_queue_;
  std::mutex frame_queue_mutex_;
  std::condition_variable frame_queue_cv_;
  
  VideoConfig config_;
};
```

### 4.2 RenderTask 实现

```cpp
void VideoPlayer::RenderTask() {
  MODULE_INFO(LOG_MODULE_VIDEO, "VideoRenderThread started");
  
  while (!state_manager_->ShouldStop()) {
    // 1. 检查是否需要暂停
    if (state_manager_->ShouldPause()) {
      state_manager_->WaitForResume();
      continue;
    }
    
    // 2. 获取下一帧（不弹出）
    std::unique_lock<std::mutex> lock(frame_queue_mutex_);
    if (frame_queue_.empty()) {
      frame_queue_cv_.wait_for(lock, std::chrono::milliseconds(10));
      continue;
    }
    
    auto& [frame, timestamp] = frame_queue_.front();
    
    // 3. 计算显示时间
    double video_pts = timestamp.pts_seconds;
    double master_clock = sync_controller_->GetMasterClock();
    double delay = video_pts - master_clock;
    
    // 4. 等待到显示时间
    if (delay > 0.001) {  // > 1ms
      lock.unlock();  // 释放锁，让其他线程可以推送帧
      std::this_thread::sleep_for(
          std::chrono::duration<double>(delay));
      lock.lock();
    } else if (delay < -0.050) {  // 延迟超过 50ms
      // 丢帧策略：跳过旧帧
      MODULE_DEBUG(LOG_MODULE_VIDEO, "Dropping late frame (delay: {}ms)", 
                   delay * 1000);
      frame_queue_.pop_front();
      continue;
    }
    
    // 5. 渲染帧
    renderer_->RenderFrame(frame.get());
    renderer_->Present();
    
    // 6. 弹出已渲染的帧
    frame_queue_.pop_front();
    lock.unlock();
    frame_queue_cv_.notify_all();
  }
  
  MODULE_INFO(LOG_MODULE_VIDEO, "VideoRenderThread stopped");
}
```

**关键点**:
- 基于 PTS 和主时钟精确控制显示时间
- 延迟过大时主动丢帧
- 响应 Pause/Resume 信号

---

## ⏱️ 5. AVSyncController - 音视频同步控制器

**位置**: Layer 3 (核心层)  
**职责**: 主时钟管理、同步算法、时钟更新

### 5.1 三种同步模式

```cpp
enum class SyncMode {
  AUDIO_MASTER,      // 音频主时钟（推荐）
  VIDEO_MASTER,      // 视频主时钟（特殊场景）
  EXTERNAL_MASTER    // 外部/系统时钟（无音频时）
};
```

| 模式 | 适用场景 | 主时钟来源 | 优势 |
|------|---------|-----------|------|
| **AUDIO_MASTER** | 音频+视频、只有音频 | 音频硬件 | 音频流畅，体验最佳 |
| **VIDEO_MASTER** | 视频演示、逐帧分析 | 视频 PTS | 视频准确 |
| **EXTERNAL_MASTER** | 只有视频、测试 | 系统时钟 | 简单可靠 |

### 5.2 核心接口

```cpp
class AVSyncController {
 public:
  AVSyncController();
  
  // ========== 同步模式 ==========
  
  void SetSyncMode(SyncMode mode);
  SyncMode GetSyncMode() const;
  
  // ========== 时钟更新 ==========
  
  void UpdateAudioClock(double audio_pts);
  void UpdateVideoClock(double video_pts);
  double GetMasterClock() const;
  
  // ========== 播放控制 ==========
  
  void Reset();   // 重置同步状态（Stop 或非 Seek 场景）
  void Pause();   // 暂停时钟
  void Resume();  // 恢复时钟
  
  // ========== Seek 支持 ==========
  
  void ResetForSeek();  // Seek 时重置
  
 private:
  SyncMode sync_mode_ = SyncMode::AUDIO_MASTER;
  
  // 音频时钟
  std::atomic<double> audio_clock_{0.0};
  std::chrono::steady_clock::time_point audio_clock_update_time_;
  
  // 视频时钟
  std::atomic<double> video_clock_{0.0};
  
  // 外部时钟
  std::chrono::steady_clock::time_point play_start_time_;
  std::chrono::duration<double> pause_duration_{0};
  
  mutable std::mutex mutex_;
};
```

### 5.3 GetMasterClock() 实现

```cpp
double AVSyncController::GetMasterClock() const {
  switch (sync_mode_) {
    case SyncMode::AUDIO_MASTER:
      return audio_clock_.load();
    
    case SyncMode::VIDEO_MASTER:
      return video_clock_.load();
    
    case SyncMode::EXTERNAL_MASTER: {
      // 计算外部时钟：当前时间 - 播放开始时间 - 暂停累计时间
      auto now = std::chrono::steady_clock::now();
      auto elapsed = now - play_start_time_ - pause_duration_;
      return std::chrono::duration<double>(elapsed).count();
    }
    
    default:
      return 0.0;
  }
}
```

---

## 📦 6. Demuxer - 解封装器

**位置**: Layer 4 (组件层)  
**职责**: 解析媒体文件格式、读取数据包、流选择、Seek 跳转

### 6.1 核心接口

```cpp
class Demuxer {
 public:
  Demuxer();
  ~Demuxer();
  
  // ========== 生命周期 ==========
  
  Result<void> Open(const std::string& url);
  void Close();
  
  // ========== 数据包读取 ==========
  
  Result<AVPacket*> ReadPacket();
  
  // ========== Seek 支持 ==========
  
  bool Seek(int64_t timestamp, bool backward);
  
  // ========== 流信息查询 ==========
  
  int active_video_stream_index() const;
  int active_audio_stream_index() const;
  AVStream* findStreamByIndex(int index) const;
  
  int64_t GetDuration() const;  // 总时长（毫秒）
  AVDictionary* GetMetadata() const;
  
 private:
  void probeStreams();  // 探测流信息
  
  AVFormatContext* format_context_;
  std::vector<int> video_streams_;
  std::vector<int> audio_streams_;
  
  int active_video_stream_index_ = -1;
  int active_audio_stream_index_ = -1;
};
```

### 6.2 Open() 实现

```cpp
Result<void> Demuxer::Open(const std::string& url) {
  // 1. 分配 AVFormatContext
  format_context_ = avformat_alloc_context();
  
  // 2. 打开输入流
  int ret = avformat_open_input(&format_context_, url.c_str(), nullptr, nullptr);
  if (ret < 0) {
    return Result<void>::Err(ErrorCode::kDemuxError, 
                             "Failed to open input: " + FFmpegErrorToString(ret));
  }
  
  // 3. 读取流信息
  ret = avformat_find_stream_info(format_context_, nullptr);
  if (ret < 0) {
    avformat_close_input(&format_context_);
    return Result<void>::Err(ErrorCode::kDemuxError,
                             "Failed to find stream info");
  }
  
  // 4. 探测视频流和音频流
  probeStreams();
  
  return Result<void>::Ok();
}
```

---

## 🎨 7. Decoder - 解码器（Video/Audio）

**位置**: Layer 4 (组件层)  
**职责**: 视频/音频解码、硬件加速、帧管理

### 7.1 基类抽象

```cpp
class Decoder {
 public:
  virtual ~Decoder() = default;
  
  // ========== 生命周期 ==========
  
  Result<void> Open(AVCodecParameters* codec_params, AVDictionary** options);
  void Close();
  bool opened() const;
  
  // ========== 解码流程 ==========
  
  Result<void> SendPacket(AVPacket* packet);
  Result<AVFrame*> ReceiveFrame();
  
  // ========== 状态管理 ==========
  
  void Flush();  // Seek 时清空内部缓冲
  
 protected:
  AVCodecContext* codec_context_ = nullptr;
  const AVCodec* codec_ = nullptr;
};
```

### 7.2 VideoDecoder 扩展

```cpp
class VideoDecoder : public Decoder {
 public:
  /**
   * @brief 打开视频解码器（支持硬件加速）
   */
  Result<void> Open(AVCodecParameters* codec_params,
                    AVDictionary** options,
                    HWDecoderContext* hw_context);
  
  bool IsHardwareDecoding() const;
  HWDecoderContext* GetHWContext() const;
  
  // 视频特定属性
  int width() const;
  int height() const;
  AVPixelFormat pixel_format() const;
  AVRational time_base() const;
  
 protected:
  Result<void> OnBeforeOpen(AVCodecContext* codec_ctx) override;
  
 private:
  HWDecoderContext* hw_context_ = nullptr;
  bool zero_copy_validated_ = false;
};
```

### 7.3 硬件加速流程

```cpp
Result<void> VideoDecoder::OnBeforeOpen(AVCodecContext* codec_ctx) {
  if (hw_context_ && hw_context_->IsInitialized()) {
    // 配置硬件加速
    auto result = hw_context_->ConfigureDecoder(codec_ctx);
    if (!result.IsOk()) {
      return result;
    }
  }
  return Result<void>::Ok();
}

Result<AVFrame*> VideoDecoder::ReceiveFrame() {
  AVFrame* frame = av_frame_alloc();
  int ret = avcodec_receive_frame(codec_context_, frame);
  
  if (ret == 0 && hw_context_ && !zero_copy_validated_) {
    // 首次硬件解码成功，验证零拷贝
    bool is_zero_copy = hw_context_->ValidateFramesContext(codec_context_);
    MODULE_INFO(LOG_MODULE_VIDEO, "Zero-copy: {}", is_zero_copy ? "YES" : "NO");
    zero_copy_validated_ = true;
  }
  
  return Result<AVFrame*>::Ok(frame);
}
```

---

## 🎵 8. AudioResampler - 音频重采样器

**位置**: Layer 4 (组件层)  
**职责**: 音频格式转换、采样率转换、SIMD 优化

### 8.1 核心接口

```cpp
class AudioResampler {
 public:
  struct ResamplerConfig {
    int target_sample_rate = 44100;
    int target_channels = 2;
    AVSampleFormat target_format = AV_SAMPLE_FMT_S16;
    int target_bits_per_sample = 16;
    bool enable_simd = true;
  };
  
  AudioResampler();
  ~AudioResampler();
  
  void SetConfig(const ResamplerConfig& config);
  
  /**
   * @brief 重采样音频帧
   * @return ResampledAudioFrame 重采样后的音频数据
   */
  Result<ResampledAudioFrame> Resample(AVFrame* frame);
  
  void Reset();  // Seek 时重置
  
 private:
  SwrContext* swr_context_ = nullptr;
  ResamplerConfig config_;
  
  // 缓冲区复用
  std::vector<uint8_t> buffer_;
};
```

### 8.2 Resample() 实现

```cpp
Result<ResampledAudioFrame> AudioResampler::Resample(AVFrame* frame) {
  // 1. 检查格式是否匹配
  if (frame->sample_rate == config_.target_sample_rate &&
      frame->ch_layout.nb_channels == config_.target_channels &&
      frame->format == config_.target_format) {
    // 格式匹配，零拷贝
    return CreateResampledFrameZeroCopy(frame);
  }
  
  // 2. 初始化 SwrContext（首次或格式变化）
  if (!swr_context_) {
    InitializeSwrContext(frame);
  }
  
  // 3. 执行重采样
  int out_samples = av_rescale_rnd(
      frame->nb_samples, config_.target_sample_rate, frame->sample_rate, 
      AV_ROUND_UP);
  
  int buffer_size = out_samples * config_.target_channels * 
                    (config_.target_bits_per_sample / 8);
  buffer_.resize(buffer_size);
  
  uint8_t* out_buffer = buffer_.data();
  int converted = swr_convert(swr_context_, &out_buffer, out_samples,
                              (const uint8_t**)frame->data, frame->nb_samples);
  
  // 4. 创建 ResampledAudioFrame
  ResampledAudioFrame resampled;
  resampled.data = std::move(buffer_);
  resampled.data_size = converted * config_.target_channels * 
                        (config_.target_bits_per_sample / 8);
  resampled.pts_seconds = frame->pts * av_q2d(frame->time_base);
  resampled.sample_rate = config_.target_sample_rate;
  resampled.channels = config_.target_channels;
  
  return Result<ResampledAudioFrame>::Ok(std::move(resampled));
}
```

---

## 🖼️ 9. Renderer - 渲染器接口

**位置**: Layer 5 (平台层)  
**职责**: 跨平台渲染抽象

### 9.1 接口定义

```cpp
class Renderer {
 public:
  virtual ~Renderer() = default;
  
  // ========== 生命周期 ==========
  
  virtual Result<void> Init(void* window_handle, int width, int height) = 0;
  virtual void Cleanup() = 0;
  
  // ========== 渲染 ==========
  
  virtual bool RenderFrame(AVFrame* frame) = 0;
  virtual void Clear() = 0;
  virtual void Present() = 0;
  
  // ========== 窗口管理 ==========
  
  virtual void OnResize(int width, int height) = 0;
  
  // ========== Seek 支持 ==========
  
  virtual void ClearCaches() = 0;  // 清空渲染缓存
  
  // ========== 信息查询 ==========
  
  virtual const char* GetRendererName() const = 0;
};
```

### 9.2 实现类

- **SDL2Renderer**: 软件渲染 + 硬件加速纹理上传
- **D3D11Renderer**: Windows DirectX 11 原生渲染，零拷贝支持

---

## 📊 组件依赖关系图

```
ZenPlayer (L2)
    ├──> Demuxer (L4)
    ├──> VideoDecoder (L4)
    ├──> AudioDecoder (L4)
    ├──> Renderer (L5)
    ├──> HWDecoderContext (L4)
    ├──> PlayerStateManager (L3) [共享]
    └──> PlaybackController (L3)
            ├──> AudioPlayer (L4)
            │      ├──> AudioOutput (L5)
            │      └──> AVSyncController (L3)
            ├──> VideoPlayer (L4)
            │      ├──> Renderer (L5)
            │      └──> AVSyncController (L3)
            ├──> AudioResampler (L4)
            ├──> AVSyncController (L3)
            └──> PlayerStateManager (L3) [共享]
```

---

## 🔗 相关文档

- [整体架构设计](architecture_overview.md) - 理解各组件在系统中的位置
- [状态管理系统](state_management.md) - PlayerStateManager 的详细设计
- [音视频同步原理与实现](av_sync_design.md) - AVSyncController 的同步算法
- [线程模型详解](threading_model.md) - 5 个核心线程的详细说明
- [零拷贝渲染详解](zero_copy_rendering.md) - 硬件加速与零拷贝实现

---

**下一步阅读**: [状态管理系统](state_management.md) - 深入了解 PlayerStateManager 的状态机设计。
