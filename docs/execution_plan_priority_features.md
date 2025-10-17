# 优先执行项：详细执行文档

> 目标：对用户提出的7项优先工作（全屏播放修复、统一错误处理、AVSync 时钟优化、条件变量通知、音频重采样性能优化、Windows 平台硬件加速支持、性能监控集成）给出详细设计、分步实现计划、测试/基准方案与验收标准。

最后更新：2024-12-19

---

## 一页概览（快速导航）

1. 支持全屏播放（SDL 渲染填充）
2. 统一错误处理模式（Result/ErrorCode 方案）
3. `AVSyncController` 时钟更新频率优化（基于时间与 PTS 的混合更新 + 平滑）
4. 条件变量通知（替换 busy-wait，改造队列与节流逻辑）
5. 音频重采样性能优化（在解码线程预重采样、重用 SwrContext、SIMD）
6. 硬件加速支持（Windows 优先）：FFmpeg 支持情况、渲染路径对比、实现方案
7. 性能监控与验证（Tracy 集成 + 基准用例）

每项包含：目标、背景、设计方案、修改点（文件/函数）、实现步骤、测试/指标、回归与验收标准、风险与缓解。

---

## 总体约束与原则

- 小步迭代：优先能快速验证效果的改动（低风险、低范围）先行。
- 可回退：每项修改应可在一个独立分支通过 CI 回滚。
- 数据驱动：所有性能相关改动需给出基线（改动前）和对比数据（改动后）。
- 最小入侵：优先在局部模块（例如 `AudioPlayer`、`VideoPlayer`、`Demuxer`、`PlaybackController`）做改造，避免大范围接口重构一次完成。

---

## 任务 1 — 支持全屏播放（SDL 画面填充）

### 目标

全屏时视频应填满屏幕或按用户选定的缩放（填充 / 等比裁剪 / 等比留黑），当前问题是进入全屏后 SDL 画面不填充窗口。

### 背景与症状

- 在 `MainWindow` 切换全屏后，SDL 渲染区未能适配窗口大小。可能原因：
  - SDL 渲染器或纹理未随窗口尺寸重建
  - 渲染目标使用固定逻辑尺寸 (`SDL_RenderSetLogicalSize`) 或 viewport 未更新
  - 如果使用 OpenGL/D3D 后端，视口（glViewport / D3D11 RS）未随 `resize` 更新

### 设计方案

1. 在接收到窗口 `resize` / `fullscreen` 事件时，通知 `SDLRenderer`：
   - 重建或调整输出纹理（texture）的尺寸为目标窗口尺寸
   - 更新渲染视口（SDL_RenderSetViewport / glViewport / D3D11_RS）
   - 根据用户缩放策略，计算目标渲染矩形（fill / fit / crop）并传递给渲染流程

2. `SDLRenderer` 应具有 `SetWindowSize(int w, int h)` 接口，内部实现：
   - 如果使用 `SDL_CreateTexture` 存储帧，纹理尺寸应尽量等于解码帧尺寸或 window size（视性能与格式而定）
   - 设置 `SDL_RenderSetLogicalSize(renderer, w, h)` 仅在选择“自动缩放（保持像素比）”模式时使用；否则直接调整 viewport

3. Qt 与 SDL 的集成： `ZenPlayer::SetRenderWindow(void* window_handle, int width, int height)` 在全屏切换时被调用或应被补充调用以刷新渲染器

### 关键修改点（建议文件）

- `src/player/video/render/impl/sdl_renderer.cpp` / `.h`
- `src/view/main_window.cpp`：在 `toggleFullScreen()` 中通知 `renderer_->SetWindowSize()` 或 `ZenPlayer::SetRenderWindow()`
- `src/player/video/video_player.cpp`：渲染时使用 `renderer_->RenderFrame(frame, target_rect)`

### 详细实现步骤

1. 调查现有 `SDLRenderer`：确认是否存在 `SetWindowSize` / `OnResize` 接口和 `Renderer::Render` 使用的目标矩形参数。
2. 在 `Renderer` 抽象中增加 `SetViewport(int w, int h)`（如果不存在）。
3. 在 `SDLRenderer::SetViewport` 中：
   - 调用 `SDL_GetRendererInfo` 检查后端能力
   - 根据当前 backend（OpenGL/D3D/Direct3D）调用 `SDL_RenderSetViewport(renderer, &rect)` 或 `glViewport(0,0,w,h)`
   - 如果当前纹理大小 != 新尺寸，决定是否重建纹理或在 Render 时按比例缩放
4. 在 `MainWindow::toggleFullScreen`：
   - 在进入/退出全屏之后调用 `player_->SetRenderWindow(win_handle, w, h)`（确保 `w/h` 是新窗口大小）
5. 提供用户缩放模式设置（枚举：Fill/Fit/Crop），并在渲染时计算 `target_rect`。

### 测试与验收

- 自动测试：在窗口从普通切换到全屏、再回退，验证 `SDLRenderer` 的 `SetViewport` 被调用（可以用日志断言）。
- 手动测试：播放不同分辨率视频（720p/1080p/4K），切换全屏观察是否填充并保持期望缩放模式。

**验收标准**：在 Windows/Linux/macOS 上，切换全屏后画面能按 `Fill` 模式完全填充窗口，无黑边或错位；`Fit` 模式下保持纵横比且居中。

---

## 任务 2 — 统一错误处理模式（Result / ErrorCode）

### 目标

引入统一的错误表示与传播机制，替换散落于代码中 `bool`、`int`、异常混用的风格，以便：
- 提高错误追踪与定位能力
- 方便记录结构化日志
- 便于上层区分可恢复错误与致命错误

### 设计原则

- 轻量、零开销（在 Release 构建下）
- 兼容现有代码，可分阶段迁移
- 与现有 `LogManager` / `StatisticsManager` 集成
- 支持携带可选的 `message`、`error_code`、`context`（例如函数/模块名）

### 提议的数据类型

1. 错误码枚举 `ErrorCode`（跨模块统一）

```cpp
enum class ErrorCode : int {
  OK = 0,
  INVALID_PARAM,
  NOT_INITIALIZED,
  ALREADY_RUNNING,
  IO_ERROR,
  DECODER_ERROR,
  RENDER_ERROR,
  NETWORK_ERROR,
  TIMEOUT,
  UNSUPPORTED_FORMAT,
  UNKNOWN
};
```

2. 统一 `Result<T>` 模板

```cpp
template<typename T>
class Result {
 public:
  static Result Ok(T value) { return Result(std::move(value), ErrorCode::OK, ""); }
  static Result Err(ErrorCode code, std::string msg="") { return Result(T(), code, std::move(msg)); }

  bool IsOk() const { return error_code_ == ErrorCode::OK; }
  T& Value() { return value_; }
  ErrorCode Code() const { return error_code_; }
  const std::string& Message() const { return message_; }

 private:
  Result(T value, ErrorCode code, std::string msg)
    : value_(std::move(value)), error_code_(code), message_(std::move(msg)) {}

  T value_;
  ErrorCode error_code_;
  std::string message_;
};

// 特化 void
using VoidResult = Result<int>; // 或者实现 Result<void>
```

3. 日志与错误链

- 在 `Result` 返回非 OK 时，调用 `LogManager` 记录 `MODULE_ERROR(...)`，并在关键路径上上报 `StatisticsManager::UpdateSystemStats/NetworkStats`（视情况）。

### 递进式迁移策略

1. 在内部实现新模块时采用 `Result<T>`（例如新实现的 `DecoderFactory::Create...`）
2. 在公共 API（如 `ZenPlayer::Open()`）先返回原有 `bool`，内部使用 `Result` 并在失败时把错误信息写入日志并返回 `false`；同时增加 `GetLastError()` 供上层获取详细信息
3. 中期目标：把 `ZenPlayer` 的关键 API 迁移为 `Result<void>` 或 `Result<T>`，逐步移除裸 `bool` 返回

### 修改点（建议文件）

- 新增 `src/player/common/error.h` / `error.cpp`
- 在 `player/*` 模块内逐步改造：`demuxer`, `codec`, `audio_output`, `renderer`, `playback_controller`

### 代码示例（使用方式）

```cpp
Result<std::unique_ptr<Decoder>> DecoderFactory::CreateVideoDecoder(...) {
  if (!codec) {
    return Result<std::unique_ptr<Decoder>>::Err(ErrorCode::INVALID_PARAM, "codec is null");
  }
  auto dec = std::make_unique<VideoDecoder>();
  if (!dec->Open(...)) {
    return Result<std::unique_ptr<Decoder>>::Err(ErrorCode::DECODER_ERROR, "open failed");
  }
  return Result<std::unique_ptr<Decoder>>::Ok(std::move(dec));
}

// 调用方
auto r = DecoderFactory::CreateVideoDecoder(...);
if (!r.IsOk()) {
  MODULE_ERROR(LOG_MODULE_CODEC, "Create decoder failed: {}", r.Message());
  return false; // 兼容旧 API
}

auto decoder = std::move(r.Value());
```

### 测试与验收

- 新增单元测试覆盖 `Result` 与 `ErrorCode` 边界（如 `DecoderFactory`、`Demuxer::Open` 失败场景）
- 在 `main` 启动路径下，当关键错误发生时， `LogManager` 输出结构化错误信息（包含 ErrorCode）

**验收标准**：新提交的模块至少 80% 使用 `Result` 返回错误信息；关键失败路径能输出可读的错误字符串与 `ErrorCode`。

---

## 任务 3 — `AVSyncController` 时钟更新频率优化

### 目标

提高同步精度与响应能力，减少时钟漂移（drift）并避免因低频更新导致的音画不同步。

### 背景

目前音频时钟更新策略可能依赖于 callback 计数（例如每 N 次回调更新一次），在 callback 频率或采样率不定时会造成更新延迟。正确策略应结合：
- 固定时间间隔（例如 50-100ms）
- 基于 PTS 的变化量触发
- 平滑（例如 EWMA）以避免抖动

### 设计方案

1. 使用高精度 `steady_clock` 记录 `last_update_time_`
2. 两个触发条件（任一满足即可更新）：
   - 时间间隔大于 `kMaxUpdateIntervalMs`（推荐 50-100ms）
   - PTS 差值大于 `kPtsDeltaThresholdMs`（推荐 30-50ms）
3. 更新时对 `audio_clock` 使用带权平滑：EWMA（指数加权移动平均）用于抑制测量噪声

公式：

- new_clock = alpha * measured_clock + (1 - alpha) * last_clock
- alpha 取值 0.2 - 0.5（经验值，可配置）

### 详细实现

在 `AVSyncController` 增加成员：

```cpp
std::chrono::steady_clock::time_point last_update_time_;
int64_t last_audio_pts_ms_ = 0;
const int kMaxUpdateIntervalMs = 50;  // 可配置
const int kPtsDeltaThresholdMs = 40;  // 可配置
const double kEwmaAlpha = 0.25;       // 可配置

void AVSyncController::UpdateAudioClock(int64_t audio_pts_ms,
                                       std::chrono::steady_clock::time_point system_time) {
  auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      system_time - last_update_time_).count();

  bool time_trigger = (elapsed_ms >= kMaxUpdateIntervalMs);
  bool pts_trigger = (std::abs(audio_pts_ms - last_audio_pts_ms_) >= kPtsDeltaThresholdMs);

  if (!time_trigger && !pts_trigger) return;

  // 计算 measured_clock
  double measured_clock = ComputeMeasuredClockFromAudio(audio_pts_ms, system_time);

  // 平滑
  master_clock_ = kEwmaAlpha * measured_clock + (1.0 - kEwmaAlpha) * master_clock_;

  last_update_time_ = system_time;
  last_audio_pts_ms_ = audio_pts_ms;
}
```

`ComputeMeasuredClockFromAudio`：将音频 pts 转换为播放时钟（归一化第一帧为 0 的策略 / 或保留绝对时间并以 play_start_time_ 计算 master_clock）。

### 测试与基准

- 使用合成视频（已知 PTS 和帧率），对比 `UpdateAudioClock` 的不同参数 (`kMaxUpdateIntervalMs=20/50/100`) 的同步误差，记录平均误差、最大误差。
- 目标：平均同步误差 < 30ms，最大 < 100ms（现场条件下可放宽）

**验收标准**：在测试用例下，修改后 avg sync error 减小至少 30%，最大偏差明显降低。

---

## 任务 4 — 条件变量通知（替换轮询/睡眠）

### 目标

用条件变量和双向通知替换轮询（`sleep_for`）或忙等逻辑，提高线程调度效率并降低 CPU 占用。

### 背景

代码中有使用 `sleep_for` 或频繁轮询队列大小的模式（如 `video_queue_.Size() >= max` 则 sleep），这些都会降低响应性并浪费 CPU。

### 设计方案

- 改造 `ThreadSafeQueue` 为带生产者/消费者信号的 `BlockingQueue`（`not_empty_cv_`、`not_full_cv_`），支持 `Push`（当队列满则等待）和 `Pop`（当队列空则等待）。
- 在 `PlaybackController` 的各任务（DemuxTask、DecodeTask、RenderThread）中使用 `BlockingQueue` 来进行阻塞等待。

### 关键修改点

- `src/player/common/thread_safe_queue.h` -> `blocking_queue.h`
- `src/player/playback_controller.cpp`：替换 `while(queue.Size() >= max)`+sleep 为 `queue.Push(item)`（Push 内阻塞直到有空间）
- `VideoPlayer::RenderLoop` 等处使用 `Pop()` 代替循环轮询

### API 建议

```cpp
template<typename T>
class BlockingQueue {
 public:
  BlockingQueue(size_t max_size);
  bool Push(T&& item); // 阻塞或返回 false 如果 stop
  bool Pop(T& out);    // 阻塞直到有元素或 stop
  void Stop();         // 唤醒所有等待并标记停止
  size_t Size() const;
};
```

### 实现注意

- `Stop()` 必须在停止时调用，以解除阻塞的线程
- 为避免死锁，`Push`/`Pop` 的阻塞应支持超时版本（`PopTimeout(ms)`）以便做周期性检查

### 测试与验收

- 测试在高帧率、高分辨率情况下，系统 CPU 占用降低（工具：`top`/`htop`、Tracy）。
- 对比：改造前后解码/渲染线程的 CPU 平均占用，期望降低 5-15%。

**验收标准**：无 busy-waiting；CPU 占用在同一测试场景下降且无功能回退。

---

## 任务 5 — 音频重采样性能优化

### 目标

把重采样从音频回调路径中剥离出来，在解码线程做重采样并缓存，减少音频回调（或 callback thread）中的 CPU 消耗与内存分配，提升时延与稳定性。

### 背景

当前实现可能在音频回调（高频）中调用 `swr_convert()`，这会导致：
- 回调路径占用大量 CPU
- 频繁分配/释放缓冲区
- 可能产生音频卡顿

### 设计方案

1. 在 `AudioDecodeTask`（或 `PlaybackController` 中的解码线程）完成 `swr_convert()`，将重采样后的 PCM 帧封装为 `ResampledAudioFrame` 推入 `audio_playback_queue_`（`BlockingQueue`）
2. `AudioOutput` 的回调只负责从 `audio_playback_queue_` 拿数据并 memcpy 到输出缓冲，尽可能做到零拷贝或最小拷贝
3. 重用 `SwrContext`：在 `AudioPlayer::Init` 时创建并保存在成员中，避免频繁 alloc/free；将输出缓冲提前分配为单一大缓冲循环使用
4. 在 `SwrContext` 中启用优化选项（SIMD），并在编译或运行时确保 FFmpeg 编译开启了优化

### 代码片段（核心示例）

```cpp
// 1. 解码线程
ResampledAudioFrame AudioDecoder::DecodeAndResample(AVFrame* frame) {
  // reuse swr_ctx_ stored in AudioPlayer
  int out_samples = swr_get_out_samples(swr_ctx_, frame->nb_samples);
  audio_buffer_.ensure_capacity(out_samples * channels * bytes_per_sample);

  int converted = swr_convert(swr_ctx_, &audio_buffer_.data(), out_samples,
                              (const uint8_t**)frame->data, frame->nb_samples);
  ResampledAudioFrame r;
  r.sample_count = converted;
  r.pts_ms = ConvertPtsToMs(frame->pts);
  r.pcm.assign(audio_buffer_.data(), audio_buffer_.data() + converted * frame_size);
  return r;
}

// 2. AudioOutput callback
size_t WasapiAudioOutput::FillCallback(uint8_t* out, size_t out_bytes) {
  size_t filled = 0;
  while (filled < out_bytes) {
    ResampledAudioFrame frame;
    if (!audio_playback_queue_.Pop(frame)) break; // stop

    // copy尽可能少次
    size_t copy_bytes = std::min<size_t>(frame.pcm.size() - frame_offset, out_bytes - filled);
    memcpy(out + filled, frame.pcm.data() + frame_offset, copy_bytes);
    filled += copy_bytes;
    frame_offset += copy_bytes;

    if (frame_offset >= frame.pcm.size()) {
      frame_offset = 0; // move next frame
    }
  }
  return filled;
}
```

### 进一步优化

- 使用 `swr_set_opts` 或 `av_opt_set_int(swr_ctx, "use_simd", 1, 0)` 启用 SIMD
- 如果重采样占用仍高，考虑替换为更高效库（如 libsamplerate）或平台特化实现

### 测试与基准

- 基准方法：用一段 48kHz、多通道音频，目标输出 44.1kHz 立体声。测量：
  - 重采样耗时（ms/frame）在解码线程中
  - 音频回调耗时（平均/最大）
  - CPU 消耗对比（改造前/改造后）
- 目标：音频回调平均耗时下降 >= 70%，整体 CPU 降低 10-30%

**验收标准**：音频回调耗时指标满足目标且无音频卡顿或 crackle。

---

## 任务 6 — 硬件加速支持（Windows 优先：硬件解码 + 硬件渲染）

### 目标

在 Windows 平台优先引入硬件解码（DXVA2 / D3D11VA / NVDEC）并评估硬件渲染路径（SDL 提供的加速 renderer 与自实现 D3D11 渲染）方案，最终实现解码/渲染端的零拷贝或最小拷贝通路以提升解码速度与降低 CPU 负载。

### 可选硬件解码方案（FFmpeg 侧）

- DXVA2 (`h264_dxva2` / `hevc_dxva2`)：较老但广泛支持
- D3D11 via `d3d11va` / `d3d11va_copy`：更现代，常用于 Windows 10+
- NVDEC (`h264_cuvid`, `h264_nvdec`, `hevc_nvdec`)：NVIDIA 专用，高性能
- Intel QuickSync (`qsv`)：Intel 平台加速
- 在 FFmpeg 中使用 `av_hwdevice_ctx_create`、`av_hwframe_transfer_data` 等 API 管理 hardware frames

### 渲染通路比较（优先 Windows）

1. 使用 SDL 加速 Renderer（依赖于 SDL 后端）
   - 优点：跨平台、集成方便、可由 SDL 选择合适的后端（Direct3D/OpenGL/Metal）
   - 缺点：想要直接使用 FFmpeg 的 `AV_HWFRAME`（例如 D3D11 的 `AV_PIX_FMT_D3D11`），需要将硬件帧与 SDL 的 GPU 纹理进行互操作，SDL 未提供统一的 API 来直接接收 `AVBufferRef` GPU 帧，需要平台相关代码或拷贝

2. 自实现 D3D11 渲染器（Windows）
   - 优点：可直接使用 FFmpeg 输出的 `D3D11` 帧（`AV_PIX_FMT_D3D11`），实现零拷贝上传到 GPU 纹理；更灵活（可用 DXGI 交换链、共享句柄跨进程）
   - 缺点：实现复杂度高、平台绑定强、需要处理设备/上下文管理、与 Qt 集成有细节（QWindow -> native handle）

3. 折中方案：FFmpeg 硬件解码 + 从 HWFrame 拷贝到系统内存并创建 SDL_Texture
   - 优点：实现简单，兼容性高
   - 缺点：存在一次从 GPU 到 CPU 到 GPU 的拷贝，性能不及零拷贝，但仍优于软件解码

### 推荐路线（阶段化）

Phase A （短期，低风险）
- 使用 FFmpeg 的硬件解码（例如 `h264_dxva2` / `h264_d3d11va` 或 `h264_nvdec`），但将硬件帧转换（`av_hwframe_transfer_data`）为 `AV_PIX_FMT_RGBA` 或 `AV_PIX_FMT_YUV420P` 后再上传为 `SDL_Texture`。
- 优点：兼容性好，实现简单，能得到部分性能提升（CPU 负载下降）。

Phase B （中期）
- 实现平台专有路径（Windows）：当检测到 `d3d11` backend 时，使用 FFmpeg 输出 `AV_PIX_FMT_D3D11`，并在自实现的 D3D11 渲染器中直接使用 `ID3D11Texture2D` 或共享句柄，尽量实现零拷贝通路。
- 需要实现 `D3D11Renderer`，并在运行时选择 `SDLRenderer`（通用）或 `D3D11Renderer`（Windows 优化）

Phase C（长期）
- 针对不同硬件（NVIDIA/Intel/AMD）做进一步优化（如 NVDEC 专用接口、QSV），并将硬件解码能力暴露给 `DecoderFactory`，用于自动选择最佳解码器

### 关键实现要点（示例代码片段）

1) 初始化 D3D11 HW device（FFmpeg）

```cpp
AVBufferRef* hw_device_ctx = nullptr;
int ret = av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_D3D11VA, nullptr, nullptr, 0);
if (ret < 0) {
  MODULE_ERROR(LOG_MODULE_CODEC, "Failed to create D3D11 device: {}", av_err2str(ret));
  return false;
}
codec_ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
```

2) 接收 hw frame（示例伪代码）

```cpp
AVFrame* frame = av_frame_alloc();
int ret = avcodec_receive_frame(codec_ctx, frame);
if (ret == 0) {
  if (frame->format == AV_PIX_FMT_D3D11) {
    // 如果想要零拷贝，需要 D3D11Renderer 能够接收该类型
    // 否则使用 av_hwframe_transfer_data 将其传回系统内存
    AVFrame* sw_frame = av_frame_alloc();
    av_hwframe_transfer_data(sw_frame, frame, 0); // GPU->CPU 拷贝
    // 上传 sw_frame 到 SDL_Texture
  }
}
```

3) SDL + D3D11 交互（若选择 SDL 渲染）
- SDL 在 Windows 上的后端可能是 Direct3D11。可以通过 `SDL_Renderer` 获得内部 `ID3D11Device` 的句柄的能力有限（通常不可见）。若需要直接共享 D3D11 纹理，需要实现自定义渲染器或使用 `SDL_Renderer` 的 `SDL_CreateTextureFromSurface`（会产生拷贝）。

### 修改点（建议文件）

- `src/player/codec/`：新增 `hw_decoder.h/.cpp` 和修改 `DecoderFactory`，支持选项 `HWAccelType`
- `src/player/video/render/`：新增 `d3d11_renderer.h/.cpp`（可选），并在 `RendererFactory` 中做运行时选择
- `CMakeLists.txt`：在 Windows 上添加 `d3d11` 相关链接（D3D11.lib 等）

### 测试与基准

测试矩阵：
- 测试分辨率：720p / 1080p / 4K
- 测试编码：H.264 / H.265
- 测试硬件：Intel iGPU / NVIDIA GPU

度量项：
- 解码 CPU 时间（ms/frame）
- 渲染时间（ms/frame）
- 总 CPU 占用率
- 帧率（FPS）和掉帧率

目标示例：在 1080p H.264 上，使用 NVDEC / D3D11VA 后，CPU 占用相比软件解码降低 >= 50%，解码时间降低 >= 60%。

### 风险与缓解

- 驱动/平台差异大：优先 Phase A（可回退的拷贝路径）以保证兼容性
- SDL 与本地 API（D3D）互操作复杂：若实现复杂，选择自定义 D3D 渲染器而不是强行在 SDL 内做互操作

---

## 任务 7 — 性能监控与验证（Tracy 集成优先）

### 目标

集成性能分析工具（推荐 Tracy），在关键路径添加采样区域（zones），建立基线（改造前）并在每次主要改动后收集对比数据（改造后），以数据证实优化效果。

### 为什么选择 Tracy？

- 轻量、线上的采样分析工具，支持 `ZoneScoped` 风格插桩
- 支持 CPU 时间线、GPU 跟踪、内存分配跟踪
- 在 C++ 项目中集成方便（开关编译宏），视图清晰易分析

### 集成步骤

1. 在 `CMakeLists.txt` 增加 Tracy 客户端依赖（或使用 `FetchContent` 引入 tracy-client 源码）
2. 在代码关键函数加入 `ZoneScoped;` 或 `ZoneScopedN("Name")`
   - `PlaybackController::DemuxLoop`
   - `PlaybackController::VideoDecodeTask`
   - `PlaybackController::AudioDecodeTask`
   - `VideoPlayer::RenderLoop`
   - `WasapiAudioOutput::AudioThreadMain` / audio callback
   - `AVSyncController::UpdateAudioClock`
3. 运行 Tracy server（或使用 tracy server 自带 UI）并收集 trace

### 采集的关键指标

- 每帧解码耗时（ms）
- 每帧渲染耗时（ms）
- 音频回调延迟与耗时（平均/最大）
- 队列长度随时间变化（demux/dec/ render queue）
- CPU 占用
- 内存分配热点

### 基准测试用例（建议）

- 固定文件播放测试：`test_1080p_h264.mp4`（局域网或本地磁盘）
- 网络流压力测试：使用 `ffmpeg -re -i input -f rtsp` 本地搭建流并播放
- 录制样本：播放 10 分钟并采样，计算平均值与 95 百分位

### 数据对比与报告格式

每次重要改动前后至少记录：
- 解码时间均值/最大值（ms/frame）
- 渲染时间均值/最大值（ms/frame）
- CPU 平均占用（%）
- 平均队列长度

示例报告片段：

```
Test: 1080p H.264 local file
Before: software decode
  Decode time: avg=12.4ms max=28.5ms
  Render time: avg=4.2ms max=10.1ms
  CPU: 36.4%
After: NVDEC + copy-to-texture (Phase A)
  Decode time: avg=2.1ms max=6.5ms  (↓83%)
  Render time: avg=4.0ms max=9.7ms  (≈)
  CPU: 8.7%  (↓76%)
```

### 自动化数据采集

- 在 CI（或本地机器）上配置 `perf` / `top` / `tracy` 脚本，运行固定用例后导出数据
- 将 CSV 保存为 `benchmarks/`，并生成对比图表（可用 Python matplotlib）

### 轻量替代方案

如果不能引入 Tracy，最小可行方案：
- 在关键位置手动计时（`Timer` 工具），将结果输出到 `statistics_manager`，周期性记录到文件
- 使用 `perf` / `htop` / `psrecord` 做 CPU 采样

---

## 里程碑与计划（建议时间估算）

> 假设一个熟悉代码的工程师，优先从 P0/低风险项开始。以下为粗略估算。

- Week 0-1 (Sprint 1)
  - 支持全屏播放修复（任务 1） — 2 天
  - 引入 BlockingQueue 并替换主要 busy-wait（任务 4） — 2 天
  - 集成 Tracy 客户端基础（任务 7） — 1 天

- Week 1-2 (Sprint 2)
  - 音频重采样优化（任务 5） — 4 天
  - AVSync 时钟频率优化（任务 3） — 2 天
  - 在关键路径添加 Tracy 标记并收集基线数据 — 1 天

- Week 2-4 (Sprint 3)
  - 引入统一错误处理骨架并在部分模块试点（任务 2） — 5 天
  - 硬件加速 Phase A（FFmpeg hw decode + sw transfer + SDL upload）— 5 天
  - 基准测试并撰写对比报告（任务 7） — 3 天

- Week 4-8（后续）
  - 硬件加速 Phase B（Windows D3D11 零拷贝实现）— 2-3 周（复杂）
  - 将 `Result` 迁移到更多模块并完善错误链 — 持续迭代

## 验收标准（整体）

- 功能正确性：全屏/窗口模式切换无渲染错误
- 稳定性：改动后无内存泄漏、死锁或频繁崩溃
- 性能：针对每项性能改进，给出基线与改进后数据，且满足上文的目标（如 CPU 降低、回调耗时下降等）
- 可回退：每项大改动都应在独立分支并能快速回滚

---

## 风险清单与缓解

- **FFmpeg 版本/编译选项差异**：某些硬件后端需要特定编译选项或 DLL（例如 NVDEC），缓解：在文档中列出所需 FFmpeg 配置并在 CI 中做早期验证
- **平台差异**：Windows D3D 与 Linux VAAPI/DRM 差异大，首版先实现跨平台安全的 Phase A，再做平台特化
- **SDL 与原生渲染互操作复杂**：若互操作成本高，选择自实现渲染器替代或使用拷贝方案作为 fallback
- **测试环境不足**：需要尽可能在多硬件上验证，缓解：列出测试矩阵并在 PR 中要求提交 bench 数据

---

## 变更清单（初始 PR 列表建议）

为了便于代码审查与回滚，建议将改动拆分成如下 PR：

1. `pr/ui-fullscreen-sdl-resize` — 修复全屏渲染（小改动，快速合并）
2. `pr/blocking-queue` — 增加 `BlockingQueue` 并替换关键 busy-wait 调用
3. `pr/tracy-instrumentation` — 引入 Tracy 并在关键路径添加 Zone 标签（不改变逻辑）
4. `pr/audio-resample-offload` — 解码线程重采样改造与 AudioOutput 回调简化
5. `pr/avsync-improve` — AVSyncController 更新策略改进
6. `pr/error-result` — 新增 `Result`/`ErrorCode` 框架并在 1-2 个模块试点
7. `pr/hwdecode-phase-a` — FFmpeg 硬件解码（Phase A）+ SDL 上传实现
8. `pr/perf-report` — 基准测试数据采集、报告与 README 中的性能结果

每个 PR 应包含：变更说明、测试步骤、Tracy 跟踪截图（若有）、性能报告（CSV/图片）

---

## 附录：快速命令与调试指南

- 启动 Tracy server（本地）并连接客户端：

```bash
# 在本地下载 tracy (server)
# 运行 Tracy UI (tracy) 程序，客户端将在运行时自动连接到 UI
./tracy
```

- 运行播放器并产生日志文件

```bash
# build/debug 假设 binary 在 build/Debug/zenplay
# 在 debug 模式下运行以记录更详细日志
./build/Debug/zenplay 2>&1 | tee zenplay_run.log
```

- 捕获基准数据（示例）

```bash
# 使用 perf 或 top 进行采样（Linux）
perf stat -p <pid> -d sleep 30
# 或使用 psrecord 记录 CPU/Memory 曲线
psrecord <pid> --interval 1 --duration 30 --log activity.csv
```

---

如果你同意这个执行计划，我可以：

- 基于第一个小 PR（`pr/ui-fullscreen-sdl-resize`）开始实现并提交变更；
- 或同时生成所有需要修改的补丁草案（分 PR）供你审阅；

请选择你希望的下一步（例如："先做 fullscreen 修复并提交 PR" 或 "先做 BlockingQueue 和 Tracy 集成"），我会立刻开始并在每个子任务完成后提供数据与变更清单。 

---

文档结束。
