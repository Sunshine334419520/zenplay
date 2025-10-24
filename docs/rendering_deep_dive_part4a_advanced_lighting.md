# 渲染深度解析 Part 4A：高级光照技术

**文档目标**：掌握现代渲染中的光照模型与实现  
**前置知识**：Part 2（渲染管线）、Part 3（软件渲染）  
**阅读时间**：35-45 分钟

---

## 📚 目录

1. [光照基础理论](#1-光照基础理论)
2. [Phong 光照模型](#2-phong-光照模型)
3. [Blinn-Phong 改进](#3-blinn-phong-改进)
4. [基于物理的渲染（PBR）](#4-基于物理的渲染pbr)
5. [全局光照基础](#5-全局光照基础)

---

## 1. 光照基础理论

### 1.1 光的物理本质

```
光的传播：
┌─────────────────────────────────────────┐
│  光源 → 直接光 → 表面 → 反射光 → 相机   │
│   ☀️     ╲           │         ╱  📷    │
│          ╲          │        ╱          │
│           ╲    漫反射+镜面反射           │
│            ╲        │      ╱            │
│             ●───────●─────●             │
│            表面法线(N)                   │
└─────────────────────────────────────────┘

关键矢量：
- L (Light)：表面点 → 光源方向
- N (Normal)：表面法线
- V (View)：表面点 → 相机方向
- R (Reflect)：光线的镜面反射方向
- H (Half)：L 和 V 的半角向量
```

### 1.2 光照分量

```cpp
// 光照由三部分组成
struct LightingResult {
    Color ambient;   // 环境光（无方向，均匀照明）
    Color diffuse;   // 漫反射（与角度相关，表面基本颜色）
    Color specular;  // 镜面反射（高光，反射光泽）
};

// 最终颜色
Color finalColor = ambient + diffuse + specular;

// 视觉效果：
//
// 纯环境光：         加漫反射：         加镜面反射：
// ┌─────────┐       ┌─────────┐       ┌─────────┐
// │░░░░░░░░░│       │░░░▒▓▒░░░│       │░░░▒█▒░░░│
// │░░░░░░░░░│       │░░▒▓▓▓▒░░│       │░░▒▓█▓▒░░│
// │░░░░░░░░░│  →    │░▒▓▓▓▓▓▒░│  →    │░▒▓███▓▒░│  ← 高光
// │░░░░░░░░░│       │░░▒▓▓▓▒░░│       │░░▒▓█▓▒░░│
// └─────────┘       └─────────┘       └─────────┘
//   平坦             有立体感           真实光泽
```

### 1.3 光源类型

```cpp
// 光源基类
struct Light {
    Color color;
    float intensity;
    
    virtual Vec3 GetDirection(Vec3 surfacePos) const = 0;
    virtual float GetAttenuation(Vec3 surfacePos) const = 0;
};

// 1. 方向光（Directional Light）
struct DirectionalLight : Light {
    Vec3 direction;  // 光照方向（指向光源）
    
    Vec3 GetDirection(Vec3 surfacePos) const override {
        return direction;  // 所有点方向相同（模拟太阳）
    }
    
    float GetAttenuation(Vec3 surfacePos) const override {
        return 1.0f;  // 无衰减
    }
};

// 2. 点光源（Point Light）
struct PointLight : Light {
    Vec3 position;
    float range;  // 影响范围
    
    Vec3 GetDirection(Vec3 surfacePos) const override {
        return Normalize(position - surfacePos);
    }
    
    float GetAttenuation(Vec3 surfacePos) const override {
        float distance = Length(position - surfacePos);
        
        // 平方衰减（物理正确）
        float attenuation = 1.0f / (distance * distance);
        
        // 范围限制
        float fade = 1.0f - Clamp(distance / range, 0.0f, 1.0f);
        return attenuation * fade * fade;
    }
};

// 3. 聚光灯（Spot Light）
struct SpotLight : Light {
    Vec3 position;
    Vec3 direction;   // 聚光方向
    float innerCone;  // 内锥角（全亮）
    float outerCone;  // 外锥角（渐暗）
    float range;
    
    Vec3 GetDirection(Vec3 surfacePos) const override {
        return Normalize(position - surfacePos);
    }
    
    float GetAttenuation(Vec3 surfacePos) const override {
        Vec3 lightDir = GetDirection(surfacePos);
        float distance = Length(position - surfacePos);
        
        // 距离衰减
        float distAttenuation = 1.0f / (distance * distance);
        float fade = 1.0f - Clamp(distance / range, 0.0f, 1.0f);
        distAttenuation *= fade * fade;
        
        // 聚光衰减
        float theta = Dot(-lightDir, direction);
        float epsilon = cos(innerCone) - cos(outerCone);
        float spotAttenuation = Clamp((theta - cos(outerCone)) / epsilon, 
                                     0.0f, 1.0f);
        
        return distAttenuation * spotAttenuation;
    }
};

// 视觉对比：
//
// 方向光（太阳）:     点光源（灯泡）:    聚光灯（手电筒）:
// ════════════        ↙  ↓  ↘            ↘ ↓ ↙
// ════════════         ←  ●  →              ╲│╱
// ════════════        ↖  ↑  ↗               ▼
// 平行射线             放射状              锥形光束
```

---

## 2. Phong 光照模型

### 2.1 Phong 模型原理

```
Phong 光照 = 环境光 + 漫反射 + 镜面反射

公式：
Color = Ka * Ia                           ← 环境光
      + Kd * Id * max(N·L, 0)            ← 漫反射（Lambert）
      + Ks * Is * max(R·V, 0)^shininess  ← 镜面反射

参数：
- Ka, Kd, Ks：材质的环境、漫反射、镜面反射系数
- Ia, Id, Is：光源的环境、漫反射、镜面强度
- N：表面法线
- L：光源方向
- R：反射方向 = 2(N·L)N - L
- V：视线方向
- shininess：高光指数（越大越聚焦）

几何示意：
       N (法线)
       │
       │  R (反射)
       │ ╱
       │╱ θr
  ─────●─────  表面
      ╱│
   θi╱ │
    ╱  │
   L   │
(光源) │

反射定律：入射角 = 反射角（θi = θr）
```

### 2.2 Phong 实现（C++）

```cpp
// Phong 材质
struct PhongMaterial {
    Color ambient;      // 环境光颜色
    Color diffuse;      // 漫反射颜色
    Color specular;     // 镜面反射颜色
    float shininess;    // 高光指数 [1, 128]
};

// Phong 光照计算
Color ComputePhongLighting(
    Vec3 position,           // 着色点位置
    Vec3 normal,             // 表面法线
    Vec3 viewDir,            // 视线方向
    const PhongMaterial& mat,
    const Light* light,
    Color ambientLight       // 环境光
) {
    // 1. 环境光
    Color ambient = mat.ambient * ambientLight;
    
    // 2. 漫反射
    Vec3 lightDir = light->GetDirection(position);
    float NdotL = std::max(Dot(normal, lightDir), 0.0f);
    Color diffuse = mat.diffuse * light->color * NdotL * light->intensity;
    
    // 3. 镜面反射
    Vec3 reflectDir = Reflect(-lightDir, normal);  // R = 2(N·L)N - L
    float RdotV = std::max(Dot(reflectDir, viewDir), 0.0f);
    float specularFactor = pow(RdotV, mat.shininess);
    Color specular = mat.specular * light->color * specularFactor * light->intensity;
    
    // 4. 衰减
    float attenuation = light->GetAttenuation(position);
    
    // 5. 合成
    return ambient + (diffuse + specular) * attenuation;
}

// 反射向量计算
Vec3 Reflect(Vec3 incident, Vec3 normal) {
    // R = I - 2(N·I)N
    return incident - normal * (2.0f * Dot(incident, normal));
}

// 使用示例
int main() {
    // 材质
    PhongMaterial gold = {
        {0.24725f, 0.1995f, 0.0745f},   // 环境光
        {0.75164f, 0.60648f, 0.22648f}, // 漫反射（金色）
        {0.628281f, 0.555802f, 0.366065f}, // 镜面反射
        51.2f  // 高光指数
    };
    
    // 光源
    PointLight light = {
        {1.0f, 1.0f, 1.0f}, // 白色
        1.0f,               // 强度
        {5.0f, 5.0f, 5.0f}, // 位置
        10.0f               // 范围
    };
    
    // 着色点
    Vec3 position = {0, 0, 0};
    Vec3 normal = {0, 1, 0};
    Vec3 viewDir = {0, 0, 1};
    
    // 计算光照
    Color result = ComputePhongLighting(position, normal, viewDir, 
                                       gold, &light, {0.1f, 0.1f, 0.1f});
    
    return 0;
}
```

### 2.3 Phong 着色器（HLSL）

```hlsl
// 顶点着色器输出
struct VS_OUTPUT {
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float2 uv       : TEXCOORD2;
};

// 常量缓冲区
cbuffer MaterialConstants : register(b0) {
    float3 ambientColor;
    float3 diffuseColor;
    float3 specularColor;
    float shininess;
};

cbuffer LightConstants : register(b1) {
    float3 lightPosition;
    float3 lightColor;
    float lightIntensity;
    float3 cameraPosition;
};

// 像素着色器
float4 PSMain(VS_OUTPUT input) : SV_TARGET {
    // 归一化
    float3 N = normalize(input.normal);
    float3 L = normalize(lightPosition - input.worldPos);
    float3 V = normalize(cameraPosition - input.worldPos);
    
    // 1. 环境光
    float3 ambient = ambientColor * 0.1;
    
    // 2. 漫反射
    float NdotL = max(dot(N, L), 0.0);
    float3 diffuse = diffuseColor * lightColor * NdotL * lightIntensity;
    
    // 3. 镜面反射
    float3 R = reflect(-L, N);
    float RdotV = max(dot(R, V), 0.0);
    float specularFactor = pow(RdotV, shininess);
    float3 specular = specularColor * lightColor * specularFactor * lightIntensity;
    
    // 4. 距离衰减
    float distance = length(lightPosition - input.worldPos);
    float attenuation = 1.0 / (distance * distance);
    
    // 5. 合成
    float3 finalColor = ambient + (diffuse + specular) * attenuation;
    
    return float4(finalColor, 1.0);
}
```

### 2.4 Phong 参数调整

```cpp
// 不同材质的 Phong 参数

// 塑料（Plastic）
PhongMaterial plastic = {
    {0.05f, 0.05f, 0.05f},    // 环境光
    {0.5f, 0.5f, 0.5f},       // 漫反射
    {0.7f, 0.7f, 0.7f},       // 镜面反射（强）
    32.0f  // 中等高光
};

// 金属（Metal）
PhongMaterial metal = {
    {0.2f, 0.2f, 0.2f},
    {0.3f, 0.3f, 0.3f},
    {1.0f, 1.0f, 1.0f},       // 强镜面反射
    128.0f  // 锐利高光
};

// 木头（Wood）
PhongMaterial wood = {
    {0.1f, 0.05f, 0.02f},
    {0.5f, 0.3f, 0.1f},       // 棕色
    {0.1f, 0.1f, 0.1f},       // 弱镜面反射
    8.0f  // 宽泛高光
};

// 橡胶（Rubber）
PhongMaterial rubber = {
    {0.05f, 0.0f, 0.0f},
    {0.5f, 0.1f, 0.1f},       // 红色
    {0.05f, 0.05f, 0.05f},    // 极弱镜面反射
    4.0f  // 几乎无高光
};

// Shininess 效果对比：
//
// Shininess = 4:      Shininess = 32:     Shininess = 128:
// ┌───────────┐       ┌───────────┐       ┌───────────┐
// │    ███    │       │     ▓     │       │     ▪     │
// │  ███████  │       │    ███    │       │    ███    │
// │ █████████ │       │   █████   │       │   █████   │
// │  ███████  │       │    ███    │       │    ███    │
// │    ███    │       │     ▓     │       │     ▪     │
// └───────────┘       └───────────┘       └───────────┘
//  宽泛柔和           中等聚焦            锐利小点
```

---

## 3. Blinn-Phong 改进

### 3.1 Blinn-Phong 原理

```
问题：Phong 模型在 V 和 R 夹角 > 90° 时会出现问题

Phong:
       N
       │    V (视线)
       │   ╱
       │  ╱ >90°
       │ ╱
  ─────●─────
      ╱│
     ╱ │
    R  │
(反射) │

当 V 和 R 夹角过大时，pow(负数, shininess) 会有问题

Blinn-Phong 改进：
使用半角向量 H = normalize(L + V)

       N
       │ H (半角)
       │╱
       ●─────
      ╱
     ╱
    L

优势：
1. H 和 N 夹角总是 < 90°
2. 计算更快（H 可以预计算）
3. 视觉效果更准确

公式：
Specular = Ks * Is * max(N·H, 0)^shininess

对比：
- Phong: max(R·V, 0)^shininess
- Blinn-Phong: max(N·H, 0)^shininess
```

### 3.2 Blinn-Phong 实现

```cpp
// Blinn-Phong 光照计算
Color ComputeBlinnPhongLighting(
    Vec3 position,
    Vec3 normal,
    Vec3 viewDir,
    const PhongMaterial& mat,
    const Light* light,
    Color ambientLight
) {
    // 1. 环境光（同 Phong）
    Color ambient = mat.ambient * ambientLight;
    
    // 2. 漫反射（同 Phong）
    Vec3 lightDir = light->GetDirection(position);
    float NdotL = std::max(Dot(normal, lightDir), 0.0f);
    Color diffuse = mat.diffuse * light->color * NdotL * light->intensity;
    
    // 3. 镜面反射（改进）
    Vec3 halfDir = Normalize(lightDir + viewDir);  // H = (L + V) / |L + V|
    float NdotH = std::max(Dot(normal, halfDir), 0.0f);
    float specularFactor = pow(NdotH, mat.shininess);
    Color specular = mat.specular * light->color * specularFactor * light->intensity;
    
    // 4. 衰减
    float attenuation = light->GetAttenuation(position);
    
    // 5. 合成
    return ambient + (diffuse + specular) * attenuation;
}

// 半角向量计算
Vec3 ComputeHalfVector(Vec3 lightDir, Vec3 viewDir) {
    return Normalize(lightDir + viewDir);
}
```

### 3.3 Blinn-Phong 着色器（HLSL）

```hlsl
// 像素着色器
float4 PSMain(VS_OUTPUT input) : SV_TARGET {
    float3 N = normalize(input.normal);
    float3 L = normalize(lightPosition - input.worldPos);
    float3 V = normalize(cameraPosition - input.worldPos);
    
    // Blinn-Phong 改进：使用半角向量
    float3 H = normalize(L + V);
    
    // 环境光
    float3 ambient = ambientColor * 0.1;
    
    // 漫反射
    float NdotL = max(dot(N, L), 0.0);
    float3 diffuse = diffuseColor * lightColor * NdotL;
    
    // 镜面反射（改进）
    float NdotH = max(dot(N, H), 0.0);
    float specularFactor = pow(NdotH, shininess);
    float3 specular = specularColor * lightColor * specularFactor;
    
    // 衰减
    float distance = length(lightPosition - input.worldPos);
    float attenuation = 1.0 / (distance * distance);
    
    // 合成
    float3 finalColor = ambient + (diffuse + specular) * attenuation;
    
    return float4(finalColor, 1.0);
}
```

### 3.4 Phong vs Blinn-Phong 对比

```cpp
// 性能对比
Benchmark Result (1920×1080, 1000 三角形):
┌──────────────────┬──────────┬────────────┐
│ 模型             │ 时间(ms) │ 说明       │
├──────────────────┼──────────┼────────────┤
│ Phong            │   2.3    │ Reflect 慢 │
│ Blinn-Phong      │   1.8    │ 快 22%     │
└──────────────────┴──────────┴────────────┘

// 视觉对比
场景：金属球，掠射角观察

Phong:                  Blinn-Phong:
┌─────────────┐         ┌─────────────┐
│      ●      │         │      ●      │
│    ▄███▄    │         │    ▄███▄    │
│  ▄███████▄  │         │  ▄███████▄  │
│ ███████████ │         │ ███████████ │
│  ▀███████▀  │         │  ▀███████▀  │
│    ▀███▀    │ ← 边缘 │    ▀███▀    │ ← 边缘
│      █ ✗   │   异常   │      ▀      │   正常
└─────────────┘         └─────────────┘
 边缘高光异常            边缘平滑
```

---

## 4. 基于物理的渲染（PBR）

### 4.1 PBR 基础理论

```
PBR (Physically Based Rendering) 目标：
物理准确 + 直观参数 + 能量守恒

核心概念：
1. 能量守恒：反射光 ≤ 入射光
2. 微表面理论：表面由微小镜面组成
3. 菲涅尔效应：掠射角反射增强

PBR 材质参数：
- Albedo（反照率）：基础颜色（替代 diffuse）
- Metallic（金属度）：[0, 1]，0=绝缘体，1=金属
- Roughness（粗糙度）：[0, 1]，0=光滑，1=粗糙
- AO（环境遮蔽）：[0, 1]，模拟缝隙阴影

微表面模型：
宏观表面（平滑）：    微观表面（粗糙）：
═══════════════        ╱╲╱╲╱╲╱╲╱╲╱╲
平面反射              每个微面片独立反射

粗糙度影响：
Roughness = 0:      Roughness = 0.5:    Roughness = 1:
     │                  ╱│╲               ╱│╲
     │                 ╱ │ ╲             ╱ │ ╲
═════●═════         ════●════         ════●════
     │               ╱  │  ╲           ╱ ╱│╲ ╲
  镜面反射          部分散射          完全散射
```

### 4.2 PBR 核心方程

```cpp
// Cook-Torrance BRDF
// f(l, v) = (D * F * G) / (4 * (n·l) * (n·v))
//
// D: 法线分布函数（Normal Distribution Function）
// F: 菲涅尔方程（Fresnel Equation）
// G: 几何遮蔽函数（Geometry Function）

// 1. 法线分布函数（GGX / Trowbridge-Reitz）
float DistributionGGX(Vec3 N, Vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = std::max(Dot(N, H), 0.0f);
    float NdotH2 = NdotH * NdotH;
    
    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0f) + 1.0f);
    denom = M_PI * denom * denom;
    
    return nom / denom;
}

// 2. 几何遮蔽函数（Smith）
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0f);
    float k = (r * r) / 8.0f;
    
    float nom = NdotV;
    float denom = NdotV * (1.0f - k) + k;
    
    return nom / denom;
}

float GeometrySmith(Vec3 N, Vec3 V, Vec3 L, float roughness) {
    float NdotV = std::max(Dot(N, V), 0.0f);
    float NdotL = std::max(Dot(N, L), 0.0f);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    
    return ggx1 * ggx2;
}

// 3. 菲涅尔方程（Schlick 近似）
Vec3 FresnelSchlick(float cosTheta, Vec3 F0) {
    return F0 + (Vec3{1.0f, 1.0f, 1.0f} - F0) * 
           pow(1.0f - cosTheta, 5.0f);
}

// F0: 0° 入射时的反射率
// - 非金属（塑料）：F0 = 0.04（约 4%）
// - 金属（铁）：F0 = 0.56
// - 金属（金）：F0 = (1.0, 0.71, 0.29)

// 菲涅尔效应示例：
//
// 垂直观察（cosθ=1）:  掠射角（cosθ≈0）:
// ┌─────────┐          ┌─────────┐
// │    │    │          │    ╱    │
// │    ▼    │          │   ╱     │
// ╞════●════╡          ╞══●──────╡
// │  反射4% │          │反射100% │
// └─────────┘          └─────────┘
//  弱反射               强反射（如水面）
```

### 4.3 完整 PBR 实现

```cpp
// PBR 材质
struct PBRMaterial {
    Color albedo;        // 基础颜色
    float metallic;      // 金属度 [0, 1]
    float roughness;     // 粗糙度 [0, 1]
    float ao;            // 环境遮蔽 [0, 1]
};

// PBR 光照计算
Color ComputePBRLighting(
    Vec3 position,
    Vec3 normal,
    Vec3 viewDir,
    const PBRMaterial& mat,
    const Light* light
) {
    Vec3 N = Normalize(normal);
    Vec3 V = Normalize(viewDir);
    Vec3 L = light->GetDirection(position);
    Vec3 H = Normalize(L + V);
    
    // 计算反射率
    Vec3 F0 = {0.04f, 0.04f, 0.04f};  // 非金属基础反射率
    Vec3 albedo = {mat.albedo.r / 255.0f, 
                   mat.albedo.g / 255.0f, 
                   mat.albedo.b / 255.0f};
    
    // 金属材质使用 albedo 作为 F0
    F0 = Lerp(F0, albedo, mat.metallic);
    
    // Cook-Torrance BRDF
    float D = DistributionGGX(N, H, mat.roughness);
    float G = GeometrySmith(N, V, L, mat.roughness);
    Vec3 F = FresnelSchlick(std::max(Dot(H, V), 0.0f), F0);
    
    Vec3 numerator = {D * G * F.x, D * G * F.y, D * G * F.z};
    float NdotL = std::max(Dot(N, L), 0.0f);
    float NdotV = std::max(Dot(N, V), 0.0f);
    float denominator = 4.0f * NdotV * NdotL + 0.0001f;  // 防止除零
    Vec3 specular = {numerator.x / denominator, 
                     numerator.y / denominator, 
                     numerator.z / denominator};
    
    // 能量守恒：漫反射 + 镜面反射 = 1
    Vec3 kS = F;  // 镜面反射比例
    Vec3 kD = {1.0f - kS.x, 1.0f - kS.y, 1.0f - kS.z};
    kD = {kD.x * (1.0f - mat.metallic),  // 金属无漫反射
          kD.y * (1.0f - mat.metallic),
          kD.z * (1.0f - mat.metallic)};
    
    // 漫反射项
    Vec3 diffuse = {kD.x * albedo.x, kD.y * albedo.y, kD.z * albedo.z};
    diffuse = {diffuse.x / M_PI, diffuse.y / M_PI, diffuse.z / M_PI};
    
    // 光源辐射度
    float attenuation = light->GetAttenuation(position);
    Vec3 radiance = {light->color.r / 255.0f * light->intensity * attenuation,
                     light->color.g / 255.0f * light->intensity * attenuation,
                     light->color.b / 255.0f * light->intensity * attenuation};
    
    // 反射方程
    Vec3 Lo = {(diffuse.x + specular.x) * radiance.x * NdotL,
               (diffuse.y + specular.y) * radiance.y * NdotL,
               (diffuse.z + specular.z) * radiance.z * NdotL};
    
    // 环境光（简化）
    Vec3 ambient = {albedo.x * 0.03f * mat.ao,
                    albedo.y * 0.03f * mat.ao,
                    albedo.z * 0.03f * mat.ao};
    
    Vec3 color = {ambient.x + Lo.x, ambient.y + Lo.y, ambient.z + Lo.z};
    
    return {
        (uint8_t)(std::min(color.x, 1.0f) * 255),
        (uint8_t)(std::min(color.y, 1.0f) * 255),
        (uint8_t)(std::min(color.z, 1.0f) * 255),
        255
    };
}
```

### 4.4 PBR 着色器（HLSL）

```hlsl
// PBR 材质常量
cbuffer MaterialConstants : register(b0) {
    float3 albedo;
    float metallic;
    float roughness;
    float ao;
};

cbuffer LightConstants : register(b1) {
    float3 lightPositions[4];
    float3 lightColors[4];
    float3 cameraPosition;
};

// 法线分布函数
float DistributionGGX(float3 N, float3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = 3.14159265 * denom * denom;
    
    return num / denom;
}

// 几何遮蔽函数
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    
    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    
    return num / denom;
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    
    return ggx1 * ggx2;
}

// 菲涅尔方程
float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

// 像素着色器
float4 PSMain(VS_OUTPUT input) : SV_TARGET {
    float3 N = normalize(input.normal);
    float3 V = normalize(cameraPosition - input.worldPos);
    
    // 计算 F0
    float3 F0 = float3(0.04, 0.04, 0.04);
    F0 = lerp(F0, albedo, metallic);
    
    // 反射方程
    float3 Lo = float3(0.0, 0.0, 0.0);
    
    // 遍历所有光源
    for (int i = 0; i < 4; i++) {
        float3 L = normalize(lightPositions[i] - input.worldPos);
        float3 H = normalize(V + L);
        float distance = length(lightPositions[i] - input.worldPos);
        float attenuation = 1.0 / (distance * distance);
        float3 radiance = lightColors[i] * attenuation;
        
        // Cook-Torrance BRDF
        float D = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(N, V, L, roughness);
        float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
        
        float3 numerator = D * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
        float3 specular = numerator / denominator;
        
        // 能量守恒
        float3 kS = F;
        float3 kD = float3(1.0, 1.0, 1.0) - kS;
        kD *= 1.0 - metallic;
        
        float NdotL = max(dot(N, L), 0.0);
        Lo += (kD * albedo / 3.14159265 + specular) * radiance * NdotL;
    }
    
    // 环境光
    float3 ambient = float3(0.03, 0.03, 0.03) * albedo * ao;
    float3 color = ambient + Lo;
    
    // HDR 色调映射
    color = color / (color + float3(1.0, 1.0, 1.0));
    // Gamma 校正
    color = pow(color, float3(1.0/2.2, 1.0/2.2, 1.0/2.2));
    
    return float4(color, 1.0);
}
```

### 4.5 PBR 参数示例

```cpp
// 常见材质的 PBR 参数

// 黄金（Gold）
PBRMaterial gold = {
    {255, 198, 99},  // Albedo（金色）
    1.0f,            // Metallic（完全金属）
    0.3f,            // Roughness（略粗糙）
    1.0f             // AO
};

// 铁（Iron）
PBRMaterial iron = {
    {196, 196, 196}, // Albedo（灰色）
    1.0f,            // Metallic
    0.5f,            // Roughness
    1.0f
};

// 塑料（Plastic）
PBRMaterial plastic = {
    {255, 0, 0},     // Albedo（红色）
    0.0f,            // Metallic（非金属）
    0.4f,            // Roughness
    1.0f
};

// 橡胶（Rubber）
PBRMaterial rubber = {
    {50, 50, 50},    // Albedo（深灰）
    0.0f,            // Metallic
    0.9f,            // Roughness（高粗糙度）
    1.0f
};

// 粗糙度对比：
//
// Roughness = 0.0:    Roughness = 0.5:    Roughness = 1.0:
// ┌───────────┐       ┌───────────┐       ┌───────────┐
// │    ●○○    │       │   ●●○○    │       │  ●●●●●    │
// │   ●●●○    │       │  ●●●●○    │       │ ●●●●●●●   │
// │  ●●●●●    │       │ ●●●●●●    │       │●●●●●●●●●  │
// │   ●●●○    │       │  ●●●●○    │       │ ●●●●●●●   │
// │    ●○○    │       │   ●●○○    │       │  ●●●●●    │
// └───────────┘       └───────────┘       └───────────┘
//  镜面反射           半光泽              漫反射
//
// 金属度对比：
//
// Metallic = 0.0:     Metallic = 0.5:     Metallic = 1.0:
// (塑料，有颜色)      (混合)               (金属，着色光)
```

---

## 5. 全局光照基础

### 5.1 渲染方程

```
全局光照（Global Illumination）：
不仅计算直接光，还计算间接光（反弹）

渲染方程（The Rendering Equation）：
Lo(p, ωo) = Le(p, ωo) + ∫Ω fr(p, ωi, ωo) Li(p, ωi) (n·ωi) dωi

符号说明：
- Lo: 出射辐射度（渲染结果）
- Le: 自发光
- fr: BRDF（材质反射函数）
- Li: 入射辐射度（递归）
- Ω: 半球
- ωi, ωo: 入射、出射方向
- n: 法线

直接光 vs 全局光：
┌─────────────────────────────────────┐
│  直接光：                            │
│  光源 ──→ 表面 ──→ 相机             │
│  ☀️      ●      📷                 │
│                                     │
│  全局光：                            │
│  光源 ──→ 表面A ──→ 表面B ──→ 相机  │
│  ☀️      ●───→●───→📷              │
│          ↓反弹  ↓反弹                │
│          ●      ●                   │
│       间接照亮 间接照亮              │
└─────────────────────────────────────┘

视觉效果：
直接光：              全局光：
┌─────────┐          ┌─────────┐
│ ▓▓▓▓▓   │          │ ▓▓▓▓▓   │
│ ▓▓▓▓░░  │          │ ▓▓▓▓▒▒  │ ← 墙壁反射
│ ▓▓▓░░░  │          │ ▓▓▓▒▒▒  │   照亮阴影
│ ░░░░░░  │          │ ▒▒▒▒▒▒  │
└─────────┘          └─────────┘
硬阴影，不真实        柔和阴影，真实
```

### 5.2 环境贴图（Environment Map）

```cpp
// Cubemap 环境贴图
struct CubemapTexture {
    Texture* faces[6];  // +X, -X, +Y, -Y, +Z, -Z
    
    // 采样方向向量
    Color Sample(Vec3 direction) const {
        Vec3 absDir = {fabs(direction.x), fabs(direction.y), fabs(direction.z)};
        int faceIndex;
        Vec2 uv;
        
        // 确定面
        if (absDir.x >= absDir.y && absDir.x >= absDir.z) {
            faceIndex = (direction.x > 0) ? 0 : 1;  // +X / -X
            uv = {-direction.z / absDir.x, -direction.y / absDir.x};
        } else if (absDir.y >= absDir.z) {
            faceIndex = (direction.y > 0) ? 2 : 3;  // +Y / -Y
            uv = {direction.x / absDir.y, -direction.z / absDir.y};
        } else {
            faceIndex = (direction.z > 0) ? 4 : 5;  // +Z / -Z
            uv = {direction.x / absDir.z, -direction.y / absDir.z};
        }
        
        // 转换到 [0, 1]
        uv.x = (uv.x + 1.0f) * 0.5f;
        uv.y = (uv.y + 1.0f) * 0.5f;
        
        return faces[faceIndex]->Sample(uv);
    }
};

// 使用环境贴图作为光源
Color ComputeEnvironmentLighting(Vec3 normal, Vec3 viewDir, 
                                const PBRMaterial& mat,
                                const CubemapTexture& envMap) {
    // 反射方向
    Vec3 R = Reflect(-viewDir, normal);
    
    // 从环境贴图采样
    Color envColor = envMap.Sample(R);
    
    // 根据粗糙度调整（简化）
    float mipLevel = mat.roughness * 5.0f;  // 假设 5 级 Mipmap
    // 实际需要在不同 Mipmap 级别采样
    
    return envColor;
}
```

### 5.3 环境光遮蔽（AO）

```cpp
// 屏幕空间环境光遮蔽（SSAO）- 简化版
float ComputeSSAO(Vec3 position, Vec3 normal, 
                 const FramebufferWithDepth& fb,
                 const Mat4& view, const Mat4& proj) {
    const int numSamples = 16;
    const float radius = 0.5f;
    float occlusion = 0.0f;
    
    // 生成随机采样点（半球）
    Vec3 samples[numSamples];
    // ... 初始化 samples（半球内随机点）
    
    // 对每个采样点测试遮蔽
    for (int i = 0; i < numSamples; i++) {
        // 采样点世界坐标
        Vec3 samplePos = position + samples[i] * radius;
        
        // 转换到屏幕空间
        Vec4 clipPos = proj * (view * Vec4{samplePos.x, samplePos.y, samplePos.z, 1.0f});
        Vec3 ndcPos = {clipPos.x / clipPos.w, clipPos.y / clipPos.w, clipPos.z / clipPos.w};
        Vec2 screenPos = {
            (ndcPos.x + 1.0f) * 0.5f * fb.width,
            (1.0f - ndcPos.y) * 0.5f * fb.height
        };
        
        // 读取深度缓冲
        int x = (int)screenPos.x;
        int y = (int)screenPos.y;
        if (x >= 0 && x < fb.width && y >= 0 && y < fb.height) {
            float bufferDepth = fb.depthBuffer[y * fb.width + x];
            
            // 如果采样点被遮挡
            if (ndcPos.z > bufferDepth) {
                occlusion += 1.0f;
            }
        }
    }
    
    // 归一化
    occlusion = 1.0f - (occlusion / numSamples);
    return occlusion;
}

// AO 效果：
//
// 无 AO:              有 AO:
// ┌───────┐          ┌───────┐
// │ ▓▓▓▓▓ │          │ ▓▓▓▓▓ │
// │ ▓▓▓▓▓ │          │ ▓▓▓▓▒ │ ← 边缘阴影
// │ ▓▓▓▓▓ │          │ ▓▓▓▒░ │
// │       │          │  ░░░  │ ← 缝隙暗
// └───────┘          └───────┘
//  平坦                深度感强
```

---

## 📚 总结

### 光照模型对比

```
┌──────────────┬────────────┬────────────┬────────────┐
│ 模型         │ 计算复杂度 │ 视觉质量   │ 适用场景   │
├──────────────┼────────────┼────────────┼────────────┤
│ Phong        │ 低         │ 中         │ 实时游戏   │
│ Blinn-Phong  │ 低         │ 中+        │ 实时游戏   │
│ PBR          │ 中         │ 高         │ AAA 游戏   │
│ 全局光照     │ 高         │ 极高       │ 离线渲染   │
└──────────────┴────────────┴────────────┴────────────┘

性能对比（1920×1080，1000 三角形）：
- Phong: 2.3 ms
- Blinn-Phong: 1.8 ms
- PBR: 3.5 ms
- PBR + IBL: 5.0 ms
```

### 核心要点

1. **Phong/Blinn-Phong**：快速、直观，适合实时
2. **PBR**：物理准确、参数直观、现代标准
3. **全局光照**：最真实，但最慢

### 下一步

**Part 4B** 将学习阴影技术（Shadow Mapping、PCF、CSM 等）

准备好继续了吗？🚀
