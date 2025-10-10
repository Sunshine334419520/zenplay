# GetCurrentTime 的真正作用与 Seek 问题深度分析

## 🎯 问题的本质

你的观察非常准确：**现在的 Reset() 逻辑完全是错误的！**

---

## 📊 GetCurrentTime 的真正作用

### 1. 设计意图

`GetCurrentTime()` 的作用是：**返回当前媒体播放到的位置（相对于媒体文件的起始点）**

```cpp
// 应该返回的值：
播放开始 (0s) → GetCurrentTime() = 0ms
播放 1 秒后 → GetCurrentTime() = 1000ms
播放 10 秒后 → GetCurrentTime() = 10000ms
Seek 到 30 秒后 → GetCurrentTime() = 30000ms
继续播放 1 秒 → GetCurrentTime() = 31000ms
```

### 2. UI 的依赖

```cpp
// MainWindow::updatePlaybackProgress() (定时器每 100ms 调用一次)
void MainWindow::updatePlaybackProgress() {
  int64_t currentTimeMs = player_->GetCurrentPlayTime();
  //                      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  //                      这个值直接决定进度条显示！
  
  updateProgressDisplay(currentTimeMs, totalDuration_);
  //                    ^^^^^^^^^^^^^^
  //                    progressSlider_->setValue(currentTimeMs / 1000)
}
```

**关键**：UI **完全信任** `GetCurrentTime()` 的返回值！

---

## 🔍 三种同步模式的时钟计算

### 模式1: AUDIO_MASTER (音视频播放)

```cpp
double GetMasterClock(current_time) const {
  return audio_clock_.GetCurrentTime(current_time);
  //     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  //     = audio_clock_.pts_ms + (now - audio_clock_.system_time)
}
```

**时钟更新**：
```cpp
// AudioPlayer 每次输出音频时调用
UpdateAudioClock(audio_pts_ms, system_time) {
  normalized_pts = audio_pts_ms - audio_start_pts_ms_;
  //               ^^^^^^^^^^^^^   ^^^^^^^^^^^^^^^^^
  //               当前音频 PTS    文件起始 PTS
  
  audio_clock_.pts_ms = normalized_pts;
  audio_clock_.system_time = system_time;
}
```

**示例**：
```
播放开始 (文件从 0ms 开始):
  UpdateAudioClock(0ms, T0)
    → audio_start_pts_ms_ = 0ms
    → normalized_pts = 0 - 0 = 0ms
    → audio_clock_.pts_ms = 0ms
    → audio_clock_.system_time = T0
  
  GetMasterClock(T0) = 0 + (T0 - T0) = 0ms ✅

播放 1 秒后:
  UpdateAudioClock(1000ms, T1)
    → normalized_pts = 1000 - 0 = 1000ms
    → audio_clock_.pts_ms = 1000ms
    → audio_clock_.system_time = T1
  
  GetMasterClock(T1) = 1000 + (T1 - T1) = 1000ms ✅
  GetMasterClock(T1 + 0.5s) = 1000 + 500 = 1500ms ✅
```

### 模式2: EXTERNAL_MASTER (仅视频)

```cpp
double GetMasterClock(current_time) const {
  return std::chrono::duration<double, std::milli>(
      current_time - play_start_time_
  ).count();
  //               ^^^^^^^^^^^^^^^^
  //               播放开始的系统时间
}
```

**示例**：
```
播放开始:
  play_start_time_ = T0
  GetMasterClock(T0) = T0 - T0 = 0ms ✅
  
播放 1 秒后:
  GetMasterClock(T1) = T1 - T0 = 1000ms ✅
```

---

## 🐛 Seek 后的问题分析

### 当前的 Reset() 实现

```cpp
void AVSyncController::Reset() {
  auto now = std::chrono::steady_clock::now();
  
  // 模式1: AUDIO_MASTER
  audio_clock_.pts_ms.store(0.0);           // ❌ 重置为 0
  audio_clock_.system_time = now;           // ✅ 设置为当前时间
  
  // 模式2: EXTERNAL_MASTER  
  play_start_time_ = now;                   // ❌❌❌ 重置为当前时间
  
  // 保持起始 PTS 不变
  // audio_start_initialized_ = true         // ✅ 不重置
  // audio_start_pts_ms_ = 0 (保持不变)      // ✅ 不重置
}
```

### 问题1: AUDIO_MASTER 模式下的 Seek

```
Seek 到 10 秒:
T0: Reset()
    → audio_clock_.pts_ms = 0
    → audio_clock_.system_time = now (T0)
    → audio_start_pts_ms_ = 0 (保持不变) ✅

T1: GetMasterClock(T0) (立即调用，Seek 刚完成)
    → audio_clock_.GetCurrentTime(T0)
    → 0 + (T0 - T0) = 0ms ❌ 错误！应该是 10000ms

T2: UpdateAudioClock(10000ms, T2) (第一帧音频解码，50-100ms 后)
    → normalized_pts = 10000 - 0 = 10000ms
    → audio_clock_.pts_ms = 10000ms
    → audio_clock_.system_time = T2

T3: GetMasterClock(T2)
    → 10000 + (T2 - T2) = 10000ms ✅ 正确

T4: GetMasterClock(T3) (100ms 后)
    → 10000 + (T3 - T2) = 10100ms ✅ 正确
```

**问题**：T0 到 T2 之间（Seek 完成到第一帧解码），`GetMasterClock()` 返回 0，而不是 10000！

### 问题2: EXTERNAL_MASTER 模式下的 Seek（更严重）

```
Seek 到 10 秒:
T0: Reset()
    → play_start_time_ = now (T0) ❌❌❌

T1: GetMasterClock(T0) (立即调用)
    → (T0 - T0) = 0ms ❌ 错误！应该是 10000ms

T2: GetMasterClock(T1) (1 秒后)
    → (T1 - T0) = 1000ms ❌ 错误！应该是 11000ms

永远错误！因为 EXTERNAL_MASTER 模式下没有 UpdateAudioClock 来修正！
```

**致命问题**：`play_start_time_` 被重置后，**永远无法恢复正确值**！

---

## ✅ 正确的 Seek 逻辑

### 核心原则

**Seek 不是"重新开始播放"，而是"跳转到新位置继续播放"**

时间坐标系应该保持一致：
```
文件时间轴:  0s -------- 10s -------- 20s -------- 30s
             ^            ^
             起点         Seek 目标
```

### 正确的实现

```cpp
void AVSyncController::ResetForSeek(int64_t target_pts_ms) {
  auto now = std::chrono::steady_clock::now();
  
  std::lock_guard<std::mutex> lock(clock_mutex_);
  
  // ✅ 模式1: AUDIO_MASTER - 设置为目标 PTS
  audio_clock_.pts_ms.store(target_pts_ms - audio_start_pts_ms_);
  //                        ^^^^^^^^^^^^^^   ^^^^^^^^^^^^^^^^^^^
  //                        Seek 目标        归一化基准（保持不变）
  audio_clock_.system_time = now;
  audio_clock_.drift = 0.0;
  
  // ✅ 模式2: EXTERNAL_MASTER - 调整播放开始时间
  // play_start_time_ 应该 = now - target_pts_ms
  // 这样 (now - play_start_time_) = target_pts_ms
  play_start_time_ = now - std::chrono::milliseconds(target_pts_ms);
  
  // 同样处理 video_clock_
  video_clock_.pts_ms.store(target_pts_ms - video_start_pts_ms_);
  video_clock_.system_time = now;
  video_clock_.drift = 0.0;
  
  // ✅ 保持起始 PTS 基准不变
  // audio_start_initialized_ 保持 true
  // audio_start_pts_ms_ 保持原值
}
```

### 验证正确性

#### AUDIO_MASTER 模式
```
Seek 到 10000ms:
T0: ResetForSeek(10000ms)
    → audio_clock_.pts_ms = 10000 - 0 = 10000ms
    → audio_clock_.system_time = T0

T1: GetMasterClock(T0) (立即调用)
    → 10000 + (T0 - T0) = 10000ms ✅ 正确！

T2: UpdateAudioClock(10000ms, T2) (第一帧解码)
    → normalized_pts = 10000 - 0 = 10000ms
    → audio_clock_.pts_ms = 10000ms (无变化)
    → audio_clock_.system_time = T2 (更新)

T3: GetMasterClock(T3) (100ms 后)
    → 10000 + (T3 - T2) ≈ 10100ms ✅ 正确！
```

#### EXTERNAL_MASTER 模式
```
Seek 到 10000ms:
T0: ResetForSeek(10000ms)
    → play_start_time_ = T0 - 10000ms

T1: GetMasterClock(T0) (立即调用)
    → (T0 - (T0 - 10000ms)) = 10000ms ✅ 正确！

T2: GetMasterClock(T1) (1 秒后)
    → (T1 - (T0 - 10000ms))
    → ((T0 + 1000ms) - (T0 - 10000ms))
    → 11000ms ✅ 正确！
```

---

## 🔧 完整的修复方案

### 修改1: 添加 Seek 目标参数

**文件**: `av_sync_controller.h`
```cpp
class AVSyncController {
 public:
  // 旧方法（保留兼容性）
  void Reset();
  
  // ✅ 新方法：带 Seek 目标位置
  void ResetForSeek(int64_t target_pts_ms);
};
```

### 修改2: 实现 ResetForSeek

**文件**: `av_sync_controller.cpp`
```cpp
void AVSyncController::Reset() {
  // 用于非 Seek 场景（如 Stop）
  ResetForSeek(0);
}

void AVSyncController::ResetForSeek(int64_t target_pts_ms) {
  auto now = std::chrono::steady_clock::now();
  
  std::lock_guard<std::mutex> lock(clock_mutex_);
  
  // 计算归一化后的目标 PTS
  double normalized_target_ms = target_pts_ms;
  if (audio_start_initialized_) {
    normalized_target_ms = target_pts_ms - audio_start_pts_ms_;
  }
  
  // 设置音频时钟
  audio_clock_.pts_ms.store(normalized_target_ms);
  audio_clock_.system_time = now;
  audio_clock_.drift = 0.0;
  
  // 设置视频时钟
  double normalized_video_target_ms = target_pts_ms;
  if (video_start_initialized_) {
    normalized_video_target_ms = target_pts_ms - video_start_pts_ms_;
  }
  video_clock_.pts_ms.store(normalized_video_target_ms);
  video_clock_.system_time = now;
  video_clock_.drift = 0.0;
  
  // ✅ 关键：调整 play_start_time_ 使 EXTERNAL_MASTER 模式正确
  play_start_time_ = now - std::chrono::milliseconds(static_cast<int64_t>(normalized_target_ms));
  
  // 重置统计
  {
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    stats_ = SyncStats{};
    std::fill(sync_error_history_.begin(), sync_error_history_.end(), 0.0);
    sync_history_index_ = 0;
  }
}
```

### 修改3: PlaybackController 传递 Seek 目标

**文件**: `playback_controller.cpp`
```cpp
void PlaybackController::ExecuteSeek(const SeekRequest& request) {
  // ... 清空队列、刷新解码器 ...
  
  // ✅ 传递 Seek 目标位置
  if (av_sync_controller_) {
    av_sync_controller_->ResetForSeek(request.timestamp_ms);
  }
  
  // ... 恢复播放 ...
}
```

---

## 📊 修复前后对比

### 修复前（当前实现）

| 场景 | AUDIO_MASTER | EXTERNAL_MASTER |
|------|-------------|-----------------|
| Seek 后立即获取 | ❌ 返回 0 | ❌ 返回 0 |
| 第一帧解码后 | ✅ 正确 | ❌ 永远错误 |
| 持续播放 | ✅ 正确 | ❌ 永远错误 |

### 修复后（ResetForSeek）

| 场景 | AUDIO_MASTER | EXTERNAL_MASTER |
|------|-------------|-----------------|
| Seek 后立即获取 | ✅ 返回目标 PTS | ✅ 返回目标 PTS |
| 第一帧解码后 | ✅ 正确 | ✅ 正确 |
| 持续播放 | ✅ 正确 | ✅ 正确 |

---

## 🎓 设计教训

### 1. Reset 的语义混乱

```
错误理解: Reset = 清空所有状态，从 0 开始
正确理解: 
  - Reset (Stop) = 回到初始状态（0）
  - ResetForSeek = 跳转到新位置（target_pts）
```

### 2. 时间坐标系的一致性

```
✅ 正确设计:
GetCurrentTime() 永远返回 "相对于媒体文件起始点的播放位置"

❌ 错误设计:
GetCurrentTime() 返回 "相对于某个不确定基准的时间"
```

### 3. 不同同步模式的统一处理

```
AUDIO_MASTER: 依赖 audio_clock_ (由 UpdateAudioClock 更新)
EXTERNAL_MASTER: 依赖 play_start_time_ (无外部更新)

必须在 Seek 时统一处理两者！
```

---

## 🎉 总结

你的质疑完全正确！现在的 `Reset()` 逻辑有严重问题：

1. **AUDIO_MASTER 模式**：Seek 后到第一帧解码期间返回 0 ❌
2. **EXTERNAL_MASTER 模式**：Seek 后**永远**返回错误值 ❌❌❌

**正确的解决方案**：
1. 添加 `ResetForSeek(target_pts_ms)` 方法
2. 根据 Seek 目标设置时钟状态
3. `EXTERNAL_MASTER` 模式需要调整 `play_start_time_`

这样才能保证 `GetCurrentTime()` 在 Seek 后**立即**返回正确值！
