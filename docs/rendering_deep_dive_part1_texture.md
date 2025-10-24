# 渲染深度解析 Part 1：纹理（Texture）的本质与原理

**文档目标**：深入理解纹理是什么、为什么需要纹理、纹理如何工作  
**前置知识**：了解图像的像素概念即可  
**阅读时间**：20-30 分钟

---

## 📚 目录

1. [什么是纹理](#1-什么是纹理)
2. [为什么需要纹理](#2-为什么需要纹理)
3. [纹理的存储和组织](#3-纹理的存储和组织)
4. [纹理坐标系统](#4-纹理坐标系统)
5. [纹理采样](#5-纹理采样)
6. [纹理过滤](#6-纹理过滤)
7. [纹理格式详解](#7-纹理格式详解)
8. [纹理的生命周期](#8-纹理的生命周期)

---

## 1. 什么是纹理

### 1.1 最直观的理解

**纹理 = GPU 内存中的一块图像数据**

想象你在玩《我的世界》，每个方块表面的图案（木头纹理、石头纹理）就是纹理。

```
现实世界类比：
墙纸 → 墙面上贴的图案
贴纸 → 笔记本上的装饰

计算机图形学：
纹理 → 显存中存储的图像数据，可以"贴"到任何要显示的表面上
```

### 1.2 纹理 vs 普通图片

| 特性 | 普通图片（CPU 内存） | 纹理（GPU 显存） |
|------|---------------------|-----------------|
| **存储位置** | 系统内存（RAM） | 显存（VRAM） |
| **访问速度** | CPU 访问快，GPU 访问慢 | GPU 访问快，CPU 访问慢 |
| **用途** | 编辑、处理、存储 | 渲染、显示 |
| **格式** | PNG、JPEG、BMP | GPU 特定格式（RGBA8、R8、NV12...） |
| **硬件优化** | ❌ 无 | ✅ 硬件缓存、并行采样 |

**关键点**：纹理是为 GPU 渲染优化的特殊图像格式。

### 1.3 纹理在渲染中的位置

```
视频播放的数据流：
┌──────────────┐
│ 视频文件     │
│ (.mp4)       │
└──────┬───────┘
       │ 解码
       ↓
┌──────────────┐
│ YUV 帧数据   │  ← 在 CPU 内存或 GPU 显存
│ (AVFrame)    │
└──────┬───────┘
       │ 上传到 GPU
       ↓
┌──────────────┐
│ GPU 纹理     │  ← 这里！关键步骤
│ (Texture)    │
└──────┬───────┘
       │ 着色器处理
       ↓
┌──────────────┐
│ 屏幕显示     │
└──────────────┘
```

---

## 2. 为什么需要纹理

### 2.1 问题：为什么不能直接用普通图片？

假设我们不使用纹理，直接用 CPU 内存中的图片：

```cpp
// ❌ 糟糕的方式：每次渲染都从 CPU 内存读取
void RenderFrame() {
    uint8_t* image_data = LoadImageFromCPUMemory();  // 在 RAM 中
    
    // GPU 需要访问 CPU 内存（跨总线）
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            Color pixel = ReadPixelFromCPU(image_data, x, y);  // 慢！
            DrawPixelToScreen(x, y, pixel);
        }
    }
}

// 性能问题：
// 1. GPU 读取 CPU 内存需要通过 PCIe 总线（慢）
// 2. 每个像素读取都是一次总线传输
// 3. 1920x1080 = 200 万次总线传输！
// 4. PCIe 3.0 x16：带宽 ~16 GB/s，但延迟高（微秒级）
```

**性能对比**：

| 操作 | 延迟 | 带宽 |
|------|------|------|
| CPU 读取 RAM | ~100 ns | ~50 GB/s |
| GPU 读取显存 | ~200 ns | ~500 GB/s |
| GPU 读取 CPU 内存 | ~1000 ns | ~16 GB/s |

### 2.2 纹理解决的问题

```cpp
// ✅ 正确的方式：使用纹理（预加载到 GPU）
void InitTexture() {
    // 一次性上传图像到 GPU 显存
    texture = CreateTexture(width, height);
    UploadDataToTexture(texture, image_data);  // 只传输一次
}

void RenderFrame() {
    // GPU 直接从自己的显存读取（快！）
    BindTexture(texture);  // 告诉 GPU 使用这个纹理
    DrawFullScreenQuad();  // GPU 自己处理，无需 CPU 参与
}

// 性能优势：
// 1. 数据已经在 GPU 显存中
// 2. GPU 可以并行读取多个像素
// 3. 硬件纹理缓存加速
// 4. 减少 PCIe 总线流量
```

### 2.3 纹理的三大优势

#### 优势 1：速度

```
GPU 从显存读取像素：
- 延迟：200 ns
- 带宽：500 GB/s
- 可以同时读取多个像素（硬件并行）

GPU 从 CPU 内存读取像素：
- 延迟：1000 ns（5 倍慢）
- 带宽：16 GB/s（31 倍慢）
- 无法并行优化
```

#### 优势 2：硬件加速特性

纹理不只是"GPU 显存中的图片"，它有特殊的硬件加速功能：

1. **纹理缓存**：GPU 有专门的纹理缓存（Texture Cache）
2. **硬件过滤**：放大/缩小时的插值由硬件完成
3. **Mipmap 支持**：自动选择合适分辨率的图像
4. **压缩格式**：支持 BC1-BC7、ASTC 等压缩格式，节省显存

#### 优势 3：抽象和灵活性

```cpp
// 纹理是一个抽象概念，可以有多种来源：

// 1. 从文件加载的图片
Texture* texture1 = LoadTextureFromFile("image.png");

// 2. 渲染到纹理（Render to Texture）
Texture* texture2 = CreateRenderTargetTexture(1920, 1080);
RenderSceneToTexture(texture2);  // 把 3D 场景渲染到纹理

// 3. 视频解码输出（我们的情况！）
Texture* texture3 = GetTextureFromVideoDecoder(frame);

// 4. 程序生成的纹理
Texture* texture4 = GenerateProceduralTexture();

// 5. 共享纹理（不同进程/API 共享）
Texture* texture5 = OpenSharedTexture(handle);
```

---

## 3. 纹理的存储和组织

### 3.1 纹理的内存布局

纹理在显存中不是简单的线性数组，而是特殊的布局以优化访问。

#### 线性布局（Linear Layout）

```
传统的图像存储方式（CPU 内存）：

像素按行存储：
[R0,G0,B0] [R1,G1,B1] [R2,G2,B2] ...  ← 第 1 行
[R3,G3,B3] [R4,G4,B4] [R5,G5,B5] ...  ← 第 2 行
[R6,G6,B6] [R7,G7,B7] [R8,G8,B8] ...  ← 第 3 行

优点：CPU 连续读取很快
缺点：GPU 访问相邻像素可能跨缓存行
```

#### 平铺布局（Tiled Layout）

```
GPU 纹理的存储方式：

将图像分成小块（通常 8x8 或 16x16），每块连续存储：

┌─────┬─────┬─────┐
│Tile0│Tile1│Tile2│  每个 Tile 内部是 8x8 像素
├─────┼─────┼─────┤  Tile 内部的像素是连续存储的
│Tile3│Tile4│Tile5│
├─────┼─────┼─────┤
│Tile6│Tile7│Tile8│
└─────┴─────┴─────┘

Tile 0 的内存布局（8x8 = 64 像素）：
[像素0,0] [像素0,1] ... [像素0,7]  ← 8 个像素连续
[像素1,0] [像素1,1] ... [像素1,7]
...
[像素7,0] [像素7,1] ... [像素7,7]

优点：
1. 空间局部性好（相邻像素物理上接近）
2. 缓存友好（一次读取可能覆盖多个相邻像素）
3. 压缩友好（每个 Tile 可以独立压缩）
```

**为什么平铺更快**？

```
场景：渲染时读取一个 4x4 的像素块

线性布局：
- 需要从 4 个不同的缓存行读取
- 可能触发 4 次内存访问

平铺布局：
- 4x4 块很可能在同一个 Tile 内
- 一次内存访问即可获取所有像素
```

### 3.2 纹理的维度

#### 1D 纹理（一维）

```
用途：颜色渐变、查找表

示例：温度颜色映射
[蓝色] [青色] [绿色] [黄色] [红色]
0°C    25°C   50°C   75°C   100°C

代码：
float temperature = 75.0;  // 75°C
Color color = Sample1DTexture(temp_gradient, temperature / 100.0);
// 返回黄色
```

#### 2D 纹理（二维，最常用）

```
用途：图片、视频帧、UI 元素

示例：视频帧
┌────────────────┐
│                │
│   1920x1080    │  ← 普通的矩形图像
│                │
└────────────────┘

代码：
Color pixel = Sample2DTexture(video_frame, u, v);
// u, v ∈ [0, 1]
```

#### 3D 纹理（三维）

```
用途：体积数据、3D 噪声

示例：医学 CT 扫描
     z
     ↑
     │  ┌─────┐
     │ ╱     ╱│  每一层是一个 2D 切片
     │├─────┤ │  堆叠起来形成 3D 体积
     ││     │╱
     │└─────┘
     └────────→ x
    ╱
   ╱ y

代码：
Color voxel = Sample3DTexture(ct_scan, x, y, z);
```

#### 立方体纹理（Cube Texture）

```
用途：环境贴图、天空盒

示例：全景环境
       ┌─────┐
       │ +Y  │  上
       │(顶) │
┌─────┬┼─────┼┬─────┬─────┐
│ -X  ││ +Z  ││ +X  │ -Z  │  左前右后
│(左) ││(前) ││(右) │(后) │
└─────┴┼─────┼┴─────┴─────┘
       │ -Y  │  下
       │(底) │
       └─────┘

代码：
// 使用方向向量采样
vec3 direction = normalize(cameraPos - worldPos);
Color envColor = SampleCubeTexture(skybox, direction);
```

---

## 4. 纹理坐标系统

### 4.1 UV 坐标

**纹理坐标（UV）是归一化的坐标，范围 [0, 1]**

```
为什么叫 UV？
- 笛卡尔坐标系用 (x, y, z)
- 为了区分，纹理坐标用 (u, v)
- 3D 纹理用 (u, v, w)

纹理坐标系：
  v
  ↑
1 ┌─────────────┐
  │             │
  │   纹理图像   │
  │             │
0 └─────────────┘→ u
  0             1

特点：
1. 与实际像素分辨率无关
2. (0, 0) = 左下角
3. (1, 1) = 右上角
4. (0.5, 0.5) = 中心
```

### 4.2 UV 映射示例

```cpp
// 示例：在屏幕上显示纹理

// 场景 1：全屏显示
┌──────────────┐   UV 映射：
│              │   (0,1) ────────── (1,1)
│   整个纹理   │     │                │
│              │     │    纹理全部    │
└──────────────┘     │     显示       │
                   (0,0) ────────── (1,0)

// 场景 2：只显示纹理的左上角 1/4
┌──────┐          UV 映射：
│ 1/4  │          (0,1) ─── (0.5,1)
└──────┘            │          │
                    │   仅左上  │
                    │   1/4    │
                  (0,0) ─── (0.5,0.5)

// 场景 3：平铺纹理（重复）
┌──┬──┬──┐       UV 映射：
├──┼──┼──┤       (0,3) ─────── (3,3)
└──┴──┴──┘         │             │
                   │  UV > 1     │
                   │  会重复     │
                 (0,0) ─────── (3,0)
```

### 4.3 像素坐标 vs UV 坐标

```cpp
// 1920x1080 的纹理

像素坐标（整数）：
- 左上角像素：(0, 0)
- 右下角像素：(1919, 1079)
- 中心像素：(960, 540)
- 范围：[0, width-1] × [0, height-1]

UV 坐标（浮点数）：
- 左下角：(0.0, 0.0)
- 右上角：(1.0, 1.0)
- 中心：(0.5, 0.5)
- 范围：[0.0, 1.0] × [0.0, 1.0]

转换公式：
pixel_x = u * width
pixel_y = (1.0 - v) * height  // 注意：v 需要翻转（OpenGL 约定）

// 或者（Direct3D 约定，v=0 在顶部）：
pixel_y = v * height
```

### 4.4 纹理寻址模式（Addressing Mode）

**问题**：当 UV 坐标超出 [0, 1] 范围时怎么办？

```cpp
// 模式 1：Wrap（重复）
UV = (2.3, 1.7)
实际采样 = (0.3, 0.7)  // 取小数部分

效果：纹理平铺
┌──┬──┬──┐
├──┼──┼──┤
└──┴──┴──┘

// 模式 2：Clamp（夹紧）
UV = (2.3, 1.7)
实际采样 = (1.0, 1.0)  // 限制在 [0, 1]

效果：边缘拉伸
┌───────┐
│       │
│   ████│  ← 右边缘颜色一直延伸
└───────┘

// 模式 3：Mirror（镜像）
UV = (2.3, 1.7)
实际采样 = (1.0 - 0.3, 1.0 - 0.7) = (0.7, 0.3)

效果：镜像平铺
┌──┬──┬──┐
├──┼──┼──┤  ← 每次越界后镜像翻转
└──┴──┴──┘

// 模式 4：Border（边框）
UV = (2.3, 1.7)
实际采样 = BorderColor  // 返回预设的边框颜色（如黑色）

效果：超出部分显示边框色
┌───────┐
│       │
│       │████  ← 黑色边框
└───────┘████
```

---

## 5. 纹理采样

### 5.1 什么是采样

**采样 = 从纹理中读取颜色值**

```cpp
// 最基本的采样操作
Color SampleTexture(Texture tex, float u, float v) {
    // 1. 计算像素坐标
    int x = (int)(u * tex.width);
    int y = (int)(v * tex.height);
    
    // 2. 读取像素颜色
    return tex.pixels[y * tex.width + x];
}

// 使用示例
Texture videoFrame = LoadVideoFrame();
Color color = SampleTexture(videoFrame, 0.5, 0.5);  // 采样中心像素
```

### 5.2 采样的硬件加速

**CPU 采样 vs GPU 采样**

```cpp
// CPU 采样（软件渲染）
Color CPUSample(Texture* tex, float u, float v) {
    int x = (int)(u * tex->width);
    int y = (int)(v * tex->height);
    
    // 从内存读取（慢）
    uint8_t* data = tex->cpu_buffer;
    int offset = (y * tex->width + x) * 4;
    
    Color color;
    color.r = data[offset + 0];
    color.g = data[offset + 1];
    color.b = data[offset + 2];
    color.a = data[offset + 3];
    return color;
}

// GPU 采样（硬件渲染）
// 在着色器中：
Color GPUSample() {
    // GPU 纹理单元（Texture Unit）硬件加速
    // 一条指令完成：
    // 1. UV 计算
    // 2. 地址转换
    // 3. 缓存查找
    // 4. 内存读取
    // 5. 过滤插值（如果需要）
    return texture.Sample(sampler, uv);
}
```

**性能对比**：

```
场景：采样 1920x1080 纹理，每帧采样 200 万次

CPU 采样：
- 每次采样：~50 ns（内存读取 + 地址计算）
- 总耗时：200万 × 50ns = 100 ms/帧
- 帧率：10 fps ❌

GPU 采样：
- 2000 个纹理单元并行工作
- 每个单元处理：1000 次采样
- 单次采样：~200 ns（包含过滤）
- 总耗时：1000 × 200ns = 0.2 ms/帧
- 帧率：5000 fps ✅
```

### 5.3 着色器中的纹理采样

```hlsl
// HLSL (Direct3D) 着色器代码

// 声明纹理和采样器
Texture2D myTexture : register(t0);      // 纹理对象
SamplerState mySampler : register(s0);   // 采样器（控制采样行为）

// 像素着色器
float4 main(float2 uv : TEXCOORD) : SV_Target
{
    // 方法 1：普通采样（带过滤）
    float4 color = myTexture.Sample(mySampler, uv);
    
    // 方法 2：指定 Mipmap 级别采样
    float4 color2 = myTexture.SampleLevel(mySampler, uv, mipLevel);
    
    // 方法 3：使用偏导数采样（自动计算 Mipmap）
    float4 color3 = myTexture.SampleGrad(mySampler, uv, ddx, ddy);
    
    // 方法 4：直接读取像素（无过滤）
    int2 pixelCoord = int2(uv.x * width, uv.y * height);
    float4 color4 = myTexture.Load(int3(pixelCoord, 0));
    
    return color;
}
```

```glsl
// GLSL (OpenGL) 着色器代码

// 声明纹理采样器（纹理 + 采样器 合二为一）
uniform sampler2D myTexture;

void main()
{
    // 方法 1：普通采样
    vec4 color = texture(myTexture, uv);
    
    // 方法 2：指定 Mipmap 级别
    vec4 color2 = textureLod(myTexture, uv, mipLevel);
    
    // 方法 3：直接读取像素
    ivec2 pixelCoord = ivec2(uv * textureSize(myTexture, 0));
    vec4 color3 = texelFetch(myTexture, pixelCoord, 0);
    
    gl_FragColor = color;
}
```

---

## 6. 纹理过滤

### 6.1 问题：纹理和屏幕分辨率不匹配

```
问题场景：

场景 1：纹理比屏幕小（放大）
纹理：64x64 像素
屏幕区域：512x512 像素
问题：1 个纹理像素要覆盖 8x8 个屏幕像素

┌──┬──┐      放大到      ┌────────┬────────┐
│  │  │                  │        │        │
├──┼──┤    ========>     │  像素  │  像素  │
│  │  │                  │  被    │  被    │
└──┴──┘                  │  拉伸  │  拉伸  │
 纹理                    └────────┴────────┘
                            屏幕

场景 2：纹理比屏幕大（缩小）
纹理：4096x4096 像素
屏幕区域：512x512 像素
问题：8x8 个纹理像素要合并成 1 个屏幕像素

┌─┬─┬─┬─┬─┬─┬─┐    缩小到    ┌──┐
├─┼─┼─┼─┼─┼─┼─┤              │  │
├─┼─┼─┼─┼─┼─┼─┤  ========>   │  │
├─┼─┼─┼─┼─┼─┼─┤              └──┘
└─┴─┴─┴─┴─┴─┴─┘
   纹理                       屏幕
```

### 6.2 放大过滤（Magnification Filter）

#### Nearest（最近邻）

```
原理：选择最近的纹理像素

纹理（4x4）放大到屏幕（16x16）：

┌─┬─┬─┬─┐      放大（Nearest）      ┌───┬───┬───┬───┐
│A│B│C│D│                           │AAA│BBB│CCC│DDD│
├─┼─┼─┼─┤                           │AAA│BBB│CCC│DDD│
│E│F│G│H│      ============>        │AAA│BBB│CCC│DDD│
├─┼─┼─┼─┤                           ├───┼───┼───┼───┤
│I│J│K│L│                           │EEE│FFF│GGG│HHH│
└─┴─┴─┴─┘                           │EEE│FFF│GGG│HHH│
 纹理                               │EEE│FFF│GGG│HHH│
                                    └───┴───┴───┴───┘
                                       屏幕

优点：快速（一次查找）
缺点：有明显的像素化（"马赛克"效果）

代码：
Color NearestFilter(Texture tex, float u, float v) {
    int x = (int)(u * tex.width);     // 直接取整
    int y = (int)(v * tex.height);
    return tex.pixels[y * tex.width + x];
}
```

#### Linear（线性插值/双线性）

```
原理：在最近的 4 个像素之间插值

采样点（红点）周围的 4 个像素：
┌────┬────┐
│ C0 │ C1 │  C0, C1, C2, C3 是 4 个相邻像素的颜色
├────●────┤  ● 是采样点（u, v）
│ C2 │ C3 │
└────┴────┘

插值过程：
1. 计算采样点在像素内的位置（fx, fy）
2. 水平插值：
   Top = lerp(C0, C1, fx)    // 上边两个像素插值
   Bottom = lerp(C2, C3, fx) // 下边两个像素插值
3. 垂直插值：
   Result = lerp(Top, Bottom, fy)

代码：
Color BilinearFilter(Texture tex, float u, float v) {
    float x = u * tex.width - 0.5;   // 像素中心对齐
    float y = v * tex.height - 0.5;
    
    int x0 = (int)floor(x);
    int y0 = (int)floor(y);
    int x1 = x0 + 1;
    int y1 = y0 + 1;
    
    float fx = x - x0;  // 小数部分
    float fy = y - y0;
    
    // 获取 4 个像素
    Color c00 = GetPixel(tex, x0, y0);
    Color c10 = GetPixel(tex, x1, y0);
    Color c01 = GetPixel(tex, x0, y1);
    Color c11 = GetPixel(tex, x1, y1);
    
    // 双线性插值
    Color top = Lerp(c00, c10, fx);
    Color bottom = Lerp(c01, c11, fx);
    return Lerp(top, bottom, fy);
}

inline Color Lerp(Color a, Color b, float t) {
    return a * (1.0 - t) + b * t;
}

视觉效果：
┌─┬─┬─┬─┐      放大（Linear）         ┌───────┐
│A│B│C│D│                             │渐变过渡│  ← 平滑过渡
├─┼─┼─┼─┤      ============>          │没有锯齿│
│E│F│G│H│                             │       │
└─┴─┴─┴─┘                             └───────┘

优点：平滑，无像素化
缺点：稍慢（4 次查找 + 3 次插值）
```

### 6.3 缩小过滤（Minification Filter）

**问题更复杂**：多个纹理像素对应 1 个屏幕像素，容易产生摩尔纹和闪烁。

#### Nearest/Linear（效果不佳）

```
高分辨率纹理缩小显示：

┌─┬─┬─┬─┬─┬─┬─┬─┐
├─┼─┼─┼─┼─┼─┼─┼─┤  纹理：64x64
├─┼─┼─┼─┼─┼─┼─┼─┤     ↓
├─┼─┼─┼─┼─┼─┼─┼─┤  缩小到
└─┴─┴─┴─┴─┴─┴─┴─┘  屏幕：8x8

问题：
1. 每个屏幕像素对应 8x8=64 个纹理像素
2. 只采样其中 1 个（Nearest）或 4 个（Linear）
3. 丢失了其他 60 个像素的信息
4. 导致摩尔纹、走样、闪烁

┌─────┐ ┌─────┐ ┌─────┐
│ 细节 │ │ ???? │ │闪烁 │  ← 相邻帧可能采样到不同像素
└─────┘ └─────┘ └─────┘
 帧1      帧2      帧3
```

#### Mipmap（解决方案）

**Mipmap = 多级渐进纹理**

```
原理：预先生成一系列不同分辨率的纹理

Level 0 (原始)：1024x1024  ← 最高清
Level 1：        512x512   ← 缩小 1/2
Level 2：        256x256   ← 缩小 1/4
Level 3：        128x128
Level 4：         64x64
...
Level 10：         1x1     ← 最模糊

视觉表示：
┌────────┐
│        │ Level 0 (1024x1024)
│        │
└────────┘
  ┌────┐
  │    │   Level 1 (512x512)
  └────┘
   ┌──┐
   │  │    Level 2 (256x256)
   └──┘
    ┌┐
    ││     Level 3 (128x128)
    └┘
     .     ...

存储空间：
- 原始纹理：1024² = 1,048,576 像素
- Level 1：512² = 262,144 像素
- Level 2：256² = 65,536 像素
- ...
- 总计：≈ 1.33 倍原始大小（额外 33%）

优势：
1. 根据距离自动选择合适的分辨率
2. 减少采样数量（每级只需 1-4 次采样）
3. 消除摩尔纹和闪烁
4. 提高缓存命中率
```

#### Trilinear 过滤（三线性）

```
原理：在两个 Mipmap 级别之间插值

场景：纹理缩小到 0.7 倍

计算：
log2(0.7) = -0.515
需要的 Mipmap 级别 = 0.515

使用的级别：
Level 0 (原始大小)：权重 0.485
Level 1 (缩小 1/2)：权重 0.515

步骤：
1. 在 Level 0 进行双线性采样 → Color0
2. 在 Level 1 进行双线性采样 → Color1
3. 在两个级别之间插值：
   Result = lerp(Color0, Color1, 0.515)

代码：
Color TrilinearFilter(Texture tex, float u, float v, float mipLevel) {
    int level0 = (int)floor(mipLevel);
    int level1 = level0 + 1;
    float blend = mipLevel - level0;
    
    // 在两个级别分别采样
    Color color0 = BilinearFilter(tex.mipmap[level0], u, v);
    Color color1 = BilinearFilter(tex.mipmap[level1], u, v);
    
    // 插值
    return Lerp(color0, color1, blend);
}

优点：非常平滑，无闪烁
缺点：稍慢（8 次纹理查找 + 7 次插值）
      但 GPU 硬件加速，实际很快
```

#### Anisotropic 过滤（各向异性）

```
问题：Trilinear 假设纹理均匀缩放，但实际可能倾斜

场景：地面纹理透视

从上往下看：         从侧面看（透视）：
┌────┐              ┌────┐
│    │              │    │  ← 近处：1 屏幕像素 = 1 纹理像素
│    │              ├────┤
│    │              ├──┤    ← 中间：1 屏幕像素 = 4 纹理像素
└────┘              ├─┤      ← 远处：1 屏幕像素 = 16 纹理像素
                    └┘

问题：
- 垂直方向缩小很多（远处）
- 水平方向缩小较少
- Mipmap 是正方形的，无法处理这种各向异性

Anisotropic 解决方案：
- 不只是采样 1 个点
- 沿纹理倾斜方向采样多个点（如 16 个）
- 对所有采样结果求平均

┌─────────────┐
│ ●●●●●●●●●●●●│  ← 沿着纹理倾斜方向采样 12 次
└─────────────┘     求平均得到最终颜色

代码（概念）：
Color AnisotropicFilter(Texture tex, float u, float v, vec2 ddx, vec2 ddy) {
    // 计算各向异性方向和比率
    float aniso_ratio = CalculateAnisotropyRatio(ddx, ddy);  // 如 16
    vec2 major_axis = CalculateMajorAxis(ddx, ddy);
    
    Color result = Color(0, 0, 0, 0);
    for (int i = 0; i < aniso_ratio; i++) {
        float offset = (i - aniso_ratio/2) / (float)aniso_ratio;
        vec2 sample_uv = vec2(u, v) + major_axis * offset;
        result += TrilinearFilter(tex, sample_uv.x, sample_uv.y);
    }
    return result / aniso_ratio;
}

质量设置：
- 1x：相当于 Trilinear（无各向异性）
- 2x：最多 2 个采样
- 4x：最多 4 个采样
- 8x：最多 8 个采样
- 16x：最多 16 个采样（最高质量）

性能影响：
- 1x → 4x：性能下降 ~10%
- 4x → 16x：性能下降 ~15%
- 但画质提升明显（特别是地面、墙壁等倾斜表面）
```

### 6.4 过滤模式对比

| 过滤模式 | 放大质量 | 缩小质量 | 性能 | 用途 |
|---------|---------|---------|------|------|
| **Nearest** | ★☆☆☆☆ | ★☆☆☆☆ | ★★★★★ | 像素艺术、UI |
| **Linear** | ★★★☆☆ | ★★☆☆☆ | ★★★★☆ | 一般纹理 |
| **Trilinear** | ★★★☆☆ | ★★★★☆ | ★★★☆☆ | 3D 游戏标准 |
| **Anisotropic 4x** | ★★★☆☆ | ★★★★☆ | ★★★☆☆ | 高质量游戏 |
| **Anisotropic 16x** | ★★★☆☆ | ★★★★★ | ★★☆☆☆ | 最高质量 |

---

## 7. 纹理格式详解

### 7.1 颜色格式

#### RGBA8（最常见）

```
格式：每个通道 8 位，共 32 位/像素

内存布局：
[R][G][B][A]  ← 1 个像素 = 4 字节
 8  8  8  8 位

取值范围：
- R, G, B, A: 0-255
- 归一化到 [0.0, 1.0]

存储大小：
1920x1080 = 2,073,600 像素
× 4 字节/像素 = 8,294,400 字节 ≈ 7.9 MB

优点：精度高，兼容性好
缺点：占用空间大
```

#### RGBA16F（高动态范围）

```
格式：每个通道 16 位半精度浮点数

取值范围：-65504 到 +65504
可以表示超过 [0, 1] 的值（HDR）

用途：
- HDR 渲染
- 后期处理
- 颜色分级

存储大小：
1920x1080 × 8 字节/像素 = 15.8 MB
```

#### R8（单通道）

```
格式：只有一个 8 位通道

用途：
- 灰度图像
- Alpha 蒙版
- 高度图
- 单独的 Y/U/V 平面（视频！）

存储大小：
1920x1080 × 1 字节/像素 = 2.0 MB
```

### 7.2 视频格式（重点）

#### YUV 4:2:0 Planar（NV12/I420）

```
格式：Y、U、V 分别存储在不同平面

NV12 布局：
┌──────────────┐
│   Y 平面     │  1920x1080，每像素 1 字节
│   (亮度)     │  = 2,073,600 字节
├──────────────┤
│  UV 平面     │  960x540（1/4 分辨率）
│ (交错存储)   │  U 和 V 交错：UVUVUVUV...
│  UVUVUVUV    │  = 1,036,800 字节
└──────────────┘
总大小：3,110,400 字节 ≈ 3 MB

对比 RGB：
RGBA8: 8.3 MB
NV12: 3 MB
节省：64% 空间！

为什么可以这样？
人眼对亮度（Y）敏感，对色度（UV）不敏感
降低色度分辨率，视觉上几乎无差异
```

**在 GPU 中如何使用 NV12 纹理**：

```cpp
// 方法 1：创建 2 个纹理
// 纹理 0：Y 平面（R8 格式）
D3D11_TEXTURE2D_DESC descY = {};
descY.Width = 1920;
descY.Height = 1080;
descY.Format = DXGI_FORMAT_R8_UNORM;  // 单通道 8 位
ID3D11Texture2D* yTexture;
device->CreateTexture2D(&descY, nullptr, &yTexture);

// 纹理 1：UV 平面（R8G8 格式）
D3D11_TEXTURE2D_DESC descUV = {};
descUV.Width = 960;   // 1/2 宽度
descUV.Height = 540;  // 1/2 高度
descUV.Format = DXGI_FORMAT_R8G8_UNORM;  // 双通道 8 位
ID3D11Texture2D* uvTexture;
device->CreateTexture2D(&descUV, nullptr, &uvTexture);

// 着色器中采样：
float y = yTexture.Sample(sampler, uv).r;
float2 uv_chroma = uvTexture.Sample(sampler, uv).rg;
float u = uv_chroma.r - 0.5;
float v = uv_chroma.g - 0.5;

// YUV → RGB 转换
float r = y + 1.402 * v;
float g = y - 0.344 * u - 0.714 * v;
float b = y + 1.772 * u;
```

```cpp
// 方法 2：使用原生 NV12 格式（Direct3D 11.1+）
D3D11_TEXTURE2D_DESC desc = {};
desc.Width = 1920;
desc.Height = 1080;
desc.Format = DXGI_FORMAT_NV12;  // 原生 NV12 格式
device->CreateTexture2D(&desc, nullptr, &texture);

// 着色器中直接采样（硬件自动处理 Y 和 UV）
float4 yuv = texture.Sample(sampler, uv);
float y = yuv.r;
float u = yuv.g - 0.5;
float v = yuv.b - 0.5;
```

### 7.3 压缩格式

#### BC1/DXT1（块压缩）

```
原理：将 4x4 像素块压缩成 8 字节

未压缩（RGBA8）：
4x4 = 16 像素 × 4 字节 = 64 字节

BC1 压缩：
4x4 = 16 像素 → 8 字节
压缩比：8:1

压缩方法：
1. 选择 2 个代表色（各 2 字节）：C0, C1
2. 计算中间色：C2, C3（插值）
3. 每个像素用 2 位索引（0/1/2/3）选择颜色

┌────┬────┬────┬────┐
│ C0 │ C2 │ C1 │ C0 │  每个像素 2 位
├────┼────┼────┼────┤  4x4 = 32 位 = 4 字节
│ C3 │ C1 │ C0 │ C2 │  + 2 个代表色 4 字节
├────┼────┼────┼────┤  = 8 字节总计
│ C1 │ C0 │ C3 │ C1 │
└────┴────┴────┴────┘

优点：
- 节省显存（1/8 大小）
- 提高缓存命中率
- GPU 硬件解压（采样时自动解压）

缺点：
- 有损压缩（色带、伪影）
- 不适合频繁更新的纹理
```

#### BC3/DXT5（带 Alpha）

```
类似 BC1，但增加 Alpha 通道压缩
16 像素 → 16 字节
- 8 字节：RGB（BC1 方法）
- 8 字节：Alpha（类似方法压缩）

压缩比：4:1
```

#### BC7（最新、最好）

```
自适应块压缩，多种模式

16 像素 → 16 字节
压缩比：4:1

优点：
- 质量接近无损
- 支持 RGB 和 RGBA
- 7 种不同的压缩模式，自动选择最优

缺点：
- 压缩慢（离线压缩）
- 需要较新硬件（DX11+）
```

### 7.4 格式选择指南

| 用途 | 推荐格式 | 原因 |
|------|---------|------|
| 视频帧（YUV） | NV12, P010 | 节省空间，硬件支持 |
| 视频帧（RGB） | RGBA8 | 兼容性好 |
| UI 元素 | RGBA8 | 需要精确颜色 |
| 游戏纹理（RGB） | BC1, BC7 | 压缩节省显存 |
| 游戏纹理（RGBA） | BC3, BC7 | 支持透明 |
| HDR 内容 | RGBA16F, RGB10A2 | 高动态范围 |
| 深度缓冲 | D24S8, D32F | 深度/模板 |

---

## 8. 纹理的生命周期

### 8.1 创建纹理

```cpp
// Direct3D 11 示例
D3D11_TEXTURE2D_DESC desc = {};
desc.Width = 1920;
desc.Height = 1080;
desc.MipLevels = 1;              // 1 = 无 Mipmap
desc.ArraySize = 1;              // 纹理数组大小
desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
desc.SampleDesc.Count = 1;       // 无多重采样
desc.Usage = D3D11_USAGE_DEFAULT;  // GPU 读写
desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;  // 用于着色器
desc.CPUAccessFlags = 0;         // CPU 不直接访问
desc.MiscFlags = 0;

ID3D11Texture2D* texture = nullptr;
HRESULT hr = device->CreateTexture2D(&desc, nullptr, &texture);
```

### 8.2 上传数据

```cpp
// 方法 1：创建时上传（静态纹理）
D3D11_SUBRESOURCE_DATA initData = {};
initData.pSysMem = pixelData;        // CPU 内存中的像素数据
initData.SysMemPitch = width * 4;    // 每行字节数
initData.SysMemSlicePitch = 0;       // 3D 纹理用

device->CreateTexture2D(&desc, &initData, &texture);

// 方法 2：创建后上传（动态纹理）
context->UpdateSubresource(
    texture,        // 目标纹理
    0,              // 子资源索引（Mipmap 级别）
    nullptr,        // 更新整个纹理
    pixelData,      // 源数据
    width * 4,      // 行间距
    0               // 深度间距
);

// 方法 3：映射内存（频繁更新）
D3D11_MAPPED_SUBRESOURCE mapped;
context->Map(texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
memcpy(mapped.pData, pixelData, width * height * 4);
context->Unmap(texture, 0);
```

### 8.3 使用纹理

```cpp
// 1. 创建着色器资源视图（SRV）
ID3D11ShaderResourceView* srv = nullptr;
device->CreateShaderResourceView(texture, nullptr, &srv);

// 2. 绑定到着色器
context->PSSetShaderResources(0, 1, &srv);  // 绑定到槽 0

// 3. 在着色器中使用
// Texture2D myTexture : register(t0);  ← 对应槽 0
// float4 color = myTexture.Sample(sampler, uv);
```

### 8.4 销毁纹理

```cpp
// 释放资源（引用计数）
if (srv) {
    srv->Release();
    srv = nullptr;
}

if (texture) {
    texture->Release();
    texture = nullptr;
}

// 或者使用智能指针
Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
// 自动释放
```

### 8.5 性能最佳实践

```cpp
// ✅ 好的做法：

// 1. 纹理池（重用纹理）
class TexturePool {
    std::unordered_map<TextureKey, ID3D11Texture2D*> pool_;
    
public:
    ID3D11Texture2D* Get(int width, int height, DXGI_FORMAT format) {
        TextureKey key = {width, height, format};
        if (pool_.find(key) != pool_.end()) {
            return pool_[key];  // 重用
        }
        
        // 创建新纹理
        auto texture = CreateTexture(width, height, format);
        pool_[key] = texture;
        return texture;
    }
};

// 2. 批量更新（减少 Map/Unmap 次数）
std::vector<Frame> frames = GetMultipleFrames();
context->Map(texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
for (auto& frame : frames) {
    // 批量写入
}
context->Unmap(texture, 0);

// 3. 使用适当的 Usage 标志
D3D11_USAGE_DEFAULT:   // GPU 读写，CPU 通过 UpdateSubresource 写入
D3D11_USAGE_DYNAMIC:   // GPU 读，CPU 频繁写入（Map/Unmap）
D3D11_USAGE_STAGING:   // CPU 读写，用于回读 GPU 数据

// ❌ 坏的做法：

// 1. 每帧创建/销毁纹理
for (int frame = 0; frame < 1000; frame++) {
    ID3D11Texture2D* tex = CreateTexture(...);  // 慢！
    // ... 使用
    tex->Release();  // 慢！
}

// 2. 频繁的 CPU-GPU 同步
context->Flush();  // 强制 GPU 完成所有命令（慢！）
context->Map(texture, 0, D3D11_MAP_READ, 0, &mapped);  // 等待 GPU（慢！）
```

---

## 📚 总结

### 核心概念回顾

1. **纹理的本质**：
   - GPU 显存中的优化图像格式
   - 为并行采样和硬件加速设计

2. **为什么需要纹理**：
   - 速度：GPU 访问显存比访问 CPU 内存快 30 倍
   - 硬件加速：纹理缓存、过滤、压缩
   - 抽象：统一的图像接口

3. **纹理坐标**：
   - UV 坐标：[0, 1] 归一化坐标
   - 与分辨率无关
   - 支持多种寻址模式

4. **纹理采样**：
   - GPU 硬件加速的像素读取
   - 支持过滤和插值
   - 比 CPU 快数千倍

5. **纹理过滤**：
   - 放大：Nearest vs Linear
   - 缩小：Mipmap + Trilinear + Anisotropic
   - 质量和性能的平衡

6. **纹理格式**：
   - RGBA8：标准格式
   - NV12：视频格式（节省 64% 空间）
   - BC1-BC7：压缩格式（节省 75-87% 显存）

### 关键要点

```
┌─────────────────────────────────────────┐
│  纹理 = GPU 优化的图像 + 硬件加速功能   │
├─────────────────────────────────────────┤
│  • 存储在 GPU 显存（快）                │
│  • 硬件采样和过滤（并行）               │
│  • 支持 Mipmap（质量 + 性能）           │
│  • 多种格式（压缩、YUV、HDR）           │
│  • 纹理坐标抽象（与分辨率无关）         │
└─────────────────────────────────────────┘
```

### 下一步学习

在 **Part 2** 中，我们将深入学习：
- GPU 渲染管线
- 顶点和像素着色器
- 渲染管线状态
- 为什么 GPU 比 CPU 快这么多

---

**准备好继续深入了吗？让我知道何时开始 Part 2！** 🚀
