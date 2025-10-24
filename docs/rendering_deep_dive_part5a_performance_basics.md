# 渲染深度解析 Part 5A：性能优化基础

**文档目标**：掌握渲染性能分析与优化的基础知识  
**前置知识**：Part 2（渲染管线）、Part 3（软件渲染）  
**阅读时间**：35-45 分钟

---

## 📚 目录

1. [性能分析基础](#1-性能分析基础)
2. [CPU 性能优化](#2-cpu-性能优化)
3. [GPU 性能优化](#3-gpu-性能优化)
4. [内存优化](#4-内存优化)
5. [性能测量工具](#5-性能测量工具)

---

## 1. 性能分析基础

### 1.1 性能目标

```
帧率目标：
┌────────────────┬──────────┬──────────┬────────────┐
│ 目标 FPS       │ 帧时间   │ 应用场景 │ 难度       │
├────────────────┼──────────┼──────────┼────────────┤
│ 30 FPS         │ 33.3 ms  │ 主机游戏 │ 低         │
│ 60 FPS         │ 16.7 ms  │ PC 游戏  │ 中         │
│ 90 FPS         │ 11.1 ms  │ VR       │ 高         │
│ 120 FPS        │  8.3 ms  │ 竞技游戏 │ 极高       │
│ 144 FPS        │  6.9 ms  │ 电竞     │ 极高       │
└────────────────┴──────────┴──────────┴────────────┘

帧时间预算（60 FPS = 16.7ms）：
┌────────────────────────────────────┐
│ CPU: 10 ms                         │
│ ├─ 游戏逻辑: 3 ms                  │
│ ├─ 物理模拟: 2 ms                  │
│ ├─ 渲染准备: 3 ms                  │
│ └─ 其他: 2 ms                      │
│                                    │
│ GPU: 15 ms                         │
│ ├─ 几何处理: 2 ms                  │
│ ├─ 光栅化: 3 ms                    │
│ ├─ 像素着色: 8 ms                  │
│ └─ 后处理: 2 ms                    │
│                                    │
│ 空闲: 1.7 ms                       │
└────────────────────────────────────┘

帧率 vs 体验：
30 FPS:  ████░░░░░░░░░░░░  可玩
60 FPS:  ████████░░░░░░░░  流畅
90 FPS:  ████████████░░░░  非常流畅（VR 最低）
120 FPS: ████████████████  极致体验
```

### 1.2 性能瓶颈类型

```cpp
// 性能瓶颈分类
enum class PerformanceBottleneck {
    CPUBound,        // CPU 限制
    GPUBound,        // GPU 限制
    MemoryBound,     // 内存带宽限制
    DrawCallBound,   // Draw Call 过多
    FillRateBound,   // 填充率限制
    VertexBound,     // 顶点处理限制
    PixelBound       // 像素着色限制
};

// 瓶颈识别流程
class PerformanceAnalyzer {
public:
    PerformanceBottleneck IdentifyBottleneck() {
        // 1. 降低分辨率测试
        float fps1080p = MeasureFPS(1920, 1080);
        float fps720p = MeasureFPS(1280, 720);
        
        if (fps720p > fps1080p * 1.5f) {
            // 分辨率敏感 = GPU 填充率限制
            return PerformanceBottleneck::FillRateBound;
        }
        
        // 2. 减少 Draw Call 测试
        float fpsNormal = MeasureFPS();
        float fpsLessDrawCalls = MeasureFPSWithFewerDrawCalls();
        
        if (fpsLessDrawCalls > fpsNormal * 1.3f) {
            // Draw Call 敏感 = CPU 限制
            return PerformanceBottleneck::DrawCallBound;
        }
        
        // 3. 简化着色器测试
        float fpsComplexShader = MeasureFPS();
        float fpsSimpleShader = MeasureFPSWithSimpleShaders();
        
        if (fpsSimpleShader > fpsComplexShader * 1.3f) {
            // 着色器敏感 = GPU 像素着色限制
            return PerformanceBottleneck::PixelBound;
        }
        
        // 4. 减少多边形测试
        float fpsHighPoly = MeasureFPS();
        float fpsLowPoly = MeasureFPSWithLowerPolyCount();
        
        if (fpsLowPoly > fpsHighPoly * 1.3f) {
            // 多边形敏感 = GPU 顶点处理限制
            return PerformanceBottleneck::VertexBound;
        }
        
        return PerformanceBottleneck::GPUBound;
    }
};

// 瓶颈示意图：
//
// CPU Bound:
// CPU: ████████████████ (100%)
// GPU: ████░░░░░░░░░░░░ (25%)
// 解决：减少 Draw Call、优化代码
//
// GPU Bound:
// CPU: ████░░░░░░░░░░░░ (25%)
// GPU: ████████████████ (100%)
// 解决：降低分辨率、简化着色器
//
// 平衡（理想）:
// CPU: ████████████░░░░ (75%)
// GPU: ████████████░░░░ (75%)
```

### 1.3 性能分析思维

```
性能优化的黄金法则：
1. 先测量，再优化（Measure, Don't Guess）
2. 优化热点，不优化全部（80/20 原则）
3. 验证优化效果（A/B 对比）

优化流程：
┌─────────────────────────────────────┐
│ 1. Profile（性能分析）              │
│    找到最慢的 10% 代码              │
│    ↓                                │
│ 2. Analyze（分析原因）              │
│    为什么慢？瓶颈在哪？             │
│    ↓                                │
│ 3. Optimize（优化实现）             │
│    修改代码、算法、数据结构         │
│    ↓                                │
│ 4. Verify（验证效果）               │
│    性能提升多少？有无副作用？       │
│    ↓                                │
│ 5. Repeat（重复）                   │
│    继续下一个热点                   │
└─────────────────────────────────────┘

常见误区：
❌ 过早优化（Premature Optimization）
❌ 优化非热点代码
❌ 牺牲可读性换取微小性能提升
❌ 不测量，凭感觉优化

正确做法：
✓ 先实现功能，后优化性能
✓ 用 Profiler 找热点
✓ 优化算法复杂度（O(n²) → O(n log n)）
✓ 利用硬件特性（SIMD、缓存）
```

---

## 2. CPU 性能优化

### 2.1 减少 Draw Call

```cpp
// Draw Call: CPU 告诉 GPU "绘制这个物体"的命令
// 问题：每次 Draw Call 有固定开销（验证、状态切换）

// ❌ 糟糕的做法：每个物体一个 Draw Call
void RenderSceneBad(const std::vector<GameObject>& objects) {
    for (const auto& obj : objects) {
        // 每次循环都是一次 Draw Call
        SetShader(obj.shader);
        SetTexture(obj.texture);
        SetTransform(obj.transform);
        DrawMesh(obj.mesh);  // Draw Call
    }
}
// 1000 个物体 = 1000 次 Draw Call
// 性能：~5 FPS（CPU 限制）

// ✓ 改进 1：批处理（Batching）
void RenderSceneWithBatching(const std::vector<GameObject>& objects) {
    // 按材质分组
    std::map<Material*, std::vector<GameObject*>> batches;
    for (auto& obj : objects) {
        batches[obj.material].push_back(&obj);
    }
    
    // 每个材质只设置一次
    for (auto& [material, batch] : batches) {
        SetShader(material->shader);
        SetTexture(material->texture);
        
        // 收集所有 Transform
        std::vector<Mat4> transforms;
        for (auto* obj : batch) {
            transforms.push_back(obj->transform);
        }
        
        // 一次 Draw Call 绘制所有实例
        DrawMeshInstanced(batch[0]->mesh, transforms);
    }
}
// 1000 个物体，10 种材质 = 10 次 Draw Call
// 性能：~60 FPS（100 倍提升）

// ✓ 改进 2：实例化渲染（Instancing）
struct InstanceData {
    Mat4 transform;
    Color color;
    // 其他 per-instance 数据
};

void RenderWithInstancing(const Mesh& mesh, 
                         const std::vector<InstanceData>& instances) {
    // 创建实例数据缓冲
    Buffer instanceBuffer;
    instanceBuffer.Upload(instances.data(), 
                         instances.size() * sizeof(InstanceData));
    
    // 绑定
    SetVertexBuffer(mesh.vertexBuffer);
    SetInstanceBuffer(instanceBuffer);
    
    // 一次 Draw Call 绘制所有实例
    DrawInstanced(mesh.vertexCount, instances.size());
}

// 实例化渲染着色器（HLSL）
struct VS_INPUT {
    float3 position : POSITION;
    float3 normal : NORMAL;
    
    // Per-instance 数据
    float4x4 instanceTransform : INSTANCE_TRANSFORM;
    float4 instanceColor : INSTANCE_COLOR;
    uint instanceID : SV_InstanceID;
};

VS_OUTPUT VSMain(VS_INPUT input) {
    VS_OUTPUT output;
    
    // 使用 per-instance transform
    float4 worldPos = mul(float4(input.position, 1.0), input.instanceTransform);
    output.position = mul(worldPos, viewProj);
    output.color = input.instanceColor;
    
    return output;
}

// Draw Call 对比：
// ┌──────────────────┬─────────────┬─────────┐
// │ 方法             │ Draw Calls  │ FPS     │
// ├──────────────────┼─────────────┼─────────┤
// │ 逐个绘制         │ 1000        │ 5       │
// │ 批处理           │ 10          │ 60      │
// │ 实例化           │ 1           │ 120     │
// └──────────────────┴─────────────┴─────────┘
```

### 2.2 状态排序

```cpp
// 状态切换（设置纹理、着色器）有开销
// 优化：最小化状态切换

// ❌ 未排序
void RenderUnsorted(std::vector<RenderCommand>& commands) {
    for (auto& cmd : commands) {
        SetShader(cmd.shader);      // 频繁切换
        SetTexture(cmd.texture);    // 频繁切换
        DrawMesh(cmd.mesh);
    }
}

// ✓ 按状态排序
struct RenderCommand {
    Shader* shader;
    Texture* texture;
    Mesh* mesh;
    Mat4 transform;
    
    // 排序键
    uint64_t GetSortKey() const {
        uint64_t key = 0;
        
        // 高位：着色器 ID（最重要）
        key |= ((uint64_t)shader->id) << 48;
        
        // 中位：纹理 ID
        key |= ((uint64_t)texture->id) << 32;
        
        // 低位：深度（前后顺序）
        uint32_t depth = *(uint32_t*)&transform._43;  // Z 值
        key |= depth;
        
        return key;
    }
};

void RenderSorted(std::vector<RenderCommand>& commands) {
    // 按排序键排序
    std::sort(commands.begin(), commands.end(), 
        [](const RenderCommand& a, const RenderCommand& b) {
            return a.GetSortKey() < b.GetSortKey();
        });
    
    // 渲染（状态切换最少）
    Shader* currentShader = nullptr;
    Texture* currentTexture = nullptr;
    
    for (auto& cmd : commands) {
        // 只在状态改变时切换
        if (cmd.shader != currentShader) {
            SetShader(cmd.shader);
            currentShader = cmd.shader;
        }
        
        if (cmd.texture != currentTexture) {
            SetTexture(cmd.texture);
            currentTexture = cmd.texture;
        }
        
        SetTransform(cmd.transform);
        DrawMesh(cmd.mesh);
    }
}

// 效果对比：
// 未排序：
// SetShader(A) → SetShader(B) → SetShader(A) → SetShader(C)
//   100 次状态切换
//
// 已排序：
// SetShader(A) → SetShader(B) → SetShader(C)
//   10 次状态切换（90% 减少）
//
// 性能提升：10-20%
```

### 2.3 多线程渲染

```cpp
// 现代 CPU：多核心
// 优化：并行生成渲染命令

class MultiThreadedRenderer {
public:
    void RenderFrame(const Scene& scene) {
        // 1. 准备阶段（单线程）
        std::vector<GameObject*> visibleObjects = CullObjects(scene);
        
        // 2. 生成命令阶段（多线程）
        int numThreads = std::thread::hardware_concurrency();
        std::vector<std::vector<RenderCommand>> perThreadCommands(numThreads);
        std::vector<std::thread> threads;
        
        int objectsPerThread = visibleObjects.size() / numThreads;
        
        for (int i = 0; i < numThreads; i++) {
            int start = i * objectsPerThread;
            int end = (i == numThreads - 1) ? visibleObjects.size() 
                                            : (i + 1) * objectsPerThread;
            
            threads.emplace_back([=, &perThreadCommands, &visibleObjects]() {
                for (int j = start; j < end; j++) {
                    RenderCommand cmd = GenerateRenderCommand(visibleObjects[j]);
                    perThreadCommands[i].push_back(cmd);
                }
            });
        }
        
        // 等待所有线程完成
        for (auto& thread : threads) {
            thread.join();
        }
        
        // 3. 合并命令（单线程）
        std::vector<RenderCommand> allCommands;
        for (auto& cmds : perThreadCommands) {
            allCommands.insert(allCommands.end(), cmds.begin(), cmds.end());
        }
        
        // 4. 排序
        std::sort(allCommands.begin(), allCommands.end(), /*...*/);
        
        // 5. 提交到 GPU（单线程，API 限制）
        for (auto& cmd : allCommands) {
            ExecuteCommand(cmd);
        }
    }
};

// 性能对比（8 核 CPU）：
// 单线程：10 ms
// 多线程：2 ms（5 倍提升）

// 注意事项：
// ✓ 命令生成可并行
// ✓ 数据准备可并行
// ❌ GPU 提交通常需要单线程（API 限制）
// ❌ 注意线程同步开销
```

### 2.4 CPU 缓存优化

```cpp
// CPU 缓存：L1 (32KB, 1 cycle) → L2 (256KB, 4 cycles) → L3 (8MB, 20 cycles) → RAM (64GB, 200 cycles)

// ❌ 缓存不友好：结构体数组（AoS）
struct Particle {
    Vec3 position;
    Vec3 velocity;
    Color color;
    float lifetime;
    // 64 字节
};

void UpdateParticlesBad(std::vector<Particle>& particles, float dt) {
    for (auto& p : particles) {
        // 读取整个结构体（64 字节），但只用 position 和 velocity
        p.position.x += p.velocity.x * dt;
        p.position.y += p.velocity.y * dt;
        p.position.z += p.velocity.z * dt;
    }
}
// 缓存利用率：24/64 = 37.5%

// ✓ 缓存友好：数组结构体（SoA）
struct ParticleSystem {
    std::vector<Vec3> positions;
    std::vector<Vec3> velocities;
    std::vector<Color> colors;
    std::vector<float> lifetimes;
};

void UpdateParticlesGood(ParticleSystem& system, float dt) {
    for (size_t i = 0; i < system.positions.size(); i++) {
        // 连续访问内存，缓存友好
        system.positions[i].x += system.velocities[i].x * dt;
        system.positions[i].y += system.velocities[i].y * dt;
        system.positions[i].z += system.velocities[i].z * dt;
    }
}
// 缓存利用率：24/24 = 100%
// 性能提升：2-3 倍

// 内存访问模式：
//
// AoS（缓存未命中多）:
// [Pos|Vel|Color|Life][Pos|Vel|Color|Life][Pos|Vel|Color|Life]
//  ↑ 读                ↑ 读                ↑ 读
//  加载 64B            加载 64B            加载 64B
//  只用 24B            只用 24B            只用 24B
//
// SoA（缓存命中高）:
// [Pos][Pos][Pos][Pos][Vel][Vel][Vel][Vel]
//  ↑    ↑    ↑    ↑    ↑    ↑    ↑    ↑
//  加载一次 64B，使用 8 个 Pos（充分利用）
```

### 2.5 避免动态分配

```cpp
// 动态分配（new/malloc）很慢
// 优化：对象池、预分配

// ❌ 频繁分配
void SpawnBulletsBad() {
    for (int i = 0; i < 100; i++) {
        Bullet* bullet = new Bullet();  // 慢！
        bullets.push_back(bullet);
    }
}

// ✓ 对象池
class BulletPool {
public:
    BulletPool(int capacity) {
        bullets.reserve(capacity);
        for (int i = 0; i < capacity; i++) {
            bullets.push_back(Bullet());
            freeList.push_back(&bullets[i]);
        }
    }
    
    Bullet* Allocate() {
        if (freeList.empty()) {
            return nullptr;  // 池满
        }
        
        Bullet* bullet = freeList.back();
        freeList.pop_back();
        activeList.push_back(bullet);
        return bullet;
    }
    
    void Free(Bullet* bullet) {
        auto it = std::find(activeList.begin(), activeList.end(), bullet);
        if (it != activeList.end()) {
            activeList.erase(it);
            freeList.push_back(bullet);
        }
    }
    
private:
    std::vector<Bullet> bullets;        // 预分配
    std::vector<Bullet*> freeList;      // 空闲列表
    std::vector<Bullet*> activeList;    // 活跃列表
};

void SpawnBulletsGood(BulletPool& pool) {
    for (int i = 0; i < 100; i++) {
        Bullet* bullet = pool.Allocate();  // 快！
        if (bullet) {
            // 使用 bullet
        }
    }
}

// 性能对比：
// new/delete: 100 次分配 = 5 ms
// 对象池: 100 次分配 = 0.1 ms（50 倍快）
```

---

## 3. GPU 性能优化

### 3.1 减少 Overdraw

```
Overdraw: 同一像素被绘制多次

场景示例（从前到后）：
┌─────────────────┐
│  天空盒 (1)     │  ← 先绘制
│   ┌────────┐    │
│   │建筑 (2)│    │  ← 覆盖天空
│   │ ┌──┐   │    │
│   │ │人│(3)│    │  ← 覆盖建筑
│   │ └──┘   │    │
│   └────────┘    │
└─────────────────┘

Overdraw = 3（每个像素平均绘制 3 次）
浪费：66% 的像素着色工作

优化：从前到后渲染（启用深度测试）
┌─────────────────┐
│   ┌──┐          │  ← 先绘制人
│   │人│(1)       │
│   └──┘          │
│  ┌────────┐     │  ← 再绘制建筑（深度测试丢弃被遮挡部分）
│  │建筑 (2)│     │
│  └────────┘     │
│  天空盒 (3)     │  ← 最后绘制天空（只绘制未被遮挡部分）
└─────────────────┘

Overdraw = 1.2（减少 60%）
```

```cpp
// 渲染顺序优化
void RenderSceneOptimized(const Scene& scene) {
    // 1. 深度预处理（Z-Prepass）
    //    先只绘制深度，不绘制颜色
    SetRenderTarget(depthOnly);
    for (auto& obj : opaqueObjects) {
        DrawMeshDepthOnly(obj);
    }
    
    // 2. 不透明物体（从前到后）
    SetRenderTarget(colorBuffer);
    SetDepthTest(true, DepthFunc::Equal);  // 只绘制深度相等的像素
    
    std::sort(opaqueObjects.begin(), opaqueObjects.end(),
        [](const Object& a, const Object& b) {
            return a.depth < b.depth;  // 前到后
        });
    
    for (auto& obj : opaqueObjects) {
        DrawMesh(obj);  // 大部分像素被 Early-Z 剔除
    }
    
    // 3. 天空盒（最后）
    SetDepthTest(true, DepthFunc::LessEqual);
    DrawSkybox();
    
    // 4. 透明物体（从后到前）
    SetDepthWrite(false);
    std::sort(transparentObjects.begin(), transparentObjects.end(),
        [](const Object& a, const Object& b) {
            return a.depth > b.depth;  // 后到前
        });
    
    for (auto& obj : transparentObjects) {
        DrawMesh(obj);
    }
}

// Early-Z 优化：
// 现代 GPU 在像素着色器之前做深度测试
// 如果像素被遮挡，不执行昂贵的像素着色器
//
// 深度预处理效果：
// 无 Z-Prepass:  Overdraw = 3.0
// 有 Z-Prepass:  Overdraw = 1.0
// 像素着色器执行次数减少 66%
```

### 3.2 LOD（Level of Detail）

```cpp
// LOD: 根据距离使用不同精度的模型

struct LODModel {
    Mesh* mesh;
    float maxDistance;  // 最大显示距离
    int triangleCount;
};

class LODSystem {
public:
    std::vector<LODModel> levels;  // LOD 级别
    
    void AddLevel(Mesh* mesh, float maxDistance) {
        levels.push_back({mesh, maxDistance, mesh->triangleCount});
    }
    
    Mesh* SelectLOD(Vec3 objectPos, Vec3 cameraPos) {
        float distance = Length(objectPos - cameraPos);
        
        for (auto& level : levels) {
            if (distance < level.maxDistance) {
                return level.mesh;
            }
        }
        
        return nullptr;  // 超出范围，不渲染
    }
};

// LOD 示例（树模型）：
// ┌────────────────┬──────────┬──────────┬────────────┐
// │ LOD 级别       │ 距离     │ 三角形数 │ 细节       │
// ├────────────────┼──────────┼──────────┼────────────┤
// │ LOD 0 (高)     │ 0-10m    │ 10,000   │ 每片叶子   │
// │ LOD 1 (中)     │ 10-30m   │ 2,000    │ 叶子簇     │
// │ LOD 2 (低)     │ 30-100m  │ 500      │ 简化树冠   │
// │ LOD 3 (广告板) │ 100-200m │ 2        │ 2D 贴图    │
// │ 不渲染         │ >200m    │ 0        │ -          │
// └────────────────┴──────────┴──────────┴────────────┘

// 使用 LOD
void RenderWithLOD(const Scene& scene, Vec3 cameraPos) {
    for (auto& tree : scene.trees) {
        Mesh* lodMesh = tree.lodSystem.SelectLOD(tree.position, cameraPos);
        
        if (lodMesh) {
            DrawMesh(lodMesh, tree.transform);
        }
    }
}

// 性能提升：
// 无 LOD：1000 棵树 × 10,000 三角形 = 10,000,000 三角形
// 有 LOD：
//   - 10 棵近树 × 10,000 = 100,000
//   - 100 棵中树 × 2,000 = 200,000
//   - 500 棵远树 × 500 = 250,000
//   - 390 棵很远树 × 2 = 780
//   总计 = 550,780 三角形（减少 94.5%）
```

### 3.3 纹理优化

```cpp
// 纹理是 GPU 内存和带宽的主要消费者

// 1. 纹理压缩
enum class TextureFormat {
    RGBA8,       // 未压缩：4 字节/像素
    DXT1,        // 压缩：0.5 字节/像素（8:1）
    DXT5,        // 压缩：1 字节/像素（4:1）
    BC7,         // 高质量压缩：1 字节/像素
    ASTC_4x4     // 移动端：1 字节/像素
};

// 内存对比（2048×2048 纹理）：
// RGBA8: 2048×2048×4 = 16 MB
// BC7:   2048×2048×1 =  4 MB（减少 75%）

// 2. Mipmap（必须）
struct Texture {
    std::vector<uint8_t*> mipLevels;
    int width, height;
    
    void GenerateMipmaps() {
        int w = width, h = height;
        
        for (int level = 1; w > 1 || h > 1; level++) {
            w = std::max(1, w / 2);
            h = std::max(1, h / 2);
            
            uint8_t* mipData = new uint8_t[w * h * 4];
            DownsampleBilinear(mipLevels[level - 1], mipData, w, h);
            mipLevels.push_back(mipData);
        }
    }
};

// Mipmap 优势：
// - 减少带宽（远处用低分辨率）
// - 减少锯齿（预过滤）
// - 缓存友好（小纹理更易缓存）

// 3. 纹理图集（Texture Atlas）
class TextureAtlas {
public:
    Texture* atlas;  // 大纹理
    std::map<std::string, Rect> uvRects;  // 每个小纹理的 UV 范围
    
    void Pack(const std::vector<Texture*>& textures) {
        // 打包算法（bin packing）
        int atlasSize = 2048;
        atlas = new Texture(atlasSize, atlasSize);
        
        int x = 0, y = 0, rowHeight = 0;
        
        for (auto* tex : textures) {
            if (x + tex->width > atlasSize) {
                x = 0;
                y += rowHeight;
                rowHeight = 0;
            }
            
            // 复制纹理到图集
            CopyTexture(tex, atlas, x, y);
            
            // 记录 UV
            uvRects[tex->name] = {
                (float)x / atlasSize,
                (float)y / atlasSize,
                (float)(x + tex->width) / atlasSize,
                (float)(y + tex->height) / atlasSize
            };
            
            x += tex->width;
            rowHeight = std::max(rowHeight, tex->height);
        }
    }
    
    Rect GetUV(const std::string& name) {
        return uvRects[name];
    }
};

// 图集优势：
// - 减少纹理切换（状态切换）
// - 减少 Draw Call（批处理）
//
// 例子：100 个小纹理
// 单独纹理：100 次纹理切换
// 纹理图集：1 次纹理切换（100 倍快）
```

### 3.4 着色器优化

```hlsl
// GPU 着色器优化技巧

// ❌ 低效：像素着色器中做复杂计算
float4 PSBad(VS_OUTPUT input) : SV_TARGET {
    // 每像素执行（1920×1080 = 2,073,600 次）
    float3 worldPos = mul(input.position, worldMatrix);  // 矩阵乘法
    float3 normal = normalize(mul(input.normal, normalMatrix));
    
    // ... 光照计算
    return color;
}

// ✓ 高效：顶点着色器中预计算
struct VS_OUTPUT {
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;  // 预计算
    float3 normal : TEXCOORD1;    // 预计算
};

VS_OUTPUT VSGood(VS_INPUT input) {
    VS_OUTPUT output;
    
    // 顶点着色器中计算（只执行几千次）
    output.worldPos = mul(float4(input.position, 1.0), worldMatrix);
    output.normal = normalize(mul(input.normal, normalMatrix));
    output.position = mul(float4(output.worldPos, 1.0), viewProj);
    
    return output;
}

float4 PSGood(VS_OUTPUT input) : SV_TARGET {
    // 像素着色器直接使用（已插值）
    float3 worldPos = input.worldPos;
    float3 normal = input.normal;
    
    // ... 光照计算
    return color;
}

// 性能对比：
// PSBad:  每帧 2,073,600 次矩阵乘法
// VSGood: 每帧 10,000 次矩阵乘法（200 倍减少）

// 其他优化技巧：
// 1. 避免分支
if (condition) {  // 慢（GPU 不擅长分支）
    // ...
}
// 改为：
float factor = condition ? 1.0 : 0.0;  // 用乘法替代

// 2. 使用内置函数
float len = sqrt(x*x + y*y + z*z);  // 慢
float len = length(float3(x, y, z));  // 快（硬件优化）

// 3. 降低精度（移动端）
float highPrecision;   // 32 位
half mediumPrecision;  // 16 位（快 2 倍）
fixed lowPrecision;    // 10 位（更快，颜色用）
```

---

## 4. 内存优化

### 4.1 显存管理

```cpp
// 显存（VRAM）是有限的（例如：8 GB）

class VRAMManager {
public:
    size_t totalVRAM = 8 * 1024 * 1024 * 1024;  // 8 GB
    size_t usedVRAM = 0;
    
    struct AllocationInfo {
        void* gpuPtr;
        size_t size;
        int lastUsedFrame;
    };
    
    std::map<std::string, AllocationInfo> allocations;
    
    void* Allocate(const std::string& name, size_t size) {
        // 检查是否超出
        if (usedVRAM + size > totalVRAM) {
            // 清理最久未使用的资源
            EvictLRU(size);
        }
        
        void* ptr = GPUMalloc(size);
        allocations[name] = {ptr, size, currentFrame};
        usedVRAM += size;
        
        return ptr;
    }
    
    void EvictLRU(size_t neededSize) {
        // 按最后使用时间排序
        std::vector<std::pair<std::string, AllocationInfo>> sorted;
        for (auto& [name, info] : allocations) {
            sorted.push_back({name, info});
        }
        
        std::sort(sorted.begin(), sorted.end(),
            [](const auto& a, const auto& b) {
                return a.second.lastUsedFrame < b.second.lastUsedFrame;
            });
        
        // 释放直到有足够空间
        size_t freed = 0;
        for (auto& [name, info] : sorted) {
            GPUFree(info.gpuPtr);
            freed += info.size;
            usedVRAM -= info.size;
            allocations.erase(name);
            
            if (freed >= neededSize) {
                break;
            }
        }
    }
    
    void Touch(const std::string& name) {
        allocations[name].lastUsedFrame = currentFrame;
    }
};

// 显存使用分析（典型 AAA 游戏）：
// ┌──────────────────┬──────────┐
// │ 资源类型         │ 显存占用 │
// ├──────────────────┼──────────┤
// │ 纹理             │ 4 GB     │
// │ 几何体（顶点）   │ 1 GB     │
// │ 帧缓冲           │ 500 MB   │
// │ Shadow Map       │ 200 MB   │
// │ 其他             │ 300 MB   │
// │ 总计             │ 6 GB     │
// └──────────────────┴──────────┘
```

### 4.2 流式加载（Streaming）

```cpp
// 流式加载：按需加载资源

class StreamingSystem {
public:
    std::thread loadThread;
    std::queue<std::string> loadQueue;
    std::mutex queueMutex;
    bool running = true;
    
    StreamingSystem() {
        // 后台加载线程
        loadThread = std::thread([this]() {
            while (running) {
                std::string resourceName;
                
                {
                    std::lock_guard<std::mutex> lock(queueMutex);
                    if (!loadQueue.empty()) {
                        resourceName = loadQueue.front();
                        loadQueue.pop();
                    }
                }
                
                if (!resourceName.empty()) {
                    // 从硬盘加载
                    Resource* res = LoadFromDisk(resourceName);
                    
                    // 上传到 GPU
                    UploadToGPU(res);
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
    }
    
    void RequestLoad(const std::string& resourceName) {
        std::lock_guard<std::mutex> lock(queueMutex);
        loadQueue.push(resourceName);
    }
    
    void Update(Vec3 cameraPos) {
        // 根据相机位置预测需要的资源
        for (auto& zone : worldZones) {
            float distance = Length(zone.center - cameraPos);
            
            if (distance < zone.loadDistance) {
                // 需要加载
                if (!zone.loaded) {
                    RequestLoad(zone.resourceName);
                    zone.loaded = true;
                }
            } else if (distance > zone.unloadDistance) {
                // 可以卸载
                if (zone.loaded) {
                    UnloadResource(zone.resourceName);
                    zone.loaded = false;
                }
            }
        }
    }
};

// 流式加载效果：
// 无流式加载：
//   启动时加载全部资源 = 60 秒
//   内存占用 = 16 GB
//
// 有流式加载：
//   启动时加载初始场景 = 5 秒
//   内存占用 = 4 GB（动态）
//   运行中无卡顿
```

---

## 5. 性能测量工具

### 5.1 内置性能计数器

```cpp
// 简单的性能计数器
class PerformanceCounter {
public:
    std::string name;
    std::chrono::high_resolution_clock::time_point startTime;
    float averageTime = 0.0f;
    int sampleCount = 0;
    
    PerformanceCounter(const std::string& n) : name(n) {}
    
    void Begin() {
        startTime = std::chrono::high_resolution_clock::now();
    }
    
    void End() {
        auto endTime = std::chrono::high_resolution_clock::now();
        float elapsed = std::chrono::duration<float, std::milli>(
            endTime - startTime).count();
        
        // 移动平均
        averageTime = averageTime * 0.95f + elapsed * 0.05f;
        sampleCount++;
    }
    
    float GetAverageMs() const {
        return averageTime;
    }
};

// 使用示例
PerformanceCounter renderCounter("Render");
PerformanceCounter physicsCounter("Physics");

void GameLoop() {
    // 渲染
    renderCounter.Begin();
    RenderScene();
    renderCounter.End();
    
    // 物理
    physicsCounter.Begin();
    UpdatePhysics();
    physicsCounter.End();
    
    // 显示
    if (showStats) {
        printf("Render: %.2f ms\n", renderCounter.GetAverageMs());
        printf("Physics: %.2f ms\n", physicsCounter.GetAverageMs());
    }
}
```

### 5.2 GPU 性能查询

```cpp
// GPU 时间查询（Direct3D 11）
class GPUTimer {
public:
    ID3D11Query* queryStart;
    ID3D11Query* queryEnd;
    ID3D11Query* queryDisjoint;
    
    void Initialize(ID3D11Device* device) {
        D3D11_QUERY_DESC desc;
        
        desc.Query = D3D11_QUERY_TIMESTAMP;
        device->CreateQuery(&desc, &queryStart);
        device->CreateQuery(&desc, &queryEnd);
        
        desc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
        device->CreateQuery(&desc, &queryDisjoint);
    }
    
    void Begin(ID3D11DeviceContext* context) {
        context->Begin(queryDisjoint);
        context->End(queryStart);
    }
    
    void End(ID3D11DeviceContext* context) {
        context->End(queryEnd);
        context->End(queryDisjoint);
    }
    
    float GetElapsedMs(ID3D11DeviceContext* context) {
        // 等待结果
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData;
        while (context->GetData(queryDisjoint, &disjointData, 
               sizeof(disjointData), 0) == S_FALSE) {
            // 等待
        }
        
        if (disjointData.Disjoint) {
            return -1.0f;  // 无效
        }
        
        UINT64 startTime, endTime;
        context->GetData(queryStart, &startTime, sizeof(startTime), 0);
        context->GetData(queryEnd, &endTime, sizeof(endTime), 0);
        
        UINT64 delta = endTime - startTime;
        float ms = (delta * 1000.0f) / disjointData.Frequency;
        
        return ms;
    }
};

// 使用
GPUTimer shadowTimer, sceneTimer, postProcessTimer;

void RenderFrame() {
    // Shadow Map
    shadowTimer.Begin(context);
    RenderShadowMap();
    shadowTimer.End(context);
    
    // Scene
    sceneTimer.Begin(context);
    RenderScene();
    sceneTimer.End(context);
    
    // Post-process
    postProcessTimer.Begin(context);
    ApplyPostProcessing();
    postProcessTimer.End(context);
    
    // 下一帧读取结果
    float shadowMs = shadowTimer.GetElapsedMs(context);
    float sceneMs = sceneTimer.GetElapsedMs(context);
    float postMs = postProcessTimer.GetElapsedMs(context);
    
    printf("GPU: Shadow=%.2fms, Scene=%.2fms, Post=%.2fms\n",
           shadowMs, sceneMs, postMs);
}
```

### 5.3 专业工具

```
推荐的性能分析工具：

1. RenderDoc (免费)
   - 帧捕获
   - 着色器调试
   - 资源查看
   - Draw Call 分析
   下载：https://renderdoc.org/

2. NVIDIA Nsight (免费)
   - GPU 性能分析
   - CUDA 调试
   - 帧分析
   - 实时监控

3. Intel GPA (免费)
   - 系统分析
   - 帧分析
   - 着色器分析

4. Visual Studio Profiler
   - CPU 性能分析
   - 内存分析
   - GPU 使用率

5. PIX (Windows, 免费)
   - Direct3D 12 专用
   - 深度性能分析
   - GPU 调试

使用流程：
1. 捕获帧（F12 in RenderDoc）
2. 查看 Draw Call 列表
3. 检查每个 Draw Call 的：
   - 三角形数量
   - 纹理大小
   - 着色器复杂度
   - 像素数量（Overdraw）
4. 找到性能热点
5. 优化并重新测试
```

---

## 📚 总结

### 核心优化原则

```
1. 测量优先
   ✓ 使用 Profiler 找热点
   ✓ 先优化最慢的 10%
   ✗ 不要凭感觉优化

2. CPU 优化
   ✓ 减少 Draw Call（批处理、实例化）
   ✓ 状态排序（减少状态切换）
   ✓ 多线程（命令生成并行）
   ✓ 缓存友好（SoA、顺序访问）

3. GPU 优化
   ✓ 减少 Overdraw（前后排序、Z-Prepass）
   ✓ LOD（距离裁剪）
   ✓ 纹理压缩（BC7、ASTC）
   ✓ 着色器优化（顶点预计算）

4. 内存优化
   ✓ 显存管理（LRU 淘汰）
   ✓ 流式加载（按需加载）
   ✓ 对象池（避免分配）
```

### 性能优化检查清单

```
□ Draw Call < 2000（批处理）
□ 三角形 < 5,000,000（LOD）
□ Overdraw < 2（排序）
□ 纹理使用压缩格式
□ 纹理有 Mipmap
□ 着色器计算在顶点着色器
□ 避免分支和循环
□ 使用对象池
□ 内存顺序访问（缓存友好）
□ 流式加载大资源
```

### 下一步

**Part 5B** 将学习高级渲染优化技术（视锥剔除、遮挡剔除、GPU Culling 等）

准备好继续了吗？🚀
