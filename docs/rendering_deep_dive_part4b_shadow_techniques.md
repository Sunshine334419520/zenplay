# 渲染深度解析 Part 4B：阴影技术

**文档目标**：掌握实时阴影渲染的核心技术  
**前置知识**：Part 2（渲染管线）、Part 4A（光照）  
**阅读时间**：35-45 分钟

---

## 📚 目录

1. [阴影基础理论](#1-阴影基础理论)
2. [Shadow Mapping 原理与实现](#2-shadow-mapping-原理与实现)
3. [PCF 软阴影](#3-pcf-软阴影)
4. [级联阴影贴图（CSM）](#4-级联阴影贴图csm)
5. [其他阴影技术](#5-其他阴影技术)

---

## 1. 阴影基础理论

### 1.1 阴影的类型

```
阴影分类：
┌──────────────────────────────────────┐
│ 1. 硬阴影（Hard Shadow）             │
│    - 边缘锐利                         │
│    - 点光源产生                       │
│    ☀️ (点光源)                       │
│      │                               │
│      ▼                               │
│    ████ (遮挡物)                     │
│    ████████ (硬阴影)                 │
│                                      │
│ 2. 软阴影（Soft Shadow）             │
│    - 边缘模糊                         │
│    - 面光源产生                       │
│    ╔═══╗ (面光源)                    │
│    ║   ║                             │
│    ╚═▼═╝                             │
│    ████ (遮挡物)                     │
│    ██▓▓▒▒ (软阴影，渐变)             │
│                                      │
│ 3. 本影与半影                         │
│    ╔═══════╗                         │
│    ║ 光源  ║                         │
│    ╚═══╦═══╝                         │
│        ║   ╲                         │
│        ▼    ╲                        │
│      ████    ╲                       │
│      ████ (本影 Umbra)               │
│      ██▓▓ (半影 Penumbra)            │
└──────────────────────────────────────┘

视觉对比：
硬阴影：            软阴影：
┌─────────┐        ┌─────────┐
│  ███    │        │  ███    │
│ █████   │        │ ████▓   │
│███████  │        │█████▓▒  │
└─────────┘        └─────────┘
边缘锐利            边缘渐变
```

### 1.2 阴影算法分类

```cpp
// 阴影算法对比
enum class ShadowTechnique {
    ShadowMapping,      // 阴影贴图（最常用）
    ShadowVolume,       // 阴影体积
    RayTracing,         // 光线追踪
    PCSS,               // 百分比渐近软阴影
    VSM,                // 方差阴影贴图
    ESM                 // 指数阴影贴图
};

// 对比表
┌────────────────┬──────────┬──────────┬──────────┬────────────┐
│ 技术           │ 质量     │ 性能     │ 实现难度 │ 适用场景   │
├────────────────┼──────────┼──────────┼──────────┼────────────┤
│ Shadow Map     │ 中       │ 高       │ 低       │ 实时游戏   │
│ Shadow Volume  │ 高       │ 低       │ 高       │ 废弃       │
│ Ray Tracing    │ 极高     │ 极低     │ 中       │ 离线/RTX   │
│ PCF            │ 中+      │ 中       │ 低       │ 软阴影     │
│ CSM            │ 高       │ 中       │ 中       │ 大场景     │
│ PCSS           │ 高       │ 低       │ 中       │ 真实软阴影 │
└────────────────┴──────────┴──────────┴──────────┴────────────┘
```

---

## 2. Shadow Mapping 原理与实现

### 2.1 Shadow Mapping 原理

```
核心思想：两次渲染
1. 从光源视角渲染，记录深度（Shadow Map）
2. 从相机视角渲染，比较深度判断阴影

过程示意：
┌─────────────────────────────────────────────┐
│ Pass 1: 从光源渲染                           │
│   ☀️ (光源)                                 │
│    │ 视线                                    │
│    │                                         │
│    ▼                                         │
│  ████ (遮挡物，深度 = 3.0)                  │
│    │                                         │
│    │                                         │
│    ▼                                         │
│  ░░░░ (地面，深度 = 5.0)                    │
│                                              │
│ 结果：Shadow Map                             │
│ ┌───────────┐                               │
│ │ 3.0  3.0  │ ← 遮挡物深度                  │
│ │ 5.0  5.0  │ ← 地面深度                    │
│ └───────────┘                               │
│                                              │
│ Pass 2: 从相机渲染                           │
│   📷 (相机)                                 │
│    │                                         │
│    ▼                                         │
│  ████ (遮挡物)                              │
│    │                                         │
│    ▼                                         │
│  ●───→ (地面点 P)                           │
│    │                                         │
│    └─→ 查询 Shadow Map                      │
│        P 的光源深度 = 5.0                    │
│        Shadow Map 深度 = 3.0                │
│        5.0 > 3.0 → 在阴影中 ✓               │
└─────────────────────────────────────────────┘
```

### 2.2 Shadow Mapping 实现

```cpp
// 阴影贴图类
class ShadowMap {
public:
    int width, height;
    float* depthBuffer;
    Mat4 lightViewProj;  // 光源的 View * Projection 矩阵
    
    ShadowMap(int w, int h) : width(w), height(h) {
        depthBuffer = new float[w * h];
    }
    
    ~ShadowMap() {
        delete[] depthBuffer;
    }
    
    // Pass 1: 从光源渲染深度
    void RenderFromLight(const std::vector<Triangle>& triangles,
                        const DirectionalLight& light) {
        // 1. 清空深度缓冲
        for (int i = 0; i < width * height; i++) {
            depthBuffer[i] = 1.0f;  // 最远深度
        }
        
        // 2. 计算光源的 View 和 Projection 矩阵
        Vec3 lightDir = Normalize(light.direction);
        Vec3 lightPos = lightDir * -10.0f;  // 假设光源在远处
        Mat4 lightView = Mat4::LookAt(lightPos, {0, 0, 0}, {0, 1, 0});
        
        // 正交投影（方向光）
        Mat4 lightProj = Mat4::Orthographic(-10, 10, -10, 10, 0.1f, 20.0f);
        lightViewProj = lightProj * lightView;
        
        // 3. 渲染所有三角形
        for (const auto& tri : triangles) {
            RasterizeTriangleDepthOnly(tri, lightViewProj);
        }
    }
    
    // 深度光栅化（只写深度）
    void RasterizeTriangleDepthOnly(const Triangle& tri, const Mat4& mvp) {
        // 顶点变换
        Vec4 v0_clip = mvp * Vec4{tri.v0.position.x, tri.v0.position.y, 
                                  tri.v0.position.z, 1.0f};
        Vec4 v1_clip = mvp * Vec4{tri.v1.position.x, tri.v1.position.y, 
                                  tri.v1.position.z, 1.0f};
        Vec4 v2_clip = mvp * Vec4{tri.v2.position.x, tri.v2.position.y, 
                                  tri.v2.position.z, 1.0f};
        
        // 透视除法
        Vec3 v0_ndc = {v0_clip.x / v0_clip.w, v0_clip.y / v0_clip.w, v0_clip.z / v0_clip.w};
        Vec3 v1_ndc = {v1_clip.x / v1_clip.w, v1_clip.y / v1_clip.w, v1_clip.z / v1_clip.w};
        Vec3 v2_ndc = {v2_clip.x / v2_clip.w, v2_clip.y / v2_clip.w, v2_clip.z / v2_clip.w};
        
        // 屏幕空间
        Vec2 v0_screen = {(v0_ndc.x + 1) * 0.5f * width, (1 - v0_ndc.y) * 0.5f * height};
        Vec2 v1_screen = {(v1_ndc.x + 1) * 0.5f * width, (1 - v1_ndc.y) * 0.5f * height};
        Vec2 v2_screen = {(v2_ndc.x + 1) * 0.5f * width, (1 - v2_ndc.y) * 0.5f * height};
        
        // 光栅化
        BBox bbox = ComputeBBox(v0_screen, v1_screen, v2_screen, width, height);
        
        for (int y = bbox.minY; y <= bbox.maxY; y++) {
            for (int x = bbox.minX; x <= bbox.maxX; x++) {
                Vec2 p = {x + 0.5f, y + 0.5f};
                
                if (!InsideTriangle(p, v0_screen, v1_screen, v2_screen)) {
                    continue;
                }
                
                // 重心坐标插值深度
                Barycentric bary = ComputeBarycentric(p, v0_screen, v1_screen, v2_screen);
                float depth = BarycentricInterpolate(v0_ndc.z, v1_ndc.z, v2_ndc.z, bary);
                
                // 深度测试
                int index = y * width + x;
                if (depth < depthBuffer[index]) {
                    depthBuffer[index] = depth;
                }
            }
        }
    }
    
    // Pass 2: 查询阴影
    float QueryShadow(Vec3 worldPosition) const {
        // 1. 转换到光源空间
        Vec4 lightSpacePos = lightViewProj * Vec4{worldPosition.x, worldPosition.y, 
                                                  worldPosition.z, 1.0f};
        
        // 2. 透视除法 → NDC [-1, 1]
        Vec3 ndc = {lightSpacePos.x / lightSpacePos.w,
                    lightSpacePos.y / lightSpacePos.w,
                    lightSpacePos.z / lightSpacePos.w};
        
        // 3. NDC → 纹理坐标 [0, 1]
        Vec2 uv = {(ndc.x + 1) * 0.5f, (1 - ndc.y) * 0.5f};
        
        // 4. 边界检查
        if (uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1) {
            return 1.0f;  // 在阴影贴图外，无阴影
        }
        
        // 5. 采样深度
        int x = (int)(uv.x * width);
        int y = (int)(uv.y * height);
        x = std::clamp(x, 0, width - 1);
        y = std::clamp(y, 0, height - 1);
        
        float closestDepth = depthBuffer[y * width + x];
        float currentDepth = ndc.z;
        
        // 6. 深度比较（加偏移避免阴影痤疮）
        float bias = 0.005f;
        float shadow = (currentDepth - bias) > closestDepth ? 0.0f : 1.0f;
        
        return shadow;  // 0 = 阴影，1 = 光照
    }
};

// 使用示例
int main() {
    // 创建阴影贴图
    ShadowMap shadowMap(1024, 1024);
    
    // 光源
    DirectionalLight light = {
        {1, 1, 1}, 1.0f,
        {1, -1, 0}  // 方向
    };
    
    // Pass 1: 渲染阴影贴图
    std::vector<Triangle> triangles = LoadScene();
    shadowMap.RenderFromLight(triangles, light);
    
    // Pass 2: 正常渲染 + 阴影查询
    for (const auto& tri : triangles) {
        // ... 光栅化三角形
        // 在像素着色器中：
        Vec3 worldPos = {/* ... */};
        float shadow = shadowMap.QueryShadow(worldPos);
        Color finalColor = lightingColor * shadow;  // 阴影遮挡光照
    }
    
    return 0;
}
```

### 2.3 Shadow Mapping 着色器（HLSL）

```hlsl
// ============= Pass 1: 渲染深度到 Shadow Map =============

// 顶点着色器
struct VS_INPUT {
    float3 position : POSITION;
};

struct VS_OUTPUT {
    float4 position : SV_POSITION;
};

cbuffer LightMatrices : register(b0) {
    float4x4 lightViewProj;
};

VS_OUTPUT VSMain(VS_INPUT input) {
    VS_OUTPUT output;
    output.position = mul(float4(input.position, 1.0), lightViewProj);
    return output;
}

// 像素着色器（空，只写深度）
void PSMain(VS_OUTPUT input) {
    // 深度自动写入 Depth Buffer
}

// ============= Pass 2: 正常渲染 + 阴影 =============

// 顶点着色器
struct VS_INPUT2 {
    float3 position : POSITION;
    float3 normal : NORMAL;
};

struct VS_OUTPUT2 {
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float4 lightSpacePos : TEXCOORD2;  // 光源空间位置
};

cbuffer Matrices : register(b0) {
    float4x4 world;
    float4x4 view;
    float4x4 proj;
    float4x4 lightViewProj;
};

VS_OUTPUT2 VSMain(VS_INPUT2 input) {
    VS_OUTPUT2 output;
    
    float4 worldPos = mul(float4(input.position, 1.0), world);
    output.worldPos = worldPos.xyz;
    output.position = mul(mul(worldPos, view), proj);
    output.normal = mul(input.normal, (float3x3)world);
    
    // 计算光源空间位置
    output.lightSpacePos = mul(worldPos, lightViewProj);
    
    return output;
}

// 阴影贴图
Texture2D shadowMapTexture : register(t0);
SamplerState shadowMapSampler : register(s0);

// 像素着色器
float4 PSMain(VS_OUTPUT2 input) : SV_TARGET {
    // 1. 计算光照（Blinn-Phong）
    float3 N = normalize(input.normal);
    float3 L = normalize(lightDirection);
    float3 V = normalize(cameraPosition - input.worldPos);
    float3 H = normalize(L + V);
    
    float3 ambient = float3(0.1, 0.1, 0.1);
    float3 diffuse = max(dot(N, L), 0.0) * float3(1.0, 1.0, 1.0);
    float3 specular = pow(max(dot(N, H), 0.0), 32.0) * float3(0.5, 0.5, 0.5);
    
    float3 lighting = ambient + diffuse + specular;
    
    // 2. 计算阴影
    // 透视除法
    float3 projCoords = input.lightSpacePos.xyz / input.lightSpacePos.w;
    
    // NDC [-1, 1] → 纹理坐标 [0, 1]
    projCoords.x = projCoords.x * 0.5 + 0.5;
    projCoords.y = -projCoords.y * 0.5 + 0.5;  // Y 翻转
    
    // 采样 Shadow Map
    float closestDepth = shadowMapTexture.Sample(shadowMapSampler, projCoords.xy).r;
    float currentDepth = projCoords.z;
    
    // 深度比较（加偏移）
    float bias = 0.005;
    float shadow = (currentDepth - bias) > closestDepth ? 0.0 : 1.0;
    
    // 边界外无阴影
    if (projCoords.x < 0.0 || projCoords.x > 1.0 || 
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        shadow = 1.0;
    }
    
    // 3. 应用阴影
    float3 finalColor = lighting * shadow;
    
    return float4(finalColor, 1.0);
}
```

### 2.4 Shadow Mapping 常见问题

```
问题 1: 阴影痤疮（Shadow Acne）
原因：深度精度不足，自阴影

无偏移：              有偏移：
┌─────────┐          ┌─────────┐
│░▓░▓░▓░▓░│          │░░░░░░░░░│
│▓░▓░▓░▓░▓│          │░░░░░░░░░│
│░▓░▓░▓░▓░│          │░░░░░░░░░│
└─────────┘          └─────────┘
条纹伪影              正常

解决方法：
float bias = max(0.05 * (1.0 - dot(N, L)), 0.005);
float shadow = (currentDepth - bias) > closestDepth ? 0.0 : 1.0;

问题 2: Peter Panning（物体悬浮）
原因：偏移过大

偏移过小：            偏移过大：
    ████                 ████
    ████████            ░░██████  ← 阴影与物体分离
  阴影痤疮              Peter Panning

解决方法：
- 使用自适应偏移
- 前面剔除（渲染背面到 Shadow Map）

问题 3: 阴影边缘锯齿
原因：Shadow Map 分辨率不足

低分辨率 (256×256):  高分辨率 (2048×2048):
┌─────────┐          ┌─────────┐
│ ███     │          │ ███     │
│ ██▓░    │          │ ███▓▒░  │
│ █▓░░    │          │ ███▓▒░  │
└─────────┘          └─────────┘
明显锯齿              平滑

解决方法：
- 提高分辨率（2048×2048 或更高）
- 使用 PCF 软化（见下节）
```

---

## 3. PCF 软阴影

### 3.1 PCF 原理

```
PCF (Percentage Closer Filtering)
不直接过滤深度值，而是过滤阴影测试结果

标准采样：            PCF 采样（3×3）：
┌───┐                ┌───┬───┬───┐
│ ● │ 采样 1 个      │ ● │ ● │ ● │ 采样 9 个
└───┘                ├───┼───┼───┤
shadow = 0 或 1      │ ● │ ● │ ● │
                     ├───┼───┼───┤
                     │ ● │ ● │ ● │
                     └───┴───┴───┘
                     shadow = (结果总和) / 9
                     例如：5/9 = 0.555（半影）

视觉效果：
无 PCF:              PCF 3×3:            PCF 5×5:
┌─────────┐         ┌─────────┐         ┌─────────┐
│ ███     │         │ ███     │         │ ███     │
│ ██░     │         │ ██▓░    │         │ ██▓▒░   │
│ █░      │         │ █▓░     │         │ █▓▒░    │
└─────────┘         └─────────┘         └─────────┘
硬边缘              软边缘              更软
```

### 3.2 PCF 实现

```cpp
// PCF 阴影查询
float QueryShadowPCF(const ShadowMap& shadowMap, Vec3 worldPosition, 
                    int filterSize = 3) {
    // 1. 转换到光源空间
    Vec4 lightSpacePos = shadowMap.lightViewProj * 
        Vec4{worldPosition.x, worldPosition.y, worldPosition.z, 1.0f};
    
    Vec3 ndc = {lightSpacePos.x / lightSpacePos.w,
                lightSpacePos.y / lightSpacePos.w,
                lightSpacePos.z / lightSpacePos.w};
    
    Vec2 uv = {(ndc.x + 1) * 0.5f, (1 - ndc.y) * 0.5f};
    float currentDepth = ndc.z;
    
    // 2. 边界检查
    if (uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1) {
        return 1.0f;
    }
    
    // 3. PCF 过滤
    float shadow = 0.0f;
    float texelSize = 1.0f / shadowMap.width;
    int halfFilter = filterSize / 2;
    
    for (int y = -halfFilter; y <= halfFilter; y++) {
        for (int x = -halfFilter; x <= halfFilter; x++) {
            // 偏移采样
            Vec2 offset = {x * texelSize, y * texelSize};
            Vec2 sampleUV = {uv.x + offset.x, uv.y + offset.y};
            
            // 采样深度
            int sx = (int)(sampleUV.x * shadowMap.width);
            int sy = (int)(sampleUV.y * shadowMap.height);
            sx = std::clamp(sx, 0, shadowMap.width - 1);
            sy = std::clamp(sy, 0, shadowMap.height - 1);
            
            float closestDepth = shadowMap.depthBuffer[sy * shadowMap.width + sx];
            
            // 深度比较
            float bias = 0.005f;
            shadow += (currentDepth - bias) > closestDepth ? 0.0f : 1.0f;
        }
    }
    
    // 4. 归一化
    shadow /= (filterSize * filterSize);
    
    return shadow;
}

// 性能对比：
// 1×1（无 PCF）：1 次采样
// 3×3 PCF：9 次采样（9 倍慢）
// 5×5 PCF：25 次采样（25 倍慢）
// 7×7 PCF：49 次采样（49 倍慢）
```

### 3.3 PCF 着色器（HLSL）

```hlsl
// PCF 阴影采样
float ShadowCalculationPCF(float4 lightSpacePos, Texture2D shadowMap, 
                          SamplerState shadowSampler) {
    // 透视除法
    float3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords.x = projCoords.x * 0.5 + 0.5;
    projCoords.y = -projCoords.y * 0.5 + 0.5;
    
    float currentDepth = projCoords.z;
    
    // 边界检查
    if (projCoords.x < 0.0 || projCoords.x > 1.0 || 
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 1.0;
    }
    
    // PCF 过滤（3×3）
    float shadow = 0.0;
    float2 texelSize;
    shadowMap.GetDimensions(texelSize.x, texelSize.y);
    texelSize = 1.0 / texelSize;
    
    float bias = 0.005;
    
    [unroll]
    for (int y = -1; y <= 1; y++) {
        [unroll]
        for (int x = -1; x <= 1; x++) {
            float2 offset = float2(x, y) * texelSize;
            float closestDepth = shadowMap.Sample(shadowSampler, 
                projCoords.xy + offset).r;
            shadow += (currentDepth - bias) > closestDepth ? 0.0 : 1.0;
        }
    }
    
    shadow /= 9.0;
    
    return shadow;
}

// 优化版本：使用硬件过滤
// Direct3D 11 支持 SampleCmpLevelZero
float ShadowCalculationPCF_Hardware(float4 lightSpacePos, 
    Texture2D shadowMap, 
    SamplerComparisonState shadowSamplerCmp) {
    float3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords.x = projCoords.x * 0.5 + 0.5;
    projCoords.y = -projCoords.y * 0.5 + 0.5;
    
    // 硬件 PCF（2×2）
    float shadow = shadowMap.SampleCmpLevelZero(shadowSamplerCmp, 
        projCoords.xy, projCoords.z - 0.005);
    
    return shadow;
}
```

### 3.4 Poisson Disk PCF

```cpp
// Poisson 分布采样点（更自然的采样模式）
static const Vec2 poissonDisk[16] = {
    {-0.94201624f, -0.39906216f},
    { 0.94558609f, -0.76890725f},
    {-0.09418410f, -0.92938870f},
    { 0.34495938f,  0.29387760f},
    {-0.91588581f,  0.45771432f},
    {-0.81544232f, -0.87912464f},
    {-0.38277543f,  0.27676845f},
    { 0.97484398f,  0.75648379f},
    { 0.44323325f, -0.97511554f},
    { 0.53742981f, -0.47373420f},
    {-0.26496911f, -0.41893023f},
    { 0.79197514f,  0.19090188f},
    {-0.24188840f,  0.99706507f},
    {-0.81409955f,  0.91437590f},
    { 0.19984126f,  0.78641367f},
    { 0.14383161f, -0.14100790f}
};

float QueryShadowPoissonPCF(const ShadowMap& shadowMap, Vec3 worldPosition) {
    Vec4 lightSpacePos = shadowMap.lightViewProj * 
        Vec4{worldPosition.x, worldPosition.y, worldPosition.z, 1.0f};
    
    Vec3 ndc = {lightSpacePos.x / lightSpacePos.w,
                lightSpacePos.y / lightSpacePos.w,
                lightSpacePos.z / lightSpacePos.w};
    
    Vec2 uv = {(ndc.x + 1) * 0.5f, (1 - ndc.y) * 0.5f};
    float currentDepth = ndc.z;
    
    float shadow = 0.0f;
    float texelSize = 1.0f / shadowMap.width;
    float searchRadius = 2.0f;  // 搜索半径
    
    // Poisson 采样
    for (int i = 0; i < 16; i++) {
        Vec2 offset = {poissonDisk[i].x * texelSize * searchRadius,
                       poissonDisk[i].y * texelSize * searchRadius};
        Vec2 sampleUV = {uv.x + offset.x, uv.y + offset.y};
        
        int sx = (int)(sampleUV.x * shadowMap.width);
        int sy = (int)(sampleUV.y * shadowMap.height);
        sx = std::clamp(sx, 0, shadowMap.width - 1);
        sy = std::clamp(sy, 0, shadowMap.height - 1);
        
        float closestDepth = shadowMap.depthBuffer[sy * shadowMap.width + sx];
        
        float bias = 0.005f;
        shadow += (currentDepth - bias) > closestDepth ? 0.0f : 1.0f;
    }
    
    shadow /= 16.0f;
    return shadow;
}

// Poisson vs 规则网格：
//
// 规则网格（3×3）:      Poisson Disk:
// ┌───────────┐         ┌───────────┐
// │ ● ● ●     │         │ ●  ●      │
// │ ● ● ●     │         │   ●  ●    │
// │ ● ● ●     │         │ ●    ●    │
// └───────────┘         │  ●   ●    │
//  规则排列             └───────────┘
//  可能有条纹            更自然，无条纹
```

---

## 4. 级联阴影贴图（CSM）

### 4.1 CSM 原理

```
问题：大场景中，近处阴影需要高精度，远处阴影可以低精度

单张 Shadow Map:     级联 Shadow Map (CSM):
┌─────────────────┐  ┌────┬─────┬─────────┐
│░░░░░░░░░░░░░░░░░│  │▓▓▓▓│▒▒▒▒▒│░░░░░░░░░│
│░░░░░░░░░░░░░░░░░│  │▓▓▓▓│▒▒▒▒▒│░░░░░░░░░│
│░░░░░░░░░░░░░░░░░│  ├────┴─────┴─────────┤
│░░░░░░░░░░░░░░░░░│  │ 近     中      远   │
│░░░░░░░░░░░░░░░░░│  │ 高精度 中精度 低精度│
└─────────────────┘  └─────────────────────┘
 全场景同精度         分级精度（级联）

CSM 分级示例（3 级）：
           相机
            📷
             │
  ┌──────────┼──────────┐
  │ Cascade 0│          │ 近景（0-10m）
  │  (高精度)│          │ Shadow Map: 2048×2048
  └──────────┼──────────┘
     ┌───────┼───────┐
     │Cascade│1      │    中景（10-50m）
     │  (中精│)      │    Shadow Map: 1024×1024
     └───────┼───────┘
        ┌────┼────┐
        │Casc│ade2│       远景（50-200m）
        │(低精│度) │       Shadow Map: 512×512
        └────┼────┘
```

### 4.2 CSM 实现

```cpp
// 级联阴影贴图
class CascadedShadowMap {
public:
    static const int NUM_CASCADES = 4;
    
    struct Cascade {
        ShadowMap shadowMap;
        Mat4 viewProj;
        float splitDistance;  // 分割距离
        
        Cascade(int resolution) : shadowMap(resolution, resolution) {}
    };
    
    std::vector<Cascade> cascades;
    
    CascadedShadowMap() {
        // 初始化级联（分辨率递减）
        cascades.push_back(Cascade(2048));  // Cascade 0
        cascades.push_back(Cascade(1024));  // Cascade 1
        cascades.push_back(Cascade(512));   // Cascade 2
        cascades.push_back(Cascade(256));   // Cascade 3
    }
    
    // 计算级联分割距离
    void ComputeCascadeSplits(float nearPlane, float farPlane, 
                             float lambda = 0.5f) {
        // 混合均匀分割和对数分割
        for (int i = 0; i < NUM_CASCADES; i++) {
            float p = (i + 1) / (float)NUM_CASCADES;
            
            // 对数分割（近处密集）
            float log = nearPlane * pow(farPlane / nearPlane, p);
            
            // 均匀分割
            float uniform = nearPlane + (farPlane - nearPlane) * p;
            
            // 混合
            cascades[i].splitDistance = lambda * log + (1 - lambda) * uniform;
        }
    }
    
    // 渲染所有级联
    void RenderAllCascades(const std::vector<Triangle>& triangles,
                          const DirectionalLight& light,
                          const Mat4& cameraView,
                          const Mat4& cameraProj,
                          float nearPlane, float farPlane) {
        // 计算分割距离
        ComputeCascadeSplits(nearPlane, farPlane);
        
        // 渲染每个级联
        for (int i = 0; i < NUM_CASCADES; i++) {
            float near = (i == 0) ? nearPlane : cascades[i - 1].splitDistance;
            float far = cascades[i].splitDistance;
            
            // 计算这个级联的视锥体
            Mat4 cascadeProj = Mat4::Perspective(M_PI / 4, 16.0f / 9.0f, near, far);
            
            // 计算包围盒
            Mat4 cascadeViewProj = ComputeCascadeViewProj(light, cameraView, 
                                                         cascadeProj);
            cascades[i].viewProj = cascadeViewProj;
            
            // 渲染深度
            cascades[i].shadowMap.RenderFromLight(triangles, light);
        }
    }
    
    // 查询阴影（自动选择级联）
    float QueryShadow(Vec3 worldPosition, Vec3 cameraPosition) const {
        // 计算到相机的距离
        float distance = Length(worldPosition - cameraPosition);
        
        // 选择级联
        int cascadeIndex = 0;
        for (int i = 0; i < NUM_CASCADES; i++) {
            if (distance < cascades[i].splitDistance) {
                cascadeIndex = i;
                break;
            }
        }
        
        // 从选中的级联查询阴影
        return cascades[cascadeIndex].shadowMap.QueryShadow(worldPosition);
    }
    
private:
    Mat4 ComputeCascadeViewProj(const DirectionalLight& light,
                               const Mat4& cameraView,
                               const Mat4& cascadeProj) {
        // 计算相机视锥体的 8 个角点
        Vec3 corners[8];
        ComputeFrustumCorners(cameraView, cascadeProj, corners);
        
        // 计算包围球
        Vec3 center = {0, 0, 0};
        for (int i = 0; i < 8; i++) {
            center.x += corners[i].x;
            center.y += corners[i].y;
            center.z += corners[i].z;
        }
        center.x /= 8;
        center.y /= 8;
        center.z /= 8;
        
        float radius = 0;
        for (int i = 0; i < 8; i++) {
            float dist = Length(corners[i] - center);
            radius = std::max(radius, dist);
        }
        
        // 光源 View 矩阵
        Vec3 lightDir = Normalize(light.direction);
        Vec3 lightPos = center - lightDir * radius;
        Mat4 lightView = Mat4::LookAt(lightPos, center, {0, 1, 0});
        
        // 正交投影（紧密包围）
        Mat4 lightProj = Mat4::Orthographic(-radius, radius, -radius, radius, 
                                           0, radius * 2);
        
        return lightProj * lightView;
    }
    
    void ComputeFrustumCorners(const Mat4& view, const Mat4& proj, 
                              Vec3 corners[8]) {
        Mat4 invVP = Inverse(proj * view);
        
        // NDC 空间的 8 个角点
        Vec3 ndcCorners[8] = {
            {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
            {-1, -1,  1}, {1, -1,  1}, {1, 1,  1}, {-1, 1,  1}
        };
        
        // 转换到世界空间
        for (int i = 0; i < 8; i++) {
            Vec4 corner = invVP * Vec4{ndcCorners[i].x, ndcCorners[i].y, 
                                       ndcCorners[i].z, 1.0f};
            corners[i] = {corner.x / corner.w, corner.y / corner.w, 
                         corner.z / corner.w};
        }
    }
};
```

### 4.3 CSM 着色器（HLSL）

```hlsl
// 级联常量
#define NUM_CASCADES 4

cbuffer CascadeConstants : register(b2) {
    float4x4 cascadeViewProj[NUM_CASCADES];
    float cascadeSplits[NUM_CASCADES];
};

Texture2DArray cascadeShadowMaps : register(t1);  // 数组纹理
SamplerComparisonState shadowSampler : register(s1);

// 选择级联
int SelectCascade(float viewDepth) {
    for (int i = 0; i < NUM_CASCADES; i++) {
        if (viewDepth < cascadeSplits[i]) {
            return i;
        }
    }
    return NUM_CASCADES - 1;
}

// CSM 阴影计算
float ShadowCalculationCSM(float3 worldPos, float viewDepth) {
    // 1. 选择级联
    int cascadeIndex = SelectCascade(viewDepth);
    
    // 2. 转换到光源空间
    float4 lightSpacePos = mul(float4(worldPos, 1.0), 
                              cascadeViewProj[cascadeIndex]);
    float3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords.x = projCoords.x * 0.5 + 0.5;
    projCoords.y = -projCoords.y * 0.5 + 0.5;
    
    // 3. 采样级联阴影贴图
    float shadow = cascadeShadowMaps.SampleCmpLevelZero(
        shadowSampler,
        float3(projCoords.xy, cascadeIndex),  // 第三维是级联索引
        projCoords.z - 0.005
    );
    
    return shadow;
}

// 可视化级联（调试用）
float3 VisualizeCascades(float viewDepth) {
    int cascadeIndex = SelectCascade(viewDepth);
    
    float3 colors[NUM_CASCADES] = {
        float3(1, 0, 0),  // 红色
        float3(0, 1, 0),  // 绿色
        float3(0, 0, 1),  // 蓝色
        float3(1, 1, 0)   // 黄色
    };
    
    return colors[cascadeIndex];
}

// 像素着色器
float4 PSMain(VS_OUTPUT input) : SV_TARGET {
    // 计算视空间深度
    float viewDepth = length(cameraPosition - input.worldPos);
    
    // 光照
    float3 lighting = ComputeLighting(input.worldPos, input.normal);
    
    // CSM 阴影
    float shadow = ShadowCalculationCSM(input.worldPos, viewDepth);
    
    // 应用阴影
    float3 finalColor = lighting * shadow;
    
    // 调试：显示级联
    // finalColor = VisualizeCascades(viewDepth);
    
    return float4(finalColor, 1.0);
}
```

### 4.4 CSM 优化

```cpp
// 优化 1: 级联之间的混合（避免突变）
float QueryShadowCSM_Blended(const CascadedShadowMap& csm, 
                             Vec3 worldPosition, 
                             Vec3 cameraPosition) {
    float distance = Length(worldPosition - cameraPosition);
    
    // 找到当前级联
    int cascadeIndex = 0;
    for (int i = 0; i < CascadedShadowMap::NUM_CASCADES; i++) {
        if (distance < csm.cascades[i].splitDistance) {
            cascadeIndex = i;
            break;
        }
    }
    
    // 查询当前级联
    float shadow = csm.cascades[cascadeIndex].shadowMap.QueryShadow(worldPosition);
    
    // 如果接近分割边界，与下一级混合
    if (cascadeIndex < CascadedShadowMap::NUM_CASCADES - 1) {
        float splitDist = csm.cascades[cascadeIndex].splitDistance;
        float blendDist = splitDist * 0.1f;  // 10% 混合区域
        
        if (distance > splitDist - blendDist) {
            // 查询下一级
            float shadowNext = csm.cascades[cascadeIndex + 1].shadowMap
                .QueryShadow(worldPosition);
            
            // 混合因子
            float blend = (distance - (splitDist - blendDist)) / blendDist;
            shadow = Lerp(shadow, shadowNext, blend);
        }
    }
    
    return shadow;
}

// 优化 2: 稳定阴影（避免抖动）
// 将级联原点对齐到纹素边界
Vec3 SnapToTexelGrid(Vec3 worldPos, float texelSize) {
    return {
        floor(worldPos.x / texelSize) * texelSize,
        floor(worldPos.y / texelSize) * texelSize,
        floor(worldPos.z / texelSize) * texelSize
    };
}
```

---

## 5. 其他阴影技术

### 5.1 PCSS（百分比渐近软阴影）

```
PCSS: Percentage Closer Soft Shadows
目标：根据遮挡物距离动态调整阴影软硬

原理：
1. 光源大小（面光源）
2. 遮挡物距离
3. 接收器距离

       ╔═══════╗
       ║ 光源  ║ 大小 = w
       ╚═══╦═══╝
           ║   ╲
      d1   ▼    ╲
         ████     ╲
           │       ╲
      d2   │        ╲
           ▼         ▼
         ░░░░      ░░░░
         接收器    接收器
         近        远

半影宽度 = w * (d2 - d1) / d1

近处：小半影（硬阴影）
远处：大半影（软阴影）
```

```cpp
// PCSS 实现（简化）
float QueryShadowPCSS(const ShadowMap& shadowMap, Vec3 worldPosition,
                     float lightSize = 0.5f) {
    // ... 转换到光源空间（同前）
    
    // 1. 查找平均遮挡物深度（搜索阶段）
    float avgBlockerDepth = 0.0f;
    int numBlockers = 0;
    float searchRadius = lightSize * 0.1f;  // 搜索半径
    
    for (int y = -2; y <= 2; y++) {
        for (int x = -2; x <= 2; x++) {
            Vec2 offset = {x * texelSize, y * texelSize};
            float depth = SampleDepth(shadowMap, uv + offset);
            
            if (depth < currentDepth) {
                avgBlockerDepth += depth;
                numBlockers++;
            }
        }
    }
    
    if (numBlockers == 0) {
        return 1.0f;  // 无遮挡
    }
    
    avgBlockerDepth /= numBlockers;
    
    // 2. 计算半影宽度
    float penumbraWidth = (currentDepth - avgBlockerDepth) / avgBlockerDepth * lightSize;
    
    // 3. PCF（根据半影宽度调整过滤大小）
    int filterSize = (int)(penumbraWidth * 10) + 1;
    filterSize = std::clamp(filterSize, 1, 7);
    
    return QueryShadowPCF(shadowMap, worldPosition, filterSize);
}
```

### 5.2 VSM（方差阴影贴图）

```
VSM: Variance Shadow Maps
优势：可以使用硬件过滤（Mipmap、双线性）

存储：不存储深度，存储深度和深度平方
- Channel R: 深度 (z)
- Channel G: 深度平方 (z²)

查询：使用切比雪夫不等式估算阴影
P(x ≥ t) ≤ σ² / (σ² + (t - μ)²)

μ = E[z] = 平均深度
σ² = E[z²] - E[z]² = 方差
```

```cpp
// VSM 阴影贴图
struct VSMShadowMap {
    int width, height;
    Vec2* moments;  // (depth, depth²)
    
    void Render(const std::vector<Triangle>& triangles) {
        // 渲染时存储 (z, z²)
        for (/* 每个像素 */) {
            float depth = /* ... */;
            moments[index] = {depth, depth * depth};
        }
        
        // 生成 Mipmap（可选，用于软阴影）
        GenerateMipmaps();
    }
    
    float QueryShadow(Vec3 worldPosition) const {
        // 采样 moments
        Vec2 m = SampleMoments(worldPosition);
        float depth = m.x;
        float depth2 = m.y;
        
        // 计算方差
        float variance = depth2 - (depth * depth);
        variance = std::max(variance, 0.0001f);  // 避免负数
        
        // 当前深度
        float currentDepth = /* 转换到光源空间 */;
        
        // 切比雪夫上界
        float d = currentDepth - depth;
        float pMax = variance / (variance + d * d);
        
        // 如果在阴影内，返回概率
        return (currentDepth <= depth) ? 1.0f : pMax;
    }
};
```

### 5.3 技术对比总结

```
┌──────────────┬──────────┬──────────┬──────────┬──────────┐
│ 技术         │ 质量     │ 性能     │ 软阴影   │ 适用     │
├──────────────┼──────────┼──────────┼──────────┼──────────┤
│ Shadow Map   │ 中       │ 极高     │ ✗        │ 基础     │
│ PCF          │ 中+      │ 高       │ ✓        │ 常用     │
│ CSM          │ 高       │ 中       │ ✗        │ 大场景   │
│ PCSS         │ 高       │ 低       │ ✓✓       │ AAA 游戏 │
│ VSM          │ 中       │ 高       │ ✓        │ 移动端   │
│ Ray Tracing  │ 极高     │ 极低     │ ✓✓✓      │ RTX 显卡 │
└──────────────┴──────────┴──────────┴──────────┴──────────┘

推荐组合：
- 实时游戏：CSM + PCF 3×3
- 移动端：Shadow Map + PCF 2×2
- AAA 游戏：CSM + PCSS
- 离线渲染：Ray Tracing
```

---

## 📚 总结

### 核心要点

1. **Shadow Mapping**：基础技术，两次渲染
2. **PCF**：软化边缘，多次采样
3. **CSM**：大场景必备，分级精度
4. **PCSS**：真实软阴影，性能较低

### 常见问题解决

```
问题                  解决方法
─────────────────    ─────────────────────────
阴影痤疮              自适应 Bias
Peter Panning        前面剔除
边缘锯齿              PCF / 高分辨率
远处精度不足          CSM
硬边缘                PCF / PCSS
级联突变              级联混合
阴影抖动              稳定化 / 对齐纹素
```

### 下一步

**Part 4C** 将学习后处理效果（Bloom、HDR、抗锯齿等）

准备好继续了吗？🚀
