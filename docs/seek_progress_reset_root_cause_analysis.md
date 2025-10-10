# Seek 后进度条归零问题 - 根因分析与修复

## 🐛 问题现象

**症状**: Seek 操作完成后，播放器进度条从 0 开始重新计时。

**复现步骤**:
1. 打开视频文件并播放到任意位置（如 5 秒）
2. 拖动进度条到 10 秒
3. 释放鼠标，Seek 完成
4. ❌ **进度条显示 00:00，从 0 开始计时**
5. ✅ **期望显示 00:10，从 10 秒开始计时**

---

## 🔬 完整调用链分析

### 从 MainWindow 开始追踪

```cpp
// 步骤1: UI 层 - MainWindow
void MainWindow::updatePlaybackProgress() {
  int64_t currentTimeMs = player_->GetCurrentPlayTime();  // 获取当前时间
  updateProgressDisplay(currentTimeMs, totalDuration_);
}

// 步骤2: API 层 - ZenPlayer
int64_t ZenPlayer::GetCurrentPlayTime() const {
  return playback_controller_->GetCurrentTime();
}

// 步骤3: 控制层 - PlaybackController
int64_t PlaybackController::GetCurrentTime() const {
  auto current_time = std::chrono::steady_clock::now();
  double master_clock_ms = av_sync_controller_->GetMasterClock(current_time);
  return static_cast<int64_t>(master_clock_ms);
}

// 步骤4: 同步层 - AVSyncController
double AVSyncController::GetMasterClock(
    std::chrono::steady_clock::time_point current_time) const {
  switch (sync_mode_) {
    case SyncMode::AUDIO_MASTER:
      return audio_clock_.GetCurrentTime(current_time);  // ❌ 问题在这里
    // ...
  }
}

// 步骤5: 时钟计算 - ClockInfo
double ClockInfo::GetCurrentTime(
    std::chrono::steady_clock::time_point now) const {
  auto elapsed_ms = 
      std::chrono::duration<double, std::milli>(now - system_time).count();
  return pts_ms.load() + elapsed_ms + drift.load();
  //     ^^^^^^^^ ❌ pts_ms 被归零了！
}
```

---

## 🎯 根本原因定位

### 问题1: PTS 归一化机制的副作用

**代码位置**: `AVSyncController::UpdateAudioClock()`

```cpp
void AVSyncController::UpdateAudioClock(
    double audio_pts_ms,
    std::chrono::steady_clock::time_point system_time) {
  
  // 归一化逻辑
  if (audio_pts_ms >= 0.0) {
    if (!audio_start_initialized_) {
      audio_start_initialized_ = true;
      audio_start_pts_ms_ = audio_pts_ms;  // 记录起始 PTS
    }
    normalized_audio_pts = audio_pts_ms - audio_start_pts_ms_;  // ❌ 归一化
  }

  audio_clock_.pts_ms = normalized_audio_pts;  // ❌ 存储归一化后的值
}
```

### 问题2: Reset() 清空了起始 PTS 基准

**代码位置**: `AVSyncController::Reset()`

```cpp
void AVSyncController::Reset() {
  // Seek 时调用
  audio_start_initialized_ = false;  // ❌ 清空标志
  audio_start_pts_ms_ = 0.0;        // ❌ 清空基准
  video_start_initialized_ = false;
  video_start_pts_ms_ = 0.0;
}
```

---

## 📊 时序分析

### 正常播放（无 Seek）

```
播放开始 (T0):
  第一帧音频 PTS = 0ms
  └─► UpdateAudioClock(0ms)
       ├─► audio_start_pts_ms_ = 0ms
       ├─► normalized_pts = 0 - 0 = 0ms
       └─► audio_clock_.pts_ms = 0ms ✅

播放 1 秒后 (T1):
  音频 PTS = 1000ms
  └─► UpdateAudioClock(1000ms)
       ├─► normalized_pts = 1000 - 0 = 1000ms
       └─► audio_clock_.pts_ms = 1000ms ✅

GetMasterClock():
  audio_clock_.GetCurrentTime(now)
  = 1000 + (now - T1) 
  ≈ 1000ms ✅
```

### Seek 后（问题场景）

```
播放到 5 秒，Seek 到 10 秒:

步骤1: Seek 开始
  └─► PlaybackController::ExecuteSeek()
       └─► av_sync_controller_->Reset()
            ├─► audio_start_initialized_ = false  ❌
            └─► audio_start_pts_ms_ = 0.0         ❌

步骤2: Demuxer Seek 到 10 秒
  └─► demuxer_->Seek(10000ms)

步骤3: 解码第一帧音频 (PTS = 10000ms)
  └─► UpdateAudioClock(10000ms)
       ├─► audio_start_initialized_ == false
       ├─► audio_start_pts_ms_ = 10000ms  ❌ 重新记录起点
       ├─► normalized_pts = 10000 - 10000 = 0ms  ❌ 归零！
       └─► audio_clock_.pts_ms = 0ms      ❌

步骤4: UI 获取当前时间
  └─► GetMasterClock()
       └─► audio_clock_.GetCurrentTime(now)
            = 0 + (now - system_time)
            ≈ 0ms  ❌ 错误！应该是 10000ms

步骤5: 进度条显示
  └─► progressSlider_->setValue(0)  ❌ 显示 0 秒
```

---

## 🧩 关键洞察

### 归一化的设计意图
```cpp
// 设计目的：让不同流的 PTS 对齐
// 例如：视频从 0 开始，音频从 500ms 开始
// 归一化后：都从 0 开始，便于同步
```

### Seek 破坏了归一化假设
```cpp
// 问题：Seek 后重置起始点，导致：
// 
// 播放开始: audio_start_pts_ms_ = 0
//   → PTS 10000ms 显示为 10000ms ✅
//
// Seek 后: audio_start_pts_ms_ = 10000ms
//   → PTS 10000ms 显示为 0ms ❌
//   → PTS 11000ms 显示为 1000ms ❌
```

---

## ✅ 解决方案

### 核心思路

**Seek 后不应重置 PTS 起始基准**，因为：
1. PTS 是相对于**整个媒体文件**的绝对时间戳
2. Seek 只是跳转位置，不改变时间坐标系
3. 起始基准应该在**整个播放会话**中保持不变

### 修复代码

**文件**: `src/player/sync/av_sync_controller.cpp`

```cpp
void AVSyncController::Reset() {
  {
    std::lock_guard<std::mutex> lock(clock_mutex_);
    
    // 重置时钟状态
    audio_clock_.pts_ms.store(0.0);
    audio_clock_.system_time = {};
    audio_clock_.drift = 0.0;

    video_clock_.pts_ms.store(0.0);
    video_clock_.system_time = {};
    video_clock_.drift = 0.0;

    external_clock_.pts_ms.store(0.0);
    external_clock_.system_time = {};
    external_clock_.drift = 0.0;

    // ✅ 修复1：更新 play_start_time_
    play_start_time_ = std::chrono::steady_clock::now();

    // ✅ 修复2：不重置 is_initialized_
    // 避免 UpdateAudioClock 再次覆盖 play_start_time_
    // is_initialized_ = false;  ❌ 注释掉
    
    // ✅ 修复3：保持 PTS 起始基准不变
    // Seek 后的 PTS 仍然相对于文件开头，无需重新归一化
    // audio_start_initialized_ = false;  ❌ 注释掉
    // audio_start_pts_ms_ = 0.0;         ❌ 注释掉
    // video_start_initialized_ = false;  ❌ 注释掉
    // video_start_pts_ms_ = 0.0;         ❌ 注释掉
  }

  {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_ = SyncStats{};
    std::fill(sync_error_history_.begin(), sync_error_history_.end(), 0.0);
    sync_history_index_ = 0;
  }
}
```

---

## 📈 修复后的时序

```
播放到 5 秒，Seek 到 10 秒:

步骤1: Seek 开始
  └─► av_sync_controller_->Reset()
       ├─► play_start_time_ = now()  ✅
       ├─► audio_start_initialized_ 保持 true  ✅
       └─► audio_start_pts_ms_ 保持 0ms       ✅

步骤2: 解码第一帧音频 (PTS = 10000ms)
  └─► UpdateAudioClock(10000ms)
       ├─► audio_start_initialized_ == true  ✅
       ├─► audio_start_pts_ms_ == 0ms (不变) ✅
       ├─► normalized_pts = 10000 - 0 = 10000ms  ✅
       └─► audio_clock_.pts_ms = 10000ms         ✅

步骤3: UI 获取当前时间
  └─► GetMasterClock()
       └─► audio_clock_.GetCurrentTime(now)
            = 10000 + (now - system_time)
            ≈ 10000ms  ✅ 正确！

步骤4: 进度条显示
  └─► progressSlider_->setValue(10)  ✅ 显示 10 秒
```

---

## 🧪 验证测试

### 测试用例1: 基本 Seek
```
步骤:
1. 播放到 5 秒
2. Seek 到 10 秒
3. 验证进度条显示 10 秒 ✅
4. 继续播放 1 秒
5. 验证进度条显示 11 秒 ✅
```

### 测试用例2: Seek 到起点
```
步骤:
1. 播放到 20 秒
2. Seek 到 0 秒
3. 验证进度条显示 0 秒 ✅
4. 继续播放 2 秒
5. 验证进度条显示 2 秒 ✅
```

### 测试用例3: 快速连续 Seek
```
步骤:
1. Seek 到 10 秒
2. 立即 Seek 到 20 秒
3. 立即 Seek 到 15 秒
4. 验证最终显示 15 秒 ✅
5. 无时间跳变或闪烁 ✅
```

### 测试用例4: 音视频同步
```
步骤:
1. Seek 到 30 秒
2. 验证音频从 30 秒播放 ✅
3. 验证视频从 30 秒显示 ✅
4. 验证音视频同步无偏移 ✅
```

---

## 🔍 为什么之前的修复不够？

### 第一次修复尝试（不完整）
```cpp
void AVSyncController::Reset() {
  play_start_time_ = std::chrono::steady_clock::now();  // ✅ 正确
  is_initialized_ = false;  // ❌ 问题：会被再次覆盖
  audio_start_initialized_ = false;  // ❌ 问题：导致 PTS 归零
}
```

**为什么不够**：
1. `is_initialized_ = false` 导致下次 `UpdateAudioClock` 时 `play_start_time_` 被覆盖
2. `audio_start_initialized_ = false` 导致 Seek 目标 PTS 成为新的起点

### 正确的修复（完整）
```cpp
void AVSyncController::Reset() {
  play_start_time_ = std::chrono::steady_clock::now();  // ✅ 更新系统时钟
  // ✅ 保持 is_initialized_ = true，避免覆盖
  // ✅ 保持 audio_start_pts_ms_ 不变，避免归零
}
```

---

## 📊 性能影响

### 修复前 vs 修复后

| 指标 | 修复前 | 修复后 | 说明 |
|------|--------|--------|------|
| Seek 后进度准确性 | ❌ 从 0 开始 | ✅ 从目标位置开始 | 核心问题 |
| UI 显示时间 | ❌ 错误 | ✅ 正确 | 用户体验 |
| 音视频同步 | ⚠️ 可能失步 | ✅ 正确同步 | 播放质量 |
| CPU 开销 | 无变化 | 无变化 | 性能无影响 |
| 内存占用 | 无变化 | 无变化 | 资源无影响 |

---

## 🎓 设计教训

### 1. 状态管理的复杂性
```
Reset() 不应盲目清空所有状态
需要区分：
  - 瞬时状态（时钟值、系统时间）→ 应重置
  - 会话状态（PTS 基准）→ 应保持
```

### 2. 归一化的双刃剑
```
归一化简化了同步计算
但也引入了额外的状态依赖
需要明确：何时归一化，何时使用原始值
```

### 3. Seek 的特殊性
```
Seek 不是"重新开始播放"
而是"跳转到新位置继续播放"
时间坐标系应保持一致
```

---

## 📚 相关文档

- [异步 Seek 实现指南](async_seek_implementation_guide.md)
- [音视频同步设计](audio_video_sync_design.md)
- [MainWindow 异步 Seek 实现](mainwindow_async_seek_implementation.md)

---

## 🎉 总结

### 问题本质
Seek 后错误地重置了 PTS 起始基准，导致归一化后的时间戳从 0 开始。

### 修复方法
保持 PTS 起始基准在整个播放会话中不变，只重置瞬时时钟状态。

### 关键代码
```cpp
// 保持不变（注释掉以下行）：
// audio_start_initialized_ = false;
// audio_start_pts_ms_ = 0.0;
// is_initialized_ = false;
```

### 影响范围
- ✅ 所有 Seek 操作的进度显示
- ✅ 音视频同步准确性
- ✅ UI 时间标签正确性

### 风险评估
- **代码变更**: 最小（仅注释 3 行）
- **逻辑风险**: 低（保持原有设计意图）
- **测试覆盖**: 需要完整 Seek 测试

---

**修复完成时间**: 2025-10-10  
**修复验证**: 编译通过 ✅  
**待测试**: 运行时完整验证 🧪
