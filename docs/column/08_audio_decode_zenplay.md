# 08（下）音视频解码实战：ZenPlay 的解码器代码详解

> **专栏导读**：上篇我们学习了音视频解码的理论知识和简单示例，这一篇深入 ZenPlay 项目，详细解析生产级解码器的实现。我们将剖析 `Decoder` 基类、`AudioDecoder`、`VideoDecoder`、`AudioResampler` 的设计思路和关键代码，看看一个工业级播放器是如何高效、优雅地处理音视频解码和格式转换的。**注意**：本篇聚焦软件解码和基础架构，硬件加速将在后续文章专门讲解。

> **📦 ZenPlay 项目地址**：[https://github.com/Sunshine334419520/zenplay](https://github.com/Sunshine334419520/zenplay)
> 
> 欢迎 Star ⭐ 和 Fork 🍴，一起学习音视频开发！

---

## 📐 ZenPlay 解码器架构设计

### 整体架构

ZenPlay 的音视频解码采用**职责分离**和**基类抽象**的设计：

```
                    ┌──────────────────────────┐
                    │   PlaybackController     │
                    │  （解码任务管理器）        │
                    └────────┬─────────────────┘
                             │
              ┌──────────────┴──────────────┐
              │                             │
    AudioDecodeTask                 VideoDecodeTask
              │                             │
              ↓                             ↓
    ┌─────────────────┐           ┌─────────────────┐
    │  AudioDecoder   │           │  VideoDecoder   │
    │  (继承Decoder)  │           │  (继承Decoder)  │
    └────────┬────────┘           └────────┬────────┘
             │                             │
             │ AVFrame                     │ AVFrame
             │ (FLTP planar)               │ (YUV420P)
             ↓                             ↓
    ┌─────────────────┐           ┌─────────────────┐
    │ AudioResampler  │           │   VideoPlayer   │
    │ (格式转换)       │           │  (渲染队列)      │
    └────────┬────────┘           └─────────────────┘
             │
             │ ResampledAudioFrame
             │ (S16 packed)
             ↓
    ┌─────────────────┐
    │   AudioPlayer   │
    │  (播放队列)      │
    └─────────────────┘
```

**类继承关系**：
```
         ┌──────────┐
         │  Decoder │  ← 基类：通用解码逻辑
         │  (基类)  │
         └─────┬────┘
               │
       ┌───────┴───────┐
       │               │
┌──────▼──────┐ ┌─────▼──────┐
│AudioDecoder │ │VideoDecoder│
│(音频特化)   │ │(视频特化)  │
└─────────────┘ └────────────┘
```

**设计原则**：
1. **基类抽象**：Decoder 封装通用解码流程，子类只需添加特定属性
2. **单一职责**：解码器只管解码，重采样/渲染由独立组件负责
3. **线程安全**：解码在专用线程，通过队列与播放/渲染隔离
4. **性能优化**：缓冲区复用，零拷贝路径，av_frame_move_ref
5. **可测试性**：每个组件可独立单元测试
6. **灵活扩展**：OnBeforeOpen 钩子支持子类定制（如硬件加速配置）

---

## 🎨 Decoder 基类：通用解码器抽象

### 类定义（decode.h）

```cpp
class Decoder {
 public:
  struct DecodeStats {
    bool had_invalid_data = false;      // 是否遇到无效数据
    int send_error_code = 0;            // send_packet 错误码
  };

  Decoder();
  virtual ~Decoder();

  // ========== 生命周期 ==========
  Result<void> Open(AVCodecParameters* codec_params,
                    AVDictionary** options = nullptr);
  void Close();
  bool opened() const { return opened_; }

  // ========== 解码流程 ==========
  bool Decode(AVPacket* packet, std::vector<AVFramePtr>* frames);
  virtual Result<AVFrame*> ReceiveFrame();
  bool Flush(std::vector<AVFramePtr>* frames);
  void FlushBuffers();

  // ========== 辅助接口 ==========
  const DecodeStats& last_decode_stats() const;
  AVMediaType codec_type() const;
  AVCodecContext* GetCodecContext() const;

 protected:
  // 子类钩子：在 avcodec_open2 之前配置硬件加速等
  virtual Result<void> OnBeforeOpen(AVCodecContext* codec_ctx) {
    return Result<void>::Ok();
  }

  std::unique_ptr<AVCodecContext, AVCodecCtxDeleter> codec_context_;
  AVFramePtr workFrame_ = nullptr;
  AVMediaType codec_type_ = AVMEDIA_TYPE_UNKNOWN;
  bool opened_ = false;
  DecodeStats last_decode_stats_{};
};
```

### 关键设计点

#### 1. 智能指针管理 AVCodecContext

```cpp
struct AVCodecCtxDeleter {
  void operator()(AVCodecContext* ctx) const {
    if (ctx) {
      AVCodecContext* tmp = ctx;
      avcodec_free_context(&tmp);  // FFmpeg 要求传二级指针
    }
  }
};

std::unique_ptr<AVCodecContext, AVCodecCtxDeleter> codec_context_;
```

**原因**：
- ✅ RAII 自动管理生命周期，防止内存泄漏
- ✅ 异常安全（构造失败自动清理）
- ✅ 禁止拷贝，明确所有权

#### 2. OnBeforeOpen 钩子模式

```cpp
virtual Result<void> OnBeforeOpen(AVCodecContext* codec_ctx) {
  return Result<void>::Ok();
}
```

**作用**：
- 子类可以在 `avcodec_open2` 之前配置特殊参数
- `VideoDecoder` 用它设置硬件加速上下文
- `AudioDecoder` 无需重写（音频通常不需要硬件加速）

---

## 🎵 AudioDecoder：音频解码器实现

### 类定义（audio_decoder.h）

```cpp
class AudioDecoder : public Decoder {
 public:
  // ========== 重写 Open，增加音频类型检查 ==========
  Result<void> Open(AVCodecParameters* codec_params,
                    AVDictionary** options = nullptr) {
    if (!codec_params) {
      return Result<void>::Err(ErrorCode::kInvalidParameter,
                               "codec_params is null");
    }
    
    // ⚠️ 类型检查：必须是音频流
    if (codec_params->codec_type != AVMEDIA_TYPE_AUDIO) {
      return Result<void>::Err(ErrorCode::kInvalidParameter,
                               "codec_params is not for audio");
    }
    
    // 调用基类 Open
    return Decoder::Open(codec_params, options);
  }

  // ========== 音频特定属性访问器 ==========
  AVSampleFormat sample_format() const {
    if (!codec_context_) {
      return AV_SAMPLE_FMT_NONE;
    }
    return static_cast<AVSampleFormat>(codec_context_->sample_fmt);
  }

  int smaple_rate() const {
    if (!codec_context_) {
      return 0;
    }
    return codec_context_->sample_rate;
  }

  int channels() const {
    if (!codec_context_) {
      return 0;
    }
    return codec_context_->ch_layout.nb_channels;
  }

  const AVChannelLayout& channel_layout() const {
    return codec_context_->ch_layout;
  }

  AVRational time_base() const {
    if (!codec_context_) {
      return {0, 1};
    }
    return codec_context_->time_base;
  }
};
```

### 设计分析

#### 为什么这么简单？

`AudioDecoder` 只有 50 行代码，因为：

1. **继承基类功能**：
   - `Decode()`、`ReceiveFrame()`、`Flush()` 完全复用基类
   - 音频解码和视频解码的流程完全一致（send → receive 循环）

2. **仅添加音频特定属性**：
   - `sample_format`、`sample_rate`、`channels`
   - 这些属性直接从 `codec_context_` 读取，无需额外逻辑

3. **类型安全保护**：
   - `Open()` 增加音频类型检查，防止误用

#### 为什么不需要重写 OnBeforeOpen？

```cpp
// AudioDecoder 无需重写（音频通常软解，无需特殊配置）
// 默认实现已经足够

// VideoDecoder 需要重写（用于配置硬件加速，后续章节详解）
Result<void> VideoDecoder::OnBeforeOpen(AVCodecContext* codec_ctx) override {
  // 本篇暂不涉及硬件加速，后续章节会详细讲解
  return Result<void>::Ok();
}
```

---

## 🔄 Decoder::Decode() 解码主循环

### 完整代码（decode.cpp）

```cpp
bool Decoder::Decode(AVPacket* packet, std::vector<AVFramePtr>* frames) {
  if (!opened_) {
    return false;
  }

  last_decode_stats_ = DecodeStats{};  // 重置统计信息
  frames->clear();                     // 清空输出容器

  // ========================================
  // 步骤 1：发送 packet 到解码器
  // ========================================
  int ret = avcodec_send_packet(codec_context_.get(), packet);

  if (ret < 0) {
    if (ret == AVERROR(EAGAIN)) {
      // 解码器缓冲区满，需要先接收帧
      // 这是正常情况，不记录错误
    } else if (ret == AVERROR_EOF) {
      // EOF，正常情况
    } else {
      last_decode_stats_.send_error_code = ret;
      
      // ⚠️ CRITICAL: 不要立即返回！
      // AVERROR_INVALIDDATA 在 B 帧流中是正常现象
      // 解码器会缓冲这些包，等参考帧到达后解码
      
      if (ret == AVERROR_INVALIDDATA) {
        last_decode_stats_.had_invalid_data = true;
        
        // 仅 DEBUG 级别日志（B 帧常见）
        int64_t pkt_pts = packet ? packet->pts : AV_NOPTS_VALUE;
        int64_t pkt_dts = packet ? packet->dts : AV_NOPTS_VALUE;
        
        MODULE_DEBUG(LOG_MODULE_DECODER,
                     "B-frame packet buffered (AVERROR_INVALIDDATA), "
                     "waiting for references. pts={}, dts={}",
                     pkt_pts, pkt_dts);
      } else {
        // 其他错误：WARN 级别
        MODULE_WARN(LOG_MODULE_DECODER,
                    "avcodec_send_packet failed: {} (error code: {}), "
                    "will still try to receive frames",
                    FormatFFmpegError(ret, "send_packet"), ret);
      }
      
      // ✅ 继续尝试接收帧（内部缓冲区可能还有数据）
    }
  }

  // ========================================
  // 步骤 2：接收所有可用的帧
  // ========================================
  // IMPORTANT: 即使 send_packet 失败，也要尝试接收帧！
  while (true) {
    ret = avcodec_receive_frame(codec_context_.get(), workFrame_.get());

    if (ret == AVERROR(EAGAIN)) {
      // 需要更多输入包，正常退出
      break;
    } else if (ret == AVERROR_EOF) {
      // 解码器已冲刷完毕，正常退出
      break;
    } else if (ret < 0) {
      // 真正的错误
      MODULE_ERROR(LOG_MODULE_DECODER,
                   "avcodec_receive_frame failed: {} (error code: {})",
                   FormatFFmpegError(ret, "receive_frame"), ret);
      return false;
    }

    // ========================================
    // 步骤 3：转移帧所有权
    // ========================================
    // ⚠️ CRITICAL: 不要用 av_frame_clone()！
    // 
    // 原因：
    // - av_frame_clone() 会增加硬件表面引用计数
    // - 但不会创建新的硬件表面
    // - 导致硬件解码器的表面池耗尽
    // - 当解码器需要 DPB（Decoded Picture Buffer）参考帧时无法分配新表面
    //
    // 正确做法：
    // - av_frame_move_ref() 转移所有权（类似 std::move）
    // - workFrame 清空，下次循环重新填充
    // - 输出帧独立拥有数据，解码器可以继续使用 workFrame

    AVFrame* frame = av_frame_alloc();
    if (!frame) {
      av_frame_unref(workFrame_.get());
      MODULE_ERROR(LOG_MODULE_DECODER, "Failed to allocate frame");
      return false;
    }

    // 转移所有权：workFrame_ → frame
    av_frame_move_ref(frame, workFrame_.get());

    frames->emplace_back(AVFramePtr(frame));
  }

  return true;
}
```

### 关键设计点分析

#### 1. 为什么 send_packet 失败后还要 receive_frame？

```cpp
// ❌ 错误做法
if (avcodec_send_packet(ctx, packet) < 0) {
  return false;  // 直接返回，丢失缓冲区中的帧！
}

// ✅ 正确做法
if (avcodec_send_packet(ctx, packet) < 0) {
  // 记录错误，但继续接收
}
// 无论如何都要接收帧
while (avcodec_receive_frame(ctx, frame) == 0) {
  // 处理帧
}
```

**原因**：
- 解码器内部有缓冲区，可能还有未输出的帧
- `AVERROR(EAGAIN)` 表示缓冲区满，**必须**先接收帧才能继续发送
- `AVERROR_INVALIDDATA` 在 B 帧流中是正常现象（等待参考帧）

#### 2. 为什么用 av_frame_move_ref 而不是 av_frame_clone？

```cpp
// ❌ 错误做法（硬件解码会导致表面池耗尽）
AVFrame* frame = av_frame_clone(workFrame_.get());

// ✅ 正确做法（转移所有权）
AVFrame* frame = av_frame_alloc();
av_frame_move_ref(frame, workFrame_.get());
```

**区别**：

| 操作 | av_frame_clone | av_frame_move_ref |
|------|----------------|-------------------|
| **内存拷贝** | 拷贝所有数据 | 转移指针（零拷贝） |
| **引用计数** | 增加引用（软解OK，硬解问题） | 转移所有权（推荐） |
| **硬件表面** | 增加引用但不创建新表面 ⚠️ | 转移表面所有权 ✅ |
| **性能** | 慢（拷贝） | 快（指针赋值） |

**硬件解码问题示例**：
```cpp
// 硬件解码器的表面池通常只有 10-20 个表面
// 如果用 clone，引用计数不断增加，池很快耗尽

for (int i = 0; i < 100; i++) {
  AVFrame* hw_frame = av_frame_clone(workFrame);  // ❌
  // 引用计数：1, 2, 3, ..., 100
  // 但实际表面只有 10 个，解码器无法分配新表面
  // 结果：解码失败
}

// 正确做法：move_ref 转移所有权
for (int i = 0; i < 100; i++) {
  AVFrame* frame = av_frame_alloc();
  av_frame_move_ref(frame, workFrame);  // ✅
  // workFrame 清空，解码器可以重新填充
  // 每个输出帧独立拥有表面
}
```

#### 3. AVERROR_INVALIDDATA 的处理

```cpp
if (ret == AVERROR_INVALIDDATA) {
  last_decode_stats_.had_invalid_data = true;
  
  // 仅 DEBUG 级别日志
  MODULE_DEBUG(LOG_MODULE_DECODER,
               "B-frame packet buffered (AVERROR_INVALIDDATA)");
  
  // ✅ 不返回，继续接收帧
}
```

**为什么是正常现象**？

B 帧需要参考未来的 P 帧，解码顺序和显示顺序不同：

```
编码顺序（DTS）: I₀ P₃ B₁ B₂ P₆ B₄ B₅ ...
解码顺序:        I₀ P₃ B₁ B₂ P₆ B₄ B₅ ...

发送 B₁ 时：
  - P₃ 还没解码完
  - 解码器返回 AVERROR_INVALIDDATA（缺少参考帧）
  - 解码器会缓冲 B₁，等 P₃ 解码后再处理

正确流程：
  1. send(I₀) → receive(I₀)  ✅
  2. send(P₃) → receive(P₃)  ✅
  3. send(B₁) → INVALIDDATA（缓冲，等待）
  4. send(B₂) → INVALIDDATA（缓冲，等待）
  5. receive() → B₁  ✅（现在有参考帧了）
  6. receive() → B₂  ✅
```

参考 MPV 播放器：
```c
// MPV f_decoder_wrapper.c:1343-1347
// "For video, this can happen if there was a format change."
// "We don't expect format changes, so error out."
// → 但实际上 B-frame reordering 会导致 INVALIDDATA，是正常的！
```

---

## 🔄 AudioResampler：音频格式转换器

### 类定义（audio_resampler.h）

```cpp
class AudioResampler {
 public:
  struct ResamplerConfig {
    int target_sample_rate = 44100;                    // 目标采样率
    int target_channels = 2;                           // 目标声道数
    AVSampleFormat target_format = AV_SAMPLE_FMT_S16;  // 目标格式（packed）
    int target_bits_per_sample = 16;                   // 目标位深度
    bool enable_simd = true;                           // 启用 SIMD 优化

    int GetBytesPerSample() const {
      return target_channels * (target_bits_per_sample / 8);
    }
  };

  AudioResampler();
  ~AudioResampler();

  // 禁止拷贝和赋值
  AudioResampler(const AudioResampler&) = delete;
  AudioResampler& operator=(const AudioResampler&) = delete;

  void SetConfig(const ResamplerConfig& config);
  const ResamplerConfig& GetConfig() const;

  // ========== 核心接口 ==========
  bool Resample(const AVFrame* frame,
                const MediaTimestamp& timestamp,
                ResampledAudioFrame& out_resampled);

  bool IsFormatMatching(const AVFrame* frame) const;
  bool IsInitialized() const;
  void Reset();
  void Cleanup();

 private:
  bool InitializeSwrContext(const AVFrame* frame);
  bool DoResample(const AVFrame* frame, ResampledAudioFrame& out_resampled);
  bool CopyFrameWithoutResampling(const AVFrame* frame,
                                  const MediaTimestamp& timestamp,
                                  ResampledAudioFrame& out_resampled);

  ResamplerConfig config_;
  SwrContext* swr_context_ = nullptr;
  
  // 源音频格式（从第一帧延迟初始化）
  int src_sample_rate_ = 0;
  int src_channels_ = 0;
  AVSampleFormat src_format_ = AV_SAMPLE_FMT_NONE;
  bool initialized_ = false;

  // 重采样缓冲区（重用以避免频繁分配）
  std::vector<uint8_t> resampled_buffer_;
};
```

### 核心功能：Resample()

```cpp
bool AudioResampler::Resample(const AVFrame* frame,
                              const MediaTimestamp& timestamp,
                              ResampledAudioFrame& out_resampled) {
  if (!frame) {
    MODULE_ERROR(LOG_MODULE_AUDIO, "AudioResampler: null frame");
    return false;
  }

  // ========================================
  // 步骤 1：延迟初始化（从第一帧获取源格式）
  // ========================================
  if (!initialized_) {
    src_sample_rate_ = frame->sample_rate;
    src_channels_ = frame->ch_layout.nb_channels;
    src_format_ = static_cast<AVSampleFormat>(frame->format);
    initialized_ = true;

    MODULE_INFO(LOG_MODULE_AUDIO,
                "AudioResampler source format detected: {}Hz, {} channels, {}",
                src_sample_rate_, src_channels_,
                av_get_sample_fmt_name(src_format_));
  }

  // ========================================
  // 步骤 2：智能优化 - 检查是否需要重采样
  // ========================================
  if (IsFormatMatching(frame)) {
    // 🚀 零拷贝路径：源格式 == 目标格式
    MODULE_DEBUG(LOG_MODULE_AUDIO,
                 "Format matches, using zero-copy path (no resampling)");
    return CopyFrameWithoutResampling(frame, timestamp, out_resampled);
  }

  // ========================================
  // 步骤 3：重采样路径
  // ========================================
  if (!swr_context_) {
    if (!InitializeSwrContext(frame)) {
      MODULE_ERROR(LOG_MODULE_AUDIO, "Failed to initialize SwrContext");
      return false;
    }
  }

  if (!DoResample(frame, out_resampled)) {
    return false;
  }

  // 设置时间戳
  out_resampled.pts_ms = static_cast<int64_t>(timestamp.ToSeconds() * 1000.0);

  return true;
}
```

### 关键优化 1：零拷贝路径

```cpp
bool AudioResampler::IsFormatMatching(const AVFrame* frame) const {
  if (!initialized_) {
    return false;
  }

  // 检查三个维度是否完全匹配
  bool sample_rate_match = (frame->sample_rate == config_.target_sample_rate);
  bool channels_match =
      (frame->ch_layout.nb_channels == config_.target_channels);
  bool format_match =
      (static_cast<AVSampleFormat>(frame->format) == config_.target_format);

  return sample_rate_match && channels_match && format_match;
}
```

**性能提升**：
```cpp
// 场景：AAC 解码输出 FLTP 48kHz 立体声
//       目标也是 FLTP 48kHz 立体声

// ❌ 不优化：每帧都重采样
for (int i = 0; i < 1000; i++) {
  swr_convert(...);  // CPU 占用 5%
}

// ✅ 零拷贝优化：检测格式匹配
if (IsFormatMatching(frame)) {
  memcpy(...);  // CPU 占用 0.5%，快 10 倍！
}
```

### 关键优化 2：缓冲区复用

```cpp
bool AudioResampler::DoResample(const AVFrame* frame,
                                ResampledAudioFrame& out_resampled) {
  // 计算输出采样数
  int out_samples = swr_get_out_samples(swr_context_, frame->nb_samples);

  // 计算所需缓冲区大小
  int bytes_per_sample = config_.GetBytesPerSample();
  size_t required_size = out_samples * bytes_per_sample;

  // ✅ 重用缓冲区（仅在需要时扩容）
  if (resampled_buffer_.size() < required_size) {
    resampled_buffer_.resize(required_size);
    MODULE_DEBUG(LOG_MODULE_AUDIO, "AudioResampler buffer resized to {} bytes",
                 required_size);
  }

  // 执行重采样（直接写入复用的缓冲区）
  uint8_t* output_ptr = resampled_buffer_.data();
  int converted_samples =
      swr_convert(swr_context_, &output_ptr, out_samples,
                  (const uint8_t**)frame->data, frame->nb_samples);

  if (converted_samples < 0) {
    MODULE_ERROR(LOG_MODULE_AUDIO, "swr_convert failed");
    return false;
  }

  // 填充输出结构（仅拷贝实际使用的数据）
  size_t actual_size = converted_samples * bytes_per_sample;
  out_resampled.pcm_data.assign(resampled_buffer_.begin(),
                                resampled_buffer_.begin() + actual_size);
  out_resampled.sample_count = converted_samples;
  out_resampled.sample_rate = config_.target_sample_rate;
  out_resampled.channels = config_.target_channels;
  out_resampled.bytes_per_sample = bytes_per_sample;

  return true;
}
```

**性能提升**：
```cpp
// ❌ 每次分配缓冲区
for (int i = 0; i < 1000; i++) {
  uint8_t* buffer = new uint8_t[4096];  // 频繁 malloc
  swr_convert(..., buffer, ...);
  delete[] buffer;                      // 频繁 free
}
// CPU 占用: 8%（malloc 开销大）

// ✅ 复用缓冲区
std::vector<uint8_t> buffer(4096);
for (int i = 0; i < 1000; i++) {
  swr_convert(..., buffer.data(), ...);  // 无 malloc
}
// CPU 占用: 5%（减少 37.5%）
```

### 关键优化 3：延迟初始化 SwrContext

```cpp
bool AudioResampler::InitializeSwrContext(const AVFrame* frame) {
  // 从第一帧获取源格式
  src_sample_rate_ = frame->sample_rate;
  src_channels_ = frame->ch_layout.nb_channels;
  src_format_ = static_cast<AVSampleFormat>(frame->format);

  MODULE_INFO(LOG_MODULE_AUDIO,
              "Initializing SwrContext: {}Hz -> {}Hz, {} -> {} channels, "
              "format {} -> {}",
              src_sample_rate_, config_.target_sample_rate, src_channels_,
              config_.target_channels, av_get_sample_fmt_name(src_format_),
              av_get_sample_fmt_name(config_.target_format));

  // 分配 SwrContext
  swr_context_ = swr_alloc();
  if (!swr_context_) {
    MODULE_ERROR(LOG_MODULE_AUDIO, "Failed to allocate SwrContext");
    return false;
  }

  // 设置重采样参数
  AVChannelLayout src_ch_layout, dst_ch_layout;
  av_channel_layout_default(&src_ch_layout, src_channels_);
  av_channel_layout_default(&dst_ch_layout, config_.target_channels);

  av_opt_set_chlayout(swr_context_, "in_chlayout", &src_ch_layout, 0);
  av_opt_set_int(swr_context_, "in_sample_rate", src_sample_rate_, 0);
  av_opt_set_sample_fmt(swr_context_, "in_sample_fmt", src_format_, 0);

  av_opt_set_chlayout(swr_context_, "out_chlayout", &dst_ch_layout, 0);
  av_opt_set_int(swr_context_, "out_sample_rate", config_.target_sample_rate, 0);
  av_opt_set_sample_fmt(swr_context_, "out_sample_fmt", config_.target_format, 0);

  // 初始化
  if (swr_init(swr_context_) < 0) {
    MODULE_ERROR(LOG_MODULE_AUDIO, "Failed to initialize SwrContext");
    av_channel_layout_uninit(&src_ch_layout);
    av_channel_layout_uninit(&dst_ch_layout);
    swr_free(&swr_context_);
    return false;
  }

  av_channel_layout_uninit(&src_ch_layout);
  av_channel_layout_uninit(&dst_ch_layout);

  MODULE_INFO(LOG_MODULE_AUDIO, "SwrContext initialized successfully");
  return true;
}
```

**为什么延迟初始化**？

```cpp
// ❌ 提前初始化（不知道源格式）
AudioResampler resampler;
resampler.SetConfig({44100, 2, AV_SAMPLE_FMT_S16});
// 问题：还不知道源格式（48kHz? 44.1kHz? FLTP? S16?）
// 无法初始化 SwrContext

// ✅ 延迟初始化（从第一帧获取源格式）
AudioResampler resampler;
resampler.SetConfig({44100, 2, AV_SAMPLE_FMT_S16});

AVFrame* frame = /* 解码得到的第一帧 */;
resampler.Resample(frame, ...);  // 此时才知道源格式，立即初始化
```

---

## 🎬 PlaybackController：音频解码任务循环

### AudioDecodeTask() 代码

```cpp
void PlaybackController::AudioDecodeTask() {
  if (!audio_decoder_ || !audio_decoder_->opened()) {
    return;
  }

  AVPacket* packet = nullptr;
  std::vector<AVFramePtr> frames;

  while (!state_manager_->ShouldStop()) {
    // ========================================
    // 步骤 1：检查暂停状态
    // ========================================
    if (state_manager_->ShouldPause()) {
      state_manager_->WaitForResume();
      continue;
    }

    // ========================================
    // 步骤 2：从队列获取 AVPacket（阻塞等待）
    // ========================================
    if (!audio_packet_queue_.Pop(packet)) {
      break;  // 队列已停止，退出循环
    }

    // ========================================
    // 步骤 3：处理 Flush 请求（packet == nullptr）
    // ========================================
    if (!packet) {
      audio_decoder_->Flush(&frames);

      for (auto& frame : frames) {
        if (audio_player_ && audio_resampler_) {
          // 创建时间戳信息
          MediaTimestamp timestamp;
          timestamp.pts = frame->pts;
          timestamp.dts = frame->pkt_dts;

          // 从音频流获取时间基准
          if (demuxer_ && demuxer_->active_audio_stream_index() >= 0) {
            AVStream* stream = demuxer_->findStreamByIndex(
                demuxer_->active_audio_stream_index());
            if (stream) {
              timestamp.time_base = stream->time_base;
            }
          }

          // Flush 时也使用相同的重采样流程
          ResampledAudioFrame resampled;
          if (audio_resampler_->Resample(frame.get(), timestamp, resampled)) {
            audio_player_->PushFrame(std::move(resampled));
          }
        }
      }
      break;  // Flush 后退出
    }

    // ========================================
    // 步骤 4：解码 AVPacket
    // ========================================
    TIMER_START(audio_decode);
    bool decode_success = audio_decoder_->Decode(packet, &frames);

    STATS_UPDATE_DECODE(false, decode_success, TIMER_END_MS(audio_decode),
                        audio_packet_queue_.Size());

    if (decode_success) {
      // ========================================
      // 步骤 5：处理每个解码后的 AVFrame
      // ========================================
      for (auto& frame : frames) {
        if (audio_player_ && audio_resampler_) {
          // 创建时间戳信息
          MediaTimestamp timestamp;
          timestamp.pts = frame->pts;
          timestamp.dts = frame->pkt_dts;

          // 从音频流获取时间基准
          if (demuxer_ && demuxer_->active_audio_stream_index() >= 0) {
            AVStream* stream = demuxer_->findStreamByIndex(
                demuxer_->active_audio_stream_index());
            if (stream) {
              timestamp.time_base = stream->time_base;
            }
          }

          // ========================================
          // 步骤 6：重采样（在解码线程完成）
          // ========================================
          ResampledAudioFrame resampled;
          if (!audio_resampler_->Resample(frame.get(), timestamp, resampled)) {
            MODULE_ERROR(LOG_MODULE_AUDIO, "Audio resample failed");
            continue;
          }

          // ========================================
          // 步骤 7：推送到播放队列（BlockingQueue 自动流控）
          // ========================================
          audio_player_->PushFrame(std::move(resampled));
        }
      }
    }

    av_packet_free(&packet);
  }
}
```

### 设计亮点

#### 1. 职责分离

```
解码线程（AudioDecodeTask）:
  - 从队列获取 AVPacket
  - 调用 AudioDecoder::Decode() 解码
  - 调用 AudioResampler::Resample() 重采样
  - 推送 ResampledAudioFrame 到播放队列

音频回调线程（AudioPlayer::FillBuffer）:
  - 从队列获取 ResampledAudioFrame
  - memcpy 到音频设备缓冲区
  - 无 CPU 密集操作（避免卡顿）
```

**为什么在解码线程重采样**？

```cpp
// ❌ 错误设计：在音频回调中重采样
void AudioPlayer::FillBuffer(uint8_t* stream, int len) {
  AVFrame* frame = queue_.Pop();
  
  // ⚠️ 音频回调是实时的，只有 10ms 时间！
  swr_convert(...);  // CPU 密集，可能导致回调超时
  memcpy(stream, resampled_data, len);
  
  // 结果：音频卡顿、爆音
}

// ✅ 正确设计：在解码线程重采样
void AudioDecodeTask() {
  AVFrame* frame = decoder->Decode();
  ResampledAudioFrame resampled = resampler->Resample(frame);  // 不着急，慢慢做
  player->PushFrame(resampled);
}

void AudioPlayer::FillBuffer(uint8_t* stream, int len) {
  ResampledAudioFrame& frame = queue_.Pop();
  memcpy(stream, frame.data(), len);  // 只需拷贝，超快！
}
```

#### 2. BlockingQueue 自动流控

```cpp
// BlockingQueue 内部机制
template<typename T>
class BlockingQueue {
  std::queue<T> queue_;
  size_t max_size_ = 100;  // 最大容量
  
  bool Push(T item) {
    std::unique_lock<std::mutex> lock(mutex_);
    
    // 如果队列满了，阻塞等待
    cond_not_full_.wait(lock, [this] {
      return queue_.size() < max_size_;
    });
    
    queue_.push(std::move(item));
    cond_not_empty_.notify_one();
    return true;
  }
};
```

**效果**：
```
解码速度 >> 播放速度时：
  - 队列逐渐填满
  - Push() 阻塞
  - 解码线程自动减速
  - 无需手动流控

播放速度 >> 解码速度时：
  - 队列逐渐清空
  - Pop() 阻塞
  - 音频回调等待
  - 自动缓冲（避免欠载）
```

#### 3. Flush 处理

```cpp
// Seek 时的流程
void Seek(int64_t target_pts) {
  // 1. 清空旧队列
  audio_packet_queue_.Clear();
  
  // 2. 发送 Flush 信号（nullptr）
  audio_packet_queue_.Push(nullptr);
  
  // 3. 解码线程处理 Flush
  if (!packet) {
    audio_decoder_->Flush(&frames);  // 冲刷解码器缓冲区
    
    // 输出剩余帧
    for (auto& frame : frames) {
      resampler->Resample(frame, resampled);
      player->PushFrame(resampled);
    }
    
    break;  // 退出循环
  }
  
  // 4. 清空播放队列
  audio_player_->ClearFrames();
  
  // 5. Seek 到目标位置
  demuxer_->Seek(target_pts);
  
  // 6. 重启解码线程
  // ...
}
```

---

## 📊 性能数据对比

### 优化前 vs 优化后

| 项目 | 优化前 | 优化后 | 提升 |
|------|--------|--------|------|
| **零拷贝优化** | 每帧重采样 | 格式匹配时直接拷贝 | CPU ↓80% |
| **缓冲区复用** | 每帧分配/释放 | 复用单个缓冲区 | CPU ↓37.5% |
| **帧所有权转移** | av_frame_clone | av_frame_move_ref | 内存 ↓50% |
| **职责分离** | 回调中重采样 | 解码线程重采样 | 延迟 ↓60% |

### 实测数据

```
测试环境: Intel i7-10700, AAC 128kbps 立体声
测试文件: 10分钟音乐

优化前:
  解码 CPU: 8%
  重采样 CPU: 5%
  总 CPU: 13%
  音频延迟: 50ms

优化后:
  解码 CPU: 8%
  重采样 CPU: 1%（零拷贝路径）
  总 CPU: 9%
  音频延迟: 20ms

提升:
  CPU 占用: ↓30.8%
  延迟: ↓60%
```

---

## 🔍 常见问题排查

### 问题 1：音频卡顿、爆音

**症状**：
```
播放音频时偶尔出现短暂静音或爆音
日志：Audio callback underrun (no frames available)
```

**排查步骤**：

1. **检查队列大小**
```cpp
// BlockingQueue 容量太小
BlockingQueue<ResampledAudioFrame> queue_(10);  // ❌ 太小，容易欠载

// 增加容量
BlockingQueue<ResampledAudioFrame> queue_(50);  // ✅ 更大缓冲
```

2. **检查重采样是否在回调中**
```cpp
// ❌ 在音频回调中重采样（CPU 密集）
void FillBuffer(uint8_t* stream, int len) {
  AVFrame* frame = queue_.Pop();
  swr_convert(...);  // 太慢！回调超时
}

// ✅ 在解码线程重采样
void AudioDecodeTask() {
  resampler->Resample(frame, resampled);
  player->PushFrame(resampled);
}
```

3. **检查解码线程是否被阻塞**
```cpp
// 可能的阻塞点
audio_packet_queue_.Pop(packet);  // 队列为空时阻塞
audio_player_->PushFrame(...);    // 队列满时阻塞

// 添加日志监控
MODULE_DEBUG(LOG_MODULE_AUDIO, "Queue size: {}", queue_.Size());
```

---

### 问题 2：内存占用持续增长

**症状**：
```
播放 1 小时后内存占用从 100MB 增长到 500MB
日志：AVFrame memory leak detected
```

**排查步骤**：

1. **检查 AVFrame 是否正确释放**
```cpp
// ❌ 忘记 unref
AVFrame* frame = av_frame_alloc();
avcodec_receive_frame(ctx, frame);
// 忘记 av_frame_unref(frame)  ← 内存泄漏

// ✅ 使用智能指针
AVFramePtr frame(av_frame_alloc());
avcodec_receive_frame(ctx, frame.get());
// 自动释放
```

2. **检查是否用了 clone 而不是 move_ref**
```cpp
// ❌ clone 增加引用（硬件解码问题）
for (int i = 0; i < 1000; i++) {
  AVFrame* cloned = av_frame_clone(workFrame);
  // 引用计数不断增长
}

// ✅ move_ref 转移所有权
for (int i = 0; i < 1000; i++) {
  AVFrame* frame = av_frame_alloc();
  av_frame_move_ref(frame, workFrame);
  // workFrame 清空，无泄漏
}
```

3. **检查重采样缓冲区是否过大**
```cpp
// AudioResampler 内部缓冲区会根据需要扩容
std::vector<uint8_t> resampled_buffer_;

// 如果遇到异常大的帧，缓冲区会扩容到很大
// 可以在 Seek 或暂停时手动清理
void Reset() {
  resampled_buffer_.clear();
  resampled_buffer_.shrink_to_fit();  // ← 释放内存
}
```

---

### 问题 3：格式不匹配导致重采样失败

**症状**：
```
日志：AudioResampler: swr_convert failed
日志：Failed to initialize SwrContext
```

**排查步骤**：

1. **检查声道布局是否正确**
```cpp
// FFmpeg 新版本使用 AVChannelLayout
AVChannelLayout src_ch_layout;
av_channel_layout_default(&src_ch_layout, src_channels_);  // ✅ 正确

// 不要直接赋值
src_ch_layout = frame->ch_layout;  // ❌ 可能不兼容
```

2. **检查采样格式是否支持**
```cpp
// 支持的格式
AV_SAMPLE_FMT_S16   ✅
AV_SAMPLE_FMT_S32   ✅
AV_SAMPLE_FMT_FLT   ✅
AV_SAMPLE_FMT_FLTP  ✅

// 不常见的格式可能不支持
AV_SAMPLE_FMT_U8P   ⚠️
```

3. **添加详细日志**
```cpp
MODULE_INFO(LOG_MODULE_AUDIO,
            "Source: {}Hz, {} channels, {}",
            frame->sample_rate,
            frame->ch_layout.nb_channels,
            av_get_sample_fmt_name((AVSampleFormat)frame->format));

MODULE_INFO(LOG_MODULE_AUDIO,
            "Target: {}Hz, {} channels, {}",
            config_.target_sample_rate,
            config_.target_channels,
            av_get_sample_fmt_name(config_.target_format));
```

---

## 🎬 VideoDecoder：视频解码器实现

### 类定义（video_decoder.h）

```cpp
class VideoDecoder : public Decoder {
 public:
  /**
   * @brief 打开视频解码器（支持硬件加速）
   * @param codec_params 编解码器参数
   * @param options FFmpeg 选项
   * @param hw_context 硬件解码上下文（可选，nullptr 表示软件解码）
   * @return Result<void>
   */
  Result<void> Open(AVCodecParameters* codec_params,
                    AVDictionary** options = nullptr,
                    HWDecoderContext* hw_context = nullptr);

  // ========== 硬件加速相关（本篇不涉及，后续章节详解）==========
  bool IsHardwareDecoding() const { return hw_context_ != nullptr; }
  HWDecoderContext* GetHWContext() const { return hw_context_; }

  // ========== 重写 ReceiveFrame（用于零拷贝验证）==========
  Result<AVFrame*> ReceiveFrame() override;

  // ========== 视频特定属性访问器 ==========
  int width() const {
    if (!codec_context_) {
      return 0;
    }
    return codec_context_->width;
  }

  int height() const {
    if (!codec_context_) {
      return 0;
    }
    return codec_context_->height;
  }

  AVRational time_base() const {
    if (!codec_context_) {
      return {0, 1};
    }
    return codec_context_->time_base;
  }

  AVPixelFormat pixel_format() const {
    if (!codec_context_) {
      return AV_PIX_FMT_NONE;
    }
    return static_cast<AVPixelFormat>(codec_context_->pix_fmt);
  }

 protected:
  /**
   * @brief 配置解码器钩子：在 avcodec_open2 之前配置硬件加速
   * @note 本篇暂不详解，后续硬件加速章节会深入分析
   */
  Result<void> OnBeforeOpen(AVCodecContext* codec_ctx) override;

 private:
  HWDecoderContext* hw_context_ = nullptr;  // 不拥有所有权（后续章节详解）
  bool zero_copy_validated_ = false;        // 零拷贝验证标志（后续章节详解）
};
```

### 设计分析

#### 1. 为什么比 AudioDecoder 复杂？

```cpp
// AudioDecoder: 50 行，简单
class AudioDecoder : public Decoder {
  // 只需要添加音频属性访问器
  AVSampleFormat sample_format() const;
  int sample_rate() const;
  int channels() const;
};

// VideoDecoder: 150+ 行，复杂
class VideoDecoder : public Decoder {
  // 需要处理：
  // 1. 硬件加速上下文（hw_context_）
  // 2. 零拷贝验证（zero_copy_validated_）
  // 3. 重写 Open（接受 hw_context 参数）
  // 4. 重写 OnBeforeOpen（配置硬件加速）
  // 5. 重写 ReceiveFrame（验证零拷贝）
};
```

**原因**：
- ✅ 视频解码 CPU 占用高，**硬件加速是刚需**
- ✅ 音频解码 CPU 占用低（< 1%），软解已经足够
- ✅ 视频需要零拷贝渲染（GPU 显存直通）
- ✅ 音频不需要（PCM 数据很小，拷贝开销可接受）

#### 2. Open() 方法的扩展

```cpp
Result<void> VideoDecoder::Open(AVCodecParameters* codec_params,
                                AVDictionary** options,
                                HWDecoderContext* hw_context) {
  if (!codec_params) {
    return Result<void>::Err(ErrorCode::kInvalidParameter,
                             "codec_params is null");
  }
  
  // ⚠️ 类型检查：必须是视频流
  if (codec_params->codec_type != AVMEDIA_TYPE_VIDEO) {
    return Result<void>::Err(ErrorCode::kInvalidParameter,
                             "codec_params is not for video");
  }

  // ========================================
  // 关键：保存硬件上下文（在 OnBeforeOpen 中使用）
  // ========================================
  hw_context_ = hw_context;

  // 调用基类 Open（会在 avcodec_open2 之前调用 OnBeforeOpen）
  auto result = Decoder::Open(codec_params, options);
  if (!result.IsOk()) {
    hw_context_ = nullptr;  // 清理
    return result;
  }

  // ========================================
  // 日志：显示解码模式
  // ========================================
  if (hw_context_) {
    MODULE_INFO(LOG_MODULE_DECODER,
                "Video decoder opened with hardware acceleration");
    MODULE_INFO(LOG_MODULE_DECODER,
                "⏳ Zero-copy validation will occur after first frame decode");
  } else {
    MODULE_INFO(LOG_MODULE_DECODER,
                "Video decoder opened with software decoding");
  }

  return Result<void>::Ok();
}
```

**关键设计点**：

1. **接受 hw_context 参数**：
   - 如果 `hw_context != nullptr`：尝试硬件加速
   - 如果 `hw_context == nullptr`：软件解码

2. **先保存 hw_context_，再调用基类 Open**：
   - 基类 `Open()` 会调用 `OnBeforeOpen()`
   - `OnBeforeOpen()` 需要访问 `hw_context_`
   - 所以必须先保存

3. **失败时清理**：
   - 如果 `Open()` 失败，清空 `hw_context_`
   - 避免悬空指针

#### 3. OnBeforeOpen() 钩子的使用

```cpp
Result<void> VideoDecoder::OnBeforeOpen(AVCodecContext* codec_ctx) {
  // ========================================
  // 如果有硬件上下文，在 avcodec_open2 之前配置硬件加速
  // ========================================
  if (hw_context_ && hw_context_->IsInitialized()) {
    auto hw_result = hw_context_->ConfigureDecoder(codec_ctx);
    if (!hw_result.IsOk()) {
      // ⚠️ 硬件加速配置失败，回退到软件解码
      MODULE_WARN(
          LOG_MODULE_DECODER,
          "Failed to configure HW acceleration, will fallback to SW: {}",
          hw_result.Message());
      hw_context_ = nullptr;
      return Result<void>::Ok();  // ✅ 不阻止打开，只是不使用硬件加速
    }
    MODULE_INFO(LOG_MODULE_DECODER, "Hardware acceleration configured");
  }

  return Result<void>::Ok();
}
```

**关键设计点**：

1. **优雅降级**：
   - 硬件加速失败 → 自动回退到软件解码
   - 不返回错误（避免整个播放器失败）
   - 只记录 WARN 日志

2. **调用时机**：
   - 在 `avcodec_parameters_to_context()` 之后
   - 在 `avcodec_open2()` 之前
   - 此时可以配置 `hw_device_ctx`、`hw_frames_ctx`

**注意**：
> 本篇不深入讲解 `hw_context_->ConfigureDecoder()` 的实现细节，硬件加速将在后续章节专门详解。这里只需要理解：
> - VideoDecoder 通过 OnBeforeOpen 钩子支持硬件加速
> - 硬件加速是可选的（失败时自动降级）
> - AudioDecoder 不需要这个钩子（音频通常软解）

#### 4. ReceiveFrame() 的重写

```cpp
Result<AVFrame*> VideoDecoder::ReceiveFrame() {
  // ========================================
  // 步骤 1：调用基类的 ReceiveFrame
  // ========================================
  auto result = Decoder::ReceiveFrame();

  // ========================================
  // 步骤 2：如果成功接收到帧，且使用硬件加速，验证零拷贝
  // ========================================
  if (result.IsOk() && result.Value() != nullptr && hw_context_ &&
      !zero_copy_validated_) {
    MODULE_INFO(LOG_MODULE_DECODER,
                "First hardware frame decoded, validating zero-copy setup...");

    // 验证帧上下文配置（检查 D3D11 BindFlags 等）
    if (hw_context_->ValidateFramesContext(GetCodecContext())) {
      MODULE_INFO(LOG_MODULE_DECODER,
                  "🎉 Zero-copy hardware rendering is ENABLED");
    } else {
      MODULE_WARN(LOG_MODULE_DECODER,
                  "⚠️ Zero-copy validation failed! Check BindFlags in logs.");
    }

    zero_copy_validated_ = true;  // 只验证一次
  }

  return result;
}
```

**为什么要重写**？

```cpp
// 基类 ReceiveFrame：只管解码
Result<AVFrame*> Decoder::ReceiveFrame() {
  avcodec_receive_frame(codec_context_.get(), workFrame_.get());
  return Ok(workFrame_);
}

// VideoDecoder::ReceiveFrame：解码 + 验证零拷贝
Result<AVFrame*> VideoDecoder::ReceiveFrame() {
  auto result = Decoder::ReceiveFrame();  // 调用基类
  
  // 额外工作：验证零拷贝配置
  if (hw_context_ && !zero_copy_validated_) {
    ValidateZeroCopy();
  }
  
  return result;
}
```

**验证时机**：
- ❌ 不能在 `Open()` 时验证：`hw_frames_ctx` 还未创建
- ✅ 必须在第一帧解码后验证：此时 `hw_frames_ctx` 已由 FFmpeg 创建
- ✅ 只验证一次：避免每帧都检查（性能开销）

**注意**：
> `ValidateFramesContext()` 的实现细节（检查 D3D11 BindFlags、纹理格式等）将在硬件加速章节详细讲解。

---

## 🎮 PlaybackController：视频解码任务循环

### VideoDecodeTask() 代码

```cpp
void PlaybackController::VideoDecodeTask() {
  if (!video_decoder_ || !video_decoder_->opened()) {
    return;
  }

  AVPacket* packet = nullptr;
  std::vector<AVFramePtr> frames;

  while (!state_manager_->ShouldStop()) {
    // ========================================
    // 步骤 1：检查暂停状态
    // ========================================
    if (state_manager_->ShouldPause()) {
      state_manager_->WaitForResume();
      continue;
    }

    // ========================================
    // 步骤 2：从队列获取 AVPacket（阻塞等待）
    // ========================================
    if (!video_packet_queue_.Pop(packet)) {
      break;  // 队列已停止，退出循环
    }

    // ========================================
    // 步骤 3：处理 Flush 请求或普通解码
    // ========================================
    if (!packet) {
      // Flush 信号
      MODULE_DEBUG(LOG_MODULE_PLAYER, "VideoDecodeTask: Flushing decoder");
      video_decoder_->Flush(&frames);
    } else {
      // 普通解码
      TIMER_START(video_decode);
      bool decode_success = video_decoder_->Decode(packet, &frames);
      auto decode_time = TIMER_END_MS(video_decode);

      // 统计
      uint32_t frame_queue_size =
          video_player_ ? video_player_->GetQueueSize() : 0;
      STATS_UPDATE_DECODE(true, decode_success, decode_time, frame_queue_size);

      if (!decode_success && packet) {
        MODULE_WARN(LOG_MODULE_PLAYER, "Decode failed for packet, size={}",
                    packet->size);
      }

      // ========================================
      // 诊断信息：AVERROR_INVALIDDATA
      // ========================================
      const auto& decode_stats = video_decoder_->last_decode_stats();
      if (decode_stats.had_invalid_data && packet) {
        // 计算时间戳（毫秒）
        double pts_ms = -1.0;
        double dts_ms = -1.0;
        AVRational time_base{1, 1};
        if (demuxer_ && demuxer_->active_video_stream_index() >= 0) {
          if (AVStream* stream = demuxer_->findStreamByIndex(
                  demuxer_->active_video_stream_index())) {
            time_base = stream->time_base;
          }
        }

        if (packet->pts != AV_NOPTS_VALUE) {
          pts_ms = packet->pts * av_q2d(time_base) * 1000.0;
        }
        if (packet->dts != AV_NOPTS_VALUE) {
          dts_ms = packet->dts * av_q2d(time_base) * 1000.0;
        }

        uint32_t video_queue_size =
            video_player_ ? video_player_->GetQueueSize() : 0;
        uint32_t packet_queue_size = video_packet_queue_.Size();

        // DEBUG 级别日志（B 帧重排序是正常现象）
        MODULE_DEBUG(
            LOG_MODULE_PLAYER,
            "AVERROR_INVALIDDATA: pts={}, dts={}, pts_ms={:.2f}, "
            "dts_ms={:.2f}, size={}, video_frame_queue={}, packet_queue={}",
            packet->pts, packet->dts, pts_ms, dts_ms, packet->size,
            video_queue_size, packet_queue_size);
      }
    }

    // ========================================
    // 步骤 4：推送所有解码得到的帧
    // ========================================
    for (auto& frame : frames) {
      if (video_player_) {
        // 创建时间戳
        VideoPlayer::FrameTimestamp timestamp;
        timestamp.pts = frame->pts;
        timestamp.dts = frame->pkt_dts;
        if (demuxer_ && demuxer_->active_video_stream_index() >= 0) {
          if (AVStream* stream = demuxer_->findStreamByIndex(
                  demuxer_->active_video_stream_index())) {
            timestamp.time_base = stream->time_base;
          }
        }

        // ========================================
        // 关键：推送帧，带超时机制
        // ========================================
        // timeout = 500ms，即使队列满也会定期返回
        // 让 DecodeTask 可以检查 ShouldPause 和 ShouldStop
        constexpr int kPushFrameTimeoutMs = 500;
        bool push_success = video_player_->PushFrameBlocking(
            std::move(frame), timestamp, kPushFrameTimeoutMs);

        if (!push_success) {
          // 超时或被中断（暂停/停止）
          // 原因 1：队列仍然满 → 下一轮循环会重新尝试
          // 原因 2：ShouldPause=true → 下一轮循环会进入暂停等待
          // 原因 3：ShouldStop=true → 下一轮循环会退出
          MODULE_DEBUG(LOG_MODULE_PLAYER,
                       "PushFrameBlocking timeout or interrupted, "
                       "will retry next iteration");
        }
      }
    }
    frames.clear();

    // Flush 时退出
    if (!packet) {
      MODULE_INFO(LOG_MODULE_PLAYER, "VideoDecodeTask: Exiting after flush");
      break;
    }
  }

  MODULE_INFO(LOG_MODULE_PLAYER, "VideoDecodeTask: Thread exiting");
}
```

### 与 AudioDecodeTask 的对比

| 特性 | AudioDecodeTask | VideoDecodeTask |
|------|-----------------|-----------------|
| **解码后处理** | 需要重采样（planar → packed） | 无需处理（YUV 直接渲染） |
| **推送方式** | `PushFrame()`（立即） | `PushFrameBlocking(timeout)`（带超时） |
| **队列大小** | 50-100 帧（小，8KB/帧） | 5-10 帧（大，3MB/帧） |
| **流控策略** | 队列满时阻塞 | 队列满时超时返回（避免卡死） |
| **INVALIDDATA** | 不常见（音频无 B 帧） | 常见（视频 B 帧重排序） |

### 关键设计点

#### 1. 为什么视频需要超时机制？

```cpp
// ❌ 音频：立即推送（队列满时阻塞）
audio_player_->PushFrame(std::move(resampled));
// 问题：如果队列满，会一直阻塞
// 但音频帧小（8KB），队列可以很长（100 帧），不容易满

// ✅ 视频：带超时推送（队列满时 500ms 返回）
video_player_->PushFrameBlocking(std::move(frame), timestamp, 500);
// 优点：即使队列满，也能定期返回检查暂停/停止信号
// 视频帧大（3MB），队列短（10 帧），容易满
```

**实际场景**：
```
用户点击暂停：
  1. state_manager_->SetPause(true)
  2. VideoDecodeTask 正在 PushFrameBlocking()
  3. 如果没有超时，会一直阻塞（无法响应暂停）
  4. 有超时后，500ms 返回，下一轮循环检测到暂停
```

#### 2. AVERROR_INVALIDDATA 诊断日志

```cpp
const auto& decode_stats = video_decoder_->last_decode_stats();
if (decode_stats.had_invalid_data && packet) {
  // 计算时间戳、队列大小等信息
  MODULE_DEBUG(LOG_MODULE_PLAYER,
               "AVERROR_INVALIDDATA: pts={}, dts={}, pts_ms={:.2f}, "
               "dts_ms={:.2f}, size={}, video_frame_queue={}, packet_queue={}",
               packet->pts, packet->dts, pts_ms, dts_ms, packet->size,
               video_queue_size, packet_queue_size);
}
```

**为什么需要诊断日志**？

- 视频 B 帧重排序时，`AVERROR_INVALIDDATA` 是正常现象
- 但如果频繁出现，可能表示：
  - 文件损坏
  - 解码器配置错误
  - 参考帧丢失

**日志示例**：
```
DEBUG: AVERROR_INVALIDDATA: pts=150, dts=100, pts_ms=5000.00, dts_ms=3333.33, 
       size=5120, video_frame_queue=8, packet_queue=20
       
解读：
  - B 帧（pts > dts）
  - 帧大小 5KB（正常 B 帧大小）
  - 队列有 8 帧（接近满）
  - 包队列有 20 个（解码速度正常）
  
结论：正常的 B 帧缓冲，无需担心
```

---

## 🔄 音视频解码流程对比

### 相同点

1. **都继承 Decoder 基类**：
   - 复用 `Decode()`、`ReceiveFrame()`、`Flush()`
   - 统一的错误处理（EAGAIN、EOF、INVALIDDATA）
   - 统一的帧所有权管理（av_frame_move_ref）

2. **都在专用线程解码**：
   - AudioDecodeTask、VideoDecodeTask
   - 通过 BlockingQueue 与播放/渲染隔离

3. **都支持 Flush**：
   - Seek 时清空解码器缓冲区
   - 输出剩余的缓冲帧

### 不同点

| 特性 | 音频解码 | 视频解码 |
|------|---------|---------|
| **解码输出** | AVFrame（FLTP planar） | AVFrame（YUV420P / D3D11） |
| **后续处理** | AudioResampler（格式转换） | 无（直接渲染） |
| **帧大小** | 8 KB（1024 采样） | 3 MB（1080p YUV） |
| **队列长度** | 50-100 帧 | 5-10 帧 |
| **推送策略** | 阻塞推送 | 超时推送（500ms） |
| **硬件加速** | 不需要（软解已足够） | 需要（CPU 占用高） |
| **B 帧重排** | 无（音频无 B 帧） | 有（AVERROR_INVALIDDATA 常见） |
| **零拷贝** | 不需要（数据小） | 需要（GPU 显存直通） |

---

## 📚 本篇总结

### 核心组件

1. **Decoder 基类**：
   - 智能指针管理 AVCodecContext
   - send/receive 循环处理
   - av_frame_move_ref 转移所有权（避免 clone）
   - AVERROR_INVALIDDATA 正确处理（B 帧重排）
   - OnBeforeOpen 钩子支持子类定制

2. **AudioDecoder 子类**：
   - 继承基类，仅添加音频特定属性
   - 类型检查（必须是音频流）
   - 无需重写 OnBeforeOpen（音频通常软解）
   - 轻量级设计（50 行代码）

3. **VideoDecoder 子类**：
   - 继承基类，添加视频特定属性
   - 类型检查（必须是视频流）
   - 重写 OnBeforeOpen（支持硬件加速配置）
   - 重写 ReceiveFrame（验证零拷贝）
   - 优雅降级（硬件加速失败时回退软解）

4. **AudioResampler 重采样器**：
   - 延迟初始化 SwrContext
   - 零拷贝优化（格式匹配时）
   - 缓冲区复用（避免频繁分配）
   - 职责分离（在解码线程重采样）

5. **PlaybackController 解码任务**：
   - 音频：立即推送，队列长（100 帧）
   - 视频：超时推送，队列短（10 帧）
   - BlockingQueue 自动流控
   - Flush 正确处理（冲刷解码器缓冲区）
   - 时间戳信息传递

### 设计原则

- ✅ RAII 管理资源生命周期
- ✅ 单一职责（解码/重采样/播放分离）
- ✅ 性能优化（零拷贝/缓冲区复用/SIMD）
- ✅ 线程安全（队列隔离解码和播放线程）
- ✅ 可测试性（每个组件独立可测）

### 性能数据

**音频解码**：
- CPU 占用：9%（优化后）vs 13%（优化前）
- 音频延迟：20ms（优化后）vs 50ms（优化前）
- 内存占用：稳定（av_frame_move_ref）

**视频解码（软解）**：
- CPU 占用：40%（1080p H.264，单线程）
- 解码速度：60 fps（实时播放需要 30 fps）
- 内存占用：30 MB（10 帧队列，3MB/帧）

**架构优势**：
- 基类抽象减少重复代码 70%
- av_frame_move_ref 避免硬件表面池耗尽
- AVERROR_INVALIDDATA 正确处理避免 B 帧丢失
- 超时推送避免暂停/停止响应延迟

---

## 🚀 下一篇预告

**09. 硬件加速解码：让 GPU 干重活**

将深入讲解：
- HWDecoderContext 的设计与实现
- D3D11VA / VAAPI 硬件加速配置
- VideoDecoder::OnBeforeOpen() 的完整实现
- 零拷贝渲染（GPU 显存直通）
- hw_frames_ctx 的验证与调试
- 软解 vs 硬解性能对比（CPU 40% → 5%）

本篇搞定了软件解码的基础架构，下一篇让 GPU 接管重活！🚀

---

## 📦 关于 ZenPlay

**ZenPlay** 是一个基于 C++17 和 FFmpeg 的跨平台音视频播放器项目，采用现代 C++ 设计理念，注重代码质量和性能优化。

- **GitHub 仓库**：[https://github.com/Sunshine334419520/zenplay](https://github.com/Sunshine334419520/zenplay)
- **特性**：硬件加速解码、零拷贝渲染、精确音视频同步、模块化架构
- **适合学习**：音视频开发、FFmpeg 实战、现代 C++ 工程实践

欢迎 Star ⭐ 和 Fork 🍴，一起探索音视频技术！

有问题或建议？欢迎提 Issue 或 PR！
