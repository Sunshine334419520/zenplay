# 渲染深度解析 Part 5B：渲染优化技术

**文档目标**：掌握高级渲染优化与剔除技术  
**前置知识**：Part 5A（性能优化基础）  
**阅读时间**：35-45 分钟

---

## 📚 目录

1. [视锥剔除（Frustum Culling）](#1-视锥剔除frustum-culling)
2. [遮挡剔除（Occlusion Culling）](#2-遮挡剔除occlusion-culling)
3. [批处理技术](#3-批处理技术)
4. [GPU Driven Rendering](#4-gpu-driven-rendering)
5. [高级优化技巧](#5-高级优化技巧)

---

## 1. 视锥剔除（Frustum Culling）

### 1.1 视锥体基础

```
视锥体（View Frustum）：相机可见的 3D 空间区域

               Near Plane
                  ┌──┐
                 ╱│  │╲
                ╱ │  │ ╲
               ╱  │  │  ╲
              ╱   │  │   ╲
          Top╱    │  │    ╲Bottom
            ╱     └──┘     ╲
           ╱   Far Plane    ╲
          ╱                  ╲
       Left                 Right
        📷 Camera

视锥体由 6 个平面定义：
- Near（近裁剪面）
- Far（远裁剪面）
- Left（左侧面）
- Right（右侧面）
- Top（上侧面）
- Bottom（下侧面）

视锥剔除目标：
只渲染在视锥体内的物体，跳过视锥体外的物体
```

### 1.2 平面表示

```cpp
// 平面方程：Ax + By + Cz + D = 0
struct Plane {
    Vec3 normal;  // (A, B, C) 归一化
    float distance;  // D
    
    Plane(Vec3 n, float d) : normal(Normalize(n)), distance(d) {}
    
    // 从三个点创建平面
    static Plane FromPoints(Vec3 p0, Vec3 p1, Vec3 p2) {
        Vec3 v1 = p1 - p0;
        Vec3 v2 = p2 - p0;
        Vec3 normal = Normalize(Cross(v1, v2));
        float distance = -Dot(normal, p0);
        return Plane(normal, distance);
    }
    
    // 点到平面的有符号距离
    float DistanceToPoint(Vec3 point) const {
        return Dot(normal, point) + distance;
    }
    
    // 点在平面的哪一侧
    bool IsPointInFront(Vec3 point) const {
        return DistanceToPoint(point) > 0;
    }
};

// 视锥体
struct Frustum {
    Plane planes[6];  // Near, Far, Left, Right, Top, Bottom
    
    enum PlaneIndex {
        NEAR = 0,
        FAR = 1,
        LEFT = 2,
        RIGHT = 3,
        TOP = 4,
        BOTTOM = 5
    };
    
    // 从 View * Projection 矩阵提取视锥体
    void ExtractFromMatrix(const Mat4& viewProj) {
        // 视锥体平面可以从 VP 矩阵的行提取
        
        // Left: row4 + row1
        planes[LEFT] = Plane(
            {viewProj.m[3][0] + viewProj.m[0][0],
             viewProj.m[3][1] + viewProj.m[0][1],
             viewProj.m[3][2] + viewProj.m[0][2]},
            viewProj.m[3][3] + viewProj.m[0][3]
        );
        
        // Right: row4 - row1
        planes[RIGHT] = Plane(
            {viewProj.m[3][0] - viewProj.m[0][0],
             viewProj.m[3][1] - viewProj.m[0][1],
             viewProj.m[3][2] - viewProj.m[0][2]},
            viewProj.m[3][3] - viewProj.m[0][3]
        );
        
        // Bottom: row4 + row2
        planes[BOTTOM] = Plane(
            {viewProj.m[3][0] + viewProj.m[1][0],
             viewProj.m[3][1] + viewProj.m[1][1],
             viewProj.m[3][2] + viewProj.m[1][2]},
            viewProj.m[3][3] + viewProj.m[1][3]
        );
        
        // Top: row4 - row2
        planes[TOP] = Plane(
            {viewProj.m[3][0] - viewProj.m[1][0],
             viewProj.m[3][1] - viewProj.m[1][1],
             viewProj.m[3][2] - viewProj.m[1][2]},
            viewProj.m[3][3] - viewProj.m[1][3]
        );
        
        // Near: row4 + row3
        planes[NEAR] = Plane(
            {viewProj.m[3][0] + viewProj.m[2][0],
             viewProj.m[3][1] + viewProj.m[2][1],
             viewProj.m[3][2] + viewProj.m[2][2]},
            viewProj.m[3][3] + viewProj.m[2][3]
        );
        
        // Far: row4 - row3
        planes[FAR] = Plane(
            {viewProj.m[3][0] - viewProj.m[2][0],
             viewProj.m[3][1] - viewProj.m[2][1],
             viewProj.m[3][2] - viewProj.m[2][2]},
            viewProj.m[3][3] - viewProj.m[2][3]
        );
    }
};
```

### 1.3 包围体测试

```cpp
// 包围球（Bounding Sphere）
struct BoundingSphere {
    Vec3 center;
    float radius;
    
    bool IsInFrustum(const Frustum& frustum) const {
        // 测试球心到每个平面的距离
        for (int i = 0; i < 6; i++) {
            float distance = frustum.planes[i].DistanceToPoint(center);
            
            // 如果球心在平面后面，且距离大于半径，则完全在外面
            if (distance < -radius) {
                return false;  // 完全在视锥体外
            }
        }
        
        return true;  // 在视锥体内或相交
    }
};

// AABB（Axis-Aligned Bounding Box）
struct AABB {
    Vec3 min;
    Vec3 max;
    
    Vec3 GetCenter() const {
        return {(min.x + max.x) * 0.5f,
                (min.y + max.y) * 0.5f,
                (min.z + max.z) * 0.5f};
    }
    
    Vec3 GetExtent() const {
        return {(max.x - min.x) * 0.5f,
                (max.y - min.y) * 0.5f,
                (max.z - min.z) * 0.5f};
    }
    
    // 获取 8 个角点
    void GetCorners(Vec3 corners[8]) const {
        corners[0] = {min.x, min.y, min.z};
        corners[1] = {max.x, min.y, min.z};
        corners[2] = {max.x, max.y, min.z};
        corners[3] = {min.x, max.y, min.z};
        corners[4] = {min.x, min.y, max.z};
        corners[5] = {max.x, min.y, max.z};
        corners[6] = {max.x, max.y, max.z};
        corners[7] = {min.x, max.y, max.z};
    }
    
    bool IsInFrustum(const Frustum& frustum) const {
        // 方法 1: 测试所有 8 个角点（准确但慢）
        Vec3 corners[8];
        GetCorners(corners);
        
        for (int i = 0; i < 6; i++) {
            bool allOutside = true;
            
            for (int j = 0; j < 8; j++) {
                if (frustum.planes[i].IsPointInFront(corners[j])) {
                    allOutside = false;
                    break;
                }
            }
            
            if (allOutside) {
                return false;  // 所有角点都在某个平面外
            }
        }
        
        return true;
    }
    
    bool IsInFrustumFast(const Frustum& frustum) const {
        // 方法 2: P-N 顶点测试（快速近似）
        Vec3 center = GetCenter();
        Vec3 extent = GetExtent();
        
        for (int i = 0; i < 6; i++) {
            const Plane& plane = frustum.planes[i];
            
            // P 顶点：沿法线方向最远的点
            Vec3 p = center;
            if (plane.normal.x >= 0) p.x += extent.x; else p.x -= extent.x;
            if (plane.normal.y >= 0) p.y += extent.y; else p.y -= extent.y;
            if (plane.normal.z >= 0) p.z += extent.z; else p.z -= extent.z;
            
            // 如果 P 顶点在平面后面，则 AABB 完全在外面
            if (plane.DistanceToPoint(p) < 0) {
                return false;
            }
        }
        
        return true;
    }
};

// 包围体对比：
//
// 包围球：
// ┌─────────┐
// │   ●●●   │  ← 简单快速
// │  ●───●  │  ← 但松散（false positive 多）
// │   ●●●   │
// └─────────┘
//
// AABB：
// ┌─────────┐
// │ ┌─────┐ │  ← 较紧密
// │ │     │ │  ← 但旋转后变松散
// │ └─────┘ │
// └─────────┘
//
// OBB（Oriented BB，本文未实现）：
// ┌─────────┐
// │    ╱──╲ │  ← 最紧密
// │   ╱    ╲│  ← 但测试最慢
// │   ╲    ╱│
// │    ╲──╱ │
// └─────────┘
```

### 1.4 视锥剔除实现

```cpp
// 视锥剔除系统
class FrustumCullingSystem {
public:
    Frustum frustum;
    int totalObjects = 0;
    int culledObjects = 0;
    
    void Update(const Mat4& viewProj) {
        frustum.ExtractFromMatrix(viewProj);
        totalObjects = 0;
        culledObjects = 0;
    }
    
    std::vector<GameObject*> CullObjects(const std::vector<GameObject*>& objects) {
        std::vector<GameObject*> visible;
        
        for (auto* obj : objects) {
            totalObjects++;
            
            // 测试包围体
            if (obj->boundingSphere.IsInFrustum(frustum)) {
                visible.push_back(obj);
            } else {
                culledObjects++;
            }
        }
        
        return visible;
    }
    
    float GetCullRatio() const {
        return totalObjects > 0 ? (float)culledObjects / totalObjects : 0.0f;
    }
};

// 使用示例
int main() {
    FrustumCullingSystem culling;
    std::vector<GameObject*> allObjects;  // 10,000 个物体
    
    while (true) {
        // 更新视锥体
        Mat4 viewProj = camera.GetViewProjectionMatrix();
        culling.Update(viewProj);
        
        // 剔除
        auto visibleObjects = culling.CullObjects(allObjects);
        
        // 只渲染可见物体
        for (auto* obj : visibleObjects) {
            Render(obj);
        }
        
        // 统计
        printf("Visible: %zu / %d (%.1f%% culled)\n",
               visibleObjects.size(),
               culling.totalObjects,
               culling.GetCullRatio() * 100);
    }
    
    return 0;
}

// 性能提升：
// 无剔除：渲染 10,000 个物体 = 15 ms
// 有剔除：渲染 1,000 个物体 = 2 ms（7.5 倍快）
//         剔除本身开销 = 0.1 ms（可忽略）
```

---

## 2. 遮挡剔除（Occlusion Culling）

### 2.1 遮挡剔除原理

```
遮挡剔除：跳过被其他物体遮挡的物体

场景示例（俯视图）：
┌─────────────────────────────────┐
│  📷 Camera                       │
│   │                              │
│   │  视线                        │
│   ▼                              │
│  ███ Building A (可见)           │
│  ███                             │
│  ███   ●●● Tree (被遮挡)         │
│  ███   ●●●                       │
│                                  │
│      ████ Building B (可见)      │
│      ████                        │
└─────────────────────────────────┘

Building A 遮挡了 Tree，不需要渲染 Tree

遮挡剔除的挑战：
- 如何快速判断物体是否被遮挡？
- 如何避免遮挡查询本身成为瓶颈？
```

### 2.2 硬件遮挡查询

```cpp
// GPU 硬件遮挡查询（Direct3D 11）

class OcclusionQuery {
public:
    ID3D11Query* query;
    bool queryActive = false;
    UINT64 visiblePixels = 0;
    
    void Initialize(ID3D11Device* device) {
        D3D11_QUERY_DESC desc;
        desc.Query = D3D11_QUERY_OCCLUSION;
        desc.MiscFlags = 0;
        device->CreateQuery(&desc, &query);
    }
    
    void Begin(ID3D11DeviceContext* context) {
        context->Begin(query);
        queryActive = true;
    }
    
    void End(ID3D11DeviceContext* context) {
        context->End(query);
    }
    
    bool IsVisible(ID3D11DeviceContext* context) {
        if (!queryActive) {
            return true;
        }
        
        // 尝试获取结果（非阻塞）
        HRESULT hr = context->GetData(query, &visiblePixels, 
                                     sizeof(visiblePixels), 
                                     D3D11_ASYNC_GETDATA_DONOTFLUSH);
        
        if (hr == S_OK) {
            queryActive = false;
            return visiblePixels > 0;
        }
        
        // 结果未就绪，假设可见
        return true;
    }
};

// 使用遮挡查询
class OcclusionCullingSystem {
public:
    std::map<GameObject*, OcclusionQuery> queries;
    
    void TestOcclusion(GameObject* obj, ID3D11DeviceContext* context) {
        auto& query = queries[obj];
        
        // 禁用颜色写入，只测试深度
        SetColorWriteMask(false, false, false, false);
        SetDepthTest(true);
        
        // 开始查询
        query.Begin(context);
        
        // 渲染包围盒
        RenderBoundingBox(obj->aabb);
        
        // 结束查询
        query.End(context);
        
        // 恢复颜色写入
        SetColorWriteMask(true, true, true, true);
    }
    
    std::vector<GameObject*> CullObjects(
        const std::vector<GameObject*>& objects,
        ID3D11DeviceContext* context) {
        
        std::vector<GameObject*> visible;
        
        // 1. 测试所有物体的遮挡
        for (auto* obj : objects) {
            TestOcclusion(obj, context);
        }
        
        // 2. 下一帧使用结果
        for (auto* obj : objects) {
            if (queries[obj].IsVisible(context)) {
                visible.push_back(obj);
            }
        }
        
        return visible;
    }
};

// 问题：查询有 1-2 帧延迟
// 解决：结合其他剔除方法（视锥剔除、距离剔除）
```

### 2.3 软件遮挡剔除（Hi-Z）

```cpp
// Hi-Z (Hierarchical Z-Buffer): 软件遮挡剔除

class HiZOcclusionCulling {
public:
    // Hi-Z 金字塔（Mipmap 链）
    std::vector<float*> hiZLevels;
    int width, height;
    
    void BuildHiZ(const float* depthBuffer, int w, int h) {
        width = w;
        height = h;
        
        // 清理旧数据
        for (auto* level : hiZLevels) {
            delete[] level;
        }
        hiZLevels.clear();
        
        // Level 0: 原始深度
        hiZLevels.push_back(new float[w * h]);
        memcpy(hiZLevels[0], depthBuffer, w * h * sizeof(float));
        
        // 生成 Mipmap
        int levelWidth = w;
        int levelHeight = h;
        
        while (levelWidth > 1 || levelHeight > 1) {
            int nextWidth = std::max(1, levelWidth / 2);
            int nextHeight = std::max(1, levelHeight / 2);
            
            float* nextLevel = new float[nextWidth * nextHeight];
            
            // 下采样：取 2×2 的最大深度（最远）
            for (int y = 0; y < nextHeight; y++) {
                for (int x = 0; x < nextWidth; x++) {
                    float* prevLevel = hiZLevels.back();
                    
                    float d00 = prevLevel[(y*2+0) * levelWidth + (x*2+0)];
                    float d10 = (x*2+1 < levelWidth) ? 
                        prevLevel[(y*2+0) * levelWidth + (x*2+1)] : d00;
                    float d01 = (y*2+1 < levelHeight) ? 
                        prevLevel[(y*2+1) * levelWidth + (x*2+0)] : d00;
                    float d11 = (x*2+1 < levelWidth && y*2+1 < levelHeight) ? 
                        prevLevel[(y*2+1) * levelWidth + (x*2+1)] : d00;
                    
                    // 保守深度：最大值（最远）
                    nextLevel[y * nextWidth + x] = 
                        std::max(std::max(d00, d10), std::max(d01, d11));
                }
            }
            
            hiZLevels.push_back(nextLevel);
            levelWidth = nextWidth;
            levelHeight = nextHeight;
        }
    }
    
    bool IsOccluded(const AABB& aabb, const Mat4& viewProj) {
        // 1. 投影 AABB 到屏幕空间
        Vec3 corners[8];
        aabb.GetCorners(corners);
        
        float minX = FLT_MAX, maxX = -FLT_MAX;
        float minY = FLT_MAX, maxY = -FLT_MAX;
        float minZ = FLT_MAX;
        
        for (int i = 0; i < 8; i++) {
            Vec4 clip = viewProj * Vec4{corners[i].x, corners[i].y, 
                                        corners[i].z, 1.0f};
            Vec3 ndc = {clip.x / clip.w, clip.y / clip.w, clip.z / clip.w};
            
            // NDC [-1, 1] → Screen [0, 1]
            float screenX = (ndc.x + 1.0f) * 0.5f;
            float screenY = (1.0f - ndc.y) * 0.5f;
            
            minX = std::min(minX, screenX);
            maxX = std::max(maxX, screenX);
            minY = std::min(minY, screenY);
            maxY = std::max(maxY, screenY);
            minZ = std::min(minZ, ndc.z);  // 最近深度
        }
        
        // 2. 计算覆盖的屏幕区域
        int pixelMinX = (int)(minX * width);
        int pixelMaxX = (int)(maxX * width);
        int pixelMinY = (int)(minY * height);
        int pixelMaxY = (int)(maxY * height);
        
        int pixelWidth = pixelMaxX - pixelMinX;
        int pixelHeight = pixelMaxY - pixelMinY;
        
        // 3. 选择合适的 Mip 级别
        int mipLevel = 0;
        int levelWidth = width;
        int levelHeight = height;
        
        for (int i = 0; i < hiZLevels.size(); i++) {
            if (pixelWidth <= levelWidth && pixelHeight <= levelHeight) {
                mipLevel = i;
                break;
            }
            levelWidth /= 2;
            levelHeight /= 2;
        }
        
        // 4. 采样 Hi-Z 深度
        float* level = hiZLevels[mipLevel];
        int sampleX = (int)(minX * (width >> mipLevel));
        int sampleY = (int)(minY * (height >> mipLevel));
        int sampleWidth = width >> mipLevel;
        
        sampleX = std::clamp(sampleX, 0, sampleWidth - 1);
        sampleY = std::clamp(sampleY, 0, (height >> mipLevel) - 1);
        
        float occluderDepth = level[sampleY * sampleWidth + sampleX];
        
        // 5. 深度比较
        // 如果物体的最近深度大于遮挡物深度，则被遮挡
        return minZ > occluderDepth;
    }
};

// Hi-Z 可视化：
//
// Level 0 (1920×1080):    Level 1 (960×540):    Level 2 (480×270):
// ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓        ▓▓▓▓▓▓▓▓              ▓▓▓▓
// ▓▓▓▒▒▒▒▒▒▒▒▒▒▓▓        ▓▓▒▒▒▒▓▓              ▓▒▒▓
// ▓▒▒░░░░░░░░░▒▓        ▓▒░░░░▒▓              ▓▒░▓
// ▓▒░░     ░░░▒▓        ▓▒░  ░▒▓              ▓▒░▓
//  ... 原始深度            ... 保守深度           ... 更保守
//
// 查询：物体投影 16×16 像素 → 使用 Level 2（快速）
```

### 2.4 实用遮挡剔除策略

```cpp
// 组合策略：快速剔除 + 精确测试

class PracticalOcclusionCulling {
public:
    FrustumCullingSystem frustumCulling;
    HiZOcclusionCulling hiZCulling;
    
    std::vector<GameObject*> CullObjects(
        const std::vector<GameObject*>& objects,
        const Mat4& viewProj) {
        
        std::vector<GameObject*> visible;
        
        // 阶段 1: 视锥剔除（快速，CPU）
        auto inFrustum = frustumCulling.CullObjects(objects);
        
        // 阶段 2: 距离剔除（快速，CPU）
        std::vector<GameObject*> nearObjects;
        for (auto* obj : inFrustum) {
            float dist = Length(obj->position - camera.position);
            if (dist < 100.0f) {
                nearObjects.push_back(obj);
            }
        }
        
        // 阶段 3: 遮挡剔除（慢，仅对近距离物体）
        for (auto* obj : nearObjects) {
            if (!hiZCulling.IsOccluded(obj->aabb, viewProj)) {
                visible.push_back(obj);
            }
        }
        
        return visible;
    }
};

// 剔除效率：
// 10,000 个物体：
//   - 视锥剔除：→ 2,000 个（80% 剔除）
//   - 距离剔除：→ 500 个（75% 剔除）
//   - 遮挡剔除：→ 200 个（60% 剔除）
//   总剔除率：98%（只渲染 200 个）
```

---

## 3. 批处理技术

### 3.1 静态批处理

```cpp
// 静态批处理：合并静态物体的网格

class StaticBatchingSystem {
public:
    struct Batch {
        Mesh* combinedMesh;
        Material* material;
        int objectCount;
    };
    
    std::vector<Batch> batches;
    
    void BuildBatches(const std::vector<StaticObject*>& objects) {
        // 按材质分组
        std::map<Material*, std::vector<StaticObject*>> groups;
        for (auto* obj : objects) {
            groups[obj->material].push_back(obj);
        }
        
        // 为每个材质创建批次
        for (auto& [material, objs] : groups) {
            Batch batch;
            batch.material = material;
            batch.objectCount = objs.size();
            
            // 合并网格
            batch.combinedMesh = CombineMeshes(objs);
            
            batches.push_back(batch);
        }
    }
    
    Mesh* CombineMeshes(const std::vector<StaticObject*>& objects) {
        std::vector<Vertex> allVertices;
        std::vector<uint32_t> allIndices;
        
        for (auto* obj : objects) {
            Mat4 transform = obj->transform;
            
            // 变换顶点到世界空间
            for (const auto& v : obj->mesh->vertices) {
                Vertex transformed;
                Vec4 pos = transform * Vec4{v.position.x, v.position.y, 
                                           v.position.z, 1.0f};
                transformed.position = {pos.x, pos.y, pos.z};
                transformed.normal = Normalize(TransformNormal(v.normal, transform));
                transformed.uv = v.uv;
                
                allVertices.push_back(transformed);
            }
            
            // 添加索引（调整偏移）
            uint32_t indexOffset = allVertices.size() - obj->mesh->vertices.size();
            for (uint32_t idx : obj->mesh->indices) {
                allIndices.push_back(idx + indexOffset);
            }
        }
        
        // 创建合并后的网格
        Mesh* combined = new Mesh();
        combined->vertices = allVertices;
        combined->indices = allIndices;
        combined->Upload();
        
        return combined;
    }
    
    void Render() {
        for (auto& batch : batches) {
            SetMaterial(batch.material);
            DrawMesh(batch.combinedMesh);  // 一次 Draw Call
        }
    }
};

// 效果：
// 未批处理：1000 个物体 = 1000 次 Draw Call
// 静态批处理：1000 个物体，10 种材质 = 10 次 Draw Call
//
// 缺点：
// - 占用更多内存（每个物体独立网格 → 合并大网格）
// - 不能移动（静态）
// - 初始化时间长
```

### 3.2 动态批处理

```cpp
// 动态批处理：运行时合并小物体

class DynamicBatchingSystem {
public:
    static const int MAX_BATCH_VERTICES = 300;  // 限制批次大小
    
    struct DynamicBatch {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        Material* material;
        
        bool CanAdd(const Mesh* mesh) const {
            return vertices.size() + mesh->vertices.size() <= MAX_BATCH_VERTICES;
        }
        
        void Add(const Mesh* mesh, const Mat4& transform) {
            uint32_t indexOffset = vertices.size();
            
            // 添加变换后的顶点
            for (const auto& v : mesh->vertices) {
                Vertex transformed = TransformVertex(v, transform);
                vertices.push_back(transformed);
            }
            
            // 添加索引
            for (uint32_t idx : mesh->indices) {
                indices.push_back(idx + indexOffset);
            }
        }
        
        void Render() {
            if (vertices.empty()) return;
            
            // 上传到临时缓冲
            UpdateDynamicBuffer(vertices, indices);
            DrawDynamicBuffer();
        }
        
        void Clear() {
            vertices.clear();
            indices.clear();
        }
    };
    
    void RenderObjects(const std::vector<GameObject*>& objects) {
        // 按材质分组
        std::map<Material*, std::vector<GameObject*>> groups;
        for (auto* obj : objects) {
            // 只批处理小物体
            if (obj->mesh->vertices.size() < 100) {
                groups[obj->material].push_back(obj);
            }
        }
        
        // 动态批处理
        for (auto& [material, objs] : groups) {
            SetMaterial(material);
            
            DynamicBatch batch;
            batch.material = material;
            
            for (auto* obj : objs) {
                if (!batch.CanAdd(obj->mesh)) {
                    // 批次满，渲染并清空
                    batch.Render();
                    batch.Clear();
                }
                
                batch.Add(obj->mesh, obj->transform);
            }
            
            // 渲染剩余
            batch.Render();
        }
    }
};

// 适用场景：
// - 小物体（树叶、石头、草）
// - 相同材质
// - 可以移动（每帧重新批处理）
//
// 性能：
// 1000 个小物体（每个 50 顶点）：
//   - 无批处理：1000 次 Draw Call
//   - 动态批处理：~20 次 Draw Call（50 倍减少）
```

### 3.3 GPU Instancing

```cpp
// GPU 实例化：最高效的批处理

class GPUInstancingSystem {
public:
    struct InstanceData {
        Mat4 worldMatrix;
        Color tint;
        // 其他 per-instance 数据
    };
    
    void RenderInstanced(const Mesh* mesh, 
                        const std::vector<InstanceData>& instances,
                        Material* material) {
        if (instances.empty()) return;
        
        // 创建/更新实例缓冲
        UpdateInstanceBuffer(instances);
        
        // 设置状态
        SetMaterial(material);
        SetVertexBuffer(mesh->vertexBuffer);
        SetIndexBuffer(mesh->indexBuffer);
        SetInstanceBuffer(instanceBuffer);
        
        // 一次 Draw Call 绘制所有实例
        DrawIndexedInstanced(
            mesh->indexCount,     // 索引数
            instances.size(),     // 实例数
            0, 0, 0
        );
    }
    
    void RenderScene(const std::vector<GameObject*>& objects) {
        // 按 (Mesh, Material) 分组
        struct MeshMaterialPair {
            const Mesh* mesh;
            Material* material;
            
            bool operator<(const MeshMaterialPair& other) const {
                if (mesh != other.mesh) return mesh < other.mesh;
                return material < other.material;
            }
        };
        
        std::map<MeshMaterialPair, std::vector<InstanceData>> groups;
        
        for (auto* obj : objects) {
            MeshMaterialPair key = {obj->mesh, obj->material};
            
            InstanceData instance;
            instance.worldMatrix = obj->transform;
            instance.tint = obj->tint;
            
            groups[key].push_back(instance);
        }
        
        // 渲染每组
        for (auto& [key, instances] : groups) {
            RenderInstanced(key.mesh, instances, key.material);
        }
    }
};

// 实例化着色器（HLSL）
struct VS_INPUT {
    // Per-vertex 数据
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    
    // Per-instance 数据
    float4x4 worldMatrix : INSTANCE_WORLD;
    float4 instanceTint : INSTANCE_TINT;
    uint instanceID : SV_InstanceID;
};

VS_OUTPUT VSMain(VS_INPUT input) {
    VS_OUTPUT output;
    
    // 使用 per-instance 数据
    float4 worldPos = mul(float4(input.position, 1.0), input.worldMatrix);
    output.position = mul(worldPos, viewProj);
    output.color = input.instanceTint;
    
    return output;
}

// 性能对比（10,000 个相同物体）：
// ┌────────────────────┬─────────────┬─────────┐
// │ 方法               │ Draw Calls  │ FPS     │
// ├────────────────────┼─────────────┼─────────┤
// │ 逐个绘制           │ 10,000      │ 3       │
// │ 静态批处理         │ 1           │ 60      │
// │ 动态批处理         │ 100         │ 45      │
// │ GPU Instancing     │ 1           │ 120     │
// └────────────────────┴─────────────┴─────────┘
```

---

## 4. GPU Driven Rendering

### 4.1 间接绘制（Indirect Draw）

```cpp
// 间接绘制：GPU 自己决定绘制什么

// Draw Indirect 参数结构
struct DrawIndexedIndirectCommand {
    uint32_t indexCount;
    uint32_t instanceCount;
    uint32_t firstIndex;
    int32_t  baseVertex;
    uint32_t firstInstance;
};

class IndirectDrawSystem {
public:
    Buffer* commandBuffer;    // GPU 缓冲，存储绘制命令
    Buffer* argumentBuffer;   // 计算着色器生成的参数
    
    void SetupIndirectDraw(const std::vector<Mesh*>& meshes) {
        std::vector<DrawIndexedIndirectCommand> commands;
        
        for (auto* mesh : meshes) {
            DrawIndexedIndirectCommand cmd;
            cmd.indexCount = mesh->indexCount;
            cmd.instanceCount = 1;  // GPU 会修改这个值
            cmd.firstIndex = 0;
            cmd.baseVertex = 0;
            cmd.firstInstance = 0;
            
            commands.push_back(cmd);
        }
        
        // 上传到 GPU
        commandBuffer->Upload(commands.data(), 
                             commands.size() * sizeof(DrawIndexedIndirectCommand));
    }
    
    void Render(int commandCount) {
        // GPU 从 commandBuffer 读取绘制参数
        DrawIndexedIndirect(
            commandBuffer,
            0,              // 偏移
            commandCount,   // 命令数量
            sizeof(DrawIndexedIndirectCommand)
        );
    }
};

// Compute Shader 生成绘制命令（HLSL）
RWStructuredBuffer<DrawIndexedIndirectCommand> commands : register(u0);
StructuredBuffer<ObjectData> objects : register(t0);

cbuffer FrustumData : register(b0) {
    float4 frustumPlanes[6];
    float3 cameraPosition;
};

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint objectID = dispatchThreadID.x;
    
    if (objectID >= objectCount) {
        return;
    }
    
    ObjectData obj = objects[objectID];
    
    // GPU 上做视锥剔除
    bool visible = true;
    for (int i = 0; i < 6; i++) {
        float4 plane = frustumPlanes[i];
        float distance = dot(plane.xyz, obj.center) + plane.w;
        
        if (distance < -obj.radius) {
            visible = false;
            break;
        }
    }
    
    // 修改绘制命令
    if (visible) {
        commands[obj.meshID].instanceCount++;  // 原子操作
    }
}
```

### 4.2 GPU Culling

```cpp
// 完整的 GPU Culling 系统

class GPUCullingSystem {
public:
    ComputeShader* cullingShader;
    Buffer* objectBuffer;          // 所有物体数据
    Buffer* visibleIndexBuffer;    // 可见物体索引
    Buffer* indirectArgsBuffer;    // 间接绘制参数
    
    void PerformCulling(int objectCount, const Frustum& frustum) {
        // 1. 设置常量
        SetFrustumPlanes(frustum);
        
        // 2. 运行计算着色器
        cullingShader->Bind();
        cullingShader->SetBuffer("objects", objectBuffer);
        cullingShader->SetBuffer("visibleIndices", visibleIndexBuffer);
        cullingShader->SetBuffer("indirectArgs", indirectArgsBuffer);
        
        int groupCount = (objectCount + 63) / 64;
        Dispatch(groupCount, 1, 1);
        
        // 3. GPU 自动使用结果渲染
    }
    
    void RenderCulledObjects() {
        // 间接绘制可见物体
        DrawIndirect(indirectArgsBuffer);
    }
};

// GPU Culling 优势：
// - CPU 完全不参与剔除（异步）
// - 支持海量物体（百万级）
// - 无 CPU → GPU 数据传输开销
//
// 性能对比（100,000 个物体）：
// CPU Culling: 10 ms (CPU) + 传输开销
// GPU Culling: 0.5 ms (GPU) + 0 传输
```

---

## 5. 高级优化技巧

### 5.1 分层剔除

```cpp
// 使用空间结构加速剔除

// 八叉树节点
struct OctreeNode {
    AABB bounds;
    std::vector<GameObject*> objects;
    OctreeNode* children[8] = {nullptr};
    bool isLeaf = true;
    
    void Build(const std::vector<GameObject*>& allObjects, int depth) {
        if (depth == 0 || allObjects.size() < 10) {
            objects = allObjects;
            isLeaf = true;
            return;
        }
        
        isLeaf = false;
        
        // 分割空间
        Vec3 center = bounds.GetCenter();
        std::vector<GameObject*> childObjects[8];
        
        for (auto* obj : allObjects) {
            int index = 0;
            if (obj->position.x > center.x) index |= 1;
            if (obj->position.y > center.y) index |= 2;
            if (obj->position.z > center.z) index |= 4;
            
            childObjects[index].push_back(obj);
        }
        
        // 递归构建子节点
        for (int i = 0; i < 8; i++) {
            if (!childObjects[i].empty()) {
                children[i] = new OctreeNode();
                children[i]->bounds = ComputeChildBounds(bounds, i);
                children[i]->Build(childObjects[i], depth - 1);
            }
        }
    }
    
    void Query(const Frustum& frustum, std::vector<GameObject*>& visible) {
        // 测试节点包围盒
        if (!bounds.IsInFrustum(frustum)) {
            return;  // 整个节点不可见
        }
        
        if (isLeaf) {
            // 叶子节点，添加所有物体
            for (auto* obj : objects) {
                if (obj->boundingSphere.IsInFrustum(frustum)) {
                    visible.push_back(obj);
                }
            }
        } else {
            // 递归查询子节点
            for (int i = 0; i < 8; i++) {
                if (children[i]) {
                    children[i]->Query(frustum, visible);
                }
            }
        }
    }
};

// 性能对比（100,000 个物体）：
// 线性剔除：100,000 次测试 = 5 ms
// 八叉树剔除：~1,000 次测试 = 0.5 ms（10 倍快）
```

### 5.2 时间相干性

```cpp
// 利用时间相干性：物体可见性帧间变化小

class TemporalCoherenceSystem {
public:
    std::set<GameObject*> visibleLastFrame;
    std::set<GameObject*> visibleThisFrame;
    
    std::vector<GameObject*> CullObjects(
        const std::vector<GameObject*>& objects,
        const Frustum& frustum) {
        
        visibleThisFrame.clear();
        
        // 1. 优先测试上一帧可见的物体（大概率仍可见）
        for (auto* obj : visibleLastFrame) {
            if (obj->boundingSphere.IsInFrustum(frustum)) {
                visibleThisFrame.insert(obj);
            }
        }
        
        // 2. 测试上一帧不可见的物体（低优先级）
        for (auto* obj : objects) {
            if (visibleLastFrame.find(obj) == visibleLastFrame.end()) {
                if (obj->boundingSphere.IsInFrustum(frustum)) {
                    visibleThisFrame.insert(obj);
                }
            }
        }
        
        // 3. 更新历史
        visibleLastFrame = visibleThisFrame;
        
        return std::vector<GameObject*>(visibleThisFrame.begin(), 
                                       visibleThisFrame.end());
    }
};

// 效果：
// - 上一帧可见 → 这一帧 95% 概率可见（提前测试）
// - 减少无效测试
```

### 5.3 LOD 过渡

```cpp
// 平滑 LOD 切换（避免 popping）

class SmoothLODSystem {
public:
    float GetBlendFactor(float distance, float threshold, float range) {
        float d = (distance - threshold) / range;
        return std::clamp(d, 0.0f, 1.0f);
    }
    
    void RenderWithLODBlending(GameObject* obj, Vec3 cameraPos) {
        float distance = Length(obj->position - cameraPos);
        
        // LOD 级别和过渡范围
        const float lod0Dist = 10.0f;
        const float lod1Dist = 30.0f;
        const float blendRange = 5.0f;
        
        if (distance < lod0Dist) {
            // 只渲染 LOD 0
            RenderMesh(obj->lod0Mesh);
        }
        else if (distance < lod0Dist + blendRange) {
            // LOD 0 → LOD 1 过渡
            float blend = GetBlendFactor(distance, lod0Dist, blendRange);
            
            // 渲染两个 LOD 并混合
            RenderMeshWithAlpha(obj->lod0Mesh, 1.0f - blend);
            RenderMeshWithAlpha(obj->lod1Mesh, blend);
        }
        else if (distance < lod1Dist) {
            // 只渲染 LOD 1
            RenderMesh(obj->lod1Mesh);
        }
        // ... 更多 LOD 级别
    }
};

// 视觉效果：
// 硬切换（Popping）:     平滑过渡：
// 距离 9m:  高精度        高精度 100%
// 距离 10m: 低精度 ← 突变 高精度 75% + 低精度 25%
// 距离 11m: 低精度        高精度 50% + 低精度 50%
// 距离 12m: 低精度        高精度 25% + 低精度 75%
// 距离 13m: 低精度        低精度 100%
//                         平滑无感
```

---

## 📚 总结

### 优化技术对比

```
┌────────────────────┬──────────┬──────────┬──────────┬────────────┐
│ 技术               │ 剔除率   │ 开销     │ 实现难度 │ 适用场景   │
├────────────────────┼──────────┼──────────┼──────────┼────────────┤
│ 视锥剔除           │ 60-80%   │ 极低     │ 低       │ 必须       │
│ 距离剔除           │ 20-40%   │ 极低     │ 极低     │ 大世界     │
│ 遮挡剔除（Hi-Z）   │ 30-60%   │ 中       │ 中       │ 城市场景   │
│ LOD                │ 90%+     │ 低       │ 低       │ 远处物体   │
│ 静态批处理         │ -        │ 中       │ 低       │ 静态场景   │
│ 动态批处理         │ -        │ 中       │ 中       │ 小物体     │
│ GPU Instancing     │ -        │ 低       │ 低       │ 相同物体   │
│ GPU Culling        │ 80%+     │ 低       │ 高       │ 海量物体   │
└────────────────────┴──────────┴──────────┴──────────┴────────────┘
```

### 优化流程建议

```
1. 基础优化（必做）
   □ 视锥剔除
   □ 距离剔除
   □ LOD 系统
   □ GPU Instancing（相同物体）

2. 进阶优化（根据场景）
   □ 遮挡剔除（城市、室内）
   □ 静态批处理（静态场景）
   □ 八叉树/BVH（复杂场景）

3. 高级优化（性能极限）
   □ GPU Culling
   □ Indirect Draw
   □ Compute Shader 预处理

4. 验证
   □ 测量 Draw Call 数量
   □ 测量渲染时间
   □ 对比优化前后 FPS
```

### 性能提升案例

```
场景：开放世界游戏（100,000 个物体）

优化前：
- Draw Call: 50,000
- 渲染时间: 100 ms
- FPS: 10

应用优化：
1. 视锥剔除 → Draw Call: 10,000 (80% 减少)
2. LOD → Draw Call: 3,000 (70% 减少)
3. GPU Instancing → Draw Call: 500 (83% 减少)
4. 遮挡剔除 → Draw Call: 200 (60% 减少)

优化后：
- Draw Call: 200
- 渲染时间: 3 ms
- FPS: 60+
- 性能提升：33 倍！
```

### 下一步

**Part 5C** 将学习实战技巧与调试方法（工具使用、常见问题、最佳实践）

准备好完成最后一部分了吗？🚀
