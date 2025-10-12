# 🔄 AVSyncController 统计迁移到 StatisticsManager

## 📋 **迁移概述**

将 `AVSyncController` 中独立的同步统计系统迁移到统一的 `StatisticsManager`，实现全局统计管理的一致性。

---

## 🎯 **迁移目标**

### **迁移前**
- `AVSyncController` 有独立的 `SyncStats` 结构
- `GetSyncStats()` 方法返回内部统计副本
- 统计数据分散，难以统一管理

### **迁移后**
- 移除 `AVSyncController` 的 `SyncStats` 结构
- 扩展 `StatisticsManager` 的 `SyncQualityStats` 结构
- 通过 `STATS_UPDATE_CLOCK` 宏更新统计
- 统一的统计数据访问接口

---

## 🔧 **具体修改**

### **1. 扩展 `SyncQualityStats` 结构**

**文件**: `src/player/stats/stats_types.h`

```cpp
struct SyncQualityStats {
  // ✅ 新增：时钟信息
  std::atomic<double> audio_clock_ms{0.0};  // 音频时钟(毫秒)
  std::atomic<double> video_clock_ms{0.0};  // 视频时钟(毫秒)
  
  // 同步偏移
  std::atomic<double> av_sync_offset_ms{0.0};   // 音视频同步偏移(毫秒)
  std::atomic<double> avg_sync_offset_ms{0.0};  // 平均同步偏移
  std::atomic<double> max_sync_error_ms{0.0};   // ✅ 新增：最大同步误差
  std::atomic<double> sync_jitter_ms{0.0};      // 同步抖动
  
  // 同步校正
  std::atomic<uint64_t> sync_corrections{0};    // 同步修正次数
  std::atomic<bool> is_in_sync{true};           // 是否同步正常
  std::string sync_quality = "Good";            // 同步质量评级
  
  // ...
};
```

---

### **2. 新增 `UpdateClockStats` 方法**

**文件**: `src/player/stats/statistics_manager.h`

```cpp
void UpdateClockStats(double audio_clock_ms,
                      double video_clock_ms,
                      double sync_offset_ms,
                      double avg_sync_error_ms,
                      double max_sync_error_ms,
                      int64_t sync_corrections);
```

**实现**: `src/player/stats/statistics_manager.cpp`

```cpp
void StatisticsManager::UpdateClockStats(double audio_clock_ms,
                                         double video_clock_ms,
                                         double sync_offset_ms,
                                         double avg_sync_error_ms,
                                         double max_sync_error_ms,
                                         int64_t sync_corrections) {
  if (!global_enabled_.load() || !config_.enabled) {
    return;
  }

  std::lock_guard<std::mutex> lock(stats_mutex_);
  auto& sync = sync_stats_;

  // 更新时钟信息
  sync.audio_clock_ms.store(audio_clock_ms);
  sync.video_clock_ms.store(video_clock_ms);
  
  // 更新同步信息
  sync.av_sync_offset_ms.store(sync_offset_ms);
  sync.avg_sync_offset_ms.store(avg_sync_error_ms);
  sync.max_sync_error_ms.store(max_sync_error_ms);
  sync.sync_corrections.store(sync_corrections);
  
  // 更新同步状态和质量评级
  bool in_sync = std::abs(sync_offset_ms) < 40.0;
  sync.is_in_sync.store(in_sync);
  
  double abs_offset = std::abs(sync_offset_ms);
  if (abs_offset < 20.0) {
    sync.sync_quality = "Excellent";
  } else if (abs_offset < 40.0) {
    sync.sync_quality = "Good";
  } else if (abs_offset < 80.0) {
    sync.sync_quality = "Fair";
  } else {
    sync.sync_quality = "Poor";
  }
}
```

---

### **3. 新增便捷宏**

**文件**: `src/player/stats/statistics_manager.h`

```cpp
#define STATS_UPDATE_CLOCK(audio_clock, video_clock, sync_offset, avg_error, \
                           max_error, corrections)                            \
  do {                                                                        \
    if (zenplay::stats::StatisticsManager::IsGlobalEnabled()) {               \
      auto* manager = zenplay::stats::StatisticsManager::GetInstance();       \
      if (manager)                                                            \
        manager->UpdateClockStats(audio_clock, video_clock, sync_offset,      \
                                  avg_error, max_error, corrections);         \
    }                                                                         \
  } while (0)
```

---

### **4. 修改 `AVSyncController`**

**文件**: `src/player/sync/av_sync_controller.h`

#### **移除**：
```cpp
// ❌ 删除 SyncStats 结构定义
struct SyncStats { ... };

// ❌ 删除 GetSyncStats() 方法
SyncStats GetSyncStats() const;

// ❌ 删除内部成员
mutable std::mutex stats_mutex_;
SyncStats stats_;
```

#### **新增**：
```cpp
// ✅ 引入 StatisticsManager
#include "player/stats/statistics_manager.h"

// ✅ 保留统计历史用于计算平均值
mutable std::mutex stats_mutex_;
std::vector<double> sync_error_history_;
size_t sync_history_index_;
int64_t sync_corrections_{0};  // 同步校正次数
```

---

**文件**: `src/player/sync/av_sync_controller.cpp`

#### **修改 `UpdateSyncStats()`**：
```cpp
void AVSyncController::UpdateSyncStats() {
  std::lock_guard<std::mutex> lock(stats_mutex_);

  // 获取时钟数据
  double audio_clock_ms = audio_clock_.pts_ms.load();
  double video_clock_ms = video_clock_.pts_ms.load();
  double sync_offset_ms = video_clock_ms - audio_clock_ms;

  // 更新同步误差历史
  sync_error_history_[sync_history_index_] = std::abs(sync_offset_ms);
  sync_history_index_ = (sync_history_index_ + 1) % SYNC_HISTORY_SIZE;

  // 计算平均和最大误差
  double avg_sync_error_ms = std::accumulate(sync_error_history_.begin(),
                                             sync_error_history_.end(), 0.0) /
                             SYNC_HISTORY_SIZE;
  double max_sync_error_ms =
      *std::max_element(sync_error_history_.begin(), sync_error_history_.end());

  // 检查是否需要同步校正
  if (std::abs(sync_offset_ms) > sync_params_.sync_threshold_ms) {
    sync_corrections_++;
  }

  // ✅ 更新到 StatisticsManager
  STATS_UPDATE_CLOCK(audio_clock_ms, video_clock_ms, sync_offset_ms,
                     avg_sync_error_ms, max_sync_error_ms, sync_corrections_);
}
```

#### **移除**：
```cpp
// ❌ 删除 GetSyncStats() 方法实现
AVSyncController::SyncStats AVSyncController::GetSyncStats() const {
  std::lock_guard<std::mutex> lock(stats_mutex_);
  return stats_;
}
```

#### **修改 `Reset()` 和 `ResetForSeek()`**：
```cpp
// ❌ 移除
stats_ = SyncStats{};

// ✅ 改为
sync_corrections_ = 0;
```

---

### **5. 修改调用方代码**

**文件**: `src/player/playback_controller.cpp`

#### **修改前**：
```cpp
auto stats = av_sync_controller_->GetSyncStats();
if (std::abs(stats.sync_offset_ms) > 100.0) {
  // ...
}
```

#### **修改后**：
```cpp
auto* stats_manager = stats::StatisticsManager::GetInstance();
if (stats_manager) {
  auto& sync_stats = stats_manager->GetSyncStats();
  double sync_offset_ms = sync_stats.av_sync_offset_ms.load();
  
  if (std::abs(sync_offset_ms) > 100.0) {
    // ...
  }
}
```

---

## 📊 **统计数据对比**

| **字段** | **迁移前（AVSyncController::SyncStats）** | **迁移后（SyncQualityStats）** |
|---------|--------------------------------------|------------------------------|
| 音频时钟 | `audio_clock_ms` | `audio_clock_ms` ✅ |
| 视频时钟 | `video_clock_ms` | `video_clock_ms` ✅ |
| 同步偏移 | `sync_offset_ms` | `av_sync_offset_ms` ✅ |
| 平均误差 | `avg_sync_error_ms` | `avg_sync_offset_ms` ✅ |
| 最大误差 | `max_sync_error_ms` | `max_sync_error_ms` ✅ |
| 同步校正次数 | `sync_corrections` | `sync_corrections` ✅ |
| 同步质量 | `sync_quality()` 方法 | `sync_quality` 字符串 ✅ |
| 同步状态 | `is_in_sync()` 方法 | `is_in_sync` 原子变量 ✅ |

---

## ✅ **迁移优势**

### **1. 统一管理**
- 所有统计数据通过 `StatisticsManager` 集中管理
- 便于全局监控和报告生成

### **2. 一致的接口**
- 使用 `STATS_UPDATE_*` 宏系列统一更新
- 使用 `GetSyncStats()` 统一访问

### **3. 更好的扩展性**
- `SyncQualityStats` 可以添加更多同步质量相关的指标
- 支持自动日志输出和瓶颈检测

### **4. 减少代码重复**
- 移除了 `AVSyncController` 中的统计管理逻辑
- 同步质量评级逻辑集中在 `StatisticsManager` 中

---

## 🔍 **使用示例**

### **更新同步时钟（自动调用）**
```cpp
// AVSyncController 内部
void AVSyncController::UpdateAudioClock(double audio_pts_ms, ...) {
  // ... 更新时钟逻辑 ...
  
  UpdateSyncStats();  // 自动更新到 StatisticsManager
}
```

### **获取同步统计**
```cpp
// 任何地方
auto* stats_manager = stats::StatisticsManager::GetInstance();
if (stats_manager) {
  auto& sync_stats = stats_manager->GetSyncStats();
  
  double audio_clock = sync_stats.audio_clock_ms.load();
  double video_clock = sync_stats.video_clock_ms.load();
  double offset = sync_stats.av_sync_offset_ms.load();
  bool in_sync = sync_stats.is_in_sync.load();
  std::string quality = sync_stats.sync_quality;
  
  // 使用统计数据...
}
```

### **生成统计报告**
```cpp
auto* stats_manager = stats::StatisticsManager::GetInstance();
if (stats_manager) {
  std::string report = stats_manager->GenerateReport();
  // 报告中包含完整的同步质量信息
}
```

---

## 🐛 **迁移注意事项**

1. **线程安全**：`SyncQualityStats` 使用 `std::atomic`，无需额外锁
2. **性能影响**：`UpdateSyncStats()` 仍在时钟更新时调用，频率不变
3. **兼容性**：保留了 `UpdateSyncStats()` 私有方法，内部逻辑不变
4. **质量评级**：从方法调用改为直接读取字符串，更高效

---

## 📝 **总结**

✅ 成功将 `AVSyncController` 的统计系统迁移到 `StatisticsManager`  
✅ 保持了原有的同步误差计算逻辑（滑动窗口平均、最大值）  
✅ 实现了统一的统计数据访问接口  
✅ 编译通过，无错误  

这次迁移增强了项目的统计系统架构，为后续的性能监控和瓶颈分析提供了更好的基础。
