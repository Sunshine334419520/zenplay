# 渲染路径选择器设计 (Render Path Selector Design)

> **文档版本**: 1.0  
> **最后更新**: 2025-11-18  
> **相关文档**: [架构总览](architecture_overview.md) · [核心组件](core_components.md) · [硬件加速详解](hardware_acceleration.md)

---

## 目录

1. [设计目标](#1-设计目标)
2. [架构设计](#2-架构设计)
3. [选择策略](#3-选择策略)
4. [平台支持](#4-平台支持)
5. [配置系统](#5-配置系统)
6. [回退机制](#6-回退机制)
7. [零拷贝优化](#7-零拷贝优化)
8. [使用示例](#8-使用示例)

---

## 1. 设计目标

### 1.1 核心目标

`RenderPathSelector` 是 ZenPlay 中负责智能选择渲染路径的核心组件，其设计目标是：

```
✅ 平台无关: 统一的接口，跨 Windows/Linux/macOS
✅ 配置驱动: 通过 GlobalConfig 灵活控制渲染策略
✅ 智能选择: 自动检测硬件能力，选择最佳渲染方案
✅ 自动回退: 硬件失败时自动降级到软件渲染
✅ 零拷贝优化: 硬件解码与渲染共享 GPU 设备
```

### 1.2 解决的问题

#### **问题 1: 渲染器创建复杂**

```
不同平台、不同硬件能力需要创建不同的渲染器：
- Windows: D3D11Renderer (硬件) vs SDLRenderer (软件)
- Linux: VaapiRenderer (硬件) vs SDLRenderer (软件)
- macOS: MetalRenderer (硬件) vs SDLRenderer (软件)

需要统一的选择逻辑，避免重复代码
```

#### **问题 2: 硬件解码与渲染协同**

```
零拷贝渲染要求：
- 硬件解码器和渲染器共享同一个 GPU 设备
- 例如: D3D11VA 解码 + D3D11 渲染共享 ID3D11Device

如果分别创建，无法实现零拷贝
```

#### **问题 3: 配置与能力匹配**

```
用户配置可能与硬件能力不匹配：
- 配置要求硬件加速，但硬件不支持 → 需要回退或报错
- 配置禁用硬件加速，即使硬件支持 → 使用软件渲染
```

### 1.3 设计原则

#### **原则 1: 职责单一**

```
RenderPathSelector 只负责选择和创建：
- 不负责渲染（由 Renderer 负责）
- 不负责解码（由 Decoder 负责）
- 不负责配置管理（由 GlobalConfig 负责）
```

#### **原则 2: 一次性决策**

```
渲染路径在播放器 Open() 时决定，整个播放周期不再改变
- 避免运行时切换渲染器的复杂性
- 简化资源管理（一次创建，统一释放）
```

#### **原则 3: 详细日志**

```
选择过程输出详细日志：
- 检测到的硬件能力
- 读取的配置项
- 做出的决策及原因
- 回退的原因（如果发生）

便于调试和用户理解
```

---

## 2. 架构设计

### 2.1 核心结构

#### **RenderPathSelection 结果类**

```cpp
struct RenderPathSelection {
  std::unique_ptr<Renderer> renderer;           // 渲染器实例（已包装为 RendererProxy）
  std::unique_ptr<HWDecoderContext> hw_context; // 硬件解码上下文（可选）
  HWDecoderType hw_decoder;                     // 硬件解码器类型
  std::string backend_name;                     // 渲染后端名称（"D3D11", "SDL" 等）
  std::string reason;                           // 选择原因（日志）
  bool is_hardware;                             // 是否硬件加速
};
```

**字段说明**:

| 字段 | 类型 | 说明 |
|------|------|------|
| `renderer` | `unique_ptr<Renderer>` | 已创建的渲染器，包装为 `RendererProxy` |
| `hw_context` | `unique_ptr<HWDecoderContext>` | 硬件解码上下文，软件渲染时为 `nullptr` |
| `hw_decoder` | `HWDecoderType` | 推荐的硬件解码器类型（`kD3D11VA`, `kNone` 等） |
| `backend_name` | `string` | 渲染后端名称，用于日志和统计 |
| `reason` | `string` | 选择原因，便于调试 |
| `is_hardware` | `bool` | 是否使用硬件加速 |

#### **RenderPathSelector 静态类**

```cpp
class RenderPathSelector {
 public:
  // 主接口：选择并创建最佳渲染路径
  static RenderPathSelection Select(AVCodecID codec_id,
                                    int width, int height,
                                    GlobalConfig* config = nullptr);
  
  // 简化接口：创建默认软件渲染器（无视频流场景）
  static std::unique_ptr<Renderer> CreateDefaultRenderer();
  
 private:
  // 平台相关的选择函数
  static RenderPathSelection SelectForWindows(...);
  static RenderPathSelection SelectForLinux(...);
  static RenderPathSelection SelectForMacOS(...);
  
  // 软件回退
  static RenderPathSelection SelectSoftwareFallback(const std::string& reason);
  
  // 辅助函数
  static bool IsHardwareAccelerationEnabled(GlobalConfig* config);
  static bool IsFallbackAllowed(GlobalConfig* config);
};
```

**为什么使用静态类？**
```
✅ 无状态：选择逻辑不需要保存状态
✅ 工具性质：提供纯工具函数，无需实例化
✅ 简化调用：ZenPlayer 直接调用，无需管理生命周期
```

### 2.2 调用流程

```
ZenPlayer::Open(url)
    |
    v
InitializeVideoRenderingPipeline()
    |
    v
检查视频流是否存在
    |
    +-- 无视频流 --> RenderPathSelector::CreateDefaultRenderer()
    |                    |
    |                    v
    |               SDLRenderer (软件渲染)
    |
    +-- 有视频流 --> RenderPathSelector::Select(codec_id, width, height)
                         |
                         v
                    读取配置 (GlobalConfig)
                         |
                         v
                    检测硬件能力 (HWDecoderTypeUtil)
                         |
                         v
                    根据平台选择:
                    - SelectForWindows()
                    - SelectForLinux()
                    - SelectForMacOS()
                         |
                         v
                    返回 RenderPathSelection
                    {
                      renderer: RendererProxy(D3D11Renderer),
                      hw_context: HWDecoderContext(D3D11VA),
                      hw_decoder: kD3D11VA,
                      backend_name: "D3D11",
                      is_hardware: true
                    }
                         |
                         v
ZenPlayer 保存结果:
  - renderer_ = selection.renderer
  - hw_decoder_context_ = selection.hw_context
                         |
                         v
video_decoder_->Open(codecpar, hw_decoder_context_)
  ↓
硬件解码器使用共享的 hw_context
```

---

## 3. 选择策略

### 3.1 决策流程

```
               ┌─────────────────────────┐
               │ RenderPathSelector::    │
               │ Select()                │
               └────────┬────────────────┘
                        │
                        v
               ┌────────────────────────┐
               │ 读取配置               │
               │ use_hardware_acceleration?│
               └────────┬───────────────┘
                        │
              ┌─────────┴─────────┐
              │                   │
             YES                 NO
              │                   │
              v                   v
     ┌────────────────┐   ┌──────────────────┐
     │ 检测硬件能力   │   │ SelectSoftware   │
     │ HWDecoderType  │   │ Fallback()       │
     └────┬───────────┘   └──────────────────┘
          │                        │
          v                        v
   ┌──────────────┐         SDLRenderer
   │ 平台分发     │         (软件渲染)
   └──┬───────────┘
      │
      ├──> Windows: SelectForWindows()
      │       │
      │       ├─> D3D11VA 可用?
      │       │     ├─> YES: 创建 D3D11Renderer + HWDecoderContext
      │       │     │        (共享 D3D11 设备，零拷贝)
      │       │     └─> NO:  尝试 DXVA2 或回退
      │       │
      │       └─> 硬件失败 + 允许回退?
      │             └─> SDLRenderer (软件)
      │
      ├──> Linux: SelectForLinux()
      │       │
      │       ├─> VAAPI 可用?
      │       │     └─> 暂未实现 VaapiRenderer，回退 SDL
      │       │
      │       └─> VDPAU 可用?
      │             └─> 暂未实现 VdpauRenderer，回退 SDL
      │
      └──> macOS: SelectForMacOS()
              │
              ├─> VideoToolbox 可用?
              │     └─> 暂未实现 MetalRenderer，回退 SDL
              │
              └─> 回退到 SDLRenderer
```

### 3.2 硬件能力检测

**调用 HWDecoderTypeUtil**:

```cpp
// 获取当前平台推荐的硬件解码器类型（按优先级排序）
auto recommended_types = HWDecoderTypeUtil::GetRecommendedTypes();

// Windows 示例返回: [kD3D11VA, kDXVA2]
// Linux 示例返回: [kVAAPI, kVDPAU]
// macOS 示例返回: [kVideoToolbox]
```

**检测逻辑**:
```
HWDecoderTypeUtil::GetRecommendedTypes() 内部:
1. 遍历 FFmpeg 支持的硬件类型
2. 检查系统是否支持（通过 av_hwdevice_ctx_create）
3. 按优先级排序（D3D11VA > DXVA2, VAAPI > VDPAU）
4. 返回可用列表
```

### 3.3 配置优先级

**配置 vs 硬件能力**:

```
优先级规则（从高到低）:

1. 配置禁用硬件加速
   → 直接使用软件渲染，不检测硬件

2. 配置启用硬件加速 + 硬件可用
   → 使用硬件渲染

3. 配置启用硬件加速 + 硬件不可用 + 允许回退
   → 使用软件渲染（输出警告日志）

4. 配置启用硬件加速 + 硬件不可用 + 禁止回退
   → 返回错误，播放器无法打开
```

---

## 4. 平台支持

### 4.1 Windows 平台

#### **支持的渲染路径**

| 渲染路径 | 硬件解码器 | 渲染器 | 零拷贝 | 状态 |
|----------|-----------|--------|--------|------|
| **D3D11 完整加速** | D3D11VA | D3D11Renderer | ✅ | ✅ 已实现 |
| **DXVA2 加速** | DXVA2 | SDLRenderer | ❌ | ⚠️ 部分实现 |
| **软件渲染** | None | SDLRenderer | N/A | ✅ 已实现 |

#### **D3D11 完整加速路径**

```
特点:
- 硬件解码: D3D11VA (FFmpeg av_hwdevice_type = AV_HWDEVICE_TYPE_D3D11VA)
- 硬件渲染: D3D11Renderer (Direct3D 11)
- 零拷贝: 共享 ID3D11Device，GPU 内存直接访问

实现:
1. 创建 HWDecoderContext，初始化 D3D11VA
2. 获取 ID3D11Device (hw_context->GetD3D11Device())
3. 创建 D3D11Renderer，设置共享设备
4. 解码帧的 D3D11Texture2D 可直接被渲染器使用

性能:
- CPU 占用: 5-10%（1080p H.264）
- 零拷贝节省: 30-50% CPU
```

**配置检查**:
```json
{
  "render": {
    "use_hardware_acceleration": true,
    "hardware": {
      "allow_d3d11va": true  // 允许 D3D11VA
    }
  }
}
```

#### **DXVA2 加速路径**

```
特点:
- 硬件解码: DXVA2 (较老的 Windows 硬件加速 API)
- 硬件渲染: 暂未实现，当前使用 SDLRenderer
- 零拷贝: ❌（需要从 GPU 拷贝到 CPU，再上传到 SDL 纹理）

状态: 部分实现
- DXVA2 解码器可用
- 但没有对应的硬件渲染器
- 性能不如 D3D11 完整加速

未来优化:
- 实现 DXVA2 到 SDL 的零拷贝路径（技术上可行但复杂）
- 或者建议用户使用 D3D11VA
```

#### **软件渲染回退**

```
触发条件:
- 配置禁用硬件加速
- D3D11VA 初始化失败
- DXVA2 不可用且允许回退

使用场景:
- 虚拟机（GPU 虚拟化不完整）
- 旧硬件（不支持 D3D11）
- 调试模式

性能:
- CPU 占用: 15-25%（1080p H.264 软件解码）
- 渲染器: SDL2 硬件加速纹理（非零拷贝，但 GPU 加速 YUV→RGB）
```

### 4.2 Linux 平台

#### **支持的渲染路径**

| 渲染路径 | 硬件解码器 | 渲染器 | 零拷贝 | 状态 |
|----------|-----------|--------|--------|------|
| **VA-API 加速** | VAAPI | VaapiRenderer | ✅ | ⚠️ 未实现 |
| **VDPAU 加速** | VDPAU | VdpauRenderer | ✅ | ⚠️ 未实现 |
| **软件渲染** | None | SDLRenderer | N/A | ✅ 已实现 |

#### **VA-API 加速路径（计划）**

```
VA-API (Video Acceleration API):
- Intel/AMD 显卡主流硬件加速接口
- 需要 libva 库支持

实现计划:
1. VaapiRenderer 基于 VA-API Surface
2. 与 HWDecoderContext 共享 VADisplay
3. 零拷贝渲染到 X11/Wayland 窗口

当前状态:
- HWDecoderContext 已支持 VAAPI 初始化
- VaapiRenderer 未实现
- 回退到 SDLRenderer（软件渲染）
```

#### **VDPAU 加速路径（计划）**

```
VDPAU (Video Decode and Presentation API for Unix):
- NVIDIA 显卡专用硬件加速接口
- 较老，逐渐被 VA-API 替代

实现计划:
- 优先级低于 VA-API
- 主要为 NVIDIA 用户提供加速

当前状态: 未实现
```

### 4.3 macOS 平台

#### **支持的渲染路径**

| 渲染路径 | 硬件解码器 | 渲染器 | 零拷贝 | 状态 |
|----------|-----------|--------|--------|------|
| **VideoToolbox 加速** | VideoToolbox | MetalRenderer | ✅ | ⚠️ 未实现 |
| **软件渲染** | None | SDLRenderer | N/A | ✅ 已实现 |

#### **VideoToolbox + Metal 加速路径（计划）**

```
VideoToolbox:
- macOS/iOS 原生硬件解码框架
- 支持 H.264, HEVC 等主流编码

Metal:
- macOS/iOS 原生 GPU 图形 API（类似 D3D11）

实现计划:
1. MetalRenderer 基于 Metal Framework
2. 与 HWDecoderContext 共享 CVPixelBuffer
3. 零拷贝渲染到 NSView/CAMetalLayer

当前状态:
- HWDecoderContext 已支持 VideoToolbox 初始化
- MetalRenderer 未实现
- 回退到 SDLRenderer
```

---

## 5. 配置系统

### 5.1 配置项定义

**zenplay.json 示例**:

```json
{
  "render": {
    "use_hardware_acceleration": true,   // 全局开关
    "backend_priority": [                // 后端优先级（暂未使用）
      "d3d11",
      "opengl",
      "software"
    ],
    "vsync": true,                       // 垂直同步
    "max_fps": 60,                       // 最大帧率
    "hardware": {
      "allow_d3d11va": true,             // 允许 D3D11VA
      "allow_dxva2": true,               // 允许 DXVA2
      "allow_fallback": true             // 硬件失败时允许回退
    }
  }
}
```

### 5.2 配置读取

**RenderPathSelector 如何读取配置**:

```cpp
// 检查硬件加速全局开关
bool IsHardwareAccelerationEnabled(GlobalConfig* config) {
  return config->GetBool("render.use_hardware_acceleration", false);
  //                     ↑ 配置路径                           ↑ 默认值
}

// 检查是否允许回退
bool IsFallbackAllowed(GlobalConfig* config) {
  return config->GetBool("render.hardware.allow_fallback", true);
}

// 检查特定硬件解码器是否允许
if (config->GetBool("render.hardware.allow_d3d11va", true)) {
  // 尝试 D3D11VA
}
```

### 5.3 配置影响决策

```
配置场景 1: 禁用硬件加速
{
  "render": {
    "use_hardware_acceleration": false
  }
}
→ 结果: 直接使用 SDLRenderer，不检测硬件

配置场景 2: 启用硬件加速，允许回退
{
  "render": {
    "use_hardware_acceleration": true,
    "hardware": {
      "allow_fallback": true
    }
  }
}
→ 结果: 尝试硬件加速，失败则回退 SDL

配置场景 3: 启用硬件加速，禁止回退
{
  "render": {
    "use_hardware_acceleration": true,
    "hardware": {
      "allow_fallback": false
    }
  }
}
→ 结果: 硬件失败则报错，播放器无法打开

配置场景 4: 禁用特定硬件解码器
{
  "render": {
    "use_hardware_acceleration": true,
    "hardware": {
      "allow_d3d11va": false,
      "allow_dxva2": true
    }
  }
}
→ 结果: 跳过 D3D11VA，尝试 DXVA2
```

---

## 6. 回退机制

### 6.1 回退触发条件

```
硬件加速失败的原因:

1. 硬件不支持
   - 显卡不支持 D3D11VA/VAAPI 等
   - 虚拟机 GPU 虚拟化不完整

2. 驱动问题
   - 显卡驱动过旧
   - 驱动损坏或不兼容

3. 资源限制
   - GPU 内存不足
   - 同时打开多个硬件解码会话

4. 编解码器不支持
   - 某些编解码器硬件不支持（如 VP9 老显卡）
```

### 6.2 回退实现

**SelectSoftwareFallback() 函数**:

```cpp
RenderPathSelection SelectSoftwareFallback(const std::string& reason) {
  // 输出日志
  ZENPLAY_INFO("Using SDL software renderer: {}", reason);
  
  RenderPathSelection result;
  
  // 创建 SDL 渲染器并包装为 RendererProxy
  auto sdl_renderer = std::make_unique<SDLRenderer>();
  result.renderer = std::make_unique<RendererProxy>(std::move(sdl_renderer));
  
  // 软件渲染不需要硬件上下文
  result.hw_context = nullptr;
  result.hw_decoder = HWDecoderType::kNone;
  result.backend_name = "SDL";
  result.reason = reason;
  result.is_hardware = false;
  
  return result;
}
```

**回退时机**:

```
SelectForWindows() 中的回退检查:

1. 配置禁用硬件加速
   → 直接 SelectSoftwareFallback("Hardware acceleration disabled by config")

2. 无硬件解码器可用
   → SelectSoftwareFallback("No hardware decoder available")

3. D3D11VA 初始化失败
   hw_context->Initialize(...) 返回错误
   → 检查 allow_fallback
       ├─> true: SelectSoftwareFallback("D3D11VA initialization failed")
       └─> false: 返回错误（renderer 为 nullptr）

4. 平台不支持（Linux/macOS 硬件渲染未实现）
   → SelectSoftwareFallback("Linux hardware renderer not implemented")
```

### 6.3 回退日志示例

```
成功硬件加速:
[INFO] [Renderer] Attempting to create D3D11 hardware acceleration pipeline
[INFO] [Renderer] ✅ D3D11 device shared between decoder and renderer (zero-copy enabled)
[INFO] [Renderer] ✅ Selected D3D11 hardware acceleration (D3D11VA decoder + D3D11 renderer)

硬件失败，允许回退:
[WARN] [Renderer] Failed to initialize D3D11VA context: Device creation failed
[INFO] [Renderer] Using SDL software renderer: Hardware acceleration initialization failed on Windows

硬件失败，禁止回退:
[ERROR] [Renderer] Hardware acceleration required but initialization failed
[ERROR] [Player] Failed to create renderer: Hardware acceleration required but initialization failed
```

---

## 7. 零拷贝优化

### 7.1 零拷贝原理

**传统渲染流程（有拷贝）**:

```
GPU 解码器内存                    CPU 内存                      GPU 渲染器内存
┌──────────────┐                ┌──────────┐                ┌──────────────┐
│ D3D11Texture │  ──拷贝1──>    │ AVFrame  │  ──拷贝2──>    │ SDL Texture  │
│ (GPU)        │                │ (CPU)    │                │ (GPU)        │
└──────────────┘                └──────────┘                └──────────────┘
     解码                           中间                          渲染
                ↓                                      ↓
        GPU → CPU 拷贝                          CPU → GPU 拷贝
        (20-30ms, 高 CPU)                      (10-20ms)
```

**零拷贝渲染流程**:

```
GPU 解码器内存 = GPU 渲染器内存（共享）
┌────────────────────────────────────┐
│ D3D11Texture (Shared ID3D11Device) │ ──直接使用──> 渲染
│ (GPU)                              │
└────────────────────────────────────┘
        解码 + 渲染
            ↓
    无拷贝，直接 GPU 内存访问
    (2-5ms, 低 CPU)
```

### 7.2 实现关键点

#### **1. 共享 D3D11 设备**

```
创建流程（SelectForWindows）:

Step 1: 创建 HWDecoderContext
  hw_context = std::make_unique<HWDecoderContext>();
  hw_context->Initialize(HWDecoderType::kD3D11VA, codec_id, width, height);
  ↓
  内部创建 ID3D11Device（通过 av_hwdevice_ctx_create）

Step 2: 获取共享设备
  ID3D11Device* shared_device = hw_context->GetD3D11Device();

Step 3: 创建 D3D11 渲染器并设置共享设备
  auto d3d11_renderer = std::make_unique<D3D11Renderer>();
  d3d11_renderer->SetSharedD3D11Device(shared_device);
  ↓
  渲染器使用相同的 ID3D11Device

结果: 解码帧的纹理与渲染器的纹理在同一个设备上，可直接访问！
```

#### **2. 纹理直接映射**

```
解码器产生 AVFrame:
  - AVFrame.data[0] = nullptr（硬件帧无 CPU 数据）
  - AVFrame.buf[0] = AVBufferRef → AVHWFramesContext → D3D11Texture2D*

渲染器获取纹理:
  ID3D11Texture2D* texture = hw_context->GetD3D11Texture(frame);
  ↓
  直接从 AVFrame 提取 D3D11Texture2D 指针

渲染器使用纹理:
  因为共享设备，可以直接创建 ShaderResourceView:
  device->CreateShaderResourceView(texture, &srv);
  ↓
  GPU 直接读取解码后的纹理，无需拷贝！
```

### 7.3 性能对比

| 指标 | 零拷贝（D3D11） | 传统拷贝（SDL） | 节省 |
|------|----------------|----------------|------|
| **CPU 占用** | 5-10% | 15-20% | **50%** |
| **帧延迟** | 2-5ms | 30-50ms | **10倍** |
| **内存带宽** | ~0 MB/s | 500-800 MB/s | **完全节省** |
| **适用场景** | D3D11VA 解码 + D3D11 渲染 | 任何软件/硬件解码 | - |

**实测数据（1080p H.264 30fps）**:
```
零拷贝路径:
- 解码: 3-5ms/帧
- 渲染: 2-3ms/帧
- 总 CPU: 8%

传统拷贝路径:
- 解码: 3-5ms/帧
- GPU→CPU 拷贝: 15-20ms/帧
- CPU→GPU 拷贝: 8-10ms/帧
- 渲染: 2-3ms/帧
- 总 CPU: 18%
```

---

## 8. 使用示例

### 8.1 ZenPlayer 中的使用

```cpp
// ZenPlayer::InitializeVideoRenderingPipeline()

// 检查是否有视频流
AVStream* video_stream = demuxer_->findStreamByIndex(...);

if (!video_stream) {
  // 场景 1: 无视频流（纯音频）
  renderer_ = RenderPathSelector::CreateDefaultRenderer();
  return Result<void>::Ok();
}

// 场景 2: 有视频流，选择最佳渲染路径
auto selection = RenderPathSelector::Select(
    video_stream->codecpar->codec_id,   // 例如 AV_CODEC_ID_H264
    video_stream->codecpar->width,      // 例如 1920
    video_stream->codecpar->height      // 例如 1080
);

if (!selection.renderer) {
  return Result<void>::Err(ErrorCode::kRenderError,
                           "Failed to create renderer: " + selection.reason);
}

// 记录选择结果
MODULE_INFO(LOG_MODULE_PLAYER,
            "Selected render path: {} (hardware: {}, decoder: {})",
            selection.backend_name,        // "D3D11"
            selection.is_hardware,         // true
            HWDecoderTypeUtil::GetName(selection.hw_decoder)); // "D3D11VA"

// 保存渲染器和硬件上下文
renderer_ = std::move(selection.renderer);           // RendererProxy(D3D11Renderer)
hw_decoder_context_ = std::move(selection.hw_context); // HWDecoderContext(D3D11VA)

// 打开视频解码器（使用硬件上下文）
return video_decoder_->Open(video_stream->codecpar, nullptr, hw_decoder_context_.get());
```

### 8.2 典型日志输出

#### **成功硬件加速（Windows D3D11）**

```
[2025-01-18 10:23:45.123] [INFO] [Player] Video stream found, selecting render path...
[2025-01-18 10:23:45.125] [INFO] [Renderer] Attempting to create D3D11 hardware acceleration pipeline
[2025-01-18 10:23:45.234] [INFO] [HWDecoder] D3D11VA device created successfully
[2025-01-18 10:23:45.235] [INFO] [Renderer] ✅ D3D11 device shared between decoder and renderer (zero-copy enabled)
[2025-01-18 10:23:45.236] [INFO] [Renderer] ✅ Selected D3D11 hardware acceleration (D3D11VA decoder + D3D11 renderer)
[2025-01-18 10:23:45.237] [INFO] [Player] Selected render path: D3D11 (hardware: true, decoder: D3D11VA, reason: D3D11VA available and initialized successfully)
```

#### **硬件失败，回退软件渲染**

```
[2025-01-18 10:24:10.123] [INFO] [Player] Video stream found, selecting render path...
[2025-01-18 10:24:10.125] [INFO] [Renderer] Attempting to create D3D11 hardware acceleration pipeline
[2025-01-18 10:24:10.234] [WARN] [HWDecoder] Failed to initialize D3D11VA context: Device creation failed
[2025-01-18 10:24:10.235] [INFO] [Renderer] Using SDL software renderer: Hardware acceleration initialization failed on Windows
[2025-01-18 10:24:10.236] [INFO] [Player] Selected render path: SDL (hardware: false, decoder: None, reason: Hardware acceleration initialization failed on Windows)
```

#### **Linux 平台（硬件渲染未实现）**

```
[2025-01-18 10:25:30.123] [INFO] [Player] Video stream found, selecting render path...
[2025-01-18 10:25:30.125] [INFO] [Renderer] VAAPI decoder available but hardware renderer not implemented yet
[2025-01-18 10:25:30.126] [INFO] [Renderer] Using SDL software renderer: Linux hardware renderer not implemented
[2025-01-18 10:25:30.127] [INFO] [Player] Selected render path: SDL (hardware: false, decoder: None, reason: Linux hardware renderer not implemented)
```

### 8.3 配置调整示例

#### **禁用硬件加速（调试/测试）**

```json
{
  "render": {
    "use_hardware_acceleration": false
  }
}
```

**结果**: 直接使用 SDL 软件渲染，不尝试硬件加速

#### **只允许 D3D11VA，禁用 DXVA2**

```json
{
  "render": {
    "use_hardware_acceleration": true,
    "hardware": {
      "allow_d3d11va": true,
      "allow_dxva2": false
    }
  }
}
```

**结果**: 跳过 DXVA2，只尝试 D3D11VA

#### **硬件失败时禁止回退（强制硬件）**

```json
{
  "render": {
    "use_hardware_acceleration": true,
    "hardware": {
      "allow_fallback": false
    }
  }
}
```

**结果**: 硬件初始化失败时，播放器报错无法打开（而非自动回退 SDL）

---

## 总结

### 核心设计要点

1. **统一接口**: `RenderPathSelection` 封装选择结果，跨平台统一
2. **配置驱动**: 通过 `GlobalConfig` 灵活控制策略
3. **智能决策**: 自动检测硬件能力，选择最佳方案
4. **自动回退**: 硬件失败时透明降级，保证播放成功
5. **零拷贝优化**: 硬件解码与渲染共享 GPU 设备，大幅提升性能

### 平台实现进度

| 平台 | 硬件渲染 | 零拷贝 | 状态 |
|------|---------|--------|------|
| **Windows** | D3D11Renderer | ✅ | ✅ 完整实现 |
| **Linux** | VaapiRenderer | ⚠️ | ⚠️ 计划中 |
| **macOS** | MetalRenderer | ⚠️ | ⚠️ 计划中 |

### 推荐阅读顺序

1. 先理解 [核心组件](core_components.md) 中的 Renderer 接口
2. 阅读本文档理解选择策略
3. 深入 [硬件加速详解](hardware_acceleration.md) 了解 HWDecoderContext
4. 参考 [零拷贝渲染详解](zero_copy_rendering.md) 理解性能优化

---

**文档维护**: 如有疑问或发现不一致，请参考源码 `render_path_selector.h/cpp` 或提出 Issue。
