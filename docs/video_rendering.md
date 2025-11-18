# 视频渲染架构 (Video Rendering Architecture)

> **文档版本**: 1.0  
> **最后更新**: 2025-11-18  
> **相关文档**: [架构总览](architecture_overview.md) · [核心组件](core_components.md) · [零拷贝渲染](zero_copy_rendering.md) · [渲染路径选择器](render_path_selector.md)

---

## 目录

1. [设计概览](#1-设计概览)
2. [Renderer 接口](#2-renderer-接口)
3. [RendererProxy 代理](#3-rendererproxy-代理)
4. [SDL 渲染器](#4-sdl-渲染器)
5. [D3D11 渲染器](#5-d3d11-渲染器)
6. [渲染流程](#6-渲染流程)
7. [性能优化](#7-性能优化)
8. [平台差异](#8-平台差异)

---

## 1. 设计概览

### 1.1 渲染架构分层

```
                      ┌─────────────────────────┐
                      │   VideoPlayer           │
                      │  (渲染线程控制)         │
                      └────────┬────────────────┘
                               │
                               v
                      ┌─────────────────────────┐
                      │   RendererProxy         │
                      │  (线程安全代理)         │
                      └────────┬────────────────┘
                               │
                     ┌─────────┴─────────┐
                     │                   │
                     v                   v
            ┌──────────────────┐ ┌──────────────────┐
            │  SDLRenderer     │ │  D3D11Renderer   │
            │ (软件/硬件加速)  │ │ (硬件零拷贝)     │
            └──────────────────┘ └──────────────────┘
                     │                   │
                     v                   v
            ┌──────────────────┐ ┌──────────────────┐
            │  SDL2 API        │ │  Direct3D 11     │
            │  (跨平台)        │ │  (Windows)       │
            └──────────────────┘ └──────────────────┘
```

### 1.2 核心设计原则

#### **原则 1: 接口统一**

```
所有渲染器实现统一的 Renderer 接口：
- Init(): 初始化渲染器
- RenderFrame(): 渲染一帧
- Clear(): 清空渲染目标
- Present(): 呈现到屏幕
- OnResize(): 处理窗口大小变化
- Cleanup(): 清理资源
```

**好处**:
```
✅ 外部代码无需关心具体实现
✅ 易于添加新的渲染器（OpenGL、Vulkan 等）
✅ 便于测试和 Mock
```

#### **原则 2: 线程安全**

```
RendererProxy 确保所有渲染操作在正确的线程执行：
- SDL2 要求所有操作在主线程（loki::ID::UI）
- D3D11 渲染器也需要在 UI 线程创建资源
- 渲染线程（VideoRenderThread）可能不在 UI 线程

RendererProxy 自动派发到 UI 线程，外部无需关心
```

#### **原则 3: 性能优先**

```
两种渲染路径：

高性能（D3D11 零拷贝）:
- 硬件解码 + 硬件渲染共享 GPU 设备
- 无 CPU 拷贝，纹理直接访问
- SRV 缓存池优化（避免重复创建）

标准性能（SDL 软件/硬件）:
- 支持软件解码
- SDL 自动选择硬件加速纹理
- YUV → RGB 转换在 GPU（如果支持）
```

---

## 2. Renderer 接口

### 2.1 接口定义

```cpp
class Renderer {
 public:
  virtual ~Renderer() = default;

  // 初始化渲染器（传入窗口句柄和视频尺寸）
  virtual Result<void> Init(void* window_handle, int width, int height) = 0;

  // 渲染一帧（AVFrame 可能是软件帧或硬件帧）
  virtual bool RenderFrame(AVFrame* frame) = 0;

  // 清空渲染目标（通常填充黑色）
  virtual void Clear() = 0;

  // 呈现渲染结果到屏幕（交换缓冲区）
  virtual void Present() = 0;

  // 处理窗口大小变化
  virtual void OnResize(int width, int height) = 0;

  // 清理所有资源
  virtual void Cleanup() = 0;

  // 获取渲染器名称（用于日志和调试）
  virtual const char* GetRendererName() const = 0;

  // 清空缓存（Seek 时调用，防止野指针）
  virtual void ClearCaches() = 0;
};
```

### 2.2 接口设计要点

#### **Init() - 初始化**

```
参数:
  - window_handle: Qt 窗口的原生句柄（HWND on Windows）
  - width, height: 视频分辨率（用于创建纹理/交换链）

返回:
  - Result<void>: 成功或失败（携带错误信息）

职责:
  1. 创建渲染上下文（SDL Renderer / D3D11 Device）
  2. 创建交换链/窗口关联
  3. 初始化着色器（D3D11）
  4. 验证硬件能力
```

#### **RenderFrame() - 渲染帧**

```
参数:
  - frame: AVFrame 指针（可能是软件帧或硬件帧）

返回:
  - bool: 成功 true / 失败 false

职责:
  1. 验证帧格式（软件 YUV / 硬件 D3D11）
  2. 创建或更新纹理
  3. 执行渲染命令（绘制四边形）
  4. 自动调用 Clear() 和 Present()
```

**关键**: RenderFrame() 是"一站式"接口，外部只需调用它

#### **ClearCaches() - 清空缓存**

```
场景: Seek 操作

问题:
  - Seek 时 FFmpeg 释放旧的硬件纹理
  - 渲染器可能缓存了指向旧纹理的 SRV
  - 新纹理恰好重用内存地址 → 野指针命中 → 崩溃

解决:
  - Seek 前调用 ClearCaches()
  - 释放所有缓存的 SRV
  - 下次 RenderFrame() 重新创建
```

**调用时机**:
```cpp
// VideoPlayer::PreSeek()
if (renderer_) {
  renderer_->ClearCaches();  // ← 关键！
}
```

---

## 3. RendererProxy 代理

### 3.1 设计目的

**问题**: SDL2 和 D3D11 都要求在特定线程执行

```
SDL2 要求:
  - 所有 SDL 操作必须在主线程（创建 SDL_Init 的线程）
  - ZenPlay 使用 loki::ID::UI 作为主线程

D3D11 要求:
  - 设备创建、资源绑定最好在同一个线程
  - 避免多线程竞争

VideoRenderThread:
  - 独立的渲染线程，可能不在 UI 线程
```

**解决**: RendererProxy 自动派发到 UI 线程

```cpp
class RendererProxy : public Renderer {
 public:
  explicit RendererProxy(std::unique_ptr<Renderer> actual_renderer);

  // 所有接口都包装为线程安全
  Result<void> Init(void* window_handle, int width, int height) override {
    return EnsureUIThread<Result<void>>([&]() {
      return actual_renderer_->Init(window_handle, width, height);
    });
  }

  bool RenderFrame(AVFrame* frame) override {
    return EnsureUIThread<bool>([&]() {
      return actual_renderer_->RenderFrame(frame);
    });
  }
  
  // ... 其他方法类似
};
```

### 3.2 线程派发实现

```cpp
template <typename ReturnT, typename Func>
ReturnT RendererProxy::EnsureUIThread(Func&& func) {
  // 如果当前就在 UI 线程，直接执行（避免开销）
  if (loki::LokiThread::CurrentlyOn(loki::ID::UI)) {
    return func();
  }

  // 否则同步派发到 UI 线程（阻塞等待结果）
  return loki::Invoke<ReturnT>(
      loki::ID::UI, FROM_HERE,
      loki::FunctionView<ReturnT()>(std::forward<Func>(func)));
}
```

**关键特性**:
```
✅ 同步执行: Invoke() 阻塞直到 UI 线程执行完成
✅ 返回值透传: 支持任意返回类型
✅ 性能优化: 如果已在 UI 线程，直接执行无派发开销
✅ 异常安全: loki::Invoke 处理异常传播
```

### 3.3 使用示例

```cpp
// ZenPlayer 中创建渲染器
auto sdl_renderer = std::make_unique<SDLRenderer>();
renderer_ = std::make_unique<RendererProxy>(std::move(sdl_renderer));
// ↑ 包装后，外部无需关心线程问题

// VideoRenderThread 中调用（可能不在 UI 线程）
renderer_->RenderFrame(video_frame->frame.get());
// ↑ RendererProxy 自动派发到 UI 线程执行
```

---

## 4. SDL 渲染器

### 4.1 设计特点

```
跨平台软件渲染器：
- 支持 Windows、Linux、macOS
- 自动选择硬件加速纹理（如果可用）
- 内置格式转换（通过 libswscale）
- 后备方案，保证兼容性
```

### 4.2 初始化流程

```
Init(window_handle, width, height)
    |
    v
1. 初始化 SDL 子系统（VIDEO）
    |
    v
2. 从窗口句柄创建 SDL_Window
   - Windows: SDL_CreateWindowFrom(HWND)
   - Linux: SDL_CreateWindowFrom(X11 Window)
    |
    v
3. 创建 SDL_Renderer
   - 优先尝试硬件加速（SDL_RENDERER_ACCELERATED）
   - 失败则回退软件渲染（SDL_RENDERER_SOFTWARE）
    |
    v
4. 设置渲染器属性
   - 启用 VSync（SDL_RENDERER_PRESENTVSYNC）
   - 设置缩放质量（linear filtering）
```

### 4.3 纹理管理

#### **动态纹理创建**

```cpp
bool SDLRenderer::CreateTexture(int width, int height, int format) {
  // 销毁旧纹理
  if (texture_) {
    SDL_DestroyTexture(texture_);
    texture_ = nullptr;
  }

  // 确定 SDL 像素格式
  Uint32 sdl_format = SDL_PIXELFORMAT_IYUV;  // YUV420P (默认)
  
  if (format == AV_PIX_FMT_NV12) {
    sdl_format = SDL_PIXELFORMAT_NV12;
  }
  // ... 其他格式

  // 创建流式纹理（可动态更新）
  texture_ = SDL_CreateTexture(
      renderer_,
      sdl_format,
      SDL_TEXTUREACCESS_STREAMING,  // ← 关键：允许 UpdateTexture
      width,
      height
  );

  frame_width_ = width;
  frame_height_ = height;
  src_pixel_format_ = static_cast<AVPixelFormat>(format);
  
  return texture_ != nullptr;
}
```

**为什么使用 STREAMING 纹理？**
```
SDL_TEXTUREACCESS_STREAMING:
  - 可以频繁更新（每帧一次）
  - CPU 可写入，GPU 可读取
  - 适合视频播放

SDL_TEXTUREACCESS_STATIC:
  - 创建后不可修改
  - 适合静态图片

SDL_TEXTUREACCESS_TARGET:
  - 可作为渲染目标
  - 适合离屏渲染
```

#### **纹理更新**

```cpp
bool SDLRenderer::UpdateTexture(AVFrame* frame) {
  // 情况 1: 格式直接支持（YUV420P, NV12 等）
  if (IsFormatNativelySupported(frame->format)) {
    return SDL_UpdateYUVTexture(
        texture_,
        nullptr,  // 更新整个纹理
        frame->data[0], frame->linesize[0],  // Y 平面
        frame->data[1], frame->linesize[1],  // U 平面
        frame->data[2], frame->linesize[2]   // V 平面
    ) == 0;
  }

  // 情况 2: 需要格式转换（RGB, BGRA 等）
  return UpdateTextureWithConversion(frame);
}
```

#### **格式转换**

```cpp
bool SDLRenderer::UpdateTextureWithConversion(AVFrame* frame) {
  // 创建或重用 SwsContext（libswscale 转换上下文）
  if (!sws_context_) {
    sws_context_ = sws_getContext(
        frame->width, frame->height, 
        static_cast<AVPixelFormat>(frame->format),  // 源格式
        frame->width, frame->height, 
        dst_pixel_format_,  // 目标格式（YUV420P）
        SWS_BILINEAR,  // 缩放算法
        nullptr, nullptr, nullptr
    );
  }

  // 分配转换后的缓冲区
  if (!converted_frame_) {
    converted_frame_ = av_frame_alloc();
    converted_buffer_size_ = av_image_get_buffer_size(
        dst_pixel_format_, frame->width, frame->height, 1);
    converted_buffer_ = (uint8_t*)av_malloc(converted_buffer_size_);
    
    av_image_fill_arrays(
        converted_frame_->data, converted_frame_->linesize,
        converted_buffer_, dst_pixel_format_,
        frame->width, frame->height, 1
    );
  }

  // 执行格式转换（CPU）
  sws_scale(
      sws_context_,
      frame->data, frame->linesize, 0, frame->height,
      converted_frame_->data, converted_frame_->linesize
  );

  // 更新纹理
  return SDL_UpdateYUVTexture(
      texture_, nullptr,
      converted_frame_->data[0], converted_frame_->linesize[0],
      converted_frame_->data[1], converted_frame_->linesize[1],
      converted_frame_->data[2], converted_frame_->linesize[2]
  ) == 0;
}
```

### 4.4 渲染流程

```cpp
bool SDLRenderer::RenderFrame(AVFrame* frame) {
  // 1. 检查纹理是否需要重建（分辨率或格式变化）
  if (frame_width_ != frame->width || 
      frame_height_ != frame->height ||
      src_pixel_format_ != frame->format) {
    CreateTexture(frame->width, frame->height, frame->format);
  }

  // 2. 更新纹理数据
  if (!UpdateTexture(frame)) {
    return false;
  }

  // 3. 清空渲染目标（黑色背景）
  Clear();

  // 4. 计算显示矩形（保持纵横比）
  SDL_Rect display_rect = CalculateDisplayRect(frame_width_, frame_height_);

  // 5. 渲染纹理到目标矩形
  SDL_RenderCopy(renderer_, texture_, nullptr, &display_rect);

  // 6. 呈现到屏幕
  Present();

  return true;
}
```

#### **纵横比保持**

```cpp
SDL_Rect SDLRenderer::CalculateDisplayRect(int frame_width, int frame_height) {
  float frame_aspect = (float)frame_width / frame_height;
  float window_aspect = (float)window_width_ / window_height_;

  SDL_Rect rect;

  if (frame_aspect > window_aspect) {
    // 视频更宽，以宽度为准
    rect.w = window_width_;
    rect.h = (int)(window_width_ / frame_aspect);
    rect.x = 0;
    rect.y = (window_height_ - rect.h) / 2;  // 垂直居中
  } else {
    // 视频更高，以高度为准
    rect.h = window_height_;
    rect.w = (int)(window_height_ * frame_aspect);
    rect.x = (window_width_ - rect.w) / 2;  // 水平居中
    rect.y = 0;
  }

  return rect;
}
```

### 4.5 性能特性

| 特性 | 说明 | 性能影响 |
|------|------|----------|
| **硬件加速纹理** | SDL 自动选择 GPU 纹理 | ✅ GPU YUV→RGB 转换 |
| **格式转换** | libswscale CPU 转换 | ⚠️ 5-10ms/帧（1080p） |
| **纹理上传** | CPU → GPU 拷贝 | ⚠️ 3-5ms/帧（1080p） |
| **跨平台** | Windows/Linux/macOS | ✅ 统一代码 |

**适用场景**:
```
✅ 软件解码 + 标准渲染
✅ 老硬件不支持 D3D11
✅ 虚拟机环境
✅ Linux/macOS（D3D11 不可用）
```

---

## 5. D3D11 渲染器

### 5.1 零拷贝架构

```
硬件解码器                   D3D11 渲染器
┌──────────────┐            ┌──────────────┐
│ D3D11VA      │            │ D3D11Renderer│
│ Decoder      │            │              │
└──────┬───────┘            └──────┬───────┘
       │                           │
       │  共享 ID3D11Device        │
       └───────────┬───────────────┘
                   │
                   v
          ┌─────────────────┐
          │ ID3D11Device    │ (共享)
          │ ID3D11Context   │
          └─────────────────┘
                   │
                   v
          ┌─────────────────┐
          │ D3D11Texture2D  │ (NV12 格式)
          │ (解码输出)      │
          └─────────────────┘
                   │
                   │ 直接访问（无拷贝）
                   v
          ┌─────────────────┐
          │ ShaderResource  │
          │ View (SRV)      │
          └─────────────────┘
                   │
                   v
          ┌─────────────────┐
          │ Pixel Shader    │ (YUV→RGB)
          └─────────────────┘
                   │
                   v
          ┌─────────────────┐
          │ Render Target   │
          │ (交换链后台缓冲) │
          └─────────────────┘
```

### 5.2 初始化流程

```cpp
Result<void> D3D11Renderer::Init(void* window_handle, int width, int height) {
  // 1. 初始化 D3D11 设备上下文
  //    - 如果有 shared_device_（来自解码器），复用它
  //    - 否则创建新设备
  auto context_result = d3d11_context_->Initialize(shared_device_);

  // 2. 初始化着色器（YUV→RGB 转换）
  ID3D11Device* device = d3d11_context_->GetDevice();
  auto shader_result = shader_->Initialize(device);

  // 3. 创建交换链（绑定到窗口）
  auto swap_chain_result = 
      swap_chain_->Initialize(device, window_handle, width, height);

  initialized_ = true;
  return Result<void>::Ok();
}
```

**共享设备验证**:
```cpp
// 设置共享设备（在 Init() 之前调用）
void D3D11Renderer::SetSharedD3D11Device(ID3D11Device* device) {
  shared_device_ = device;
  MODULE_INFO("Shared D3D11 device set: {}", (void*)device);
}

// 在 CreateShaderResourceViews 中验证
Microsoft::WRL::ComPtr<ID3D11Device> texture_device;
texture->GetDevice(texture_device.GetAddressOf());

if (texture_device.Get() != device) {
  MODULE_ERROR("❌ Device mismatch! Zero-copy failed!");
  return Err("D3D11 device mismatch");
}
```

### 5.3 SRV 缓存池

**问题**: FFmpeg 使用纹理池（4-16 个纹理循环使用）

```
FFmpeg 纹理池:
  texture_0 → decode → present → reuse
  texture_1 → decode → present → reuse
  texture_2 → decode → present → reuse
  ...
  texture_15 → decode → present → reuse

如果每次 RenderFrame() 都创建 SRV:
  - CreateShaderResourceView() 开销 ~0.5ms
  - 30fps × 0.5ms = 15ms/秒浪费
  - 完全可以缓存复用！
```

**解决**: SRV 缓存池

```cpp
struct SRVCache {
  ID3D11Texture2D* texture;    // 纹理指针（作为 key）
  UINT array_slice;             // 数组索引（NV12 可能是纹理数组）
  ComPtr<ID3D11ShaderResourceView> y_srv;   // Y 平面 SRV
  ComPtr<ID3D11ShaderResourceView> uv_srv;  // UV 平面 SRV
};

std::vector<SRVCache> srv_pool_;  // 缓存池
```

**查找逻辑**:
```cpp
Result<void> D3D11Renderer::CreateShaderResourceViews(AVFrame* frame) {
  ID3D11Texture2D* texture = (ID3D11Texture2D*)frame->data[0];
  UINT array_slice = (UINT)(uintptr_t)frame->data[1];

  // 1. 在池中查找
  for (auto& cache : srv_pool_) {
    if (cache.texture == texture && cache.array_slice == array_slice) {
      // 缓存命中！
      srv_cache_hits_++;
      y_srv_ = cache.y_srv;
      uv_srv_ = cache.uv_srv;
      return Ok();
    }
  }

  // 2. 缓存未命中，创建新 SRV
  srv_cache_misses_++;
  
  SRVCache new_cache;
  new_cache.texture = texture;
  new_cache.array_slice = array_slice;
  
  // 创建 Y 平面 SRV (R8_UNORM)
  device->CreateShaderResourceView(texture, &y_srv_desc, &new_cache.y_srv);
  
  // 创建 UV 平面 SRV (R8G8_UNORM)
  device->CreateShaderResourceView(texture, &uv_srv_desc, &new_cache.uv_srv);
  
  // 添加到池
  srv_pool_.push_back(std::move(new_cache));
  
  return Ok();
}
```

**性能统计**:
```
每 100 次命中输出统计:
📊 SRV Pool: 95 hits, 5 misses, pool size: 5 (95.0% hit rate)

典型池大小: 4-8 个 SRV 缓存（对应 FFmpeg 纹理池）
命中率: 95%+ （稳定后几乎全部命中）
节省: ~0.5ms × 28帧/秒 = 14ms/秒
```

### 5.4 YUV → RGB 转换

**像素着色器**（GPU 执行）:

```hlsl
// Y 平面纹理（亮度）
Texture2D<float> yTexture : register(t0);

// UV 平面纹理（色度，NV12 格式）
Texture2D<float2> uvTexture : register(t1);

SamplerState samplerState : register(s0);

float4 PSMain(PSInput input) : SV_TARGET {
  // 1. 采样 Y 值（亮度）
  float y = yTexture.Sample(samplerState, input.texCoord);
  
  // 2. 采样 UV 值（色度）
  float2 uv = uvTexture.Sample(samplerState, input.texCoord);
  
  // 3. YUV → RGB 转换（BT.709 标准）
  float u = uv.x - 0.5;
  float v = uv.y - 0.5;
  
  float r = y + 1.5748 * v;
  float g = y - 0.1873 * u - 0.4681 * v;
  float b = y + 1.8556 * u;
  
  return float4(r, g, b, 1.0);
}
```

**为什么在 GPU 转换？**
```
CPU 转换（软件解码）:
  - libswscale: 10-15ms/帧（1080p）
  - 占用 CPU 资源

GPU 转换（像素着色器）:
  - <1ms/帧（并行处理）
  - 零 CPU 开销
  - 与渲染管线融合
```

### 5.5 渲染流程

```cpp
bool D3D11Renderer::RenderFrame(AVFrame* frame) {
  // 1. 验证帧格式
  if (frame->format != AV_PIX_FMT_D3D11) {
    MODULE_ERROR("Frame format is not D3D11");
    return false;
  }

  // 2. 提取 D3D11 纹理（零拷贝关键）
  ID3D11Texture2D* decoded_texture = (ID3D11Texture2D*)frame->data[0];

  // 3. 创建或复用 SRV
  auto srv_result = CreateShaderResourceViews(frame);

  // 4. 清空渲染目标
  Clear();

  // 5. 渲染全屏四边形
  RenderQuad();

  // 6. 呈现到屏幕
  Present();

  return true;
}
```

**RenderQuad() 细节**:
```cpp
Result<void> D3D11Renderer::RenderQuad() {
  ID3D11DeviceContext* ctx = d3d11_context_->GetDeviceContext();

  // 1. 设置渲染目标（交换链后台缓冲）
  ID3D11RenderTargetView* rtv = swap_chain_->GetRenderTargetView();
  ctx->OMSetRenderTargets(1, &rtv, nullptr);

  // 2. 设置视口
  D3D11_VIEWPORT viewport = {0, 0, width_, height_, 0.0f, 1.0f};
  ctx->RSSetViewports(1, &viewport);

  // 3. 应用着色器
  shader_->Apply(ctx);

  // 4. 绑定 YUV 纹理
  shader_->SetYUVTextures(ctx, y_srv_.Get(), uv_srv_.Get());

  // 5. 绘制（4 个顶点，三角形带）
  //    顶点着色器使用 SV_VertexID 自动生成位置，无需顶点缓冲
  ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  ctx->Draw(4, 0);

  return Ok();
}
```

### 5.6 性能特性

| 特性 | 说明 | 性能影响 |
|------|------|----------|
| **零拷贝** | GPU 纹理直接访问 | ✅ 节省 20-30ms/帧 |
| **SRV 缓存** | 复用 ShaderResourceView | ✅ 节省 0.5ms/帧 |
| **GPU YUV→RGB** | 像素着色器转换 | ✅ <1ms/帧 |
| **硬件加速** | Direct3D 11 | ✅ 总 CPU 5-10% |

**适用场景**:
```
✅ Windows 平台
✅ D3D11VA 硬件解码
✅ 现代显卡（支持 D3D11）
✅ 高性能要求（4K、多实例）
```

---

## 6. 渲染流程

### 6.1 完整流程图

```
VideoPlayer::VideoRenderThread()
    |
    v
从队列获取 VideoFrame
    |
    v
计算显示时间（CalculateFrameDisplayTime）
    |
    v
检查是否需要丢帧（ShouldDropFrame）
    |
    ├─> 需要丢帧 → continue（跳过此帧）
    │
    └─> 不丢帧
        |
        v
等待到目标显示时间（sleep_until）
        |
        v
调用 renderer_->RenderFrame(frame)
        |
        v
    ┌───────────────────────┐
    │   RendererProxy       │
    │  (检查当前线程)       │
    └───────┬───────────────┘
            │
    ┌───────┴────────┐
    │                │
在 UI 线程        不在 UI 线程
    │                │
    v                v
直接执行        loki::Invoke 派发到 UI 线程
    │                │
    └───────┬────────┘
            │
            v
    ┌────────────────────┐
    │ SDLRenderer /      │
    │ D3D11Renderer      │
    └────────┬───────────┘
             │
             v
    ┌────────────────────┐
    │ 1. 验证帧格式      │
    │ 2. 创建/更新纹理   │
    │ 3. 清空渲染目标    │
    │ 4. 渲染四边形      │
    │ 5. 呈现到屏幕      │
    └────────────────────┘
```

### 6.2 时序图

```
时间轴 →
VideoRenderThread    RendererProxy    UI Thread    SDL/D3D11
      |                   |               |            |
      |-- GetFrame() ---->|               |            |
      |<-- VideoFrame ----                |            |
      |                   |               |            |
      |-- RenderFrame --->|               |            |
      |                   |-- Invoke ---->|            |
      |                   |               |-- Init --->|
      |                   |               |<- Ok ------
      |                   |               |-- Render ->|
      |                   |               |            |-- GPU
      |                   |               |<- Done ----   (异步)
      |                   |<-- Done ------              |
      |<-- Ok ------------                              |
      |                                                 |
      |-- UpdateClock -->                              |
      |                                                 |
      |-- Sleep(16ms) ---------------------------------|
      |                                                 |
      |-- NextFrame -----------------------------------|
```

### 6.3 关键时间点

```
t0: 从队列获取帧
  ↓
t1: 计算目标显示时间（基于 PTS 和主时钟）
  ↓
t2: 等待到目标时间（sleep_until）
  ↓
t3: 开始渲染（RenderFrame 调用）
  ↓
t4: 纹理更新完成
  ↓
t5: GPU 渲染提交
  ↓
t6: Present() 返回（可能等待 VSync）
  ↓
t7: 更新视频时钟

理想延迟: t7 - t0 ≈ 2-5ms (D3D11) / 10-20ms (SDL)
```

---

## 7. 性能优化

### 7.1 渲染性能对比

| 渲染器 | 解码类型 | CPU 占用 | 渲染延迟 | 内存拷贝 | 适用场景 |
|--------|---------|---------|---------|---------|----------|
| **D3D11** | D3D11VA 硬件 | 5-10% | 2-5ms | 零拷贝 | Windows 高性能 |
| **SDL硬件加速** | 软件解码 | 15-20% | 10-15ms | 1 次拷贝 | 跨平台标准 |
| **SDL软件渲染** | 软件解码 | 25-30% | 15-20ms | 1 次拷贝 | 兼容性最大 |

### 7.2 优化技术

#### **1. 纹理池复用**

```
问题: 每帧创建/销毁纹理开销大

解决: 
  - FFmpeg 使用纹理池（4-16 个纹理）
  - 渲染器缓存对应的 SRV
  - 纹理指针作为 key，快速查找

效果: 
  - 首次: 创建 SRV（0.5ms）
  - 后续: 缓存命中（0.01ms）
  - 节省: 95%+ 时间
```

#### **2. 格式预检查**

```cpp
// 渲染前检查格式，避免重复转换
if (frame_width_ != frame->width || 
    frame_height_ != frame->height ||
    src_pixel_format_ != frame->format) {
  // 只在格式变化时重建纹理
  CreateTexture(frame->width, frame->height, frame->format);
}
```

#### **3. VSync 优化**

```
启用 VSync:
  - 避免画面撕裂
  - 减少 GPU 负载（限制帧率）
  - Present() 阻塞等待刷新周期

禁用 VSync:
  - 低延迟（立即 Present）
  - 可能撕裂
  - GPU 满负荷运行
```

**配置**:
```json
{
  "render": {
    "vsync": true,  // 推荐启用
    "max_fps": 60
  }
}
```

#### **4. 异步渲染**

```
当前: 同步渲染（Present() 阻塞）

优化方向:
  - 渲染线程提交 GPU 命令后立即返回
  - GPU 异步执行
  - CPU 继续处理下一帧

挑战:
  - 需要管理多个渲染缓冲
  - 同步机制复杂
```

---

## 8. 平台差异

### 8.1 Windows 平台

**可用渲染器**:
- ✅ D3D11Renderer（零拷贝，推荐）
- ✅ SDLRenderer（兼容）

**特性**:
```
D3D11 零拷贝:
  - D3D11VA 硬件解码 + D3D11 渲染
  - 共享 ID3D11Device
  - 最佳性能

SDL 硬件加速:
  - 使用 DirectX 后端（自动选择）
  - YUV → RGB 在 GPU
  - 需要纹理上传（1 次拷贝）
```

### 8.2 Linux 平台

**可用渲染器**:
- ✅ SDLRenderer（主要选择）
- ⚠️ VaapiRenderer（计划中）

**特性**:
```
SDL 硬件加速:
  - 使用 OpenGL 后端
  - YUV → RGB 通过 shader
  - 性能良好

VA-API 零拷贝（计划）:
  - VAAPI 硬件解码 + VA-API 渲染
  - 共享 VADisplay
  - 类似 D3D11 零拷贝
```

### 8.3 macOS 平台

**可用渲染器**:
- ✅ SDLRenderer（主要选择）
- ⚠️ MetalRenderer（计划中）

**特性**:
```
SDL 硬件加速:
  - 使用 Metal 后端
  - YUV → RGB 通过 Metal shader
  - 性能优秀

Metal 零拷贝（计划）:
  - VideoToolbox 硬件解码 + Metal 渲染
  - 共享 CVPixelBuffer
  - 原生 macOS 加速
```

---

## 总结

### 核心设计理念

1. **接口统一**: Renderer 抽象层，易于扩展
2. **线程安全**: RendererProxy 自动处理线程派发
3. **性能优先**: D3D11 零拷贝 + SRV 缓存池
4. **跨平台**: SDL 保证兼容性
5. **灵活配置**: 支持软件/硬件渲染切换

### 性能最佳实践

```
高性能场景（Windows）:
  → D3D11VA 解码 + D3D11 渲染
  → 零拷贝 + SRV 缓存
  → 总 CPU 5-10%

标准场景（跨平台）:
  → 软件解码 + SDL 硬件加速纹理
  → 1 次拷贝 + GPU YUV→RGB
  → 总 CPU 15-25%

兼容场景（老硬件）:
  → 软件解码 + SDL 软件渲染
  → 1 次拷贝 + CPU YUV→RGB
  → 总 CPU 25-35%
```

### 推荐阅读顺序

1. 先理解本文档的渲染架构
2. 深入 [零拷贝渲染详解](zero_copy_rendering.md) 了解 D3D11 优化
3. 参考 [渲染路径选择器](render_path_selector.md) 了解如何选择渲染器
4. 阅读 [硬件加速详解](hardware_acceleration.md) 理解解码与渲染协同

---

**文档维护**: 如有疑问或发现不一致，请参考源码 `src/player/video/render/` 或提出 Issue。
