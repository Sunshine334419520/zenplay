# 🔄 ZenPlay 项目 Result/ErrorCode 重构计划

**文档版本**: v1.0  
**创建日期**: 2024-12-20  
**状态**: 规划阶段 ✏️  
**预计工期**: 2-4 周（分 4 个阶段执行）

---

## 📋 目录

- [1. 重构概述](#1-重构概述)
- [2. 重构目标](#2-重构目标)
- [3. 重构范围](#3-重构范围)
- [4. 分阶段执行计划](#4-分阶段执行计划)
- [5. 详细重构方案](#5-详细重构方案)
- [6. 兼容性策略](#6-兼容性策略)
- [7. 测试策略](#7-测试策略)
- [8. 风险评估与缓解](#8-风险评估与缓解)
- [9. 验收标准](#9-验收标准)
- [10. 附录](#10-附录)

---

## 1. 重构概述

### 1.1 背景

ZenPlay 项目当前使用混合的错误处理方式：

```cpp
// ❌ 当前方式 - 不一致的错误处理
bool Open(const std::string& url);           // 返回 bool，无错误信息
bool Start();                                // 返回 bool
void Stop();                                 // 无返回值
bool Seek(int64_t timestamp, bool backward); // 返回 bool
```

**问题**：
1. **信息丢失**：bool 无法携带错误原因
2. **调试困难**：出错时不知道具体哪里失败
3. **日志混乱**：错误日志散落在各处
4. **用户体验差**：无法向用户展示具体错误信息

### 1.2 解决方案

引入统一的 `Result<T>` 和 `ErrorCode` 系统：

```cpp
// ✅ 重构后 - 统一且详细的错误处理
Result<void> Open(const std::string& url);
Result<void> Start();
Result<void> Stop();
Result<void> Seek(int64_t timestamp, bool backward);
```

**优势**：
1. ✅ **详细错误信息**：ErrorCode + 错误消息
2. ✅ **易于调试**：完整的错误追踪链
3. ✅ **类型安全**：编译期检查
4. ✅ **易于维护**：统一的错误处理模式

---

## 2. 重构目标

### 2.1 主要目标

| 目标 | 说明 | 优先级 |
|------|------|--------|
| 统一错误处理 | 所有核心 API 使用 Result<T> | P0 |
| 提升调试体验 | 错误信息可追溯 | P0 |
| 向后兼容 | 不破坏现有代码 | P0 |
| 文档完善 | 更新所有相关文档 | P1 |

### 2.2 非目标

- ❌ 不改变现有的多线程架构
- ❌ 不改变 PlayerStateManager 状态机
- ❌ 不改变 FFmpeg 集成方式
- ❌ 不修改第三方库接口

---

## 3. 重构范围

### 3.1 核心模块（必须重构）

#### 3.1.1 ZenPlayer（播放器主接口）

**当前签名**：
```cpp
class ZenPlayer {
 public:
  bool Open(const std::string& url);
  void Close();
  bool SetRenderWindow(void* window_handle, int width, int height);
  void OnWindowResize(int width, int height);
  bool Play();
  bool Pause();
  bool Stop();
  bool Seek(int64_t timestamp, bool backward = false);
  void SeekAsync(int64_t timestamp_ms, bool backward);
};
```

**重构后签名**：
```cpp
class ZenPlayer {
 public:
  Result<void> Open(const std::string& url);
  Result<void> Close();
  Result<void> SetRenderWindow(void* window_handle, int width, int height);
  Result<void> OnWindowResize(int width, int height);
  Result<void> Play();
  Result<void> Pause();
  Result<void> Stop();
  Result<void> Seek(int64_t timestamp, bool backward = false);
  Result<void> SeekAsync(int64_t timestamp_ms, bool backward);
  
  // 向后兼容的 bool 版本（标记为 deprecated）
  [[deprecated("Use Result<void> Open() instead")]]
  bool OpenLegacy(const std::string& url);
  
  // 获取最后的错误信息
  ErrorCode GetLastError() const;
  std::string GetLastErrorMessage() const;
};
```

**涉及文件**：
- `src/player/zen_player.h`
- `src/player/zen_player.cpp`

---

#### 3.1.2 Demuxer（解封装器）

**当前签名**：
```cpp
class Demuxer {
 public:
  bool Open(const std::string& url);
  void Close();
  bool ReadPacket(AVPacket** packet);
  bool Seek(int64_t timestamp, bool backward = false);
  int64_t GetDuration() const;
};
```

**重构后签名**：
```cpp
class Demuxer {
 public:
  Result<void> Open(const std::string& url);
  Result<void> Close();
  Result<AVPacket*> ReadPacket();
  Result<void> Seek(int64_t timestamp, bool backward = false);
  Result<int64_t> GetDuration() const;
  
 private:
  ErrorCode last_error_ = ErrorCode::OK;
  std::string last_error_message_;
};
```

**错误码映射**：
```cpp
// FFmpeg 错误 → ErrorCode
AVERROR_EOF           → ErrorCode::IO_ERROR
AVERROR(EIO)          → ErrorCode::IO_ERROR
AVERROR(EINVAL)       → ErrorCode::INVALID_PARAM
AVERROR_INVALIDDATA   → ErrorCode::INVALID_FILE_FORMAT
AVERROR_STREAM_NOT_FOUND → ErrorCode::STREAM_NOT_FOUND
```

**涉及文件**：
- `src/player/demuxer/demuxer.h`
- `src/player/demuxer/demuxer.cpp`

---

#### 3.1.3 Decoder（解码器）

**当前签名**：
```cpp
class Decoder {
 public:
  bool Open(AVCodecParameters* codec_params, AVDictionary** options = nullptr);
  void Close();
  bool SendPacket(AVPacket* packet);
  bool ReceiveFrame(AVFrame** frame);
};
```

**重构后签名**：
```cpp
class Decoder {
 public:
  Result<void> Open(AVCodecParameters* codec_params, AVDictionary** options = nullptr);
  Result<void> Close();
  Result<void> SendPacket(AVPacket* packet);
  Result<AVFrame*> ReceiveFrame();
  
 private:
  ErrorCode MapFFmpegError(int av_error);
};
```

**涉及文件**：
- `src/player/codec/decode.h`
- `src/player/codec/decode.cpp`
- `src/player/codec/video_decoder.h`
- `src/player/codec/audio_decoder.h`

---

#### 3.1.4 AudioOutput（音频输出）

**当前签名**：
```cpp
class AudioOutput {
 public:
  virtual bool Initialize(const AudioFormat& format) = 0;
  virtual bool Start() = 0;
  virtual void Stop() = 0;
  virtual void Pause() = 0;
  virtual void Resume() = 0;
  virtual void Flush() = 0;
};
```

**重构后签名**：
```cpp
class AudioOutput {
 public:
  virtual Result<void> Initialize(const AudioFormat& format) = 0;
  virtual Result<void> Start() = 0;
  virtual Result<void> Stop() = 0;
  virtual Result<void> Pause() = 0;
  virtual Result<void> Resume() = 0;
  virtual Result<void> Flush() = 0;
  
 protected:
  ErrorCode last_error_ = ErrorCode::OK;
  std::string last_error_message_;
};
```

**WASAPI 实现**：
```cpp
class WASAPIAudioOutput : public AudioOutput {
 public:
  Result<void> Initialize(const AudioFormat& format) override {
    HRESULT hr = audio_client_->Initialize(...);
    if (FAILED(hr)) {
      return Result<void>::Err(
          ErrorCode::AUDIO_DEVICE_INIT_FAILED,
          FormatHRESULT(hr));
    }
    return Result<void>::Ok();
  }
  
 private:
  std::string FormatHRESULT(HRESULT hr);
};
```

**涉及文件**：
- `src/player/audio/audio_output.h`
- `src/player/audio/impl/wasapi_audio_output.h`
- `src/player/audio/impl/wasapi_audio_output.cpp`

---

#### 3.1.5 AudioPlayer & VideoPlayer

**当前签名**：
```cpp
class AudioPlayer {
 public:
  bool Init(const AudioConfig& config = AudioConfig{});
  bool Start();
  void Stop();
  void Pause();
  void Resume();
  void Flush();
};

class VideoPlayer {
 public:
  bool Start();
  void Stop();
  void Pause();
  void Resume();
};
```

**重构后签名**：
```cpp
class AudioPlayer {
 public:
  Result<void> Init(const AudioConfig& config = AudioConfig{});
  Result<void> Start();
  Result<void> Stop();
  Result<void> Pause();
  Result<void> Resume();
  Result<void> Flush();
};

class VideoPlayer {
 public:
  Result<void> Start();
  Result<void> Stop();
  Result<void> Pause();
  Result<void> Resume();
};
```

**涉及文件**：
- `src/player/audio/audio_player.h`
- `src/player/audio/audio_player.cpp`
- `src/player/video/video_player.h`
- `src/player/video/video_player.cpp`

---

#### 3.1.6 Renderer（渲染器）

**当前签名**：
```cpp
class Renderer {
 public:
  virtual bool Initialize(void* window_handle, int width, int height) = 0;
  virtual void Cleanup() = 0;
  virtual bool RenderFrame(AVFrame* frame) = 0;
  virtual void SetViewport(int width, int height) = 0;
};
```

**重构后签名**：
```cpp
class Renderer {
 public:
  virtual Result<void> Initialize(void* window_handle, int width, int height) = 0;
  virtual Result<void> Cleanup() = 0;
  virtual Result<void> RenderFrame(AVFrame* frame) = 0;
  virtual Result<void> SetViewport(int width, int height) = 0;
};
```

**涉及文件**：
- `src/player/video/render/renderer.h`
- `src/player/video/render/impl/sdl_renderer.h`
- `src/player/video/render/impl/sdl_renderer.cpp`

---

#### 3.1.7 PlaybackController（播放控制器）

**当前签名**：
```cpp
class PlaybackController {
 public:
  bool Start();
  void Stop();
  void Pause();
  void Resume();
  bool Seek(int64_t timestamp_ms);
  void SeekAsync(int64_t timestamp_ms, bool backward);
};
```

**重构后签名**：
```cpp
class PlaybackController {
 public:
  Result<void> Start();
  Result<void> Stop();
  Result<void> Pause();
  Result<void> Resume();
  Result<void> Seek(int64_t timestamp_ms);
  Result<void> SeekAsync(int64_t timestamp_ms, bool backward);
};
```

**涉及文件**：
- `src/player/playback_controller.h`
- `src/player/playback_controller.cpp`

---

### 3.2 支持模块（可选重构）

#### 3.2.1 PlayerStateManager

**说明**：状态机本身不需要返回 Result，但状态转换失败可以记录详细信息。

**当前签名**：
```cpp
bool RequestStateChange(PlayerState new_state);
bool TransitionToPlaying();
```

**重构后（可选）**：
```cpp
Result<void> RequestStateChange(PlayerState new_state);
Result<void> TransitionToPlaying();
```

**优先级**：P1（低优先级，可后续重构）

---

#### 3.2.2 AVSyncController

**说明**：同步控制器主要是内部使用，暂不重构。

**优先级**：P2（最低优先级）

---

## 4. 分阶段执行计划

### 📅 第一阶段：基础设施（第 1 周）

**目标**：建立错误处理基础设施和工具函数。

#### 任务清单

- [x] **Task 1.1**：实现 Result<T> 和 ErrorCode（已完成）
  - 文件：`src/player/common/error.h`, `error.cpp`
  - 单元测试：`tests/test_result_error.cpp`

- [ ] **Task 1.2**：实现 FFmpeg 错误码转换工具
  ```cpp
  // src/player/common/ffmpeg_error_utils.h
  ErrorCode MapFFmpegError(int av_error);
  std::string FormatFFmpegError(int av_error);
  
  // 使用示例
  int ret = av_read_frame(ctx, packet);
  if (ret < 0) {
    return Result<AVPacket*>::Err(
        MapFFmpegError(ret),
        FormatFFmpegError(ret));
  }
  ```

- [ ] **Task 1.3**：实现 HRESULT 错误码转换工具（Windows）
  ```cpp
  // src/player/common/win32_error_utils.h
  ErrorCode MapHRESULT(HRESULT hr);
  std::string FormatHRESULT(HRESULT hr);
  ```

- [ ] **Task 1.4**：实现便利宏
  ```cpp
  // src/player/common/error_macros.h
  
  // 检查 Result 并在失败时记录日志并返回
  #define RETURN_IF_ERROR(result, module) \
    do { \
      if (!(result).IsOk()) { \
        MODULE_ERROR(module, "Operation failed: {}", (result).FullMessage()); \
        return result; \
      } \
    } while(0)
  
  // 检查 bool 并转换为 Result
  #define BOOL_TO_RESULT(expr, error_code, msg) \
    ((expr) ? Result<void>::Ok() : Result<void>::Err(error_code, msg))
  ```

- [ ] **Task 1.5**：更新 CMakeLists.txt 和编译配置

---

### 📅 第二阶段：底层模块重构（第 2 周）

**目标**：重构底层依赖较少的模块。

#### 任务清单

- [ ] **Task 2.1**：重构 Demuxer
  - 文件：`src/player/demuxer/demuxer.h`, `demuxer.cpp`
  - 测试：新增 `tests/test_demuxer_result.cpp`
  - 预计工时：4 小时

- [ ] **Task 2.2**：重构 Decoder
  - 文件：`src/player/codec/decode.h`, `decode.cpp`
  - 子类：`video_decoder.h`, `audio_decoder.h`
  - 测试：新增 `tests/test_decoder_result.cpp`
  - 预计工时：6 小时

- [ ] **Task 2.3**：重构 Renderer
  - 文件：
    - `src/player/video/render/renderer.h`
    - `src/player/video/render/impl/sdl_renderer.h`
    - `src/player/video/render/impl/sdl_renderer.cpp`
  - 测试：更新 `tests/test_renderer.cpp`
  - 预计工时：4 小时

- [ ] **Task 2.4**：重构 AudioOutput
  - 文件：
    - `src/player/audio/audio_output.h`
    - `src/player/audio/impl/wasapi_audio_output.h`
    - `src/player/audio/impl/wasapi_audio_output.cpp`
  - 测试：新增 `tests/test_audio_output_result.cpp`
  - 预计工时：6 小时

**验收标准**：
- ✅ 所有底层模块 API 使用 Result<T>
- ✅ FFmpeg 错误正确映射到 ErrorCode
- ✅ 单元测试覆盖 90%+
- ✅ 所有测试通过

---

### 📅 第三阶段：中层模块重构（第 3 周）

**目标**：重构中层业务逻辑模块。

#### 任务清单

- [ ] **Task 3.1**：重构 AudioPlayer
  - 文件：`src/player/audio/audio_player.h`, `audio_player.cpp`
  - 依赖：AudioOutput（已重构）
  - 测试：更新 `tests/test_audio_player.cpp`
  - 预计工时：6 小时

- [ ] **Task 3.2**：重构 VideoPlayer
  - 文件：`src/player/video/video_player.h`, `video_player.cpp`
  - 依赖：Renderer（已重构）
  - 测试：更新 `tests/test_video_player.cpp`
  - 预计工时：4 小时

- [ ] **Task 3.3**：重构 PlaybackController
  - 文件：`src/player/playback_controller.h`, `playback_controller.cpp`
  - 依赖：Demuxer, Decoder, AudioPlayer, VideoPlayer
  - 测试：更新 `tests/test_playback_controller.cpp`
  - 预计工时：8 小时

**验收标准**：
- ✅ 所有中层模块 API 使用 Result<T>
- ✅ 错误信息正确传播到上层
- ✅ 单元测试覆盖 85%+
- ✅ 所有测试通过

---

### 📅 第四阶段：顶层模块重构与集成（第 4 周）

**目标**：重构用户接口层，并提供向后兼容层。

#### 任务清单

- [ ] **Task 4.1**：重构 ZenPlayer 主接口
  - 文件：`src/player/zen_player.h`, `zen_player.cpp`
  - 实现新的 Result<T> API
  - 保留旧的 bool API（标记为 deprecated）
  - 预计工时：8 小时

- [ ] **Task 4.2**：实现向后兼容层
  ```cpp
  // ZenPlayer 兼容实现示例
  bool ZenPlayer::OpenLegacy(const std::string& url) {
    auto result = Open(url);
    if (!result.IsOk()) {
      last_error_ = result.Code();
      last_error_message_ = result.Message();
      MODULE_ERROR(LOG_MODULE_PLAYER, "Open failed: {}", result.FullMessage());
      return false;
    }
    return true;
  }
  ```
  - 预计工时：4 小时

- [ ] **Task 4.3**：更新 UI 层（MainWindow）
  - 文件：`src/view/main_window.cpp`
  - 更新错误显示逻辑
  - 预计工时：4 小时

- [ ] **Task 4.4**：集成测试
  - 完整的播放流程测试
  - 错误场景测试
  - 性能测试
  - 预计工时：8 小时

- [ ] **Task 4.5**：文档更新
  - 更新架构文档
  - 更新 API 文档
  - 编写迁移指南
  - 预计工时：6 小时

**验收标准**：
- ✅ 所有公共 API 使用 Result<T>
- ✅ 向后兼容层工作正常
- ✅ UI 能正确显示错误信息
- ✅ 所有集成测试通过
- ✅ 文档完整更新

---

## 5. 详细重构方案

### 5.1 Demuxer 重构示例

#### 5.1.1 Open 方法重构

**Before**：
```cpp
bool Demuxer::Open(const std::string& url) {
  int ret = avformat_open_input(&format_context_, url.c_str(), nullptr, nullptr);
  if (ret < 0) {
    MODULE_ERROR(LOG_MODULE_DEMUX, "Failed to open input: {}", url);
    return false;
  }
  
  ret = avformat_find_stream_info(format_context_, nullptr);
  if (ret < 0) {
    MODULE_ERROR(LOG_MODULE_DEMUX, "Failed to find stream info");
    avformat_close_input(&format_context_);
    return false;
  }
  
  probeStreams();
  return true;
}
```

**After**：
```cpp
Result<void> Demuxer::Open(const std::string& url) {
  int ret = avformat_open_input(&format_context_, url.c_str(), nullptr, nullptr);
  if (ret < 0) {
    return Result<void>::Err(
        MapFFmpegError(ret),
        fmt::format("Failed to open input '{}': {}", url, FormatFFmpegError(ret)));
  }
  
  ret = avformat_find_stream_info(format_context_, nullptr);
  if (ret < 0) {
    avformat_close_input(&format_context_);
    return Result<void>::Err(
        MapFFmpegError(ret),
        fmt::format("Failed to find stream info: {}", FormatFFmpegError(ret)));
  }
  
  probeStreams();
  
  if (active_video_stream_index_ < 0 && active_audio_stream_index_ < 0) {
    avformat_close_input(&format_context_);
    return Result<void>::Err(
        ErrorCode::STREAM_NOT_FOUND,
        "No video or audio stream found in file");
  }
  
  MODULE_INFO(LOG_MODULE_DEMUX, "Opened file: {}", url);
  return Result<void>::Ok();
}
```

#### 5.1.2 ReadPacket 方法重构

**Before**：
```cpp
bool Demuxer::ReadPacket(AVPacket** packetRet) {
  AVPacket* packet = av_packet_alloc();
  if (!packet) {
    return false;
  }
  
  int ret = av_read_frame(format_context_, packet);
  if (ret < 0) {
    av_packet_free(&packet);
    return false;
  }
  
  *packetRet = packet;
  return true;
}
```

**After**：
```cpp
Result<AVPacket*> Demuxer::ReadPacket() {
  AVPacket* packet = av_packet_alloc();
  if (!packet) {
    return Result<AVPacket*>::Err(
        ErrorCode::OUT_OF_MEMORY,
        "Failed to allocate AVPacket");
  }
  
  int ret = av_read_frame(format_context_, packet);
  if (ret < 0) {
    av_packet_free(&packet);
    
    if (ret == AVERROR_EOF) {
      return Result<AVPacket*>::Err(
          ErrorCode::IO_ERROR,
          "End of file reached");
    }
    
    return Result<AVPacket*>::Err(
        MapFFmpegError(ret),
        fmt::format("Failed to read packet: {}", FormatFFmpegError(ret)));
  }
  
  return Result<AVPacket*>::Ok(packet);
}
```

#### 5.1.3 调用方更新

**Before**：
```cpp
// PlaybackController::DemuxTask()
AVPacket* packet = nullptr;
if (!demuxer_->ReadPacket(&packet)) {
  // EOF or error
  continue;
}
```

**After**：
```cpp
// PlaybackController::DemuxTask()
auto result = demuxer_->ReadPacket();
if (!result.IsOk()) {
  if (result.Code() == ErrorCode::IO_ERROR) {
    // EOF - normal end of playback
    MODULE_INFO(LOG_MODULE_DEMUX, "End of file");
    break;
  }
  MODULE_ERROR(LOG_MODULE_DEMUX, "Read packet failed: {}", result.FullMessage());
  continue;
}

AVPacket* packet = result.TakeValue();
```

---

### 5.2 Decoder 重构示例

#### 5.2.1 Open 方法重构

**Before**：
```cpp
bool Decoder::Open(AVCodecParameters* codec_params, AVDictionary** options) {
  const AVCodec* codec = avcodec_find_decoder(codec_params->codec_id);
  if (!codec) {
    MODULE_ERROR(LOG_MODULE_DECODER, "Codec not found");
    return false;
  }
  
  codec_context_.reset(avcodec_alloc_context3(codec));
  if (!codec_context_) {
    MODULE_ERROR(LOG_MODULE_DECODER, "Failed to allocate codec context");
    return false;
  }
  
  int ret = avcodec_parameters_to_context(codec_context_.get(), codec_params);
  if (ret < 0) {
    MODULE_ERROR(LOG_MODULE_DECODER, "Failed to copy codec parameters");
    return false;
  }
  
  ret = avcodec_open2(codec_context_.get(), codec, options);
  if (ret < 0) {
    MODULE_ERROR(LOG_MODULE_DECODER, "Failed to open codec");
    codec_context_.reset();
    return false;
  }
  
  opened_ = true;
  return true;
}
```

**After**：
```cpp
Result<void> Decoder::Open(AVCodecParameters* codec_params, AVDictionary** options) {
  if (!codec_params) {
    return Result<void>::Err(
        ErrorCode::INVALID_PARAM,
        "codec_params is null");
  }
  
  const AVCodec* codec = avcodec_find_decoder(codec_params->codec_id);
  if (!codec) {
    return Result<void>::Err(
        ErrorCode::DECODER_NOT_FOUND,
        fmt::format("Decoder not found for codec_id: {}", codec_params->codec_id));
  }
  
  codec_context_.reset(avcodec_alloc_context3(codec));
  if (!codec_context_) {
    return Result<void>::Err(
        ErrorCode::OUT_OF_MEMORY,
        "Failed to allocate codec context");
  }
  
  int ret = avcodec_parameters_to_context(codec_context_.get(), codec_params);
  if (ret < 0) {
    codec_context_.reset();
    return Result<void>::Err(
        MapFFmpegError(ret),
        fmt::format("Failed to copy codec parameters: {}", FormatFFmpegError(ret)));
  }
  
  ret = avcodec_open2(codec_context_.get(), codec, options);
  if (ret < 0) {
    codec_context_.reset();
    return Result<void>::Err(
        ErrorCode::DECODER_INIT_FAILED,
        fmt::format("Failed to open codec: {}", FormatFFmpegError(ret)));
  }
  
  opened_ = true;
  codec_type_ = codec_params->codec_type;
  
  MODULE_INFO(LOG_MODULE_DECODER, "Decoder opened: {}", codec->name);
  return Result<void>::Ok();
}
```

---

### 5.3 AudioOutput 重构示例（WASAPI）

#### 5.3.1 Initialize 方法重构

**Before**：
```cpp
bool WASAPIAudioOutput::Initialize(const AudioFormat& format) {
  HRESULT hr = CoCreateInstance(...);
  if (FAILED(hr)) {
    return false;
  }
  
  hr = audio_client_->Initialize(...);
  if (FAILED(hr)) {
    return false;
  }
  
  return true;
}
```

**After**：
```cpp
Result<void> WASAPIAudioOutput::Initialize(const AudioFormat& format) {
  // 验证参数
  if (format.sample_rate <= 0 || format.sample_rate > 192000) {
    return Result<void>::Err(
        ErrorCode::INVALID_PARAM,
        fmt::format("Invalid sample rate: {}", format.sample_rate));
  }
  
  // 创建音频客户端
  HRESULT hr = CoCreateInstance(
      CLSID_MMDeviceEnumerator, nullptr,
      CLSCTX_ALL, IID_IMMDeviceEnumerator,
      (void**)&device_enumerator_);
  if (FAILED(hr)) {
    return Result<void>::Err(
        ErrorCode::AUDIO_DEVICE_NOT_FOUND,
        fmt::format("Failed to create device enumerator: {}", FormatHRESULT(hr)));
  }
  
  hr = device_enumerator_->GetDefaultAudioEndpoint(
      eRender, eConsole, &audio_device_);
  if (FAILED(hr)) {
    return Result<void>::Err(
        ErrorCode::AUDIO_DEVICE_NOT_FOUND,
        fmt::format("Failed to get default audio device: {}", FormatHRESULT(hr)));
  }
  
  hr = audio_device_->Activate(
      IID_IAudioClient, CLSCTX_ALL,
      nullptr, (void**)&audio_client_);
  if (FAILED(hr)) {
    return Result<void>::Err(
        ErrorCode::AUDIO_DEVICE_INIT_FAILED,
        fmt::format("Failed to activate audio device: {}", FormatHRESULT(hr)));
  }
  
  // 初始化音频客户端
  WAVEFORMATEX wave_format = {};
  wave_format.wFormatTag = WAVE_FORMAT_PCM;
  wave_format.nChannels = format.channels;
  wave_format.nSamplesPerSec = format.sample_rate;
  wave_format.wBitsPerSample = 16;
  wave_format.nBlockAlign = wave_format.nChannels * wave_format.wBitsPerSample / 8;
  wave_format.nAvgBytesPerSec = wave_format.nSamplesPerSec * wave_format.nBlockAlign;
  
  hr = audio_client_->Initialize(
      AUDCLNT_SHAREMODE_SHARED,
      0,
      buffer_duration_,
      0,
      &wave_format,
      nullptr);
  if (FAILED(hr)) {
    return Result<void>::Err(
        ErrorCode::AUDIO_DEVICE_INIT_FAILED,
        fmt::format("Failed to initialize audio client: {}", FormatHRESULT(hr)));
  }
  
  // 获取渲染客户端
  hr = audio_client_->GetService(IID_IAudioRenderClient, (void**)&render_client_);
  if (FAILED(hr)) {
    return Result<void>::Err(
        ErrorCode::AUDIO_OUTPUT_ERROR,
        fmt::format("Failed to get render client: {}", FormatHRESULT(hr)));
  }
  
  MODULE_INFO(LOG_MODULE_AUDIO, "WASAPI initialized: {} Hz, {} channels",
              format.sample_rate, format.channels);
  return Result<void>::Ok();
}
```

---

### 5.4 ZenPlayer 重构示例

#### 5.4.1 Open 方法重构

**Before**：
```cpp
bool ZenPlayer::Open(const std::string& url) {
  if (is_opened_) {
    Close();
  }
  
  state_manager_->TransitionToOpening();
  
  if (!demuxer_->Open(url)) {
    MODULE_ERROR(LOG_MODULE_PLAYER, "Failed to open demuxer");
    return false;
  }
  
  // ... 打开解码器 ...
  
  is_opened_ = true;
  state_manager_->TransitionToStopped();
  return true;
}
```

**After**：
```cpp
Result<void> ZenPlayer::Open(const std::string& url) {
  MODULE_INFO(LOG_MODULE_PLAYER, "Opening URL: {}", url);
  
  // 如果已打开，先关闭
  if (is_opened_) {
    auto close_result = Close();
    if (!close_result.IsOk()) {
      MODULE_WARN(LOG_MODULE_PLAYER, "Close failed: {}", close_result.Message());
    }
  }
  
  // 转换状态
  if (!state_manager_->TransitionToOpening()) {
    return Result<void>::Err(
        ErrorCode::INVALID_STATE,
        "Cannot transition to Opening state");
  }
  
  // 打开解封装器
  auto demux_result = demuxer_->Open(url);
  if (!demux_result.IsOk()) {
    state_manager_->TransitionToError();
    return Result<void>::Err(
        demux_result.Code(),
        fmt::format("Demuxer open failed: {}", demux_result.Message()));
  }
  
  // 打开视频解码器
  AVStream* video_stream = 
      demuxer_->findStreamByIndex(demuxer_->active_video_stream_index());
  if (video_stream) {
    auto video_result = video_decoder_->Open(video_stream->codecpar);
    if (!video_result.IsOk()) {
      demuxer_->Close();
      state_manager_->TransitionToError();
      return Result<void>::Err(
          video_result.Code(),
          fmt::format("Video decoder open failed: {}", video_result.Message()));
    }
  }
  
  // 打开音频解码器
  AVStream* audio_stream =
      demuxer_->findStreamByIndex(demuxer_->active_audio_stream_index());
  if (audio_stream) {
    auto audio_result = audio_decoder_->Open(audio_stream->codecpar);
    if (!audio_result.IsOk()) {
      video_decoder_->Close();
      demuxer_->Close();
      state_manager_->TransitionToError();
      return Result<void>::Err(
          audio_result.Code(),
          fmt::format("Audio decoder open failed: {}", audio_result.Message()));
    }
  }
  
  // 创建播放控制器
  playback_controller_ = std::make_unique<PlaybackController>(
      state_manager_, demuxer_.get(), video_decoder_.get(),
      audio_decoder_.get(), renderer_.get());
  
  is_opened_ = true;
  state_manager_->TransitionToStopped();
  
  MODULE_INFO(LOG_MODULE_PLAYER, "File opened successfully");
  return Result<void>::Ok();
}
```

#### 5.4.2 向后兼容实现

```cpp
// 旧版本 API（标记为 deprecated）
[[deprecated("Use Result<void> Open() instead")]]
bool ZenPlayer::OpenLegacy(const std::string& url) {
  auto result = Open(url);
  if (!result.IsOk()) {
    last_error_ = result.Code();
    last_error_message_ = result.Message();
    MODULE_ERROR(LOG_MODULE_PLAYER, "Open failed: {}", result.FullMessage());
    return false;
  }
  return true;
}

// 获取最后的错误
ErrorCode ZenPlayer::GetLastError() const {
  return last_error_;
}

std::string ZenPlayer::GetLastErrorMessage() const {
  return last_error_message_;
}
```

---

## 6. 兼容性策略

### 6.1 向后兼容层

为了不破坏现有代码，我们提供两种方案：

#### 方案 A：双 API（推荐）

同时保留 bool 版本和 Result 版本：

```cpp
class ZenPlayer {
 public:
  // 新版本 API
  Result<void> Open(const std::string& url);
  Result<void> Play();
  Result<void> Pause();
  Result<void> Stop();
  
  // 旧版本 API（标记为 deprecated）
  [[deprecated("Use Result<void> Open() instead")]]
  bool OpenLegacy(const std::string& url);
  
  [[deprecated("Use Result<void> Play() instead")]]
  bool PlayLegacy();
  
  [[deprecated("Use Result<void> Pause() instead")]]
  bool PauseLegacy();
  
  [[deprecated("Use Result<void> Stop() instead")]]
  bool StopLegacy();
  
  // 错误查询接口
  ErrorCode GetLastError() const;
  std::string GetLastErrorMessage() const;
  
 private:
  ErrorCode last_error_ = ErrorCode::OK;
  std::string last_error_message_;
};
```

**优点**：
- ✅ 完全向后兼容
- ✅ 旧代码无需立即修改
- ✅ 可逐步迁移

**缺点**：
- ❌ API 膨胀
- ❌ 需要维护两套实现

---

#### 方案 B：适配器模式

提供适配器类封装 Result 为 bool：

```cpp
// 新版本核心 API
class ZenPlayerCore {
 public:
  Result<void> Open(const std::string& url);
  Result<void> Play();
  // ...
};

// 兼容层适配器
class ZenPlayer {
 public:
  bool Open(const std::string& url) {
    auto result = core_.Open(url);
    return result.IsOk();
  }
  
  bool Play() {
    auto result = core_.Play();
    return result.IsOk();
  }
  
  ErrorCode GetLastError() const {
    return core_.GetLastError();
  }
  
 private:
  ZenPlayerCore core_;
};
```

**优点**：
- ✅ 核心代码统一
- ✅ 适配层简单

**缺点**：
- ❌ 引入额外层次
- ❌ 性能略有损失

---

### 6.2 推荐方案

**采用方案 A（双 API）**，理由：

1. **渐进式迁移**：旧代码继续工作，新代码使用新 API
2. **明确的弃用路径**：通过 `[[deprecated]]` 引导开发者迁移
3. **最小性能开销**：直接调用，无额外抽象层
4. **时间计划**：
   - 第 1-4 周：实现新 API + 兼容层
   - 第 5-8 周：逐步迁移内部调用
   - 第 9-12 周：更新外部调用和文档
   - 第 13 周后：可考虑移除旧 API

---

## 7. 测试策略

### 7.1 单元测试

每个重构的模块都需要新增/更新单元测试：

```cpp
// tests/test_demuxer_result.cpp
TEST(DemuxerResultTest, OpenSuccessful) {
  Demuxer demuxer;
  auto result = demuxer.Open("test.mp4");
  
  EXPECT_TRUE(result.IsOk());
  EXPECT_EQ(result.Code(), ErrorCode::OK);
}

TEST(DemuxerResultTest, OpenFileNotFound) {
  Demuxer demuxer;
  auto result = demuxer.Open("nonexistent.mp4");
  
  EXPECT_FALSE(result.IsOk());
  EXPECT_EQ(result.Code(), ErrorCode::IO_ERROR);
  EXPECT_FALSE(result.Message().empty());
}

TEST(DemuxerResultTest, OpenInvalidFormat) {
  Demuxer demuxer;
  auto result = demuxer.Open("invalid.txt");
  
  EXPECT_FALSE(result.IsOk());
  EXPECT_EQ(result.Code(), ErrorCode::INVALID_FILE_FORMAT);
}
```

### 7.2 集成测试

```cpp
// tests/test_zenplayer_integration.cpp
TEST(ZenPlayerIntegrationTest, FullPlaybackCycle) {
  ZenPlayer player;
  
  // Open
  auto open_result = player.Open("test_video.mp4");
  ASSERT_TRUE(open_result.IsOk()) << open_result.FullMessage();
  
  // Play
  auto play_result = player.Play();
  ASSERT_TRUE(play_result.IsOk()) << play_result.FullMessage();
  
  std::this_thread::sleep_for(std::chrono::seconds(2));
  
  // Pause
  auto pause_result = player.Pause();
  ASSERT_TRUE(pause_result.IsOk()) << pause_result.FullMessage();
  
  // Resume
  auto resume_result = player.Play();
  ASSERT_TRUE(resume_result.IsOk()) << resume_result.FullMessage();
  
  // Stop
  auto stop_result = player.Stop();
  ASSERT_TRUE(stop_result.IsOk()) << stop_result.FullMessage();
  
  // Close
  auto close_result = player.Close();
  ASSERT_TRUE(close_result.IsOk()) << close_result.FullMessage();
}

TEST(ZenPlayerIntegrationTest, ErrorHandlingChain) {
  ZenPlayer player;
  
  // 尝试打开不存在的文件
  auto result = player.Open("nonexistent.mp4");
  EXPECT_FALSE(result.IsOk());
  EXPECT_EQ(result.Code(), ErrorCode::IO_ERROR);
  
  // 尝试在未打开时播放
  result = player.Play();
  EXPECT_FALSE(result.IsOk());
  EXPECT_EQ(result.Code(), ErrorCode::NOT_INITIALIZED);
}
```

### 7.3 性能测试

```cpp
// tests/benchmark_result_overhead.cpp
TEST(ResultBenchmark, ResultVsBoolOverhead) {
  const int iterations = 1000000;
  
  // 测试 bool 版本
  auto start_bool = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < iterations; ++i) {
    bool result = SomeFunctionReturningBool();
    if (!result) {
      // error handling
    }
  }
  auto end_bool = std::chrono::high_resolution_clock::now();
  
  // 测试 Result 版本
  auto start_result = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < iterations; ++i) {
    Result<void> result = SomeFunctionReturningResult();
    if (!result.IsOk()) {
      // error handling
    }
  }
  auto end_result = std::chrono::high_resolution_clock::now();
  
  auto duration_bool = std::chrono::duration_cast<std::chrono::microseconds>(
      end_bool - start_bool).count();
  auto duration_result = std::chrono::duration_cast<std::chrono::microseconds>(
      end_result - start_result).count();
  
  std::cout << "Bool: " << duration_bool << " us\n";
  std::cout << "Result: " << duration_result << " us\n";
  std::cout << "Overhead: " << (duration_result - duration_bool) << " us\n";
  
  // 验证开销小于 5%
  EXPECT_LT(duration_result, duration_bool * 1.05);
}
```

---

## 8. 风险评估与缓解

### 8.1 技术风险

| 风险 | 影响 | 可能性 | 缓解措施 |
|------|------|--------|----------|
| 性能下降 | 中 | 低 | 性能基准测试，优化热路径 |
| 编译错误 | 高 | 中 | 分阶段重构，每个阶段独立测试 |
| 运行时崩溃 | 高 | 低 | 完整的单元测试和集成测试 |
| API 不兼容 | 高 | 低 | 保留旧 API，逐步弃用 |

### 8.2 进度风险

| 风险 | 影响 | 可能性 | 缓解措施 |
|------|------|--------|----------|
| 工期延误 | 中 | 中 | 预留缓冲时间，关键路径监控 |
| 资源不足 | 中 | 低 | 分阶段执行，可暂停恢复 |
| 依赖阻塞 | 低 | 低 | 底层优先，减少依赖 |

### 8.3 质量风险

| 风险 | 影响 | 可能性 | 缓解措施 |
|------|------|--------|----------|
| 测试覆盖不足 | 高 | 中 | 强制单元测试，代码审查 |
| 错误消息不清晰 | 中 | 中 | 错误消息规范，用户测试 |
| 文档不同步 | 中 | 高 | 文档更新纳入任务清单 |

---

## 9. 验收标准

### 9.1 功能验收

- [ ] ✅ 所有核心 API 使用 Result<T>
- [ ] ✅ 所有 FFmpeg 错误正确映射
- [ ] ✅ 所有 WASAPI 错误正确映射
- [ ] ✅ 错误消息清晰可读
- [ ] ✅ 错误码覆盖所有场景

### 9.2 质量验收

- [ ] ✅ 单元测试覆盖率 > 85%
- [ ] ✅ 所有单元测试通过
- [ ] ✅ 所有集成测试通过
- [ ] ✅ 无编译警告
- [ ] ✅ 无内存泄漏

### 9.3 性能验收

- [ ] ✅ Release 编译下性能下降 < 5%
- [ ] ✅ 错误路径性能影响可忽略
- [ ] ✅ 无显著的启动时间增加

### 9.4 文档验收

- [ ] ✅ API 文档完整更新
- [ ] ✅ 架构文档反映新设计
- [ ] ✅ 错误码文档完整
- [ ] ✅ 迁移指南清晰
- [ ] ✅ 示例代码正确

---

## 10. 附录

### 10.1 FFmpeg 错误码映射表

| FFmpeg 错误 | ErrorCode | 说明 |
|-------------|-----------|------|
| AVERROR_EOF | IO_ERROR | 文件结束 |
| AVERROR(EIO) | IO_ERROR | I/O 错误 |
| AVERROR(ENOMEM) | OUT_OF_MEMORY | 内存不足 |
| AVERROR(EINVAL) | INVALID_PARAM | 无效参数 |
| AVERROR_INVALIDDATA | INVALID_FILE_FORMAT | 无效数据 |
| AVERROR_STREAM_NOT_FOUND | STREAM_NOT_FOUND | 流未找到 |
| AVERROR_DECODER_NOT_FOUND | DECODER_NOT_FOUND | 解码器未找到 |
| AVERROR_UNKNOWN | UNKNOWN | 未知错误 |

### 10.2 HRESULT 错误码映射表

| HRESULT | ErrorCode | 说明 |
|---------|-----------|------|
| E_POINTER | INVALID_PARAM | 空指针 |
| E_OUTOFMEMORY | OUT_OF_MEMORY | 内存不足 |
| E_INVALIDARG | INVALID_PARAM | 无效参数 |
| AUDCLNT_E_DEVICE_INVALIDATED | AUDIO_DEVICE_NOT_FOUND | 设备无效 |
| AUDCLNT_E_NOT_INITIALIZED | NOT_INITIALIZED | 未初始化 |
| AUDCLNT_E_ALREADY_INITIALIZED | ALREADY_RUNNING | 已初始化 |
| AUDCLNT_E_UNSUPPORTED_FORMAT | AUDIO_FORMAT_NOT_SUPPORTED | 不支持的格式 |

### 10.3 工具函数实现参考

```cpp
// src/player/common/ffmpeg_error_utils.h
#pragma once
#include "error.h"
extern "C" {
#include <libavutil/error.h>
}

namespace zenplay {
namespace player {

// FFmpeg 错误码转换
inline ErrorCode MapFFmpegError(int av_error) {
  switch (av_error) {
    case AVERROR_EOF:
      return ErrorCode::IO_ERROR;
    case AVERROR(EIO):
      return ErrorCode::IO_ERROR;
    case AVERROR(ENOMEM):
      return ErrorCode::OUT_OF_MEMORY;
    case AVERROR(EINVAL):
      return ErrorCode::INVALID_PARAM;
    case AVERROR_INVALIDDATA:
      return ErrorCode::INVALID_FILE_FORMAT;
    case AVERROR_STREAM_NOT_FOUND:
      return ErrorCode::STREAM_NOT_FOUND;
    case AVERROR_DECODER_NOT_FOUND:
      return ErrorCode::DECODER_NOT_FOUND;
    default:
      return ErrorCode::UNKNOWN;
  }
}

// 格式化 FFmpeg 错误消息
inline std::string FormatFFmpegError(int av_error) {
  char errbuf[AV_ERROR_MAX_STRING_SIZE];
  av_strerror(av_error, errbuf, sizeof(errbuf));
  return std::string(errbuf);
}

}  // namespace player
}  // namespace zenplay
```

```cpp
// src/player/common/win32_error_utils.h (Windows only)
#pragma once
#ifdef OS_WIN
#include "error.h"
#include <windows.h>
#include <comdef.h>

namespace zenplay {
namespace player {

// HRESULT 错误码转换
inline ErrorCode MapHRESULT(HRESULT hr) {
  if (SUCCEEDED(hr)) {
    return ErrorCode::OK;
  }
  
  switch (hr) {
    case E_POINTER:
    case E_INVALIDARG:
      return ErrorCode::INVALID_PARAM;
    case E_OUTOFMEMORY:
      return ErrorCode::OUT_OF_MEMORY;
    case AUDCLNT_E_DEVICE_INVALIDATED:
      return ErrorCode::AUDIO_DEVICE_NOT_FOUND;
    case AUDCLNT_E_NOT_INITIALIZED:
      return ErrorCode::NOT_INITIALIZED;
    case AUDCLNT_E_ALREADY_INITIALIZED:
      return ErrorCode::ALREADY_RUNNING;
    case AUDCLNT_E_UNSUPPORTED_FORMAT:
      return ErrorCode::AUDIO_FORMAT_NOT_SUPPORTED;
    default:
      return ErrorCode::UNKNOWN;
  }
}

// 格式化 HRESULT 错误消息
inline std::string FormatHRESULT(HRESULT hr) {
  _com_error err(hr);
  return std::string(err.ErrorMessage());
}

}  // namespace player
}  // namespace zenplay
#endif  // OS_WIN
```

### 10.4 便利宏参考

```cpp
// src/player/common/error_macros.h
#pragma once
#include "error.h"
#include "log_manager.h"

// 检查 Result 并在失败时记录日志并返回
#define RETURN_IF_ERROR(result, module) \
  do { \
    const auto& __result = (result); \
    if (!__result.IsOk()) { \
      MODULE_ERROR(module, "Operation failed: {}", __result.FullMessage()); \
      return __result; \
    } \
  } while(0)

// 检查 Result 并在失败时记录日志并返回自定义错误
#define RETURN_IF_ERROR_WITH(result, module, error_code, message) \
  do { \
    const auto& __result = (result); \
    if (!__result.IsOk()) { \
      MODULE_ERROR(module, "{}: {}", (message), __result.FullMessage()); \
      return Result<void>::Err((error_code), (message)); \
    } \
  } while(0)

// 检查 bool 并转换为 Result
#define BOOL_TO_RESULT(expr, error_code, message) \
  ((expr) ? Result<void>::Ok() : Result<void>::Err((error_code), (message)))

// 检查指针是否为空
#define CHECK_NOT_NULL(ptr, error_code, message) \
  do { \
    if (!(ptr)) { \
      return Result<void>::Err((error_code), (message)); \
    } \
  } while(0)
```

---

## 📝 总结

本重构计划提供了将 ZenPlay 项目从 bool 错误处理迁移到 Result<T>/ErrorCode 系统的完整路线图。

**关键要点**：

1. **分阶段执行**：4 周，每周一个阶段，降低风险
2. **向后兼容**：保留旧 API，逐步弃用
3. **全面测试**：单元测试、集成测试、性能测试
4. **详细文档**：API 文档、迁移指南、错误码手册
5. **风险可控**：识别风险，制定缓解措施

**预期收益**：

- ✅ 统一的错误处理模式
- ✅ 详细的错误信息和追踪
- ✅ 更好的调试和维护体验
- ✅ 更专业的用户错误提示
- ✅ 为后续功能扩展奠定基础

---

**文档版本历史**：
- v1.0 (2024-12-20): 初始版本

**审阅者**: [待定]  
**批准者**: [待定]  
**实施负责人**: [待定]

