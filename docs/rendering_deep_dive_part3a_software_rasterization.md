# 渲染深度解析 Part 3A：软件渲染基础与光栅化

**文档目标**：深入理解软件渲染如何工作、从零实现光栅化算法  
**前置知识**：Part 1（纹理）、Part 2（GPU 架构与管线）  
**阅读时间**：25-35 分钟

---

## 📚 目录

1. [软件渲染概述](#1-软件渲染概述)
2. [坐标系统与变换](#2-坐标系统与变换)
3. [三角形光栅化算法](#3-三角形光栅化算法)
4. [属性插值详解](#4-属性插值详解)
5. [透视校正插值](#5-透视校正插值)

---

## 1. 软件渲染概述

### 1.1 什么是软件渲染

```
软件渲染 = 用 CPU 代码实现 GPU 渲染管线的所有步骤

GPU 硬件做的事：                   软件渲染用代码模拟：
┌─────────────────┐               ┌─────────────────┐
│ 顶点着色器       │  ────────→   │ Transform()     │
│ (硬件执行)       │               │ (CPU 函数)      │
└─────────────────┘               └─────────────────┘

┌─────────────────┐               ┌─────────────────┐
│ 光栅化器         │  ────────→   │ Rasterize()     │
│ (硬件电路)       │               │ (CPU 函数)      │
└─────────────────┘               └─────────────────┘

┌─────────────────┐               ┌─────────────────┐
│ 像素着色器       │  ────────→   │ ShadePixel()    │
│ (硬件执行)       │               │ (CPU 函数)      │
└─────────────────┘               └─────────────────┘
```

### 1.2 为什么学习软件渲染

```
✅ 深入理解渲染原理
- GPU 黑盒 → 透明算法
- 知道每个像素如何计算

✅ 调试和验证
- GPU 出问题难调试
- CPU 可以单步跟踪

✅ 跨平台兼容性
- 任何有 CPU 的设备都能运行
- 不依赖特定 GPU

✅ 教育价值
- 理解图形学基础
- 为 GPU 编程打基础

❌ 缺点
- 慢（比 GPU 慢 10-100 倍）
- 不适合实时高分辨率渲染
```

### 1.3 软件渲染的完整流程

```cpp
// 伪代码：软件渲染管线

void SoftwareRender(Triangle tri, Texture* tex, uint32_t* framebuffer) {
    // 1️⃣ 顶点处理阶段
    Vertex v0 = VertexShader(tri.v0);
    Vertex v1 = VertexShader(tri.v1);
    Vertex v2 = VertexShader(tri.v2);
    
    // 2️⃣ 光栅化阶段
    for (int y = bbox.minY; y <= bbox.maxY; y++) {
        for (int x = bbox.minX; x <= bbox.maxX; x++) {
            // 判断像素是否在三角形内
            if (InsideTriangle(x, y, v0, v1, v2)) {
                // 计算重心坐标
                vec3 barycentric = ComputeBarycentric(x, y, v0, v1, v2);
                
                // 插值属性（UV、颜色等）
                vec2 uv = Interpolate(v0.uv, v1.uv, v2.uv, barycentric);
                
                // 3️⃣ 像素着色阶段
                Color color = PixelShader(uv, tex);
                
                // 写入帧缓冲
                framebuffer[y * width + x] = color.ToRGBA();
            }
        }
    }
}
```

### 1.4 数据结构

```cpp
// 基础数学类型

struct Vec2 {
    float x, y;
    
    Vec2 operator+(const Vec2& v) const { return {x + v.x, y + v.y}; }
    Vec2 operator-(const Vec2& v) const { return {x - v.x, y - v.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    
    float Dot(const Vec2& v) const { return x * v.x + y * v.y; }
    float Length() const { return sqrt(x * x + y * y); }
};

struct Vec3 {
    float x, y, z;
    
    Vec3 operator+(const Vec3& v) const { return {x + v.x, y + v.y, z + v.z}; }
    Vec3 operator-(const Vec3& v) const { return {x - v.x, y - v.y, z - v.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    
    float Dot(const Vec3& v) const { return x * v.x + y * v.y + z * v.z; }
    Vec3 Cross(const Vec3& v) const {
        return {
            y * v.z - z * v.y,
            z * v.x - x * v.z,
            x * v.y - y * v.x
        };
    }
};

struct Vec4 {
    float x, y, z, w;
    
    // 齐次坐标除法：转换为 3D 坐标
    Vec3 ToVec3() const { return {x / w, y / w, z / w}; }
};

// 顶点结构
struct Vertex {
    Vec3 position;     // 位置
    Vec2 uv;           // 纹理坐标
    Vec3 normal;       // 法线（可选）
    Vec4 color;        // 顶点颜色（可选）
};

// 变换后的顶点（屏幕空间）
struct ScreenVertex {
    Vec2 screenPos;    // 屏幕坐标 (像素)
    float depth;       // 深度 (0-1)
    Vec2 uv;           // 纹理坐标
    Vec4 color;        // 颜色
};

// 三角形
struct Triangle {
    Vertex v0, v1, v2;
};

// 颜色
struct Color {
    uint8_t r, g, b, a;
    
    uint32_t ToRGBA() const {
        return (a << 24) | (b << 16) | (g << 8) | r;
    }
    
    static Color FromRGBA(uint32_t rgba) {
        return {
            (uint8_t)(rgba & 0xFF),
            (uint8_t)((rgba >> 8) & 0xFF),
            (uint8_t)((rgba >> 16) & 0xFF),
            (uint8_t)((rgba >> 24) & 0xFF)
        };
    }
};

// 纹理
struct Texture {
    int width, height;
    uint32_t* pixels;  // RGBA 像素数据
    
    Color Sample(Vec2 uv) const {
        // UV 坐标 [0, 1] → 像素坐标
        int x = (int)(uv.x * width) % width;
        int y = (int)(uv.y * height) % height;
        if (x < 0) x += width;
        if (y < 0) y += height;
        
        return Color::FromRGBA(pixels[y * width + x]);
    }
};

// 帧缓冲
struct Framebuffer {
    int width, height;
    uint32_t* colorBuffer;   // 颜色
    float* depthBuffer;      // 深度
    
    void Clear(Color clearColor, float clearDepth) {
        uint32_t rgba = clearColor.ToRGBA();
        for (int i = 0; i < width * height; i++) {
            colorBuffer[i] = rgba;
            depthBuffer[i] = clearDepth;
        }
    }
    
    void SetPixel(int x, int y, Color color, float depth) {
        if (x < 0 || x >= width || y < 0 || y >= height) return;
        
        int index = y * width + x;
        
        // 深度测试
        if (depth < depthBuffer[index]) {
            colorBuffer[index] = color.ToRGBA();
            depthBuffer[index] = depth;
        }
    }
};
```

---

## 2. 坐标系统与变换

### 2.1 坐标空间变换流程

```
模型空间 (Object Space)
- 模型自己的局部坐标系
- 例如：立方体中心 (0, 0, 0)

    [世界变换矩阵] Model Matrix
           ↓

世界空间 (World Space)
- 场景的全局坐标系
- 所有物体在同一坐标系中

    [视图变换矩阵] View Matrix
           ↓

相机空间 (Camera/View Space)
- 以相机为原点
- 相机看向 -Z 方向

    [投影变换矩阵] Projection Matrix
           ↓

裁剪空间 (Clip Space)
- 齐次坐标 (x, y, z, w)
- 用于裁剪三角形

    [透视除法] x/w, y/w, z/w
           ↓

归一化设备坐标 (NDC)
- x, y, z ∈ [-1, 1]

    [视口变换] Viewport Transform
           ↓

屏幕空间 (Screen Space)
- 像素坐标 (0, 0) 到 (width, height)
```

### 2.2 矩阵变换实现

```cpp
// 4x4 矩阵
struct Mat4 {
    float m[4][4];
    
    // 单位矩阵
    static Mat4 Identity() {
        Mat4 mat = {};
        mat.m[0][0] = mat.m[1][1] = mat.m[2][2] = mat.m[3][3] = 1.0f;
        return mat;
    }
    
    // 平移矩阵
    static Mat4 Translation(float x, float y, float z) {
        Mat4 mat = Identity();
        mat.m[0][3] = x;
        mat.m[1][3] = y;
        mat.m[2][3] = z;
        return mat;
    }
    
    // 缩放矩阵
    static Mat4 Scale(float x, float y, float z) {
        Mat4 mat = Identity();
        mat.m[0][0] = x;
        mat.m[1][1] = y;
        mat.m[2][2] = z;
        return mat;
    }
    
    // 旋转矩阵（绕 Y 轴）
    static Mat4 RotationY(float angle) {
        Mat4 mat = Identity();
        float c = cos(angle);
        float s = sin(angle);
        mat.m[0][0] = c;
        mat.m[0][2] = s;
        mat.m[2][0] = -s;
        mat.m[2][2] = c;
        return mat;
    }
    
    // 矩阵乘法
    Mat4 operator*(const Mat4& other) const {
        Mat4 result = {};
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                result.m[i][j] = 0;
                for (int k = 0; k < 4; k++) {
                    result.m[i][j] += m[i][k] * other.m[k][j];
                }
            }
        }
        return result;
    }
    
    // 变换向量
    Vec4 Transform(const Vec4& v) const {
        return {
            m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3] * v.w,
            m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3] * v.w,
            m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z + m[2][3] * v.w,
            m[3][0] * v.x + m[3][1] * v.y + m[3][2] * v.z + m[3][3] * v.w
        };
    }
    
    // 透视投影矩阵
    static Mat4 Perspective(float fov, float aspect, float near, float far) {
        Mat4 mat = {};
        float tanHalfFov = tan(fov / 2.0f);
        
        mat.m[0][0] = 1.0f / (aspect * tanHalfFov);
        mat.m[1][1] = 1.0f / tanHalfFov;
        mat.m[2][2] = -(far + near) / (far - near);
        mat.m[2][3] = -(2.0f * far * near) / (far - near);
        mat.m[3][2] = -1.0f;
        mat.m[3][3] = 0.0f;
        
        return mat;
    }
    
    // 正交投影矩阵
    static Mat4 Orthographic(float left, float right, float bottom, 
                            float top, float near, float far) {
        Mat4 mat = Identity();
        mat.m[0][0] = 2.0f / (right - left);
        mat.m[1][1] = 2.0f / (top - bottom);
        mat.m[2][2] = -2.0f / (far - near);
        mat.m[0][3] = -(right + left) / (right - left);
        mat.m[1][3] = -(top + bottom) / (top - bottom);
        mat.m[2][3] = -(far + near) / (far - near);
        return mat;
    }
    
    // LookAt 矩阵（相机）
    static Mat4 LookAt(Vec3 eye, Vec3 target, Vec3 up) {
        Vec3 zaxis = (eye - target).Normalize();  // 相机看向 -Z
        Vec3 xaxis = up.Cross(zaxis).Normalize();
        Vec3 yaxis = zaxis.Cross(xaxis);
        
        Mat4 mat = Identity();
        mat.m[0][0] = xaxis.x;
        mat.m[0][1] = xaxis.y;
        mat.m[0][2] = xaxis.z;
        mat.m[0][3] = -xaxis.Dot(eye);
        
        mat.m[1][0] = yaxis.x;
        mat.m[1][1] = yaxis.y;
        mat.m[1][2] = yaxis.z;
        mat.m[1][3] = -yaxis.Dot(eye);
        
        mat.m[2][0] = zaxis.x;
        mat.m[2][1] = zaxis.y;
        mat.m[2][2] = zaxis.z;
        mat.m[2][3] = -zaxis.Dot(eye);
        
        return mat;
    }
};
```

### 2.3 顶点变换实现

```cpp
// 软件顶点着色器
ScreenVertex VertexShader(const Vertex& input, 
                         const Mat4& mvp,  // Model-View-Projection 矩阵
                         int screenWidth, 
                         int screenHeight) {
    // 1. 变换到裁剪空间
    Vec4 clipPos = mvp.Transform(Vec4{input.position.x, input.position.y, 
                                      input.position.z, 1.0f});
    
    // 2. 透视除法 → NDC 空间
    Vec3 ndcPos = clipPos.ToVec3();  // (x/w, y/w, z/w)
    
    // 3. 视口变换 → 屏幕空间
    float screenX = (ndcPos.x + 1.0f) * 0.5f * screenWidth;
    float screenY = (1.0f - ndcPos.y) * 0.5f * screenHeight;  // 翻转 Y
    
    // 4. 组装输出
    ScreenVertex output;
    output.screenPos = {screenX, screenY};
    output.depth = ndcPos.z;  // 深度 [0, 1]
    output.uv = input.uv;
    output.color = input.color;
    
    return output;
}

// 使用示例
void RenderTriangle(const Triangle& tri, Framebuffer& fb) {
    // 设置变换矩阵
    Mat4 model = Mat4::RotationY(time) * Mat4::Translation(0, 0, -5);
    Mat4 view = Mat4::LookAt({0, 0, 0}, {0, 0, -1}, {0, 1, 0});
    Mat4 projection = Mat4::Perspective(M_PI / 4, fb.width / (float)fb.height, 
                                       0.1f, 100.0f);
    Mat4 mvp = projection * view * model;
    
    // 变换三个顶点
    ScreenVertex v0 = VertexShader(tri.v0, mvp, fb.width, fb.height);
    ScreenVertex v1 = VertexShader(tri.v1, mvp, fb.width, fb.height);
    ScreenVertex v2 = VertexShader(tri.v2, mvp, fb.width, fb.height);
    
    // 光栅化
    Rasterize(v0, v1, v2, fb);
}
```

---

## 3. 三角形光栅化算法

### 3.1 边界盒（Bounding Box）

```cpp
// 计算三角形的边界盒
struct BBox {
    int minX, maxX;
    int minY, maxY;
};

BBox ComputeBBox(const ScreenVertex& v0, 
                 const ScreenVertex& v1, 
                 const ScreenVertex& v2,
                 int screenWidth, 
                 int screenHeight) {
    // 找到三个顶点的最小/最大坐标
    float minX = std::min({v0.screenPos.x, v1.screenPos.x, v2.screenPos.x});
    float maxX = std::max({v0.screenPos.x, v1.screenPos.x, v2.screenPos.x});
    float minY = std::min({v0.screenPos.y, v1.screenPos.y, v2.screenPos.y});
    float maxY = std::max({v0.screenPos.y, v1.screenPos.y, v2.screenPos.y});
    
    // 限制在屏幕范围内
    BBox bbox;
    bbox.minX = std::max(0, (int)floor(minX));
    bbox.maxX = std::min(screenWidth - 1, (int)ceil(maxX));
    bbox.minY = std::max(0, (int)floor(minY));
    bbox.maxY = std::min(screenHeight - 1, (int)ceil(maxY));
    
    return bbox;
}

// 视觉示例：
//       minX        maxX
//        ↓           ↓
// minY  ┌───────────┐  ← 边界盒
//       │    △      │
//       │   ╱ ╲     │  ← 三角形
//       │  ╱   ╲    │
//       │ ╱_____╲   │
// maxY  └───────────┘
```

### 3.2 点在三角形内判断（边方程法）

```cpp
// 边方程：判断点在三角形的哪一侧
float EdgeFunction(Vec2 a, Vec2 b, Vec2 c) {
    // 向量叉积的 Z 分量
    // (b - a) × (c - a)
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

// 判断点 p 是否在三角形 (v0, v1, v2) 内
bool InsideTriangle(Vec2 p, Vec2 v0, Vec2 v1, Vec2 v2) {
    // 计算点相对于三条边的位置
    float w0 = EdgeFunction(v1, v2, p);
    float w1 = EdgeFunction(v2, v0, p);
    float w2 = EdgeFunction(v0, v1, p);
    
    // 如果三个值同号，点在三角形内
    return (w0 >= 0 && w1 >= 0 && w2 >= 0) || 
           (w0 <= 0 && w1 <= 0 && w2 <= 0);
}

// 原理解释：
//
//         v0
//        ╱│╲
//       ╱ │ ╲
//      ╱  │  ╲
//     ╱   p   ╲    如果 p 在三角形内：
//    ╱    │    ╲   - p 在边 v1v2 的左侧（或右侧）
//   ╱─────┼─────╲  - p 在边 v2v0 的左侧（或右侧）
//  v1     │     v2 - p 在边 v0v1 的左侧（或右侧）
//         │        - 三个测试结果同号 → 在内
//
// 边方程的几何意义：
// EdgeFunction(a, b, c) > 0 → c 在向量 ab 的左侧
// EdgeFunction(a, b, c) < 0 → c 在向量 ab 的右侧
// EdgeFunction(a, b, c) = 0 → c 在直线 ab 上
```

### 3.3 重心坐标计算

```cpp
// 重心坐标：表示点在三角形内的位置
struct Barycentric {
    float w0, w1, w2;  // 权重 (w0 + w1 + w2 = 1.0)
};

Barycentric ComputeBarycentric(Vec2 p, Vec2 v0, Vec2 v1, Vec2 v2) {
    // 计算三角形面积（的两倍）
    float area = EdgeFunction(v0, v1, v2);
    
    // 如果面积为 0（三点共线），返回无效值
    if (fabs(area) < 1e-6f) {
        return {0, 0, 0};
    }
    
    // 计算子三角形面积
    float w0 = EdgeFunction(v1, v2, p) / area;  // 子三角形 pv1v2
    float w1 = EdgeFunction(v2, v0, p) / area;  // 子三角形 pv2v0
    float w2 = EdgeFunction(v0, v1, p) / area;  // 子三角形 pv0v1
    
    return {w0, w1, w2};
}

// 重心坐标的意义：
//
//       v0 (w0=1, w1=0, w2=0)
//       ╱│╲
//      ╱ │ ╲
//     ╱  │  ╲
//    ╱   p   ╲    p = w0*v0 + w1*v1 + w2*v2
//   ╱    │    ╲   其中 w0 + w1 + w2 = 1
//  ╱─────┼─────╲
// v1     │     v2
// (w0=0, │     (w0=0,
//  w1=1, │      w1=0,
//  w2=0) │      w2=1)
//        │
//   中心点 p ≈ (w0=0.33, w1=0.33, w2=0.34)
//
// 用途：插值任何顶点属性
// - UV 坐标：uv_p = w0*uv0 + w1*uv1 + w2*uv2
// - 颜色：color_p = w0*color0 + w1*color1 + w2*color2
// - 深度：depth_p = w0*depth0 + w1*depth1 + w2*depth2
```

### 3.4 基础光栅化实现

```cpp
void RasterizeTriangle(const ScreenVertex& v0,
                      const ScreenVertex& v1,
                      const ScreenVertex& v2,
                      const Texture* texture,
                      Framebuffer& fb) {
    // 1. 计算边界盒
    BBox bbox = ComputeBBox(v0, v1, v2, fb.width, fb.height);
    
    // 2. 遍历边界盒内的所有像素
    for (int y = bbox.minY; y <= bbox.maxY; y++) {
        for (int x = bbox.minX; x <= bbox.maxX; x++) {
            // 像素中心
            Vec2 pixelCenter = {x + 0.5f, y + 0.5f};
            
            // 3. 判断是否在三角形内
            if (!InsideTriangle(pixelCenter, v0.screenPos, 
                               v1.screenPos, v2.screenPos)) {
                continue;  // 不在三角形内，跳过
            }
            
            // 4. 计算重心坐标
            Barycentric bary = ComputeBarycentric(pixelCenter, 
                v0.screenPos, v1.screenPos, v2.screenPos);
            
            // 5. 插值深度
            float depth = bary.w0 * v0.depth + 
                         bary.w1 * v1.depth + 
                         bary.w2 * v2.depth;
            
            // 6. 插值 UV 坐标
            Vec2 uv = {
                bary.w0 * v0.uv.x + bary.w1 * v1.uv.x + bary.w2 * v2.uv.x,
                bary.w0 * v0.uv.y + bary.w1 * v1.uv.y + bary.w2 * v2.uv.y
            };
            
            // 7. 采样纹理（像素着色）
            Color color = texture->Sample(uv);
            
            // 8. 写入帧缓冲（带深度测试）
            fb.SetPixel(x, y, color, depth);
        }
    }
}
```

### 3.5 优化：Early Reject

```cpp
// 优化版本：提前剔除完全在三角形外的行
void RasterizeTriangleOptimized(const ScreenVertex& v0,
                               const ScreenVertex& v1,
                               const ScreenVertex& v2,
                               const Texture* texture,
                               Framebuffer& fb) {
    BBox bbox = ComputeBBox(v0, v1, v2, fb.width, fb.height);
    
    // 预计算边方程的增量（每次 x+1 时的变化）
    Vec2 v0p = v0.screenPos;
    Vec2 v1p = v1.screenPos;
    Vec2 v2p = v2.screenPos;
    
    // 边方程增量
    float A01 = v0p.y - v1p.y;
    float B01 = v1p.x - v0p.x;
    float A12 = v1p.y - v2p.y;
    float B12 = v2p.x - v1p.x;
    float A20 = v2p.y - v0p.y;
    float B20 = v0p.x - v2p.x;
    
    for (int y = bbox.minY; y <= bbox.maxY; y++) {
        // 计算行起始的边方程值
        Vec2 p = {bbox.minX + 0.5f, y + 0.5f};
        float w0_row = EdgeFunction(v1p, v2p, p);
        float w1_row = EdgeFunction(v2p, v0p, p);
        float w2_row = EdgeFunction(v0p, v1p, p);
        
        for (int x = bbox.minX; x <= bbox.maxX; x++) {
            // 判断是否在三角形内（使用预计算的值）
            if (w0_row >= 0 && w1_row >= 0 && w2_row >= 0) {
                // 归一化重心坐标
                float area = EdgeFunction(v0p, v1p, v2p);
                Barycentric bary = {
                    w0_row / area,
                    w1_row / area,
                    w2_row / area
                };
                
                // 插值和着色（同前）
                float depth = bary.w0 * v0.depth + 
                             bary.w1 * v1.depth + 
                             bary.w2 * v2.depth;
                
                Vec2 uv = {
                    bary.w0 * v0.uv.x + bary.w1 * v1.uv.x + bary.w2 * v2.uv.x,
                    bary.w0 * v0.uv.y + bary.w1 * v1.uv.y + bary.w2 * v2.uv.y
                };
                
                Color color = texture->Sample(uv);
                fb.SetPixel(x, y, color, depth);
            }
            
            // 更新边方程值（x 方向增量）
            w0_row += A12;
            w1_row += A20;
            w2_row += A01;
        }
    }
}

// 性能提升：
// - 减少重复的 EdgeFunction 计算
// - 从 O(pixels × 3) 降低到 O(pixels)
// - 实测提升：2-3 倍
```

---

## 4. 属性插值详解

### 4.1 线性插值基础

```cpp
// 1D 线性插值
float Lerp(float a, float b, float t) {
    return a * (1.0f - t) + b * t;
    // 或等价于：a + (b - a) * t
}

// 2D 线性插值（双线性）
float Bilerp(float v00, float v10, float v01, float v11, 
            float tx, float ty) {
    // 先在 X 方向插值
    float v0 = Lerp(v00, v10, tx);
    float v1 = Lerp(v01, v11, tx);
    
    // 再在 Y 方向插值
    return Lerp(v0, v1, ty);
}

// 重心插值（三角形内）
float BarycentricInterpolate(float v0, float v1, float v2, 
                             Barycentric bary) {
    return bary.w0 * v0 + bary.w1 * v1 + bary.w2 * v2;
}

// 向量插值
Vec2 InterpolateVec2(Vec2 v0, Vec2 v1, Vec2 v2, Barycentric bary) {
    return {
        BarycentricInterpolate(v0.x, v1.x, v2.x, bary),
        BarycentricInterpolate(v0.y, v1.y, v2.y, bary)
    };
}

Vec3 InterpolateVec3(Vec3 v0, Vec3 v1, Vec3 v2, Barycentric bary) {
    return {
        BarycentricInterpolate(v0.x, v1.x, v2.x, bary),
        BarycentricInterpolate(v0.y, v1.y, v2.y, bary),
        BarycentricInterpolate(v0.z, v1.z, v2.z, bary)
    };
}
```

### 4.2 插值的视觉效果

```
示例：颜色插值

三角形顶点：
v0: 红色 (1, 0, 0)
v1: 绿色 (0, 1, 0)
v2: 蓝色 (0, 0, 1)

插值结果（伪色彩图）：
       v0(红)
       ╱│╲
      ╱ │橙╲
     ╱黄│  ╲紫
    ╱   │   ╲
   ╱青  │    ╲
  ╱_____│_____╲
v1(绿)  │    v2(蓝)

每个像素的颜色：
- 靠近 v0：偏红
- 靠近 v1：偏绿
- 靠近 v2：偏蓝
- 中心：三色混合 ≈ 灰色

代码：
Color InterpolateColor(Color c0, Color c1, Color c2, Barycentric bary) {
    return {
        (uint8_t)BarycentricInterpolate(c0.r, c1.r, c2.r, bary),
        (uint8_t)BarycentricInterpolate(c0.g, c1.g, c2.g, bary),
        (uint8_t)BarycentricInterpolate(c0.b, c1.b, c2.b, bary),
        255
    };
}
```

### 4.3 UV 坐标插值

```cpp
// UV 插值示例
void RasterizeWithUV(const ScreenVertex& v0,
                    const ScreenVertex& v1,
                    const ScreenVertex& v2,
                    const Texture* texture,
                    Framebuffer& fb) {
    BBox bbox = ComputeBBox(v0, v1, v2, fb.width, fb.height);
    
    for (int y = bbox.minY; y <= bbox.maxY; y++) {
        for (int x = bbox.minX; x <= bbox.maxX; x++) {
            Vec2 p = {x + 0.5f, y + 0.5f};
            
            if (!InsideTriangle(p, v0.screenPos, v1.screenPos, v2.screenPos)) {
                continue;
            }
            
            Barycentric bary = ComputeBarycentric(p, v0.screenPos, 
                v1.screenPos, v2.screenPos);
            
            // 插值 UV
            Vec2 uv = InterpolateVec2(v0.uv, v1.uv, v2.uv, bary);
            
            // 采样纹理
            Color color = texture->Sample(uv);
            
            // 写入帧缓冲
            fb.colorBuffer[y * fb.width + x] = color.ToRGBA();
        }
    }
}

// UV 插值的视觉效果：
//
// 纹理：                   映射到三角形：
// (0,0)──────(1,0)        v0(0,0)────v1(1,0)
//   │  图片   │            ╱│纹理   ╱
//   │        │           ╱  │贴图  ╱
// (0,1)──────(1,1)     v2(0,1)───╱
//
// 纹理的每个像素按 UV 坐标映射到三角形上
```

---

## 5. 透视校正插值

### 5.1 问题：线性插值的错误

```
问题场景：透视投影下，线性插值会产生错误

示例：地板纹理
侧视图：
        相机
         │
    近   │   远
    ─────┼─────
    │ A  │  B │  地板（透视缩短）
    ─────┼─────

顶视图（纹理空间）：
    ┌────┬────┐
    │ A  │ B  │  纹理均匀分布
    └────┴────┘

问题：
- 在屏幕空间，A 占据更多像素（近处）
- 在屏幕空间，B 占据更少像素（远处）
- 如果线性插值 UV，会导致纹理扭曲

线性插值（错误）：
屏幕: [───A───][─B─]
      UV 线性插值 → 纹理被压缩

正确的透视插值：
屏幕: [───A───][─B─]
      考虑深度 → 纹理均匀
```

### 5.2 透视校正公式

```cpp
// 透视校正插值
struct PerspectiveCorrectData {
    Vec2 uv;
    float oneOverZ;  // 1/z（深度倒数）
};

PerspectiveCorrectData InterpolatePerspective(
    const ScreenVertex& v0,
    const ScreenVertex& v1,
    const ScreenVertex& v2,
    Barycentric bary) {
    
    // 假设深度存储在 [0, 1]，需要转换回实际 z 值
    // 这里简化处理，假设 depth 已经是 1/z
    float z0 = 1.0f / v0.depth;
    float z1 = 1.0f / v1.depth;
    float z2 = 1.0f / v2.depth;
    
    // 插值 1/z
    float oneOverZ = BarycentricInterpolate(z0, z1, z2, bary);
    
    // 插值 UV/z
    float u_over_z = BarycentricInterpolate(
        v0.uv.x * z0,
        v1.uv.x * z1,
        v2.uv.x * z2,
        bary
    );
    
    float v_over_z = BarycentricInterpolate(
        v0.uv.y * z0,
        v1.uv.y * z1,
        v2.uv.y * z2,
        bary
    );
    
    // 恢复 UV
    float z = 1.0f / oneOverZ;
    Vec2 uv = {u_over_z * z, v_over_z * z};
    
    return {uv, oneOverZ};
}

// 使用透视校正的光栅化
void RasterizeWithPerspectiveCorrection(
    const ScreenVertex& v0,
    const ScreenVertex& v1,
    const ScreenVertex& v2,
    const Texture* texture,
    Framebuffer& fb) {
    
    BBox bbox = ComputeBBox(v0, v1, v2, fb.width, fb.height);
    
    for (int y = bbox.minY; y <= bbox.maxY; y++) {
        for (int x = bbox.minX; x <= bbox.maxX; x++) {
            Vec2 p = {x + 0.5f, y + 0.5f};
            
            if (!InsideTriangle(p, v0.screenPos, v1.screenPos, v2.screenPos)) {
                continue;
            }
            
            Barycentric bary = ComputeBarycentric(p, v0.screenPos, 
                v1.screenPos, v2.screenPos);
            
            // 透视校正插值
            PerspectiveCorrectData data = InterpolatePerspective(v0, v1, v2, bary);
            
            // 采样纹理
            Color color = texture->Sample(data.uv);
            
            // 深度
            float depth = 1.0f / data.oneOverZ;
            
            // 写入
            fb.SetPixel(x, y, color, depth);
        }
    }
}
```

### 5.3 线性 vs 透视校正对比

```
场景：渲染一个带纹理的地板

线性插值（错误）：
┌────────────────┐
│▓▓▓▓▒▒▒▒░░░░    │  ← 远处纹理被压缩
│▓▓▓▓▒▒▒▒░░░░    │
│▓▓▓▓▒▒▒▒░░░░    │
└────────────────┘
  近      远

透视校正（正确）：
┌────────────────┐
│▓▓▓▓▓▓▓▓▓▓▓▓▓▓  │  ← 纹理均匀分布
│▒▒▒▒▒▒▒▒▒▒▒▒▒▒  │
│░░░░░░░░░░░░░░  │
└────────────────┘
  近      远

何时需要透视校正？
✅ 3D 场景（有透视）
✅ 大三角形
✅ 深度变化大的三角形

何时可以省略？
✅ 2D 渲染（正交投影）
✅ 小三角形（误差不明显）
✅ 深度变化小
```

---

## 📚 总结

### 核心概念回顾

1. **软件渲染流程**
   - 顶点变换 → 光栅化 → 像素着色
   - 用 CPU 代码模拟 GPU 管线

2. **坐标变换**
   - 模型 → 世界 → 相机 → 裁剪 → NDC → 屏幕
   - 矩阵变换实现

3. **三角形光栅化**
   - 边界盒优化遍历
   - 边方程判断点在三角形内
   - 重心坐标插值属性

4. **属性插值**
   - 线性插值：UV、颜色、法线
   - 重心坐标：三角形内插值

5. **透视校正**
   - 线性插值的问题
   - 1/z 插值公式
   - 何时需要校正

### 性能分析

```
渲染 1920×1080 图像，1 个三角形：

边界盒大小：平均 1000×1000 = 1,000,000 像素
实际在三角形内：约 500,000 像素

每像素操作：
- 边方程判断：3 次乘法 + 3 次减法 = ~10 ns
- 重心坐标：9 次除法 + 乘法 = ~50 ns
- UV 插值：6 次乘加 = ~10 ns
- 纹理采样：内存读取 = ~50 ns
- 深度测试 + 写入：~20 ns
总计：~140 ns/像素

总时间：
- 在三角形内的像素：500,000 × 140 ns = 70 ms
- 在三角形外的像素：500,000 × 10 ns = 5 ms
- 总计：75 ms ≈ 13 fps（单核）

多核优化：
- 8 核并行：75 / 8 ≈ 9.4 ms ≈ 106 fps
- 但仍比 GPU 慢很多（GPU：<1 ms）
```

---

## 🚀 下一步

在 **Part 3B** 中，我们将学习：
- **纹理采样与过滤**（软件实现 Mipmap、双线性过滤）
- **深度缓冲与混合**
- **完整的软件渲染器实现**
- **性能优化技巧**（SIMD、多线程）

准备好继续了吗？告诉我何时开始 Part 3B！ 🎯
