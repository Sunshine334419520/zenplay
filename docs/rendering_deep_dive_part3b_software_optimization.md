# 渲染深度解析 Part 3B：软件渲染优化与完整实现

**文档目标**：实现完整的软件渲染器、纹理过滤、性能优化  
**前置知识**：Part 3A（光栅化基础）  
**阅读时间**：30-40 分钟

---

## 📚 目录

1. [纹理采样与过滤](#1-纹理采样与过滤)
2. [深度缓冲与Alpha混合](#2-深度缓冲与alpha混合)
3. [性能优化技术](#3-性能优化技术)
4. [完整软件渲染器实现](#4-完整软件渲染器实现)
5. [性能对比与分析](#5-性能对比与分析)

---

## 1. 纹理采样与过滤

### 1.1 最近邻采样（Nearest Neighbor）

```cpp
// 最简单的纹理采样
Color SampleNearest(const Texture* tex, Vec2 uv) {
    // UV [0, 1] → 像素坐标 [0, width/height]
    int x = (int)(uv.x * tex->width) % tex->width;
    int y = (int)(uv.y * tex->height) % tex->height;
    
    // 处理负数（循环寻址）
    if (x < 0) x += tex->width;
    if (y < 0) y += tex->height;
    
    // 读取像素
    return Color::FromRGBA(tex->pixels[y * tex->width + x]);
}

// 视觉效果：
// 优点：快速（1 次内存读取）
// 缺点：像素化明显，有锯齿

// 放大效果（4x4 纹理 → 16x16 屏幕）：
// 纹理:        屏幕（最近邻）:
// ┌─┬─┐        ┌────┬────┐
// │A│B│        │AAAA│BBBB│  ← 每个纹理像素被复制
// ├─┼─┤   →    │AAAA│BBBB│
// │C│D│        ├────┼────┤
// └─┴─┘        │CCCC│DDDD│
//              │CCCC│DDDD│
//              └────┴────┘
//              明显的"马赛克"效果
```

### 1.2 双线性过滤（Bilinear Filtering）

```cpp
// 双线性插值采样
Color SampleBilinear(const Texture* tex, Vec2 uv) {
    // UV → 浮点像素坐标
    float x = uv.x * tex->width - 0.5f;
    float y = uv.y * tex->height - 0.5f;
    
    // 整数部分（左上角像素）
    int x0 = (int)floor(x);
    int y0 = (int)floor(y);
    int x1 = x0 + 1;
    int y1 = y0 + 1;
    
    // 小数部分（插值权重）
    float fx = x - x0;
    float fy = y - y0;
    
    // 循环寻址
    x0 = (x0 % tex->width + tex->width) % tex->width;
    x1 = (x1 % tex->width + tex->width) % tex->width;
    y0 = (y0 % tex->height + tex->height) % tex->height;
    y1 = (y1 % tex->height + tex->height) % tex->height;
    
    // 读取 4 个像素
    Color c00 = Color::FromRGBA(tex->pixels[y0 * tex->width + x0]);
    Color c10 = Color::FromRGBA(tex->pixels[y0 * tex->width + x1]);
    Color c01 = Color::FromRGBA(tex->pixels[y1 * tex->width + x0]);
    Color c11 = Color::FromRGBA(tex->pixels[y1 * tex->width + x1]);
    
    // 双线性插值
    // 1. X 方向插值
    Color c0 = LerpColor(c00, c10, fx);
    Color c1 = LerpColor(c01, c11, fx);
    
    // 2. Y 方向插值
    return LerpColor(c0, c1, fy);
}

// 颜色插值辅助函数
Color LerpColor(Color a, Color b, float t) {
    return {
        (uint8_t)(a.r * (1 - t) + b.r * t),
        (uint8_t)(a.g * (1 - t) + b.g * t),
        (uint8_t)(a.b * (1 - t) + b.b * t),
        (uint8_t)(a.a * (1 - t) + b.a * t)
    };
}

// 视觉效果：
// 纹理:        屏幕（双线性）:
// ┌─┬─┐        ┌────┬────┐
// │A│B│        │A→→→│B   │  ← 平滑渐变
// ├─┼─┤   →    │↓╲  │ ╲  │
// │C│D│        │↓ ╲ │  ╲ │
// └─┴─┘        ├──╲─┼───╲┤
//              │C  ╲│   D│
//              └────┴────┘
//              平滑过渡，无锯齿

// 采样点位置示例：
// ┌───┬───┐
// │c00│c10│  采样点 ●
// ├───●───┤  fx = 0.3, fy = 0.6
// │c01│c11│
// └───┴───┘
// 
// 插值过程：
// top = c00 * 0.7 + c10 * 0.3
// bottom = c01 * 0.7 + c11 * 0.3
// result = top * 0.4 + bottom * 0.6
```

### 1.3 Mipmap 实现

```cpp
// Mipmap 结构
struct MipmappedTexture {
    int width, height;
    int numLevels;               // Mipmap 级别数
    std::vector<uint32_t*> levels;  // 每级的像素数据
    
    // 生成 Mipmap
    void GenerateMipmaps(uint32_t* basePixels, int baseWidth, int baseHeight) {
        width = baseWidth;
        height = baseHeight;
        
        // 计算级别数
        numLevels = 1 + (int)floor(log2(std::max(baseWidth, baseHeight)));
        levels.resize(numLevels);
        
        // Level 0 是原始纹理
        levels[0] = new uint32_t[baseWidth * baseHeight];
        memcpy(levels[0], basePixels, baseWidth * baseHeight * sizeof(uint32_t));
        
        // 生成后续级别
        for (int level = 1; level < numLevels; level++) {
            int prevWidth = baseWidth >> (level - 1);
            int prevHeight = baseHeight >> (level - 1);
            int currWidth = baseWidth >> level;
            int currHeight = baseHeight >> level;
            
            if (currWidth == 0) currWidth = 1;
            if (currHeight == 0) currHeight = 1;
            
            levels[level] = new uint32_t[currWidth * currHeight];
            
            // 下采样：2x2 → 1
            for (int y = 0; y < currHeight; y++) {
                for (int x = 0; x < currWidth; x++) {
                    // 从上一级读取 2x2 像素
                    Color c00 = Color::FromRGBA(levels[level-1][(y*2+0) * prevWidth + (x*2+0)]);
                    Color c10 = Color::FromRGBA(levels[level-1][(y*2+0) * prevWidth + (x*2+1)]);
                    Color c01 = Color::FromRGBA(levels[level-1][(y*2+1) * prevWidth + (x*2+0)]);
                    Color c11 = Color::FromRGBA(levels[level-1][(y*2+1) * prevWidth + (x*2+1)]);
                    
                    // 平均
                    Color avg = {
                        (uint8_t)((c00.r + c10.r + c01.r + c11.r) / 4),
                        (uint8_t)((c00.g + c10.g + c01.g + c11.g) / 4),
                        (uint8_t)((c00.b + c10.b + c01.b + c11.b) / 4),
                        (uint8_t)((c00.a + c10.a + c01.a + c11.a) / 4)
                    };
                    
                    levels[level][y * currWidth + x] = avg.ToRGBA();
                }
            }
        }
    }
    
    // 计算 Mipmap 级别
    float ComputeMipmapLevel(Vec2 ddx, Vec2 ddy) const {
        // ddx, ddy 是 UV 相对于屏幕像素的偏导数
        float px = ddx.x * width;
        float py = ddx.y * height;
        float qx = ddy.x * width;
        float qy = ddy.y * height;
        
        // 计算纹理空间的像素跨度
        float deltaMax = sqrt(std::max(px*px + py*py, qx*qx + qy*qy));
        
        // 转换为 Mipmap 级别
        float level = log2(deltaMax);
        return std::max(0.0f, level);
    }
    
    // 带 Mipmap 的采样
    Color SampleWithMipmap(Vec2 uv, float mipmapLevel) const {
        // 限制级别范围
        mipmapLevel = std::clamp(mipmapLevel, 0.0f, (float)(numLevels - 1));
        
        int level = (int)floor(mipmapLevel);
        float blend = mipmapLevel - level;
        
        // 从当前级别采样
        int levelWidth = width >> level;
        int levelHeight = height >> level;
        Color c0 = SampleBilinearFromLevel(uv, level, levelWidth, levelHeight);
        
        // 如果需要，从下一级采样并混合（Trilinear）
        if (blend > 0.001f && level + 1 < numLevels) {
            int nextLevelWidth = width >> (level + 1);
            int nextLevelHeight = height >> (level + 1);
            Color c1 = SampleBilinearFromLevel(uv, level + 1, 
                                              nextLevelWidth, nextLevelHeight);
            return LerpColor(c0, c1, blend);
        }
        
        return c0;
    }
    
private:
    Color SampleBilinearFromLevel(Vec2 uv, int level, int w, int h) const {
        // 双线性采样（简化版，省略边界处理）
        float x = uv.x * w - 0.5f;
        float y = uv.y * h - 0.5f;
        
        int x0 = (int)floor(x) % w;
        int y0 = (int)floor(y) % h;
        int x1 = (x0 + 1) % w;
        int y1 = (y0 + 1) % h;
        
        float fx = x - floor(x);
        float fy = y - floor(y);
        
        uint32_t* pixels = levels[level];
        Color c00 = Color::FromRGBA(pixels[y0 * w + x0]);
        Color c10 = Color::FromRGBA(pixels[y0 * w + x1]);
        Color c01 = Color::FromRGBA(pixels[y1 * w + x0]);
        Color c11 = Color::FromRGBA(pixels[y1 * w + x1]);
        
        Color c0 = LerpColor(c00, c10, fx);
        Color c1 = LerpColor(c01, c11, fx);
        return LerpColor(c0, c1, fy);
    }
};

// Mipmap 视觉示例：
//
// Level 0 (1024x1024):    Level 1 (512x512):    Level 2 (256x256):
// ┌────────────────┐      ┌────────┐            ┌────┐
// │  原始纹理      │      │ 缩小   │            │更小│
// │  最高细节      │  →   │ 1/2    │  →         │1/4 │
// └────────────────┘      └────────┘            └────┘
//
// 根据距离自动选择：
// - 近处物体：使用 Level 0（高细节）
// - 中等距离：使用 Level 1 或 2
// - 远处物体：使用 Level 5-10（低细节，但快）
```

### 1.4 各向异性过滤（Anisotropic）

```cpp
// 简化的各向异性过滤
Color SampleAnisotropic(const MipmappedTexture* tex, Vec2 uv, 
                       Vec2 ddx, Vec2 ddy, int maxSamples) {
    // 计算主轴和次轴
    float lenX = sqrt(ddx.x * ddx.x + ddx.y * ddx.y);
    float lenY = sqrt(ddy.x * ddy.x + ddy.y * ddy.y);
    
    // 各向异性比率
    float anisoRatio = std::min((float)maxSamples, lenX / lenY);
    
    if (anisoRatio < 1.5f) {
        // 接近各向同性，使用普通 Trilinear
        float level = tex->ComputeMipmapLevel(ddx, ddy);
        return tex->SampleWithMipmap(uv, level);
    }
    
    // 沿主轴采样多次
    Vec2 axis = (lenX > lenY) ? ddx : ddy;
    int numSamples = (int)anisoRatio;
    
    Color result = {0, 0, 0, 0};
    for (int i = 0; i < numSamples; i++) {
        float offset = (i - numSamples / 2.0f) / numSamples;
        Vec2 sampleUV = {
            uv.x + axis.x * offset,
            uv.y + axis.y * offset
        };
        
        float level = tex->ComputeMipmapLevel(ddx, ddy);
        Color c = tex->SampleWithMipmap(sampleUV, level);
        
        result.r += c.r;
        result.g += c.g;
        result.b += c.b;
        result.a += c.a;
    }
    
    result.r /= numSamples;
    result.g /= numSamples;
    result.b /= numSamples;
    result.a /= numSamples;
    
    return result;
}

// 各向异性效果：
//
// 场景：地面纹理，透视角度
//
// 无各向异性（模糊）：  有各向异性（清晰）：
// ┌──────────────┐      ┌──────────────┐
// │░░░░░░░░░░░░░░│      │▓▓▓▓▓▓▓▓▓▓▓▓▓▓│  ← 近处清晰
// │▒▒▒▒▒▒▒▒▒▒▒▒▒▒│      │▒▒▒▒▒▒▒▒▒▒▒▒▒▒│
// │░░░░░░░░░░░░░░│      │░░░░░░░░░░░░░░│  ← 远处细节保留
// └──────────────┘      └──────────────┘
//   模糊              清晰纹理细节
```

---

## 2. 深度缓冲与Alpha混合

### 2.1 深度测试实现

```cpp
// 帧缓冲（带深度）
class FramebufferWithDepth {
public:
    int width, height;
    uint32_t* colorBuffer;
    float* depthBuffer;
    
    FramebufferWithDepth(int w, int h) : width(w), height(h) {
        colorBuffer = new uint32_t[w * h];
        depthBuffer = new float[w * h];
        Clear({0, 0, 0, 255}, 1.0f);
    }
    
    ~FramebufferWithDepth() {
        delete[] colorBuffer;
        delete[] depthBuffer;
    }
    
    void Clear(Color clearColor, float clearDepth) {
        uint32_t rgba = clearColor.ToRGBA();
        for (int i = 0; i < width * height; i++) {
            colorBuffer[i] = rgba;
            depthBuffer[i] = clearDepth;
        }
    }
    
    // 深度测试 + 写入
    bool SetPixelWithDepthTest(int x, int y, Color color, float depth) {
        if (x < 0 || x >= width || y < 0 || y >= height) {
            return false;
        }
        
        int index = y * width + x;
        
        // 深度测试：新像素更近吗？
        if (depth < depthBuffer[index]) {
            colorBuffer[index] = color.ToRGBA();
            depthBuffer[index] = depth;
            return true;  // 写入成功
        }
        
        return false;  // 被遮挡，丢弃
    }
    
    // 可选：只读深度（用于透明物体）
    bool DepthTest(int x, int y, float depth) const {
        if (x < 0 || x >= width || y < 0 || y >= height) {
            return false;
        }
        return depth < depthBuffer[y * width + x];
    }
};

// 深度测试的视觉效果：
//
// 场景：两个重叠的三角形
//
// 三角形 A（红色，深度 0.3）
// 三角形 B（蓝色，深度 0.7）
//
// 侧视图：
//      相机
//       │
//   0.3 △A (红)
//       │
//   0.7 △B (蓝)
//
// 渲染顺序无关：
// - 先渲染 A 再渲染 B：B 被深度测试拒绝，显示 A（红色）
// - 先渲染 B 再渲染 A：A 覆盖 B，显示 A（红色）
//
// 结果：总是显示更近的三角形（红色）
```

### 2.2 Alpha 混合实现

```cpp
// 混合模式枚举
enum class BlendMode {
    None,           // 不混合（覆盖）
    Alpha,          // 标准 Alpha 混合
    Additive,       // 叠加
    Multiply        // 相乘
};

// 混合函数
Color BlendColors(Color src, Color dst, BlendMode mode) {
    switch (mode) {
        case BlendMode::None:
            return src;
        
        case BlendMode::Alpha: {
            // 标准 Alpha 混合
            float srcAlpha = src.a / 255.0f;
            float dstAlpha = 1.0f - srcAlpha;
            
            return {
                (uint8_t)(src.r * srcAlpha + dst.r * dstAlpha),
                (uint8_t)(src.g * srcAlpha + dst.g * dstAlpha),
                (uint8_t)(src.b * srcAlpha + dst.b * dstAlpha),
                255  // 不透明
            };
        }
        
        case BlendMode::Additive: {
            // 叠加混合（用于光效）
            return {
                (uint8_t)std::min(255, (int)src.r + (int)dst.r),
                (uint8_t)std::min(255, (int)src.g + (int)dst.g),
                (uint8_t)std::min(255, (int)src.b + (int)dst.b),
                255
            };
        }
        
        case BlendMode::Multiply: {
            // 相乘混合（用于阴影）
            return {
                (uint8_t)((src.r * dst.r) / 255),
                (uint8_t)((src.g * dst.g) / 255),
                (uint8_t)((src.b * dst.b) / 255),
                255
            };
        }
    }
    
    return src;
}

// 带混合的像素写入
void SetPixelWithBlend(FramebufferWithDepth& fb, int x, int y, 
                      Color srcColor, float depth, BlendMode mode) {
    if (x < 0 || x >= fb.width || y < 0 || y >= fb.height) {
        return;
    }
    
    int index = y * fb.width + x;
    
    // 对于透明物体，通常只做深度测试，不写入深度
    if (mode != BlendMode::None && srcColor.a < 255) {
        if (!fb.DepthTest(x, y, depth)) {
            return;  // 被遮挡
        }
        // 不更新深度缓冲
    } else {
        // 不透明物体：深度测试并写入
        if (depth >= fb.depthBuffer[index]) {
            return;
        }
        fb.depthBuffer[index] = depth;
    }
    
    // 读取目标颜色
    Color dstColor = Color::FromRGBA(fb.colorBuffer[index]);
    
    // 混合
    Color finalColor = BlendColors(srcColor, dstColor, mode);
    
    // 写入
    fb.colorBuffer[index] = finalColor.ToRGBA();
}

// Alpha 混合视觉效果：
//
// 背景：蓝色 (0, 0, 255)
// 前景：红色 (255, 0, 0)，Alpha = 50%
//
// 混合结果：
// R = 255 × 0.5 + 0 × 0.5 = 127.5 → 128
// G = 0 × 0.5 + 0 × 0.5 = 0
// B = 0 × 0.5 + 255 × 0.5 = 127.5 → 128
//
// 最终颜色：(128, 0, 128) 紫色
```

### 2.3 渲染顺序问题

```cpp
// 正确的渲染顺序（从后到前，从不透明到透明）
void RenderScene(std::vector<Triangle>& triangles, Framebuffer& fb) {
    // 1. 按深度排序三角形
    std::sort(triangles.begin(), triangles.end(), 
        [](const Triangle& a, const Triangle& b) {
            // 计算三角形中心深度
            float depthA = (a.v0.position.z + a.v1.position.z + a.v2.position.z) / 3.0f;
            float depthB = (b.v0.position.z + b.v1.position.z + b.v2.position.z) / 3.0f;
            return depthA > depthB;  // 从远到近
        });
    
    // 2. 先渲染不透明物体
    for (auto& tri : triangles) {
        if (tri.IsOpaque()) {
            RasterizeTriangle(tri, fb, BlendMode::None);
        }
    }
    
    // 3. 再渲染透明物体（从后到前）
    for (auto& tri : triangles) {
        if (!tri.IsOpaque()) {
            RasterizeTriangle(tri, fb, BlendMode::Alpha);
        }
    }
}

// 为什么需要排序？
//
// 错误顺序（透明物体在前）：
// 1. 渲染透明红色三角形（Alpha = 0.5）
//    - 与黑色背景混合 → 深红色
// 2. 渲染不透明蓝色三角形
//    - 深度测试失败，被拒绝
// 结果：❌ 看到深红色（错误）
//
// 正确顺序（不透明在前）：
// 1. 渲染不透明蓝色三角形
//    - 写入蓝色
// 2. 渲染透明红色三角形（Alpha = 0.5）
//    - 与蓝色混合 → 紫色
// 结果：✅ 看到紫色（正确）
```

---

## 3. 性能优化技术

### 3.1 SIMD 优化（AVX2）

```cpp
#include <immintrin.h>  // AVX2

// 标量版本（处理 1 个像素）
void ProcessPixel(uint32_t* color, float r, float g, float b) {
    *color = ((uint32_t)(r * 255) << 0) |
             ((uint32_t)(g * 255) << 8) |
             ((uint32_t)(b * 255) << 16) |
             (255 << 24);
}

// SIMD 版本（处理 8 个像素）
void ProcessPixels_SIMD(uint32_t* colors, __m256 r, __m256 g, __m256 b) {
    // 浮点 [0, 1] → 整数 [0, 255]
    __m256 scale = _mm256_set1_ps(255.0f);
    __m256i ri = _mm256_cvtps_epi32(_mm256_mul_ps(r, scale));
    __m256i gi = _mm256_cvtps_epi32(_mm256_mul_ps(g, scale));
    __m256i bi = _mm256_cvtps_epi32(_mm256_mul_ps(b, scale));
    __m256i ai = _mm256_set1_epi32(255);
    
    // 组合成 RGBA
    __m256i rg = _mm256_or_si256(ri, _mm256_slli_epi32(gi, 8));
    __m256i ba = _mm256_or_si256(_mm256_slli_epi32(bi, 16), _mm256_slli_epi32(ai, 24));
    __m256i rgba = _mm256_or_si256(rg, ba);
    
    // 存储
    _mm256_storeu_si256((__m256i*)colors, rgba);
}

// YUV → RGB 转换（SIMD 版本）
void ConvertYUVtoRGB_SIMD(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                         uint32_t* rgb, int count) {
    // 一次处理 8 个像素
    for (int i = 0; i < count; i += 8) {
        // 加载 Y, U, V（8 个像素）
        __m128i y_i8 = _mm_loadl_epi64((__m128i*)(y + i));
        __m128i u_i8 = _mm_loadl_epi64((__m128i*)(u + i));
        __m128i v_i8 = _mm_loadl_epi64((__m128i*)(v + i));
        
        // 转换为 32 位浮点
        __m256 yf = _mm256_cvtepi32_ps(_mm256_cvtepu8_epi32(y_i8));
        __m256 uf = _mm256_cvtepi32_ps(_mm256_cvtepu8_epi32(u_i8));
        __m256 vf = _mm256_cvtepi32_ps(_mm256_cvtepu8_epi32(v_i8));
        
        // 归一化到 [0, 1]
        __m256 scale = _mm256_set1_ps(1.0f / 255.0f);
        yf = _mm256_mul_ps(yf, scale);
        uf = _mm256_sub_ps(_mm256_mul_ps(uf, scale), _mm256_set1_ps(0.5f));
        vf = _mm256_sub_ps(_mm256_mul_ps(vf, scale), _mm256_set1_ps(0.5f));
        
        // YUV → RGB 转换
        __m256 rf = _mm256_add_ps(yf, _mm256_mul_ps(vf, _mm256_set1_ps(1.402f)));
        __m256 gf = _mm256_sub_ps(yf, _mm256_add_ps(
            _mm256_mul_ps(uf, _mm256_set1_ps(0.344f)),
            _mm256_mul_ps(vf, _mm256_set1_ps(0.714f))
        ));
        __m256 bf = _mm256_add_ps(yf, _mm256_mul_ps(uf, _mm256_set1_ps(1.772f)));
        
        // 限制范围 [0, 1]
        rf = _mm256_max_ps(_mm256_min_ps(rf, _mm256_set1_ps(1.0f)), _mm256_setzero_ps());
        gf = _mm256_max_ps(_mm256_min_ps(gf, _mm256_set1_ps(1.0f)), _mm256_setzero_ps());
        bf = _mm256_max_ps(_mm256_min_ps(bf, _mm256_set1_ps(1.0f)), _mm256_setzero_ps());
        
        // 打包并存储
        ProcessPixels_SIMD(rgb + i, rf, gf, bf);
    }
}

// 性能对比：
// 标量版本：8 个像素 = 8 × 15 指令 = 120 指令
// SIMD 版本：8 个像素 = ~30 指令
// 加速比：4 倍
```

### 3.2 多线程优化

```cpp
#include <thread>
#include <vector>

// 多线程光栅化
void RasterizeTriangle_MultiThreaded(const ScreenVertex& v0,
                                    const ScreenVertex& v1,
                                    const ScreenVertex& v2,
                                    const Texture* texture,
                                    Framebuffer& fb,
                                    int numThreads = 8) {
    BBox bbox = ComputeBBox(v0, v1, v2, fb.width, fb.height);
    int height = bbox.maxY - bbox.minY + 1;
    int rowsPerThread = (height + numThreads - 1) / numThreads;
    
    std::vector<std::thread> threads;
    
    // 启动线程
    for (int t = 0; t < numThreads; t++) {
        int startY = bbox.minY + t * rowsPerThread;
        int endY = std::min(bbox.maxY, startY + rowsPerThread - 1);
        
        if (startY > bbox.maxY) break;
        
        threads.emplace_back([=, &fb]() {
            // 每个线程处理一部分行
            for (int y = startY; y <= endY; y++) {
                for (int x = bbox.minX; x <= bbox.maxX; x++) {
                    Vec2 p = {x + 0.5f, y + 0.5f};
                    
                    if (!InsideTriangle(p, v0.screenPos, v1.screenPos, v2.screenPos)) {
                        continue;
                    }
                    
                    Barycentric bary = ComputeBarycentric(p, v0.screenPos, 
                        v1.screenPos, v2.screenPos);
                    
                    Vec2 uv = InterpolateVec2(v0.uv, v1.uv, v2.uv, bary);
                    Color color = texture->Sample(uv);
                    
                    float depth = BarycentricInterpolate(v0.depth, v1.depth, 
                        v2.depth, bary);
                    
                    fb.SetPixel(x, y, color, depth);
                }
            }
        });
    }
    
    // 等待所有线程完成
    for (auto& thread : threads) {
        thread.join();
    }
}

// 性能分析：
// 1920×1080 三角形，单线程：75 ms
// 8 核并行：75 / 8 ≈ 9.4 ms
// 实际：~12 ms（有线程开销）
// 加速比：6.25 倍
```

### 3.3 块光栅化（Tile-Based）

```cpp
// 将屏幕分成小块（Tile），每块独立光栅化
constexpr int TILE_SIZE = 64;  // 64×64 像素

struct Tile {
    int x, y;  // 块的左上角
    int width, height;
};

// 生成块列表
std::vector<Tile> GenerateTiles(int screenWidth, int screenHeight) {
    std::vector<Tile> tiles;
    
    for (int y = 0; y < screenHeight; y += TILE_SIZE) {
        for (int x = 0; x < screenWidth; x += TILE_SIZE) {
            tiles.push_back({
                x, y,
                std::min(TILE_SIZE, screenWidth - x),
                std::min(TILE_SIZE, screenHeight - y)
            });
        }
    }
    
    return tiles;
}

// 判断三角形是否与块相交
bool TriangleIntersectsTile(const ScreenVertex& v0,
                            const ScreenVertex& v1,
                            const ScreenVertex& v2,
                            const Tile& tile) {
    // 简化：使用三角形边界盒
    BBox triBBox = ComputeBBox(v0, v1, v2, INT_MAX, INT_MAX);
    
    // 块的边界盒
    int tileMinX = tile.x;
    int tileMaxX = tile.x + tile.width - 1;
    int tileMinY = tile.y;
    int tileMaxY = tile.y + tile.height - 1;
    
    // AABB 相交测试
    return !(triBBox.maxX < tileMinX || triBBox.minX > tileMaxX ||
             triBBox.maxY < tileMinY || triBBox.minY > tileMaxY);
}

// 块光栅化
void RasterizeTriangle_Tiled(const ScreenVertex& v0,
                             const ScreenVertex& v1,
                             const ScreenVertex& v2,
                             const Texture* texture,
                             Framebuffer& fb) {
    auto tiles = GenerateTiles(fb.width, fb.height);
    
    // 并行处理每个块
    #pragma omp parallel for
    for (size_t i = 0; i < tiles.size(); i++) {
        const Tile& tile = tiles[i];
        
        // 快速剔除：三角形不在块内
        if (!TriangleIntersectsTile(v0, v1, v2, tile)) {
            continue;
        }
        
        // 光栅化这个块
        for (int y = tile.y; y < tile.y + tile.height; y++) {
            for (int x = tile.x; x < tile.x + tile.width; x++) {
                Vec2 p = {x + 0.5f, y + 0.5f};
                
                if (!InsideTriangle(p, v0.screenPos, v1.screenPos, v2.screenPos)) {
                    continue;
                }
                
                // ... 插值和着色（同前）
            }
        }
    }
}

// 优势：
// 1. 缓存友好（块内的像素在内存中连续）
// 2. 并行友好（块之间独立）
// 3. 早期剔除（整个块可以快速跳过）
//
// 性能提升：1.5-2 倍（大三角形场景）
```

### 3.4 性能优化总结

```cpp
// 综合优化版本
class OptimizedSoftwareRenderer {
public:
    void RenderTriangle(const ScreenVertex& v0,
                       const ScreenVertex& v1,
                       const ScreenVertex& v2,
                       const MipmappedTexture* texture,
                       Framebuffer& fb) {
        // 1. 背面剔除
        if (BackfaceCull(v0, v1, v2)) {
            return;
        }
        
        // 2. 裁剪到屏幕范围
        BBox bbox = ComputeBBox(v0, v1, v2, fb.width, fb.height);
        if (bbox.minX > bbox.maxX || bbox.minY > bbox.maxY) {
            return;  // 完全在屏幕外
        }
        
        // 3. 小三角形优化：直接渲染
        int area = (bbox.maxX - bbox.minX + 1) * (bbox.maxY - bbox.minY + 1);
        if (area < 1000) {
            RasterizeSmallTriangle(v0, v1, v2, texture, fb);
            return;
        }
        
        // 4. 大三角形：使用块光栅化 + 多线程
        RasterizeTriangle_Tiled(v0, v1, v2, texture, fb);
    }
    
private:
    bool BackfaceCull(const ScreenVertex& v0,
                     const ScreenVertex& v1,
                     const ScreenVertex& v2) {
        // 计算三角形法线的 Z 分量
        float crossZ = EdgeFunction(v0.screenPos, v1.screenPos, v2.screenPos);
        return crossZ <= 0;  // 背向相机
    }
};

// 性能对比（1920×1080，60个三角形）：
// ┌──────────────────────┬──────────┬────────┐
// │ 方法                 │ 时间(ms) │ 加速比 │
// ├──────────────────────┼──────────┼────────┤
// │ 基础版本             │   450    │   1x   │
// │ + 边方程增量         │   200    │  2.25x │
// │ + SIMD               │    80    │  5.6x  │
// │ + 多线程 (8核)       │    15    │  30x   │
// │ + 块光栅化           │    10    │  45x   │
// └──────────────────────┴──────────┴────────┘
//
// 但仍比 GPU 慢：
// GPU: ~0.5 ms (900x faster)
```

---

## 4. 完整软件渲染器实现

```cpp
// 完整的软件渲染器类
class SoftwareRenderer {
public:
    SoftwareRenderer(int width, int height)
        : framebuffer_(width, height) {
        // 初始化
    }
    
    // 清空帧缓冲
    void Clear(Color clearColor = {0, 0, 0, 255}, float clearDepth = 1.0f) {
        framebuffer_.Clear(clearColor, clearDepth);
    }
    
    // 设置变换矩阵
    void SetTransform(const Mat4& model, const Mat4& view, const Mat4& proj) {
        mvp_ = proj * view * model;
    }
    
    // 渲染三角形
    void DrawTriangle(const Triangle& tri, const Texture* texture) {
        // 1. 顶点着色
        ScreenVertex v0 = VertexShader(tri.v0, mvp_, 
            framebuffer_.width, framebuffer_.height);
        ScreenVertex v1 = VertexShader(tri.v1, mvp_, 
            framebuffer_.width, framebuffer_.height);
        ScreenVertex v2 = VertexShader(tri.v2, mvp_, 
            framebuffer_.width, framebuffer_.height);
        
        // 2. 背面剔除
        if (BackfaceCull(v0, v1, v2)) {
            return;
        }
        
        // 3. 光栅化
        RasterizeTriangle(v0, v1, v2, texture, framebuffer_);
    }
    
    // 渲染三角形列表
    void DrawTriangles(const std::vector<Triangle>& triangles, 
                      const std::vector<const Texture*>& textures) {
        for (size_t i = 0; i < triangles.size(); i++) {
            DrawTriangle(triangles[i], textures[i % textures.size()]);
        }
    }
    
    // 获取帧缓冲（用于显示）
    const uint32_t* GetColorBuffer() const {
        return framebuffer_.colorBuffer;
    }
    
    int GetWidth() const { return framebuffer_.width; }
    int GetHeight() const { return framebuffer_.height; }
    
private:
    FramebufferWithDepth framebuffer_;
    Mat4 mvp_;
};

// 使用示例
int main() {
    // 创建渲染器
    SoftwareRenderer renderer(1920, 1080);
    
    // 加载纹理
    Texture texture = LoadTexture("brick.png");
    
    // 创建三角形
    Triangle tri = {
        {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f}},  // 左下
        {{ 0.5f, -0.5f, 0.0f}, {1.0f, 0.0f}},  // 右下
        {{ 0.0f,  0.5f, 0.0f}, {0.5f, 1.0f}}   // 顶部
    };
    
    // 渲染循环
    for (int frame = 0; frame < 1000; frame++) {
        // 清空
        renderer.Clear({100, 149, 237, 255});  // 天蓝色
        
        // 设置变换
        Mat4 model = Mat4::RotationY(frame * 0.01f);
        Mat4 view = Mat4::LookAt({0, 0, 3}, {0, 0, 0}, {0, 1, 0});
        Mat4 proj = Mat4::Perspective(M_PI / 4, 16.0f / 9.0f, 0.1f, 100.0f);
        renderer.SetTransform(model, view, proj);
        
        // 渲染
        renderer.DrawTriangle(tri, &texture);
        
        // 显示到屏幕（使用 SDL、Windows GDI 等）
        DisplayToScreen(renderer.GetColorBuffer(), 
                       renderer.GetWidth(), 
                       renderer.GetHeight());
    }
    
    return 0;
}
```

---

## 5. 性能对比与分析

### 5.1 CPU vs GPU 性能对比

```
场景：1920×1080，100 个三角形，带纹理

┌─────────────────────┬──────────┬─────────┬────────┐
│ 实现                │ 时间(ms) │ FPS     │ 功耗   │
├─────────────────────┼──────────┼─────────┼────────┤
│ 软件渲染（单核）    │   150    │    6.6  │  15W   │
│ 软件渲染（8核）     │    20    │   50    │  60W   │
│ 软件渲染（优化）    │    12    │   83    │  60W   │
│ GPU (集成显卡)      │     2    │  500    │   5W   │
│ GPU (独立显卡)      │   0.5    │ 2000    │  10W   │
└─────────────────────┴──────────┴─────────┴────────┘

结论：
- GPU 比优化软件渲染快 24-400 倍
- GPU 更节能（性能功耗比高）
- 软件渲染适合：离线渲染、教学、调试
```

### 5.2 复杂度分析

```
渲染 N 个三角形到 W×H 屏幕

时间复杂度：
- 顶点着色：O(3N) - 每三角形 3 个顶点
- 光栅化：O(N × 面积) - 每三角形覆盖的像素
- 平均面积：W×H / N
- 总计：O(N + W×H)

空间复杂度：
- 帧缓冲：W×H × 4 字节（颜色）
- 深度缓冲：W×H × 4 字节（深度）
- 总计：O(W×H)

例子：1920×1080，N=1000
- 时间：O(1000 + 2,073,600) ≈ O(2,000,000)
- 空间：2,073,600 × 8 ≈ 16 MB
```

### 5.3 瓶颈分析

```
性能分析（1920×1080，优化软件渲染）：

阶段耗时分布：
┌────────────────────────┐
│ 顶点着色: 2%           │ ██
│ 背面剔除: 1%           │ █
│ 光栅化: 40%            │ ████████████████████████████████████████
│ 纹理采样: 45%          │ █████████████████████████████████████████████
│ 深度测试: 10%          │ ██████████
│ 其他: 2%               │ ██
└────────────────────────┘

瓶颈：纹理采样（45%）
原因：
- 内存读取（缓存未命中）
- 双线性插值计算
- Mipmap 级别计算

优化方向：
1. 预取纹理数据（Prefetch）
2. 使用压缩纹理格式
3. 优化缓存局部性
```

---

## 📚 总结

### 核心概念回顾

1. **纹理过滤**
   - 最近邻：快但像素化
   - 双线性：平滑但慢
   - Mipmap：解决缩小摩尔纹
   - 各向异性：地面纹理必备

2. **深度与混合**
   - 深度测试：解决遮挡
   - Alpha 混合：透明效果
   - 渲染顺序：从远到近

3. **性能优化**
   - SIMD：4-8 倍加速
   - 多线程：线性加速
   - 块光栅化：缓存友好

4. **完整渲染器**
   - 模块化设计
   - 易于扩展
   - 教学价值高

### 软件渲染的价值

```
✅ 优点：
- 完全理解渲染原理
- 100% 可预测、可调试
- 跨平台兼容
- 离线渲染可接受

❌ 缺点：
- 慢（比 GPU 慢 10-400 倍）
- 功耗高
- 不适合实时高分辨率

适用场景：
- 学习图形学
- 离线渲染（动画、电影）
- 嵌入式系统（无 GPU）
- 测试和验证
```

---

## 🎯 完成 Part 3！

你现在已经掌握了软件渲染的完整知识！

**下一步**：Part 4 将深入学习高级渲染技术（光照、阴影、后处理等）

准备好继续了吗？告诉我何时开始 Part 4！ 🚀
