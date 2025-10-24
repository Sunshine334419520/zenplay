# 渲染深度解析 Part 2B：渲染管线详解

**文档目标**：深入理解 GPU 渲染管线的每个阶段、顶点着色器、像素着色器的工作原理  
**前置知识**：Part 1（纹理）、Part 2A（GPU 架构）  
**阅读时间**：30-40 分钟

---

## 📚 目录

1. [渲染管线概览](#1-渲染管线概览)
2. [输入装配阶段](#2-输入装配阶段)
3. [顶点着色器阶段](#3-顶点着色器阶段)
4. [光栅化阶段](#4-光栅化阶段)
5. [像素着色器阶段](#5-像素着色器阶段)
6. [输出合并阶段](#6-输出合并阶段)
7. [完整渲染示例](#7-完整渲染示例)

---

## 1. 渲染管线概览

### 1.1 什么是渲染管线

**渲染管线 = 从 3D 数据到屏幕像素的流水线**

```
类比：汽车工厂流水线

原材料 (钢材) → [切割] → [焊接] → [喷漆] → [组装] → 成品汽车

渲染管线：
顶点数据 → [顶点处理] → [光栅化] → [像素着色] → [合并] → 屏幕图像
```

### 1.2 Direct3D 11 渲染管线

```
┌─────────────────────────────────────────────────────────┐
│                     渲染管线                             │
├─────────────────────────────────────────────────────────┤
│                                                          │
│  1️⃣ Input Assembler (IA) - 输入装配                    │
│     ┌─────────────────────────────────────────┐        │
│     │ • 读取顶点数据                          │        │
│     │ • 读取索引数据                          │        │
│     │ • 组装图元 (三角形、线、点)             │        │
│     └─────────────────────────────────────────┘        │
│                       ↓                                 │
│  2️⃣ Vertex Shader (VS) - 顶点着色器 [可编程] 🔧      │
│     ┌─────────────────────────────────────────┐        │
│     │ • 变换顶点位置 (模型 → 世界 → 屏幕)    │        │
│     │ • 计算每顶点数据 (UV、颜色、法线等)     │        │
│     │ • 并行处理每个顶点                      │        │
│     └─────────────────────────────────────────┘        │
│                       ↓                                 │
│  3️⃣ Rasterizer (RS) - 光栅化 [固定功能]              │
│     ┌─────────────────────────────────────────┐        │
│     │ • 三角形 → 像素                         │        │
│     │ • 裁剪 (屏幕外的三角形)                 │        │
│     │ • 剔除 (背面三角形)                     │        │
│     │ • 插值顶点属性 (UV、颜色等)             │        │
│     └─────────────────────────────────────────┘        │
│                       ↓                                 │
│  4️⃣ Pixel Shader (PS) - 像素着色器 [可编程] 🔧       │
│     ┌─────────────────────────────────────────┐        │
│     │ • 计算每个像素的颜色                    │        │
│     │ • 纹理采样 (最常见操作)                 │        │
│     │ • 光照计算                              │        │
│     │ • 并行处理每个像素                      │        │
│     └─────────────────────────────────────────┘        │
│                       ↓                                 │
│  5️⃣ Output Merger (OM) - 输出合并 [部分可配置]       │
│     ┌─────────────────────────────────────────┐        │
│     │ • 深度测试 (Z-Buffer)                   │        │
│     │ • 模板测试 (Stencil Buffer)             │        │
│     │ • 颜色混合 (Alpha Blending)             │        │
│     │ • 写入渲染目标 (帧缓冲)                 │        │
│     └─────────────────────────────────────────┘        │
│                       ↓                                 │
│              显示到屏幕 🖥️                              │
└─────────────────────────────────────────────────────────┘

关键点：
✅ 顶点着色器 (VS) 和像素着色器 (PS) 是可编程的 (你写代码控制)
✅ 其他阶段是固定功能或可配置的 (硬件内置)
✅ 整个管线是并行的 (多个阶段同时工作)
```

### 1.3 管线的并行性

```
流水线并行（不同阶段同时工作）：

时间 →
┌─────────────────────────────────────────────┐
│ 三角形 0: [IA] → [VS] → [RS] → [PS] → [OM] │
│ 三角形 1:       [IA] → [VS] → [RS] → [PS]  │
│ 三角形 2:             [IA] → [VS] → [RS]   │
│ 三角形 3:                   [IA] → [VS]    │
└─────────────────────────────────────────────┘

同时：
- IA 处理三角形 3
- VS 处理三角形 2
- RS 处理三角形 1
- PS 处理三角形 0

数据并行（同一阶段内并行）：

VS 阶段：
┌──────────────────────────────┐
│ 线程 0: 处理顶点 0            │
│ 线程 1: 处理顶点 1            │  同时执行
│ 线程 2: 处理顶点 2            │
│ ...                           │
│ 线程 N: 处理顶点 N            │
└──────────────────────────────┘

PS 阶段：
┌──────────────────────────────┐
│ 线程 0: 处理像素 0            │
│ 线程 1: 处理像素 1            │  同时执行
│ 线程 2: 处理像素 2            │
│ ...                           │
│ 线程 N: 处理像素 N            │
└──────────────────────────────┘

结果：惊人的并行度！
- 流水线并行：5 个阶段
- 数据并行：数千个线程
- 总并行度：数万个操作同时进行
```

---

## 2. 输入装配阶段

### 2.1 顶点数据结构

```cpp
// 定义顶点格式
struct Vertex {
    float position[3];  // x, y, z (位置)
    float uv[2];        // u, v (纹理坐标)
};

// 全屏四边形的顶点数据（用于显示视频帧）
Vertex vertices[4] = {
    // 位置 (x, y, z)         UV 坐标 (u, v)
    { {-1.0f, -1.0f, 0.0f},  {0.0f, 1.0f} },  // 左下
    { { 1.0f, -1.0f, 0.0f},  {1.0f, 1.0f} },  // 右下
    { {-1.0f,  1.0f, 0.0f},  {0.0f, 0.0f} },  // 左上
    { { 1.0f,  1.0f, 0.0f},  {1.0f, 0.0f} },  // 右上
};

视觉表示：
屏幕坐标系 (NDC: Normalized Device Coordinates)
        y
        ↑
   (-1,1) ┌─────────┐ (1,1)
        │         │
  ──────┼─────────┼────→ x
        │         │
  (-1,-1)└─────────┘ (1,-1)
```

### 2.2 创建顶点缓冲

```cpp
// Direct3D 11 代码

// 1. 创建顶点缓冲 (在 GPU 显存中)
D3D11_BUFFER_DESC bufferDesc = {};
bufferDesc.Usage = D3D11_USAGE_DEFAULT;        // GPU 读
bufferDesc.ByteWidth = sizeof(vertices);       // 4 顶点 × 20 字节 = 80 字节
bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
bufferDesc.CPUAccessFlags = 0;                 // CPU 不访问

D3D11_SUBRESOURCE_DATA initData = {};
initData.pSysMem = vertices;                   // 初始数据

ID3D11Buffer* vertexBuffer;
device->CreateBuffer(&bufferDesc, &initData, &vertexBuffer);

// 2. 定义输入布局（告诉 GPU 如何解析顶点数据）
D3D11_INPUT_ELEMENT_DESC layout[] = {
    // 语义名称  索引 格式                    输入槽 偏移  实例化
    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
};
//            ↑                                    ↑  ↑
//            语义 (VS 输入)                       槽 偏移字节
//                                                  0  position: 0-11 字节
//                                                     texcoord: 12-19 字节

ID3D11InputLayout* inputLayout;
device->CreateInputLayout(layout, 2, vsBlob->GetBufferPointer(), 
    vsBlob->GetBufferSize(), &inputLayout);

// 3. 绑定到管线
UINT stride = sizeof(Vertex);  // 20 字节
UINT offset = 0;
context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
context->IASetInputLayout(inputLayout);
context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
//                               ↑
//                               拓扑类型：三角形条带
```

### 2.3 图元拓扑类型

```
Triangle List (三角形列表)：
每 3 个顶点一个三角形

顶点: 0 1 2  3 4 5
三角形: △0(0,1,2)  △1(3,4,5)

  0──1      3──4
  │ ╱       │ ╱
  2         5

Triangle Strip (三角形条带) ← 视频渲染常用：
共享顶点，每个新顶点形成一个三角形

顶点: 0 1 2 3
三角形: △0(0,1,2)  △1(2,1,3)

  0──1
  │╲ │╲
  2──3

优势：4 个顶点 = 2 个三角形（节省数据）

Line List (线列表)：
每 2 个顶点一条线

  0──1    2──3

Point List (点列表)：
每个顶点一个点

  0  1  2  3
```

---

## 3. 顶点着色器阶段

### 3.1 顶点着色器的作用

```
输入：单个顶点的属性
输出：变换后的顶点属性

主要任务：
1. 坐标变换 (模型空间 → 屏幕空间)
2. 计算并传递数据到像素着色器 (UV、颜色等)

┌───────────────────┐
│  Vertex Shader    │
│                   │
│  输入:            │
│  • position       │
│  • uv             │
│                   │
│  输出:            │
│  • 屏幕位置       │
│  • uv (传递)      │
└───────────────────┘
```

### 3.2 简单的顶点着色器（视频渲染）

```hlsl
// SimpleVertexShader.hlsl

// 输入结构 (对应 C++ 的 Vertex)
struct VSInput {
    float3 position : POSITION;  // 顶点位置
    float2 uv       : TEXCOORD0; // UV 坐标
};

// 输出结构 (传递给像素着色器)
struct VSOutput {
    float4 position : SV_POSITION;  // 屏幕空间位置 (必须)
    float2 uv       : TEXCOORD0;    // UV 坐标 (传递)
};

// 顶点着色器主函数
VSOutput main(VSInput input) {
    VSOutput output;
    
    // 1. 位置变换 (这里是全屏四边形，直接输出)
    output.position = float4(input.position, 1.0);
    //                ↑
    //                从 float3 扩展到 float4 (齐次坐标)
    //                w = 1.0 表示位置 (不是方向)
    
    // 2. 传递 UV 坐标
    output.uv = input.uv;
    
    return output;
}
```

**逐行解析**：

```hlsl
float4 output.position = float4(input.position, 1.0);

解释：
- input.position: (-1, -1, 0) 到 (1, 1, 0) (NDC 坐标)
- float4(..., 1.0): 转换为齐次坐标 (x, y, z, w)
- w = 1.0: 表示这是位置，不是方向向量

为什么是 float4？
GPU 硬件需要齐次坐标 (x/w, y/w, z/w) 来处理透视投影
对于 2D 或正交投影，w=1.0 即可

最终屏幕坐标：
(-1, -1) → 屏幕左下角
( 1,  1) → 屏幕右上角
( 0,  0) → 屏幕中心
```

### 3.3 复杂的顶点着色器（3D 场景）

```hlsl
// 常量缓冲区 (Constant Buffer) - 所有顶点共享的数据
cbuffer TransformBuffer : register(b0) {
    float4x4 worldMatrix;       // 模型 → 世界
    float4x4 viewMatrix;        // 世界 → 相机
    float4x4 projectionMatrix;  // 相机 → 屏幕
};

struct VSInput {
    float3 position : POSITION;
    float3 normal   : NORMAL;    // 法线 (用于光照)
    float2 uv       : TEXCOORD0;
};

struct VSOutput {
    float4 position    : SV_POSITION;  // 屏幕位置
    float3 worldPos    : TEXCOORD0;    // 世界空间位置
    float3 normal      : NORMAL;       // 法线
    float2 uv          : TEXCOORD1;    // UV
};

VSOutput main(VSInput input) {
    VSOutput output;
    
    // 1. 顶点位置变换 (模型空间 → 屏幕空间)
    float4 worldPos = mul(float4(input.position, 1.0), worldMatrix);
    float4 viewPos = mul(worldPos, viewMatrix);
    output.position = mul(viewPos, projectionMatrix);
    
    // 2. 计算世界空间位置 (用于光照)
    output.worldPos = worldPos.xyz;
    
    // 3. 变换法线到世界空间
    output.normal = mul(float4(input.normal, 0.0), worldMatrix).xyz;
    //                                       ↑
    //                                       w=0.0 表示方向向量，不是位置
    
    // 4. 传递 UV
    output.uv = input.uv;
    
    return output;
}
```

**坐标变换过程**：

```
模型空间 (Local Space)：
- 模型自己的坐标系
- 例如：立方体中心是 (0, 0, 0)

  [worldMatrix] 变换
        ↓

世界空间 (World Space)：
- 场景的全局坐标系
- 例如：立方体在世界中的位置 (10, 5, 3)

  [viewMatrix] 变换
        ↓

相机空间 (View Space)：
- 以相机为原点
- 相机看向 -Z 方向

  [projectionMatrix] 变换
        ↓

裁剪空间 (Clip Space)：
- 齐次坐标 (x, y, z, w)
- GPU 硬件自动执行透视除法：(x/w, y/w, z/w)

        ↓

归一化设备坐标 (NDC)：
- x, y, z ∈ [-1, 1]
- 这是 SV_POSITION 的最终值

        ↓

屏幕空间 (Screen Space)：
- 像素坐标
- 由硬件自动转换
```

### 3.4 顶点着色器的并行执行

```
场景：4 个顶点

CPU 准备数据：
┌──────┬──────┬──────┬──────┐
│顶点 0│顶点 1│顶点 2│顶点 3│
└──────┴──────┴──────┴──────┘

GPU 并行执行顶点着色器：
┌──────────────────────────────┐
│ VS 线程 0: 处理顶点 0         │
│ VS 线程 1: 处理顶点 1         │  同时执行
│ VS 线程 2: 处理顶点 2         │  相同代码
│ VS 线程 3: 处理顶点 3         │  不同数据
└──────────────────────────────┘

输出（传递给光栅化）：
┌──────┬──────┬──────┬──────┐
│输出 0│输出 1│输出 2│输出 3│
└──────┴──────┴──────┴──────┘

性能：
- 4 个顶点在 1 个 Warp 中 (32 个线程槽位，只用了 4 个)
- 处理时间 ≈ 1 个顶点的时间
- 如果有 10,000 个顶点：
  * CPU 串行: 10,000 × T
  * GPU 并行: ceil(10,000 / 32) × T ≈ 313 × T
  * 加速比: ~32 倍
```

---

## 4. 光栅化阶段

### 4.1 光栅化的作用

**光栅化 = 三角形 → 像素**

```
输入：3 个顶点 (三角形)
输出：覆盖的所有像素

例子：
顶点 A: (100, 100)  UV: (0.0, 0.0)
顶点 B: (200, 100)  UV: (1.0, 0.0)
顶点 C: (150, 200)  UV: (0.5, 1.0)

三角形：
    C (150,200)
    /\
   /  \
  /    \
 /______\
A        B
(100,100) (200,100)

光栅化后生成的像素（示意）：
     C
    ███
   █████
  ███████
 █████████
A_________B

每个 █ 是一个像素，会执行像素着色器
```

### 4.2 光栅化的步骤

```
1️⃣ 裁剪 (Clipping)
┌─────────────┐ 屏幕边界
│  ╱        ╲ │
│ ╱   可见   ╲│ ← 三角形的这部分保留
│╱___________╲│
└─────────────┘
 ╲           ╱  ← 屏幕外的部分被裁剪掉
  ╲_________╱

2️⃣ 透视除法 (Perspective Division)
(x, y, z, w) → (x/w, y/w, z/w)
- 将齐次坐标转换为 NDC
- 实现透视效果（近大远小）

3️⃣ 视口变换 (Viewport Transform)
NDC [-1, 1] → 屏幕像素 [0, width] × [0, height]

例子：
NDC (-1, -1) → 屏幕 (0, 0)
NDC ( 1,  1) → 屏幕 (1920, 1080)
NDC ( 0,  0) → 屏幕 (960, 540)

4️⃣ 三角形遍历 (Triangle Traversal)
确定哪些像素在三角形内部

算法：边方程 (Edge Equation)
对每个像素 (x, y):
  E0 = (x - x0) * (y1 - y0) - (y - y0) * (x1 - x0)
  E1 = (x - x1) * (y2 - y1) - (y - y1) * (x2 - x1)
  E2 = (x - x2) * (y0 - y2) - (y - y2) * (x0 - x2)
  
  如果 E0, E1, E2 同号 → 像素在三角形内 ✅

5️⃣ 属性插值 (Attribute Interpolation)
对三角形内的每个像素，插值顶点属性

例子：UV 坐标插值
顶点 A: UV = (0.0, 0.0)
顶点 B: UV = (1.0, 0.0)
顶点 C: UV = (0.5, 1.0)

三角形中心像素的 UV:
UV = (0.0 + 1.0 + 0.5) / 3 = (0.5, 0.33)

实际使用重心坐标 (Barycentric Coordinates):
P = w0 * A + w1 * B + w2 * C
其中 w0 + w1 + w2 = 1.0
```

### 4.3 属性插值详解

```
重心坐标插值：

三角形：
    C (UV: 0.5, 1.0)
    /\
   /  \
  / P● \  ← 像素 P
 /      \
A────────B
UV:      UV:
(0,0)    (1,0)

计算 P 的重心坐标 (w0, w1, w2)：
w0 = 面积(PBC) / 面积(ABC)
w1 = 面积(APC) / 面积(ABC)
w2 = 面积(ABP) / 面积(ABC)

假设 P 在三角形中心附近：
w0 = 0.33, w1 = 0.33, w2 = 0.34

插值 UV：
UV_P = w0 * UV_A + w1 * UV_B + w2 * UV_C
     = 0.33 * (0.0, 0.0) + 0.33 * (1.0, 0.0) + 0.34 * (0.5, 1.0)
     = (0.0, 0.0) + (0.33, 0.0) + (0.17, 0.34)
     = (0.5, 0.34)

这个插值后的 UV 传递给像素着色器！
```

### 4.4 光栅化的硬件优化

```
问题：1920x1080 = 2,073,600 像素，如何快速确定哪些在三角形内？

优化 1：层次遍历 (Hierarchical Traversal)
不是逐像素测试，而是：
1. 测试 64x64 块 → 如果完全在外，跳过整个块
2. 测试 8x8 块 → 如果完全在外，跳过
3. 测试单个像素

┌───────┬───────┐
│ 64x64 │ 64x64 │  ← 块测试：快速剔除大片区域
├───────┼───────┤
│ △     │       │  ← 只处理与三角形相交的块
└───────┴───────┘

优化 2：Early-Z 剔除
在像素着色器之前，测试深度
如果像素被遮挡，不执行昂贵的像素着色器

┌──────────────────────┐
│ 三角形 A (深度 0.5)   │
│   遮挡了              │
│ 三角形 B (深度 0.8)   │
└──────────────────────┘

对于三角形 B 的像素：
1. 光栅化生成像素
2. 读取深度缓冲：0.5
3. 当前像素深度：0.8 (更远)
4. Early-Z 剔除 → 不执行像素着色器 ✅

节省：
- 不执行纹理采样（慢）
- 不执行复杂计算
- 性能提升：2-5 倍

优化 3：Quad 模式（2x2 像素组）
GPU 总是以 2x2 像素组（Quad）为单位处理

┌──┬──┐
│P0│P1│  ← 一个 Quad (4 个像素)
├──┼──┤
│P2│P3│
└──┴──┘

为什么？
- 计算 Mipmap 级别需要相邻像素的导数
- 硬件优化：4 个像素共享控制逻辑

代价：
- 即使三角形只覆盖 1 个像素，也会处理整个 Quad
- 其他 3 个像素是"辅助像素"（不输出，但参与计算）
```

---

## 5. 像素着色器阶段

### 5.1 像素着色器的作用

```
输入：插值后的顶点属性
输出：像素颜色 (RGBA)

主要任务：
1. 纹理采样
2. 光照计算
3. 颜色混合
4. 任何per-pixel计算

┌───────────────────┐
│  Pixel Shader     │
│                   │
│  输入:            │
│  • UV (插值)      │
│  • 位置           │
│  • 法线 (可选)    │
│                   │
│  输出:            │
│  • RGBA 颜色      │
└───────────────────┘
```

### 5.2 简单的像素着色器（纹理采样）

```hlsl
// SimplePixelShader.hlsl

// 输入 (来自顶点着色器)
struct PSInput {
    float4 position : SV_POSITION;  // 屏幕位置 (不常用)
    float2 uv       : TEXCOORD0;    // UV 坐标
};

// 纹理和采样器
Texture2D myTexture : register(t0);
SamplerState mySampler : register(s0);

// 像素着色器主函数
float4 main(PSInput input) : SV_TARGET {
    // 采样纹理
    float4 color = myTexture.Sample(mySampler, input.uv);
    
    // 直接返回纹理颜色
    return color;
}
```

**逐行解析**：

```hlsl
float4 color = myTexture.Sample(mySampler, input.uv);

解释：
- myTexture: 纹理对象 (在显存中)
- mySampler: 采样器 (控制过滤、寻址模式)
- input.uv: 纹理坐标 (0.0 - 1.0)
- color: 采样到的 RGBA 颜色

Sample() 做了什么：
1. 将 UV (0.0-1.0) 转换为像素坐标
2. 根据采样器设置决定过滤方式 (Linear/Nearest)
3. 如果需要，计算 Mipmap 级别
4. 从纹理缓存读取数据
5. 执行插值
6. 返回颜色

硬件执行：
- 纹理单元 (Texture Unit) 硬件加速
- 延迟：~400 周期
- 但被延迟隐藏掩盖
```

### 5.3 YUV → RGB 像素着色器（视频渲染）

```hlsl
// YUVtoRGB_PixelShader.hlsl

struct PSInput {
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

// YUV 纹理
Texture2D yTexture : register(t0);  // Y 平面 (亮度)
Texture2D uTexture : register(t1);  // U 平面 (蓝色差)
Texture2D vTexture : register(t2);  // V 平面 (红色差)

SamplerState linearSampler : register(s0);

// YUV → RGB 转换矩阵（BT.709 标准）
static const float3x3 yuv_to_rgb = {
    1.0,     0.0,      1.5748,   // R
    1.0,    -0.1873,  -0.4681,   // G
    1.0,     1.8556,   0.0       // B
};

float4 main(PSInput input) : SV_TARGET {
    // 1. 采样 YUV 三个平面
    float y = yTexture.Sample(linearSampler, input.uv).r;
    float u = uTexture.Sample(linearSampler, input.uv).r - 0.5;
    float v = vTexture.Sample(linearSampler, input.uv).r - 0.5;
    
    // 2. YUV → RGB 转换
    float3 yuv = float3(y, u, v);
    float3 rgb = mul(yuv, yuv_to_rgb);
    
    // 3. 限制范围 [0, 1]
    rgb = saturate(rgb);  // clamp(rgb, 0.0, 1.0)
    
    // 4. 返回颜色
    return float4(rgb, 1.0);
}
```

**执行示例**：

```
像素 (960, 540) - 屏幕中心

输入：
- input.uv = (0.5, 0.5)

步骤 1：采样
- yTexture.Sample(..., (0.5, 0.5)) → 0.5  (中等亮度)
- uTexture.Sample(..., (0.5, 0.5)) → 0.5  (无蓝色偏移)
- vTexture.Sample(..., (0.5, 0.5)) → 0.5  (无红色偏移)

步骤 2：归一化
- y = 0.5
- u = 0.5 - 0.5 = 0.0
- v = 0.5 - 0.5 = 0.0

步骤 3：矩阵乘法
- yuv = (0.5, 0.0, 0.0)
- rgb = (0.5, 0.0, 0.0) × yuv_to_rgb
     = (0.5 × 1.0, 0.5 × 1.0, 0.5 × 1.0)
     = (0.5, 0.5, 0.5)

步骤 4：输出
- color = (0.5, 0.5, 0.5, 1.0) ← 灰色

这个颜色写入帧缓冲的 (960, 540) 位置
```

### 5.4 复杂的像素着色器（光照）

```hlsl
// PhongLighting_PixelShader.hlsl

struct PSInput {
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;  // 世界空间位置
    float3 normal   : NORMAL;     // 法线
    float2 uv       : TEXCOORD1;
};

// 常量缓冲区
cbuffer LightBuffer : register(b0) {
    float3 lightPos;      // 光源位置
    float3 cameraPos;     // 相机位置
    float3 lightColor;    // 光源颜色
};

Texture2D diffuseTexture : register(t0);
SamplerState linearSampler : register(s0);

float4 main(PSInput input) : SV_TARGET {
    // 1. 采样纹理颜色
    float3 baseColor = diffuseTexture.Sample(linearSampler, input.uv).rgb;
    
    // 2. 归一化法线
    float3 N = normalize(input.normal);
    
    // 3. 计算光照方向
    float3 L = normalize(lightPos - input.worldPos);
    
    // 4. 计算视线方向
    float3 V = normalize(cameraPos - input.worldPos);
    
    // 5. 计算半角向量
    float3 H = normalize(L + V);
    
    // 6. 漫反射 (Lambertian)
    float diffuse = max(dot(N, L), 0.0);
    
    // 7. 镜面反射 (Blinn-Phong)
    float specular = pow(max(dot(N, H), 0.0), 32.0);
    
    // 8. 环境光
    float ambient = 0.1;
    
    // 9. 组合
    float3 finalColor = baseColor * (ambient + diffuse) * lightColor 
                      + specular * lightColor;
    
    return float4(finalColor, 1.0);
}
```

### 5.5 像素着色器的并行执行

```
场景：渲染 1920x1080 图像

GPU 组织方式：
- Warp 大小：32 个线程
- Quad 模式：2x2 像素组
- 每个 Warp 处理：32 / 4 = 8 个 Quads = 32 个像素

┌──┬──┬──┬──┐
│Q0│Q1│Q2│Q3│  ← 前 8 个 Quads (32 像素)
├──┼──┼──┼──┤
│Q4│Q5│Q6│Q7│
└──┴──┴──┴──┘

Warp 执行（所有 32 个线程同时）：
周期 1: 所有线程采样 Y 纹理
  线程 0: 采样像素 0
  线程 1: 采样像素 1
  ...
  线程 31: 采样像素 31

周期 N: 所有线程采样 U 纹理
  [同时进行]

周期M: 所有线程计算 RGB
  [同时进行]

总 Warp 数：
- 像素总数: 1,920 × 1,080 = 2,073,600
- 每 Warp: 32 像素
- Warp 数: 2,073,600 / 32 = 64,800 个

SM 调度：
- 128 个 SM (RTX 4090)
- 每个 SM: 64,800 / 128 = 506 个 Warps
- 每个 SM 并发: ~16-32 个 Warps
- 处理时间: 506 / 16 ≈ 32 批次

如果每批次 10 微秒：
总时间: 32 × 10 μs = 320 μs = 0.32 ms

帧率: 1 / 0.32 ms ≈ 3000 fps！
(实际还有其他开销，通常 500-1000 fps)
```

---

## 6. 输出合并阶段

### 6.1 深度测试（Z-Buffer）

```
问题：多个三角形重叠，哪个在前面？

解决：深度缓冲区

深度缓冲区 (Z-Buffer)：
- 与颜色缓冲区大小相同
- 每个像素存储深度值 (0.0 = 近, 1.0 = 远)

侧视图：
相机 →  近      远
       △A △B
       ↓   ↓
       0.3 0.7  ← 深度值

深度测试算法：
for (每个像素) {
    float currentDepth = 读取深度缓冲;
    float newDepth = 新像素的深度;
    
    if (newDepth < currentDepth) {  // 更近
        写入颜色缓冲 = 新颜色;
        写入深度缓冲 = newDepth;
    } else {  // 更远，被遮挡
        丢弃这个像素;  // 不写入
    }
}

配置 (Direct3D 11)：
D3D11_DEPTH_STENCIL_DESC depthDesc = {};
depthDesc.DepthEnable = TRUE;
depthDesc.DepthFunc = D3D11_COMPARISON_LESS;  // newDepth < currentDepth
depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
```

### 6.2 Alpha 混合（透明）

```
Alpha 混合公式：
finalColor = srcColor * srcBlend + dstColor * dstBlend

参数：
- srcColor: 新像素的颜色
- dstColor: 已有像素的颜色 (帧缓冲中)
- srcBlend: 新像素的混合因子
- dstBlend: 已有像素的混合因子

常见模式：

1. 不透明 (No Blend)
   srcBlend = 1.0, dstBlend = 0.0
   finalColor = srcColor × 1.0 + dstColor × 0.0 = srcColor

2. 标准透明 (Alpha Blend)
   srcBlend = srcAlpha, dstBlend = (1 - srcAlpha)
   finalColor = srcColor × srcAlpha + dstColor × (1 - srcAlpha)
   
   例子：
   srcColor = (1.0, 0.0, 0.0, 0.5)  红色，50% 透明
   dstColor = (0.0, 0.0, 1.0, 1.0)  蓝色，不透明
   
   finalColor = (1.0, 0.0, 0.0) × 0.5 + (0.0, 0.0, 1.0) × 0.5
              = (0.5, 0.0, 0.0) + (0.0, 0.0, 0.5)
              = (0.5, 0.0, 0.5)  紫色 (红 + 蓝)

3. 叠加 (Additive Blend)
   srcBlend = 1.0, dstBlend = 1.0
   finalColor = srcColor + dstColor
   用途：光晕、粒子效果

配置 (Direct3D 11)：
D3D11_BLEND_DESC blendDesc = {};
blendDesc.RenderTarget[0].BlendEnable = TRUE;
blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
```

### 6.3 模板测试（Stencil）

```
模板缓冲区：
- 每像素 8 位整数 (0-255)
- 用于遮罩、阴影体积等

例子：镜面反射

步骤 1: 渲染镜子，写入模板
- 镜子区域：stencil = 1
- 其他区域：stencil = 0

步骤 2: 渲染反射物体
- 只在 stencil = 1 的区域渲染
- 结果：反射只出现在镜子内

┌──────────────────┐
│                  │
│  ┌────────┐      │
│  │ 镜子   │      │  镜子区域 stencil = 1
│  │ (反射) │      │  反射物体只绘制在这里
│  └────────┘      │
│                  │
└──────────────────┘
```

---

## 7. 完整渲染示例

### 7.1 渲染视频帧的完整流程

```cpp
// 完整的渲染代码 (Direct3D 11)

// ========== 初始化（只需一次）==========

// 1. 创建顶点缓冲
Vertex vertices[4] = { /* 全屏四边形 */ };
ID3D11Buffer* vertexBuffer = CreateVertexBuffer(device, vertices, sizeof(vertices));

// 2. 编译着色器
ID3DBlob* vsBlob = CompileShader("VertexShader.hlsl", "main", "vs_5_0");
ID3DBlob* psBlob = CompileShader("PixelShader.hlsl", "main", "ps_5_0");

ID3D11VertexShader* vertexShader;
ID3D11PixelShader* pixelShader;
device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), 
    nullptr, &vertexShader);
device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), 
    nullptr, &pixelShader);

// 3. 创建输入布局
D3D11_INPUT_ELEMENT_DESC layout[] = { /* ... */ };
ID3D11InputLayout* inputLayout;
device->CreateInputLayout(layout, 2, vsBlob->GetBufferPointer(), 
    vsBlob->GetBufferSize(), &inputLayout);

// 4. 创建纹理（用于 YUV 数据）
ID3D11Texture2D* yTexture = CreateTexture(device, 1920, 1080, DXGI_FORMAT_R8_UNORM);
ID3D11Texture2D* uTexture = CreateTexture(device, 960, 540, DXGI_FORMAT_R8_UNORM);
ID3D11Texture2D* vTexture = CreateTexture(device, 960, 540, DXGI_FORMAT_R8_UNORM);

// 5. 创建采样器
D3D11_SAMPLER_DESC samplerDesc = {};
samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;  // 三线性过滤
samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;

ID3D11SamplerState* sampler;
device->CreateSamplerState(&samplerDesc, &sampler);

// ========== 渲染每一帧 ==========

void RenderFrame(AVFrame* frame) {
    // 1️⃣ 上传 YUV 数据到 GPU
    context->UpdateSubresource(yTexture, 0, nullptr, 
        frame->data[0], frame->linesize[0], 0);
    context->UpdateSubresource(uTexture, 0, nullptr, 
        frame->data[1], frame->linesize[1], 0);
    context->UpdateSubresource(vTexture, 0, nullptr, 
        frame->data[2], frame->linesize[2], 0);
    
    // 2️⃣ 设置渲染目标
    context->OMSetRenderTargets(1, &renderTargetView, nullptr);
    
    // 3️⃣ 清空渲染目标
    float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    context->ClearRenderTargetView(renderTargetView, clearColor);
    
    // 4️⃣ 设置视口
    D3D11_VIEWPORT viewport = {0, 0, 1920, 1080, 0.0f, 1.0f};
    context->RSSetViewports(1, &viewport);
    
    // 5️⃣ 设置输入装配
    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
    context->IASetInputLayout(inputLayout);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    
    // 6️⃣ 设置着色器
    context->VSSetShader(vertexShader, nullptr, 0);
    context->PSSetShader(pixelShader, nullptr, 0);
    
    // 7️⃣ 绑定纹理和采样器
    ID3D11ShaderResourceView* srvs[3] = {ySRV, uSRV, vSRV};
    context->PSSetShaderResources(0, 3, srvs);
    context->PSSetSamplers(0, 1, &sampler);
    
    // 8️⃣ 绘制！
    context->Draw(4, 0);  // 4 个顶点 (全屏四边形)
    
    // 9️⃣ 呈现
    swapChain->Present(1, 0);  // VSync
}
```

### 7.2 管线执行时间线

```
时间线 (假设帧率 60 fps, 每帧 16.6 ms)：

CPU 时间线：
[0 ms]    开始帧
[0.1 ms]  UpdateSubresource (上传 YUV 数据) ← CPU → GPU 传输
[0.2 ms]  设置渲染状态
[0.3 ms]  Draw() 调用 ← 提交命令到 GPU
[0.4 ms]  CPU 继续做其他事 (解码下一帧等)

GPU 时间线：
[0.3 ms]  收到 Draw 命令
[0.4 ms]  输入装配 (IA)
[0.5 ms]  顶点着色器 (VS) - 4 个顶点，几乎瞬间
[0.6 ms]  光栅化 (RS) - 生成 2,073,600 个像素
[1.0 ms]  像素着色器 (PS) - 主要耗时！
          - 采样 3 个纹理 (Y, U, V)
          - YUV → RGB 转换
          - 2,073,600 像素并行处理
[1.3 ms]  输出合并 (OM)
[1.4 ms]  Present - 等待 VSync
[16.6 ms] VSync - 显示到屏幕

性能瓶颈：
- 像素着色器：0.4 ms (28% 时间)
- VSync 等待：15.2 ms (92% 时间) ← 主要"浪费"

优化潜力：
- 关闭 VSync: 可达 1000+ fps
- 4K 分辨率: 像素着色器时间 × 4 ≈ 1.6 ms
- 仍然非常快！
```

---

## 📚 总结

### 核心概念回顾

1. **渲染管线的 5 个阶段**
   - 输入装配 → 顶点着色器 → 光栅化 → 像素着色器 → 输出合并
   
2. **可编程阶段**
   - 顶点着色器：变换顶点，计算属性
   - 像素着色器：计算像素颜色，纹理采样
   
3. **固定功能阶段**
   - 光栅化：三角形 → 像素，属性插值
   - 输出合并：深度测试、混合
   
4. **并行执行**
   - 流水线并行：5 个阶段同时工作
   - 数据并行：数千线程同时处理
   
5. **性能关键点**
   - 像素着色器通常是瓶颈
   - 纹理采样是最常见操作
   - Early-Z 可大幅提升性能

### 关键代码模式

```hlsl
// 顶点着色器模板
VSOutput VS(VSInput input) {
    VSOutput output;
    output.position = TransformPosition(input.position);
    output.uv = input.uv;  // 传递属性
    return output;
}

// 像素着色器模板
float4 PS(PSInput input) : SV_TARGET {
    float4 color = texture.Sample(sampler, input.uv);  // 采样
    // ... 计算
    return color;
}
```

---

## 🎯 完成！

你现在已经完成了 **Part 2（GPU 架构与渲染管线）** 的学习！

### 已掌握的知识

✅ Part 1：纹理的本质、采样、过滤、格式  
✅ Part 2A：GPU 架构、并行计算、内存系统  
✅ Part 2B：渲染管线、着色器编程、光栅化

### 下一步学习

在后续的 Part 中，我们可以深入：
- **Part 3：软件渲染实现**（CPU 如何一步步渲染）
- **Part 4：高级着色器技术**（光照、阴影、后处理）
- **Part 5：性能优化**（批处理、实例化、LOD）
- **Part 6：实战案例**（完整播放器渲染流程）

**准备好继续了吗？告诉我你想深入哪个主题！** 🚀
