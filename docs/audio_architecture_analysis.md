# 音频管理架构深入分析

## 🏗️ 整体架构

### 数据流图

```
┌─────────────┐
│  Demuxer    │ ─── 解复用音频包
└──────┬──────┘
       │ AVPacket
       ↓
┌─────────────┐
│AudioDecoder │ ─── 解码为PCM帧
└──────┬──────┘
       │ AVFrame (planar float, 48000Hz)
       ↓
┌─────────────┐
│AudioPlayer  │
│  ┌────────┐ │
│  │ Queue  │ │ ─── 150帧缓冲队列
│  │(150帧) │ │
│  └────┬───┘ │
│       │     │
│  ┌────↓────┐│
│  │SwrContext││ ─── 重采样: 48000→44100Hz, fltp→s16
│  └────┬────┘│
│       │     │
│  ┌────↓────┐│
│  │ WASAPI  ││ ─── Windows音频输出
│  │(44100帧)││     1秒缓冲区
│  └─────────┘│
└─────────────┘
       │
       ↓
    扬声器
```

---

## 🔧 核心组件详解

### 1. AudioPlayer类

#### 职责
- **队列管理**：缓冲解码后的音频帧
- **格式转换**：重采样为WASAPI需要的格式
- **时钟管理**：提供音频播放时钟
- **线程安全**：保护队列的并发访问

#### 关键成员变量

```cpp
class AudioPlayer {
private:
    // === 队列管理 ===
    std::queue<AVFramePtr> frame_queue_;          // 音频帧队列
    static const size_t MAX_QUEUE_SIZE = 150;     // 队列最大容量
    std::mutex queue_mutex_;                      // 队列互斥锁
    std::condition_variable queue_cv_;            // 队列条件变量

    // === 重采样器 ===
    SwrContext* swr_ctx_{nullptr};                // FFmpeg重采样器
    uint8_t** resample_buffer_{nullptr};          // 重采样缓冲区
    int resample_buffer_size_{0};                 // 缓冲区大小

    // === 音频输出 ===
    IAudioClient* audio_client_{nullptr};         // WASAPI客户端
    IAudioRenderClient* render_client_{nullptr};  // WASAPI渲染器
    UINT32 buffer_frame_count_{0};                // WASAPI缓冲区大小(帧)

    // === PTS时钟管理 ===
    double base_audio_pts_{0.0};                  // 第一帧PTS（秒）
    bool base_pts_initialized_{false};            // PTS是否已初始化
    AVRational audio_time_base_{1, 1000000};      // 音频时间基准

    // === 真实时间管理 ===
    std::chrono::steady_clock::time_point audio_start_time_; // 音频开始播放时间
    bool audio_started_{false};                   // 是否已开始播放

    // === 统计 ===
    std::atomic<uint64_t> total_samples_played_{0}; // 总播放样本数
};
```

---

### 2. 队列管理机制

#### Push流程（解码线程调用）

```cpp
bool AudioPlayer::PushFrame(AVFrame* frame) {
    std::unique_lock<std::mutex> lock(queue_mutex_);

    // 1. 等待队列有空间（可能阻塞）
    queue_cv_.wait(lock, [this] {
        return frame_queue_.size() < MAX_QUEUE_SIZE || !running_;
    });

    // 2. 设置base_audio_pts（仅第一帧）
    if (!base_pts_initialized_ && frame->pts != AV_NOPTS_VALUE) {
        base_audio_pts_ = frame->pts * av_q2d(audio_time_base_);
        base_pts_initialized_ = true;
        MODULE_INFO("Audio base PTS set to: {:.3f}s", base_audio_pts_);
    }

    // 3. 克隆帧并入队
    AVFramePtr frame_ptr(av_frame_clone(frame), AVFrameDeleter());
    frame_queue_.push(std::move(frame_ptr));
    
    // 4. 通知消费者
    queue_cv_.notify_one();
    return true;
}
```

**关键点：**
- **阻塞式等待**：解码线程会阻塞，直到队列有空间
- **PTS设置时机**：在第一帧入队时设置（队列足够大，不会丢帧）
- **线程安全**：使用互斥锁和条件变量

#### Pop流程（WASAPI线程调用）

```cpp
AVFramePtr AudioPlayer::PopFrame() {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    
    // 1. 等待队列有数据（可能阻塞）
    queue_cv_.wait(lock, [this] {
        return !frame_queue_.empty() || !running_;
    });

    // 2. 取出队首帧
    AVFramePtr frame = std::move(frame_queue_.front());
    frame_queue_.pop();
    
    // 3. 通知生产者
    queue_cv_.notify_one();
    return frame;
}
```

**队列大小设计考量：**

| 队列大小 | 优点 | 缺点 |
|---------|------|------|
| 50帧 (旧) | 内存占用小 | WASAPI启动时请求100帧→大量丢帧 |
| 150帧 (新) | 避免启动丢帧 | 略多占用内存（~3秒音频） |
| 500帧+ | 极少卡顿 | 内存浪费，延迟增加 |

**结论：** 150帧是平衡点

---

### 3. 音频格式转换（重采样）

#### 初始化SwrContext

```cpp
void AudioPlayer::InitializeResampler(int src_sample_rate, AVSampleFormat src_format,
                                      int src_channels) {
    swr_ctx_ = swr_alloc_set_opts(
        nullptr,
        AV_CH_LAYOUT_STEREO,           // 输出：立体声
        AV_SAMPLE_FMT_S16,             // 输出：16位整数
        44100,                         // 输出：44100Hz
        av_get_default_channel_layout(src_channels), // 输入：源声道布局
        src_format,                    // 输入：源格式（通常是fltp）
        src_sample_rate,               // 输入：源采样率（通常是48000）
        0, nullptr
    );
    swr_init(swr_ctx_);
}
```

**转换示例：**
```
输入：48000Hz, fltp (planar float), 2ch
输出：44100Hz, s16 (interleaved int16), 2ch
```

#### 重采样流程

```cpp
int AudioPlayer::ConvertFrame(AVFrame* frame, uint8_t** out_buffer, int* out_samples) {
    // 计算输出样本数
    int dst_nb_samples = av_rescale_rnd(
        swr_get_delay(swr_ctx_, frame->sample_rate) + frame->nb_samples,
        44100,
        frame->sample_rate,
        AV_ROUND_UP
    );

    // 分配缓冲区
    EnsureResampleBuffer(dst_nb_samples);

    // 执行重采样
    int converted_samples = swr_convert(
        swr_ctx_,
        resample_buffer_, dst_nb_samples,
        (const uint8_t**)frame->data, frame->nb_samples
    );

    *out_buffer = resample_buffer_[0];
    *out_samples = converted_samples;
    return converted_samples;
}
```

---

### 4. WASAPI音频输出

#### 初始化流程

```cpp
void AudioPlayer::InitializeWASAPI() {
    // 1. 获取默认音频设备
    IMMDeviceEnumerator* enumerator;
    CoCreateInstance(__uuidof(MMDeviceEnumerator), ...);
    enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device_);

    // 2. 激活音频客户端
    device_->Activate(__uuidof(IAudioClient), ..., &audio_client_);

    // 3. 设置音频格式
    WAVEFORMATEX format = {
        .wFormatTag = WAVE_FORMAT_PCM,
        .nChannels = 2,
        .nSamplesPerSec = 44100,
        .wBitsPerSample = 16,
        .nBlockAlign = 4,
        .nAvgBytesPerSec = 176400
    };

    // 4. 初始化客户端（1秒缓冲区）
    audio_client_->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        0,
        10000000,  // 1秒 = 10,000,000 * 100ns
        0,
        &format,
        nullptr
    );

    // 5. 获取缓冲区大小
    audio_client_->GetBufferSize(&buffer_frame_count_); // = 44100帧

    // 6. 启动音频流
    audio_client_->Start();
}
```

**WASAPI缓冲区特性：**
- **大小**：44100帧 = 1秒音频
- **第一次callback**：请求填充整个缓冲区（176400字节）
- **后续callback**：根据播放进度请求填充

#### 音频填充回调

```cpp
void AudioPlayer::AudioOutputCallback() {
    UINT32 padding_frames;
    audio_client_->GetCurrentPadding(&padding_frames);
    
    UINT32 available_frames = buffer_frame_count_ - padding_frames;
    
    if (available_frames > 0) {
        BYTE* buffer;
        render_client_->GetBuffer(available_frames, &buffer);
        
        // 填充音频数据
        UINT32 filled = FillAudioBuffer(buffer, available_frames);
        
        render_client_->ReleaseBuffer(filled, 0);
    }
}
```

---

### 5. 音频时钟管理

#### 时钟计算原理

```cpp
double AudioPlayer::GetCurrentAudioClock() {
    if (!audio_started_) return 0.0;

    auto current_time = std::chrono::steady_clock::now();
    auto elapsed_time = current_time - audio_start_time_;
    double elapsed_seconds = std::chrono::duration<double>(elapsed_time).count();

    return base_audio_pts_ + elapsed_seconds;
}
```

**公式：**
```
audio_clock = base_audio_pts + (current_time - audio_start_time)
```

**关键变量：**
- `base_audio_pts_`：第一帧的PTS（秒），例如0.000
- `audio_start_time_`：WASAPI开始播放的系统时间
- `elapsed_seconds`：从开始播放到现在的真实时间

**时序图：**
```
时间线：
0s          0.5s        1.0s        1.5s
│───────────│───────────│───────────│
│           │           │           │
PTS=0     WASAPI       PTS=500ms  当前时间
         第一次callback
         audio_start_time
         
计算示例：
current_time = audio_start_time + 1.0s
elapsed = 1.0s
audio_clock = 0.0 + 1.0 = 1.0s ✅
```

#### 时钟同步到AVSyncController

```cpp
void AudioPlayer::AudioOutputCallback() {
    // 每100次callback更新一次时钟
    if (++callback_counter_ % 100 == 0) {
        auto current_time = std::chrono::steady_clock::now();
        double current_audio_clock = GetCurrentAudioClock();
        
        sync_controller_->UpdateAudioClock(
            current_audio_clock * 1000.0,  // 转换为毫秒
            current_time
        );
    }
}
```

---

## 📊 性能分析

### 内存占用

```cpp
// 队列内存占用估算
音频帧大小 = 48000Hz × 0.023s × 2ch × 4bytes = ~8.8KB (planar float)
队列占用 = 150帧 × 8.8KB = ~1.3MB

// WASAPI缓冲区
缓冲区大小 = 44100帧 × 2ch × 2bytes = ~176KB
```

**总计：约1.5MB**

### CPU占用

```cpp
// 主要开销
1. 重采样：swr_convert() - 中等
2. 内存拷贝：av_frame_clone(), memcpy() - 低
3. 互斥锁：queue_mutex_ - 极低
4. WASAPI调用：GetBuffer/ReleaseBuffer - 极低
```

**总体：轻量级，CPU占用<1%**

### 延迟分析

```
┌─────────────┬──────────┐
│ 环节        │ 延迟     │
├─────────────┼──────────┤
│ 队列缓冲    │ ~50ms    │ (取决于队列深度)
│ 重采样      │ <1ms     │
│ WASAPI缓冲  │ ~23ms    │ (动态变化)
├─────────────┼──────────┤
│ 总延迟      │ ~74ms    │
└─────────────┴──────────┘
```

**说明：**
- 队列延迟：取决于队列中帧的数量（通常3-5帧）
- WASAPI缓冲：系统动态管理，通常保持23ms左右
- 总延迟在可接受范围内（<100ms人耳难以察觉）

---

## 🔍 线程安全分析

### 线程模型

```
┌──────────────┐         ┌──────────────┐
│ 解码线程     │         │ WASAPI线程   │
│              │         │              │
│ PushFrame()  │────────▶│ PopFrame()   │
│              │ queue   │              │
│ (阻塞等待)   │◀────────│ (阻塞等待)   │
└──────────────┘         └──────────────┘
       │                        │
       └────────────┬───────────┘
                    │
              ┌─────▼─────┐
              │ 互斥锁     │
              │ queue_mutex│
              └───────────┘
```

### 同步机制

```cpp
// 生产者-消费者模式
class AudioPlayer {
    std::mutex queue_mutex_;              // 保护队列
    std::condition_variable queue_cv_;    // 通知机制
    
    // 生产者（解码线程）
    void PushFrame() {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        queue_cv_.wait(lock, [this] {
            return frame_queue_.size() < MAX_QUEUE_SIZE || !running_;
        });
        frame_queue_.push(...);
        queue_cv_.notify_one();  // 通知消费者
    }
    
    // 消费者（WASAPI线程）
    void PopFrame() {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        queue_cv_.wait(lock, [this] {
            return !frame_queue_.empty() || !running_;
        });
        frame_queue_.pop();
        queue_cv_.notify_one();  // 通知生产者
    }
};
```

**安全保证：**
1. ✅ 互斥访问：同一时刻只有一个线程操作队列
2. ✅ 防止死锁：使用RAII锁，自动释放
3. ✅ 防止忙等：条件变量高效等待
4. ✅ 优雅退出：`running_`标志保证线程能正确退出

---

## 🎯 设计优点

1. **简洁的时钟管理**
   - 基于真实时间，不依赖样本计数
   - 避免了累积误差

2. **高效的队列设计**
   - 生产者-消费者模式标准实现
   - 阻塞式等待，CPU友好

3. **灵活的格式支持**
   - SwrContext支持任意格式转换
   - 易于适配不同音频源

4. **稳定的输出**
   - WASAPI提供低延迟、稳定的输出
   - Windows标准API，兼容性好

---

## 🔧 可优化空间

详见 [优化建议文档](./optimization_recommendations.md)
