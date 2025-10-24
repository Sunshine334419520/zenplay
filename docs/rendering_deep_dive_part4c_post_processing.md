# 渲染深度解析 Part 4C：后处理效果

**文档目标**：掌握现代游戏的后处理特效技术  
**前置知识**：Part 2（渲染管线）、Part 4A（光照）  
**阅读时间**：35-45 分钟

---

## 📚 目录

1. [后处理基础](#1-后处理基础)
2. [HDR 与色调映射](#2-hdr-与色调映射)
3. [Bloom 辉光效果](#3-bloom-辉光效果)
4. [抗锯齿技术](#4-抗锯齿技术)
5. [其他常见后处理](#5-其他常见后处理)

---

## 1. 后处理基础

### 1.1 什么是后处理

```
后处理（Post-Processing）：
在完成场景渲染后，对整个图像进行的屏幕空间效果处理

渲染流程：
┌─────────────────────────────────────────────────┐
│ 1. 场景渲染 → 帧缓冲                             │
│    ┌───────┐                                     │
│    │ 3D 场景│ → Render Target (纹理)             │
│    └───────┘                                     │
│                                                  │
│ 2. 后处理链                                      │
│    ┌─────────┐   ┌─────────┐   ┌─────────┐     │
│    │ HDR     │ → │ Bloom   │ → │ Tone Map│     │
│    └─────────┘   └─────────┘   └─────────┘     │
│         ↓             ↓             ↓           │
│    ┌─────────┐   ┌─────────┐   ┌─────────┐     │
│    │  FXAA   │ → │ Vignette│ → │ 最终图像│     │
│    └─────────┘   └─────────┘   └─────────┘     │
│                                                  │
│ 3. 显示到屏幕                                    │
│    ┌───────┐                                     │
│    │显示器 │ ← Swap Chain                       │
│    └───────┘                                     │
└─────────────────────────────────────────────────┘

优势：
✓ 与场景几何无关（屏幕空间）
✓ 性能可预测（固定分辨率）
✓ 易于组合和调整
✓ 艺术效果丰富
```

### 1.2 后处理框架

```cpp
// 后处理基类
class PostProcessEffect {
public:
    virtual ~PostProcessEffect() = default;
    
    // 处理纹理
    virtual void Process(const Texture* input, Texture* output) = 0;
    
    // 是否启用
    bool enabled = true;
};

// 后处理管理器
class PostProcessManager {
public:
    std::vector<std::unique_ptr<PostProcessEffect>> effects;
    
    void AddEffect(std::unique_ptr<PostProcessEffect> effect) {
        effects.push_back(std::move(effect));
    }
    
    void ProcessChain(const Texture* sceneTexture, Texture* finalOutput) {
        if (effects.empty()) {
            // 无后处理，直接复制
            Copy(sceneTexture, finalOutput);
            return;
        }
        
        // 创建临时纹理（乒乓缓冲）
        Texture tempA(sceneTexture->width, sceneTexture->height);
        Texture tempB(sceneTexture->width, sceneTexture->height);
        
        const Texture* input = sceneTexture;
        Texture* output = &tempA;
        
        // 应用每个效果
        for (size_t i = 0; i < effects.size(); i++) {
            if (!effects[i]->enabled) {
                continue;
            }
            
            // 最后一个效果输出到最终缓冲
            if (i == effects.size() - 1) {
                output = finalOutput;
            }
            
            effects[i]->Process(input, output);
            
            // 乒乓交换
            input = output;
            output = (output == &tempA) ? &tempB : &tempA;
        }
    }
    
private:
    void Copy(const Texture* src, Texture* dst) {
        memcpy(dst->pixels, src->pixels, 
               src->width * src->height * sizeof(uint32_t));
    }
};

// 使用示例
int main() {
    PostProcessManager postProcess;
    
    // 添加效果
    postProcess.AddEffect(std::make_unique<HDREffect>());
    postProcess.AddEffect(std::make_unique<BloomEffect>());
    postProcess.AddEffect(std::make_unique<ToneMappingEffect>());
    postProcess.AddEffect(std::make_unique<FXAAEffect>());
    
    // 渲染循环
    while (true) {
        // 1. 渲染场景到纹理
        RenderScene(&sceneTexture);
        
        // 2. 后处理
        postProcess.ProcessChain(&sceneTexture, &finalTexture);
        
        // 3. 显示
        Present(&finalTexture);
    }
    
    return 0;
}
```

### 1.3 全屏四边形（Fullscreen Quad）

```cpp
// 后处理的基础：全屏四边形渲染

// 顶点数据（覆盖整个屏幕的两个三角形）
struct FullscreenVertex {
    Vec2 position;  // NDC 坐标 [-1, 1]
    Vec2 uv;        // 纹理坐标 [0, 1]
};

FullscreenVertex fullscreenQuad[6] = {
    // 三角形 1
    {{-1.0f, -1.0f}, {0.0f, 1.0f}},  // 左下
    {{ 1.0f, -1.0f}, {1.0f, 1.0f}},  // 右下
    {{ 1.0f,  1.0f}, {1.0f, 0.0f}},  // 右上
    
    // 三角形 2
    {{-1.0f, -1.0f}, {0.0f, 1.0f}},  // 左下
    {{ 1.0f,  1.0f}, {1.0f, 0.0f}},  // 右上
    {{-1.0f,  1.0f}, {0.0f, 0.0f}}   // 左上
};

// 覆盖范围：
// ┌─────────────────┐  NDC 坐标
// │(-1,1)    (1,1)  │
// │  ┌─────────┐    │
// │  │         │    │  全屏四边形
// │  │         │    │
// │  └─────────┘    │
// │(-1,-1)   (1,-1) │
// └─────────────────┘

// 基础后处理着色器
// 顶点着色器（直通）
struct VS_OUTPUT {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VS_OUTPUT VSMain(FullscreenVertex input) {
    VS_OUTPUT output;
    output.position = float4(input.position, 0.0, 1.0);
    output.uv = input.uv;
    return output;
}

// 像素着色器（采样输入纹理）
Texture2D inputTexture : register(t0);
SamplerState inputSampler : register(s0);

float4 PSMain(VS_OUTPUT input) : SV_TARGET {
    return inputTexture.Sample(inputSampler, input.uv);
}
```

---

## 2. HDR 与色调映射

### 2.1 HDR 基础

```
HDR (High Dynamic Range): 高动态范围

问题：真实世界亮度范围 > 显示器范围
- 太阳：100,000 cd/m²
- 室内灯光：100 cd/m²
- 显示器：0-500 cd/m²

LDR vs HDR：
┌──────────────────────────────────────┐
│ LDR (Low Dynamic Range)              │
│ - 每通道 8 位 [0, 255]               │
│ - 范围固定 [0, 1]                    │
│ - 亮度被截断                         │
│   ┌─────────────┐                    │
│   │ 0   128  255│                    │
│   └─────────────┘                    │
│    暗    中    亮（截断）            │
│                                      │
│ HDR (High Dynamic Range)             │
│ - 每通道 16/32 位浮点                │
│ - 范围 [0, ∞)                        │
│ - 保留所有亮度信息                   │
│   ┌─────────────────────────────┐   │
│   │ 0   1   10  100  1000 10000│   │
│   └─────────────────────────────┘   │
│    暗   中   亮   很亮   极亮        │
└──────────────────────────────────────┘

视觉对比：
LDR:                   HDR:
┌─────────────┐       ┌─────────────┐
│  ███████    │       │  ●█████     │ ← 高光细节
│ █████████   │       │ ████████    │
│█████████    │       │███████████  │
└─────────────┘       └─────────────┘
高光过曝，无细节       高光有细节
```

### 2.2 HDR 渲染

```cpp
// HDR 帧缓冲（浮点格式）
class HDRFramebuffer {
public:
    int width, height;
    Vec3* colorBuffer;  // RGB 浮点（可以 > 1.0）
    
    HDRFramebuffer(int w, int h) : width(w), height(h) {
        colorBuffer = new Vec3[w * h];
    }
    
    ~HDRFramebuffer() {
        delete[] colorBuffer;
    }
    
    void Clear(Vec3 clearColor) {
        for (int i = 0; i < width * height; i++) {
            colorBuffer[i] = clearColor;
        }
    }
    
    void SetPixel(int x, int y, Vec3 color) {
        // 允许颜色 > 1.0（HDR）
        colorBuffer[y * width + x] = color;
    }
    
    Vec3 GetPixel(int x, int y) const {
        return colorBuffer[y * width + x];
    }
};

// HDR 光照计算示例
Vec3 ComputeHDRLighting(Vec3 position, Vec3 normal, Vec3 viewDir) {
    Vec3 result = {0, 0, 0};
    
    // 多个光源（可能非常亮）
    PointLight lights[10];
    
    for (int i = 0; i < 10; i++) {
        Vec3 L = Normalize(lights[i].position - position);
        float NdotL = std::max(Dot(normal, L), 0.0f);
        
        // 光照可以 > 1.0（HDR）
        float intensity = lights[i].intensity;  // 例如：5.0, 10.0
        Vec3 lightColor = {
            lights[i].color.r / 255.0f * intensity,
            lights[i].color.g / 255.0f * intensity,
            lights[i].color.b / 255.0f * intensity
        };
        
        result.x += lightColor.x * NdotL;
        result.y += lightColor.y * NdotL;
        result.z += lightColor.z * NdotL;
    }
    
    return result;  // 可能远大于 1.0
}
```

### 2.3 色调映射（Tone Mapping）

```cpp
// 色调映射：HDR [0, ∞) → LDR [0, 1]

// 1. Reinhard 算子（最简单）
Vec3 ToneMappingReinhard(Vec3 hdrColor) {
    // L / (1 + L)
    return {
        hdrColor.x / (1.0f + hdrColor.x),
        hdrColor.y / (1.0f + hdrColor.y),
        hdrColor.z / (1.0f + hdrColor.z)
    };
}

// 2. 曝光调整 Reinhard
Vec3 ToneMappingReinhardExposure(Vec3 hdrColor, float exposure) {
    Vec3 exposed = {
        hdrColor.x * exposure,
        hdrColor.y * exposure,
        hdrColor.z * exposure
    };
    
    return ToneMappingReinhard(exposed);
}

// 3. Uncharted 2 (Filmic)
Vec3 Uncharted2Tonemap(Vec3 x) {
    float A = 0.15f;  // Shoulder Strength
    float B = 0.50f;  // Linear Strength
    float C = 0.10f;  // Linear Angle
    float D = 0.20f;  // Toe Strength
    float E = 0.02f;  // Toe Numerator
    float F = 0.30f;  // Toe Denominator
    
    Vec3 result;
    for (int i = 0; i < 3; i++) {
        float val = ((float*)&x)[i];
        ((float*)&result)[i] = 
            ((val * (A * val + C * B) + D * E) / 
             (val * (A * val + B) + D * F)) - E / F;
    }
    
    return result;
}

Vec3 ToneMappingUncharted2(Vec3 hdrColor, float exposure) {
    Vec3 curr = Uncharted2Tonemap(ScaleVec3(hdrColor, exposure));
    
    Vec3 whiteScale = {11.2f, 11.2f, 11.2f};
    Vec3 white = Uncharted2Tonemap(whiteScale);
    
    return {
        curr.x / white.x,
        curr.y / white.y,
        curr.z / white.z
    };
}

// 4. ACES (Academy Color Encoding System)
Vec3 ToneMappingACES(Vec3 x) {
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    
    return {
        Saturate((x.x * (a * x.x + b)) / (x.x * (c * x.x + d) + e)),
        Saturate((x.y * (a * x.y + b)) / (x.y * (c * x.y + d) + e)),
        Saturate((x.z * (a * x.z + b)) / (x.z * (c * x.z + d) + e))
    };
}

// 对比（输入 HDR 值 = 5.0）:
// ┌──────────────┬─────────┬────────────┐
// │ 算法         │ 输出    │ 特点       │
// ├──────────────┼─────────┼────────────┤
// │ Reinhard     │ 0.833   │ 简单，过亮 │
// │ Uncharted 2  │ 0.721   │ 电影感     │
// │ ACES         │ 0.658   │ 行业标准   │
// └──────────────┴─────────┴────────────┘

// Gamma 校正（最后一步）
Vec3 GammaCorrection(Vec3 linearColor, float gamma = 2.2f) {
    return {
        pow(linearColor.x, 1.0f / gamma),
        pow(linearColor.y, 1.0f / gamma),
        pow(linearColor.z, 1.0f / gamma)
    };
}

// 完整流程
Color HDRToLDR(Vec3 hdrColor, float exposure = 1.0f) {
    // 1. 色调映射
    Vec3 ldr = ToneMappingACES(ScaleVec3(hdrColor, exposure));
    
    // 2. Gamma 校正
    Vec3 gamma = GammaCorrection(ldr);
    
    // 3. 转换为 8 位
    return {
        (uint8_t)(std::clamp(gamma.x, 0.0f, 1.0f) * 255),
        (uint8_t)(std::clamp(gamma.y, 0.0f, 1.0f) * 255),
        (uint8_t)(std::clamp(gamma.z, 0.0f, 1.0f) * 255),
        255
    };
}
```

### 2.4 色调映射着色器（HLSL）

```hlsl
// HDR 纹理（浮点格式）
Texture2D hdrTexture : register(t0);
SamplerState hdrSampler : register(s0);

cbuffer ToneMappingParams : register(b0) {
    float exposure;
    float gamma;
};

// ACES 色调映射
float3 ACESFilm(float3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// 像素着色器
float4 PSMain(VS_OUTPUT input) : SV_TARGET {
    // 1. 采样 HDR 纹理
    float3 hdrColor = hdrTexture.Sample(hdrSampler, input.uv).rgb;
    
    // 2. 曝光调整
    hdrColor *= exposure;
    
    // 3. 色调映射
    float3 ldrColor = ACESFilm(hdrColor);
    
    // 4. Gamma 校正
    ldrColor = pow(ldrColor, 1.0 / gamma);
    
    return float4(ldrColor, 1.0);
}
```

---

## 3. Bloom 辉光效果

### 3.1 Bloom 原理

```
Bloom: 模拟相机/眼睛对强光的响应

效果：亮区域向周围扩散光晕

无 Bloom:             有 Bloom:
┌─────────────┐      ┌─────────────┐
│     ●       │      │   ░▒▓█▓▒░   │ ← 光晕扩散
│             │      │  ░▒▓███▓▒░  │
│             │      │   ░▒▓█▓▒░   │
└─────────────┘      └─────────────┘
  点光源              辉光效果

流程：
┌─────────────────────────────────────────┐
│ 1. 提取亮区域（Bright Pass）            │
│    原图 → 阈值过滤 → 亮区域             │
│    ┌───┐    ┌───┐    ┌───┐             │
│    │█░░│ →  │█  │ →  │█  │             │
│    └───┘    └───┘    └───┘             │
│                                         │
│ 2. 模糊（Gaussian Blur）                │
│    ┌───┐    ┌───┐    ┌───┐             │
│    │█  │ →  │▓▒░│ →  │▒░ │             │
│    └───┘    └───┘    └───┘             │
│                                         │
│ 3. 合成（Additive Blend）               │
│    原图 + 模糊 = 最终                   │
│    ┌───┐   ┌───┐   ┌───┐               │
│    │█░░│ + │▒░ │ = │█▓▒│               │
│    └───┘   └───┘   └───┘               │
└─────────────────────────────────────────┘
```

### 3.2 Bloom 实现

```cpp
// Bloom 效果类
class BloomEffect : public PostProcessEffect {
public:
    float threshold = 1.0f;      // 亮度阈值
    float intensity = 1.0f;      // 辉光强度
    int blurIterations = 5;      // 模糊迭代次数
    
    void Process(const Texture* input, Texture* output) override {
        // 1. 提取亮区域
        Texture brightPass(input->width, input->height);
        ExtractBrightAreas(input, &brightPass, threshold);
        
        // 2. 降采样（提高性能）
        int blurWidth = input->width / 4;
        int blurHeight = input->height / 4;
        Texture downsampled(blurWidth, blurHeight);
        Downsample(&brightPass, &downsampled);
        
        // 3. 高斯模糊
        Texture blurred(blurWidth, blurHeight);
        GaussianBlur(&downsampled, &blurred, blurIterations);
        
        // 4. 上采样
        Texture upsampled(input->width, input->height);
        Upsample(&blurred, &upsampled);
        
        // 5. 叠加到原图
        AdditiveBlend(input, &upsampled, output, intensity);
    }
    
private:
    // 提取亮区域
    void ExtractBrightAreas(const Texture* input, Texture* output, 
                           float threshold) {
        for (int y = 0; y < input->height; y++) {
            for (int x = 0; x < input->width; x++) {
                Color c = input->GetPixel(x, y);
                
                // 计算亮度
                float luminance = 0.2126f * c.r / 255.0f +
                                 0.7152f * c.g / 255.0f +
                                 0.0722f * c.b / 255.0f;
                
                // 阈值过滤
                if (luminance > threshold) {
                    output->SetPixel(x, y, c);
                } else {
                    output->SetPixel(x, y, {0, 0, 0, 255});
                }
            }
        }
    }
    
    // 高斯模糊
    void GaussianBlur(const Texture* input, Texture* output, int iterations) {
        // 创建临时纹理（乒乓）
        Texture temp(input->width, input->height);
        
        const Texture* src = input;
        Texture* dst = output;
        
        for (int i = 0; i < iterations; i++) {
            // 水平模糊
            GaussianBlurHorizontal(src, &temp);
            
            // 垂直模糊
            GaussianBlurVertical(&temp, dst);
            
            src = dst;
        }
    }
    
    // 水平高斯模糊
    void GaussianBlurHorizontal(const Texture* input, Texture* output) {
        // 5 tap 高斯核
        float kernel[5] = {0.0545f, 0.2442f, 0.4026f, 0.2442f, 0.0545f};
        int offsets[5] = {-2, -1, 0, 1, 2};
        
        for (int y = 0; y < input->height; y++) {
            for (int x = 0; x < input->width; x++) {
                float r = 0, g = 0, b = 0;
                
                for (int i = 0; i < 5; i++) {
                    int sampleX = std::clamp(x + offsets[i], 0, input->width - 1);
                    Color c = input->GetPixel(sampleX, y);
                    
                    r += c.r * kernel[i];
                    g += c.g * kernel[i];
                    b += c.b * kernel[i];
                }
                
                output->SetPixel(x, y, {
                    (uint8_t)r, (uint8_t)g, (uint8_t)b, 255
                });
            }
        }
    }
    
    // 垂直高斯模糊（类似）
    void GaussianBlurVertical(const Texture* input, Texture* output) {
        // 类似 GaussianBlurHorizontal，但在 Y 方向
        // ... 实现略
    }
    
    // 降采样（双线性）
    void Downsample(const Texture* input, Texture* output) {
        float scaleX = (float)input->width / output->width;
        float scaleY = (float)input->height / output->height;
        
        for (int y = 0; y < output->height; y++) {
            for (int x = 0; x < output->width; x++) {
                float srcX = x * scaleX;
                float srcY = y * scaleY;
                
                Color c = input->SampleBilinear({srcX / input->width, 
                                                 srcY / input->height});
                output->SetPixel(x, y, c);
            }
        }
    }
    
    // 上采样（双线性）
    void Upsample(const Texture* input, Texture* output) {
        // 与 Downsample 相反
        Downsample(input, output);  // 实现相同
    }
    
    // 叠加混合
    void AdditiveBlend(const Texture* base, const Texture* bloom, 
                      Texture* output, float intensity) {
        for (int y = 0; y < base->height; y++) {
            for (int x = 0; x < base->width; x++) {
                Color c1 = base->GetPixel(x, y);
                Color c2 = bloom->GetPixel(x, y);
                
                output->SetPixel(x, y, {
                    (uint8_t)std::min(255, (int)c1.r + (int)(c2.r * intensity)),
                    (uint8_t)std::min(255, (int)c1.g + (int)(c2.g * intensity)),
                    (uint8_t)std::min(255, (int)c1.b + (int)(c2.b * intensity)),
                    255
                });
            }
        }
    }
};

// 高斯核权重（标准差 σ = 1.0）:
// ┌─────────────────┐
// │ 0.06  0.24  0.40│  ← 中心权重最大
// │ 0.24  0.06      │  ← 边缘权重小
// └─────────────────┘
```

### 3.3 Bloom 着色器（HLSL）

```hlsl
// ========== Pass 1: 提取亮区域 ==========
Texture2D sceneTexture : register(t0);
SamplerState sceneSampler : register(s0);

cbuffer BloomParams : register(b0) {
    float threshold;
};

float4 PSBrightPass(VS_OUTPUT input) : SV_TARGET {
    float3 color = sceneTexture.Sample(sceneSampler, input.uv).rgb;
    
    // 计算亮度
    float luminance = dot(color, float3(0.2126, 0.7152, 0.0722));
    
    // 阈值过滤
    float contribution = max(0, luminance - threshold);
    contribution /= luminance + 0.0001;  // 避免除零
    
    return float4(color * contribution, 1.0);
}

// ========== Pass 2: 高斯模糊（水平）==========
Texture2D brightTexture : register(t0);

float4 PSBlurHorizontal(VS_OUTPUT input) : SV_TARGET {
    float2 texelSize;
    brightTexture.GetDimensions(texelSize.x, texelSize.y);
    texelSize = 1.0 / texelSize;
    
    // 5 tap 高斯核
    float weights[5] = {0.0545, 0.2442, 0.4026, 0.2442, 0.0545};
    int offsets[5] = {-2, -1, 0, 1, 2};
    
    float3 result = float3(0, 0, 0);
    
    [unroll]
    for (int i = 0; i < 5; i++) {
        float2 offset = float2(offsets[i] * texelSize.x, 0);
        result += brightTexture.Sample(sceneSampler, input.uv + offset).rgb * weights[i];
    }
    
    return float4(result, 1.0);
}

// ========== Pass 3: 高斯模糊（垂直）==========
float4 PSBlurVertical(VS_OUTPUT input) : SV_TARGET {
    float2 texelSize;
    brightTexture.GetDimensions(texelSize.x, texelSize.y);
    texelSize = 1.0 / texelSize;
    
    float weights[5] = {0.0545, 0.2442, 0.4026, 0.2442, 0.0545};
    int offsets[5] = {-2, -1, 0, 1, 2};
    
    float3 result = float3(0, 0, 0);
    
    [unroll]
    for (int i = 0; i < 5; i++) {
        float2 offset = float2(0, offsets[i] * texelSize.y);
        result += brightTexture.Sample(sceneSampler, input.uv + offset).rgb * weights[i];
    }
    
    return float4(result, 1.0);
}

// ========== Pass 4: 合成 ==========
Texture2D bloomTexture : register(t1);

cbuffer CompositeParams : register(b1) {
    float bloomIntensity;
};

float4 PSComposite(VS_OUTPUT input) : SV_TARGET {
    float3 sceneColor = sceneTexture.Sample(sceneSampler, input.uv).rgb;
    float3 bloomColor = bloomTexture.Sample(sceneSampler, input.uv).rgb;
    
    // 叠加
    float3 finalColor = sceneColor + bloomColor * bloomIntensity;
    
    return float4(finalColor, 1.0);
}
```

### 3.4 多级 Bloom（Dual Kawase）

```cpp
// Dual Kawase Blur: 更高效的 Bloom
// 特点：同时降采样和模糊

class DualKawaseBloom : public PostProcessEffect {
public:
    int downIterations = 5;   // 下采样次数
    float bloomIntensity = 1.0f;
    
    void Process(const Texture* input, Texture* output) override {
        // 创建 Mipmap 链
        std::vector<Texture*> mips;
        int w = input->width / 2;
        int h = input->height / 2;
        
        for (int i = 0; i < downIterations; i++) {
            mips.push_back(new Texture(w, h));
            w /= 2;
            h /= 2;
            if (w < 1 || h < 1) break;
        }
        
        // 下采样 + 模糊
        KawaseDownsample(input, mips[0]);
        for (size_t i = 1; i < mips.size(); i++) {
            KawaseDownsample(mips[i - 1], mips[i]);
        }
        
        // 上采样 + 混合
        for (int i = mips.size() - 2; i >= 0; i--) {
            KawaseUpsample(mips[i + 1], mips[i]);
        }
        
        // 合成
        Texture upsampled(input->width, input->height);
        Upsample(mips[0], &upsampled);
        AdditiveBlend(input, &upsampled, output, bloomIntensity);
        
        // 清理
        for (auto* mip : mips) {
            delete mip;
        }
    }
    
private:
    // Kawase 下采样（5 tap）
    void KawaseDownsample(const Texture* input, Texture* output) {
        // 采样模式：
        //   ●   ●
        //     ○
        //   ●   ●
        // ○ = 中心，● = 采样点
        
        float halfPixelX = 0.5f / input->width;
        float halfPixelY = 0.5f / input->height;
        
        for (int y = 0; y < output->height; y++) {
            for (int x = 0; x < output->width; x++) {
                float u = (x + 0.5f) / output->width;
                float v = (y + 0.5f) / output->height;
                
                Vec2 offsets[4] = {
                    {-halfPixelX, -halfPixelY},
                    { halfPixelX, -halfPixelY},
                    {-halfPixelX,  halfPixelY},
                    { halfPixelX,  halfPixelY}
                };
                
                float r = 0, g = 0, b = 0;
                for (int i = 0; i < 4; i++) {
                    Color c = input->SampleBilinear({u + offsets[i].x, 
                                                     v + offsets[i].y});
                    r += c.r;
                    g += c.g;
                    b += c.b;
                }
                
                output->SetPixel(x, y, {
                    (uint8_t)(r / 4), (uint8_t)(g / 4), (uint8_t)(b / 4), 255
                });
            }
        }
    }
    
    // Kawase 上采样（9 tap）
    void KawaseUpsample(const Texture* input, Texture* output) {
        // 采样模式：
        // ● ● ● ●
        // ● ○ ○ ●
        // ● ○ ○ ●
        // ● ● ● ●
        
        // 实现略（类似下采样，但采样点更多）
    }
};
```

---

## 4. 抗锯齿技术

### 4.1 锯齿问题

```
锯齿（Aliasing）：离散采样导致的阶梯状边缘

原因：
┌────────────────────────────────────┐
│ 真实边缘（连续）：                 │
│    ╱                                │
│   ╱                                 │
│  ╱                                  │
│ ╱                                   │
│                                     │
│ 像素化后（离散）：                 │
│    ██                               │
│   ███                               │
│  ███                                │
│ ███                                 │
│ 明显的"楼梯"                        │
└────────────────────────────────────┘

无抗锯齿：            有抗锯齿：
┌─────────┐          ┌─────────┐
│   ████  │          │   ▓▓▓▒  │
│  ████   │          │  ▓▓▓▒░  │
│ ████    │          │ ▓▓▓▒░   │
└─────────┘          └─────────┘
边缘锯齿              边缘平滑
```

### 4.2 FXAA（Fast Approximate Anti-Aliasing）

```cpp
// FXAA: 基于图像的快速抗锯齿
// 优势：后处理，性能高，无需多重采样

class FXAAEffect : public PostProcessEffect {
public:
    float edgeThreshold = 0.125f;      // 边缘检测阈值
    float edgeThresholdMin = 0.0312f;  // 最小阈值
    
    void Process(const Texture* input, Texture* output) override {
        for (int y = 1; y < input->height - 1; y++) {
            for (int x = 1; x < input->width - 1; x++) {
                Color result = ProcessPixel(input, x, y);
                output->SetPixel(x, y, result);
            }
        }
    }
    
private:
    Color ProcessPixel(const Texture* input, int x, int y) {
        // 1. 采样中心和周围像素
        Color M  = input->GetPixel(x, y);      // 中心
        Color N  = input->GetPixel(x, y - 1);  // 北
        Color S  = input->GetPixel(x, y + 1);  // 南
        Color E  = input->GetPixel(x + 1, y);  // 东
        Color W  = input->GetPixel(x - 1, y);  // 西
        Color NW = input->GetPixel(x - 1, y - 1);
        Color NE = input->GetPixel(x + 1, y - 1);
        Color SW = input->GetPixel(x - 1, y + 1);
        Color SE = input->GetPixel(x + 1, y + 1);
        
        // 2. 计算亮度
        float lumM  = Luminance(M);
        float lumN  = Luminance(N);
        float lumS  = Luminance(S);
        float lumE  = Luminance(E);
        float lumW  = Luminance(W);
        float lumNW = Luminance(NW);
        float lumNE = Luminance(NE);
        float lumSW = Luminance(SW);
        float lumSE = Luminance(SE);
        
        // 3. 检测边缘
        float lumMin = std::min(lumM, std::min(std::min(lumN, lumS), 
                                               std::min(lumE, lumW)));
        float lumMax = std::max(lumM, std::max(std::max(lumN, lumS), 
                                               std::max(lumE, lumW)));
        float lumRange = lumMax - lumMin;
        
        // 如果不是边缘，直接返回
        if (lumRange < std::max(edgeThresholdMin, lumMax * edgeThreshold)) {
            return M;
        }
        
        // 4. 确定边缘方向
        float lumNS = lumN + lumS;
        float lumEW = lumE + lumW;
        bool isHorizontal = abs(lumNS - 2 * lumM) >= abs(lumEW - 2 * lumM);
        
        // 5. 沿边缘方向混合
        if (isHorizontal) {
            // 水平边缘，垂直混合
            float blend = abs(lumN - lumM) / lumRange;
            Color c1 = input->GetPixel(x, y - 1);
            Color c2 = input->GetPixel(x, y + 1);
            return LerpColor(c1, c2, blend);
        } else {
            // 垂直边缘，水平混合
            float blend = abs(lumW - lumM) / lumRange;
            Color c1 = input->GetPixel(x - 1, y);
            Color c2 = input->GetPixel(x + 1, y);
            return LerpColor(c1, c2, blend);
        }
    }
    
    float Luminance(Color c) {
        return 0.299f * c.r / 255.0f +
               0.587f * c.g / 255.0f +
               0.114f * c.b / 255.0f;
    }
};

// FXAA 流程示意：
//
// 1. 检测边缘：
//    ┌─────────┐
//    │ 0.2 0.8 │  ← 亮度差大 = 边缘
//    │ 0.2 0.8 │
//    └─────────┘
//
// 2. 确定方向：
//    │ │  ← 垂直边缘
//    │ │
//
// 3. 沿边缘混合：
//    ┌─────────┐
//    │ 0.2 0.5 │  ← 插值
//    │ 0.2 0.5 │
//    └─────────┘
```

### 4.3 FXAA 着色器（HLSL）

```hlsl
Texture2D inputTexture : register(t0);
SamplerState inputSampler : register(s0);

cbuffer FXAAParams : register(b0) {
    float2 rcpFrame;  // 1.0 / 分辨率
    float edgeThreshold;
    float edgeThresholdMin;
};

// 亮度计算
float Luminance(float3 color) {
    return dot(color, float3(0.299, 0.587, 0.114));
}

float4 PSMain(VS_OUTPUT input) : SV_TARGET {
    float2 uv = input.uv;
    
    // 采样 3×3 邻域
    float3 rgbM  = inputTexture.Sample(inputSampler, uv).rgb;
    float3 rgbN  = inputTexture.Sample(inputSampler, uv + float2(0, -rcpFrame.y)).rgb;
    float3 rgbS  = inputTexture.Sample(inputSampler, uv + float2(0,  rcpFrame.y)).rgb;
    float3 rgbE  = inputTexture.Sample(inputSampler, uv + float2( rcpFrame.x, 0)).rgb;
    float3 rgbW  = inputTexture.Sample(inputSampler, uv + float2(-rcpFrame.x, 0)).rgb;
    float3 rgbNW = inputTexture.Sample(inputSampler, uv + float2(-rcpFrame.x, -rcpFrame.y)).rgb;
    float3 rgbNE = inputTexture.Sample(inputSampler, uv + float2( rcpFrame.x, -rcpFrame.y)).rgb;
    float3 rgbSW = inputTexture.Sample(inputSampler, uv + float2(-rcpFrame.x,  rcpFrame.y)).rgb;
    float3 rgbSE = inputTexture.Sample(inputSampler, uv + float2( rcpFrame.x,  rcpFrame.y)).rgb;
    
    // 计算亮度
    float lumM  = Luminance(rgbM);
    float lumN  = Luminance(rgbN);
    float lumS  = Luminance(rgbS);
    float lumE  = Luminance(rgbE);
    float lumW  = Luminance(rgbW);
    float lumNW = Luminance(rgbNW);
    float lumNE = Luminance(rgbNE);
    float lumSW = Luminance(rgbSW);
    float lumSE = Luminance(rgbSE);
    
    // 检测边缘
    float lumMin = min(lumM, min(min(lumN, lumS), min(lumE, lumW)));
    float lumMax = max(lumM, max(max(lumN, lumS), max(lumE, lumW)));
    float lumRange = lumMax - lumMin;
    
    if (lumRange < max(edgeThresholdMin, lumMax * edgeThreshold)) {
        return float4(rgbM, 1.0);  // 无边缘
    }
    
    // 确定边缘方向
    float lumNS = lumN + lumS;
    float lumEW = lumE + lumW;
    
    bool isHorizontal = abs(lumNS - 2.0 * lumM) >= abs(lumEW - 2.0 * lumM);
    
    // 沿边缘方向混合
    float3 result;
    if (isHorizontal) {
        float blend = abs(lumN - lumM) / lumRange;
        result = lerp(rgbN, rgbS, blend);
    } else {
        float blend = abs(lumW - lumM) / lumRange;
        result = lerp(rgbW, rgbE, blend);
    }
    
    return float4(result, 1.0);
}
```

### 4.4 TAA（Temporal Anti-Aliasing）

```cpp
// TAA: 时间抗锯齿
// 原理：混合当前帧和历史帧

class TAAEffect : public PostProcessEffect {
public:
    Texture* historyBuffer = nullptr;  // 上一帧
    float blendFactor = 0.9f;          // 历史权重
    
    void Process(const Texture* input, Texture* output) override {
        if (!historyBuffer) {
            // 第一帧，初始化历史缓冲
            historyBuffer = new Texture(input->width, input->height);
            Copy(input, historyBuffer);
            Copy(input, output);
            return;
        }
        
        // 混合当前帧和历史帧
        for (int y = 0; y < input->height; y++) {
            for (int x = 0; x < input->width; x++) {
                Color current = input->GetPixel(x, y);
                Color history = historyBuffer->GetPixel(x, y);
                
                // 时间混合
                Color result = LerpColor(current, history, blendFactor);
                
                output->SetPixel(x, y, result);
            }
        }
        
        // 更新历史缓冲
        Copy(output, historyBuffer);
    }
};

// TAA 优势：
// - 高质量（接近超采样）
// - 性能好（复用历史）
//
// TAA 问题：
// - 运动模糊（ghosting）
// - 需要运动向量
```

---

## 5. 其他常见后处理

### 5.1 景深（Depth of Field）

```cpp
// 景深: 模拟相机对焦效果

class DepthOfFieldEffect : public PostProcessEffect {
public:
    float focusDistance = 10.0f;  // 对焦距离
    float focusRange = 5.0f;      // 对焦范围
    float blurAmount = 3.0f;      // 模糊强度
    
    void Process(const Texture* input, Texture* output) override {
        const float* depthBuffer = GetDepthBuffer();  // 获取深度
        
        for (int y = 0; y < input->height; y++) {
            for (int x = 0; x < input->width; x++) {
                float depth = depthBuffer[y * input->width + x];
                
                // 计算模糊量（根据距离对焦点的偏移）
                float blur = abs(depth - focusDistance) / focusRange;
                blur = std::clamp(blur, 0.0f, 1.0f) * blurAmount;
                
                // 模糊采样
                Color blurred = SampleBlurred(input, x, y, (int)blur);
                
                output->SetPixel(x, y, blurred);
            }
        }
    }
    
private:
    Color SampleBlurred(const Texture* tex, int x, int y, int radius) {
        if (radius == 0) {
            return tex->GetPixel(x, y);
        }
        
        float r = 0, g = 0, b = 0;
        int count = 0;
        
        for (int dy = -radius; dy <= radius; dy++) {
            for (int dx = -radius; dx <= radius; dx++) {
                int sx = std::clamp(x + dx, 0, tex->width - 1);
                int sy = std::clamp(y + dy, 0, tex->height - 1);
                
                Color c = tex->GetPixel(sx, sy);
                r += c.r;
                g += c.g;
                b += c.b;
                count++;
            }
        }
        
        return {(uint8_t)(r / count), (uint8_t)(g / count), 
                (uint8_t)(b / count), 255};
    }
};

// 景深效果：
// 无景深：              有景深：
// ┌─────────┐          ┌─────────┐
// │ ▓▓▓▓▓▓  │          │ ░░░░░░  │ ← 远处模糊
// │ ▓▓▓▓▓▓  │          │ ▓▓▓▓▓▓  │ ← 对焦点清晰
// │ ▓▓▓▓▓▓  │          │ ░░░░░░  │ ← 近处模糊
// └─────────┘          └─────────┘
//  全清晰                电影感
```

### 5.2 色差（Chromatic Aberration）

```cpp
// 色差: 模拟镜头的色彩分离

class ChromaticAberrationEffect : public PostProcessEffect {
public:
    float strength = 0.002f;  // 色差强度
    
    void Process(const Texture* input, Texture* output) override {
        for (int y = 0; y < input->height; y++) {
            for (int x = 0; x < input->width; x++) {
                // 计算到中心的偏移
                float centerX = input->width / 2.0f;
                float centerY = input->height / 2.0f;
                
                Vec2 offset = {
                    (x - centerX) / centerX,
                    (y - centerY) / centerY
                };
                
                // 不同颜色通道采样不同位置
                int xR = x + (int)(offset.x * strength * input->width);
                int xG = x;
                int xB = x - (int)(offset.x * strength * input->width);
                
                xR = std::clamp(xR, 0, input->width - 1);
                xB = std::clamp(xB, 0, input->width - 1);
                
                uint8_t r = input->GetPixel(xR, y).r;
                uint8_t g = input->GetPixel(xG, y).g;
                uint8_t b = input->GetPixel(xB, y).b;
                
                output->SetPixel(x, y, {r, g, b, 255});
            }
        }
    }
};

// 色差效果（夸张）：
// 原图：     ████      色差后：  █▓▒░
//                                ░▒▓█
//                      边缘出现彩色条纹
```

### 5.3 暗角（Vignette）

```cpp
// 暗角: 边缘变暗效果

class VignetteEffect : public PostProcessEffect {
public:
    float intensity = 0.5f;  // 强度
    float falloff = 0.4f;    // 衰减
    
    void Process(const Texture* input, Texture* output) override {
        float centerX = input->width / 2.0f;
        float centerY = input->height / 2.0f;
        float maxDist = sqrt(centerX * centerX + centerY * centerY);
        
        for (int y = 0; y < input->height; y++) {
            for (int x = 0; x < input->width; x++) {
                // 到中心的距离
                float dx = x - centerX;
                float dy = y - centerY;
                float dist = sqrt(dx * dx + dy * dy) / maxDist;
                
                // 暗角系数
                float vignette = 1.0f - pow(dist, falloff) * intensity;
                vignette = std::clamp(vignette, 0.0f, 1.0f);
                
                // 应用
                Color c = input->GetPixel(x, y);
                output->SetPixel(x, y, {
                    (uint8_t)(c.r * vignette),
                    (uint8_t)(c.g * vignette),
                    (uint8_t)(c.b * vignette),
                    255
                });
            }
        }
    }
};

// 暗角效果：
// 无暗角：              有暗角：
// ┌─────────┐          ┌─────────┐
// │▓▓▓▓▓▓▓▓▓│          │░░░░░░░░░│
// │▓▓▓▓▓▓▓▓▓│          │░▒▓▓▓▓▓▒░│
// │▓▓▓▓▓▓▓▓▓│          │░░░░░░░░░│
// └─────────┘          └─────────┘
//  均匀亮度              边缘暗，聚焦中心
```

### 5.4 后处理性能对比

```
┌──────────────────┬──────────┬──────────┬────────────┐
│ 效果             │ 时间(ms) │ 质量     │ 适用场景   │
├──────────────────┼──────────┼──────────┼────────────┤
│ Tone Mapping     │   0.2    │ 必须     │ HDR 渲染   │
│ Bloom            │   1.5    │ 高       │ 几乎所有   │
│ FXAA             │   0.5    │ 中       │ 性能优先   │
│ TAA              │   0.8    │ 高       │ 质量优先   │
│ Depth of Field   │   2.0    │ 高       │ 电影感     │
│ Chromatic Abbr.  │   0.3    │ 低       │ 特殊效果   │
│ Vignette         │   0.1    │ 低       │ 氛围渲染   │
│ 完整后处理链     │   5.4    │ AAA      │ 现代游戏   │
└──────────────────┴──────────┴──────────┴────────────┘

性能优化建议：
1. 使用 Compute Shader（GPU 并行）
2. 降低分辨率处理（如 Bloom）
3. 按需启用（动态调整）
4. 合并 Pass（减少纹理切换）
```

---

## 📚 总结

### Part 4（A/B/C）完整回顾

```
Part 4A - 高级光照：
✓ Phong / Blinn-Phong（经典模型）
✓ PBR（物理渲染）
✓ 全局光照基础

Part 4B - 阴影技术：
✓ Shadow Mapping（基础）
✓ PCF（软阴影）
✓ CSM（大场景）
✓ PCSS / VSM（高级）

Part 4C - 后处理效果：
✓ HDR + 色调映射（基础）
✓ Bloom（辉光）
✓ 抗锯齿（FXAA / TAA）
✓ 景深、色差、暗角
```

### 现代游戏渲染管线

```
完整流程：
┌─────────────────────────────────────────┐
│ 1. 几何处理                              │
│    顶点变换 → 背面剔除 → 裁剪           │
│                                          │
│ 2. 光栅化                                │
│    三角形 → 像素                         │
│                                          │
│ 3. 光照（PBR）                           │
│    BRDF + IBL + 多光源                  │
│                                          │
│ 4. 阴影（CSM + PCF）                     │
│    深度测试 + 软化                       │
│                                          │
│ 5. 后处理链                              │
│    HDR → Bloom → Tone Mapping → FXAA    │
│                                          │
│ 6. UI 渲染                               │
│    文字 + 图标 + HUD                     │
│                                          │
│ 7. 显示                                  │
│    SwapChain → 显示器                    │
└─────────────────────────────────────────┘
```

### 下一步

**Part 5** 将学习性能优化、实战技巧与调试方法！

恭喜完成 Part 4（三部分）的学习！🎉  
准备好继续 Part 5 了吗？🚀
