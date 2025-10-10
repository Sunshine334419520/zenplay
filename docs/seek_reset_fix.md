# Seek 后进度条重置为 0 的问题修复

## 问题描述

在执行 Seek 操作后，播放进度条会立即重置为 0，而不是显示 Seek 的目标位置。虽然播放会从目标位置开始，但进度条需要等待一小段时间才能更新到正确位置。

## 根本原因

问题出在 `AVSyncController::Reset()` 方法的设计上：

1. **时钟归零问题**：`Reset()` 将所有时钟的 `pts_ms` 设置为 0，导致 `GetCurrentTime()` 返回 0
2. **用途混淆**：`Reset()` 被用于两种不同场景：
   - Stop 操作：需要完全清空状态
   - Seek 操作：只需要重置同步状态，但不应该把当前播放位置归零

### GetCurrentTime() 的工作原理

```cpp
double ClockInfo::GetCurrentTime(std::chrono::steady_clock::time_point now) const {
  return pts_ms.load() + 
         std::chrono::duration<double, std::milli>(now - system_time).count() + 
         drift;
}
```

- `pts_ms`：最后一次更新时的 PTS（已归一化，相对于文件起始位置）
- `system_time`：最后一次更新的系统时间
- `drift`：时钟漂移校正

**计算逻辑**：当前时间 = 最后已知位置 + 从那时到现在经过的时间 + 漂移校正

### Seek 后的问题流程

1. **Seek 开始**：
   - 调用 `av_sync_controller_->Reset()`
   - 所有时钟的 `pts_ms` 被设置为 0
   - `system_time` 被设置为当前时间

2. **UI 查询时间**：
   - `GetCurrentPlayTime()` 调用 `GetMasterClock()`
   - `GetCurrentTime()` 计算：`0 + (now - system_time) + 0`
   - 由于 `system_time` 刚刚被设置为现在，所以返回接近 0

3. **等待解码器恢复**：
   - 需要等待 Seek 后的第一个音频/视频帧解码
   - `UpdateAudioClock()` 或 `UpdateVideoClock()` 被调用
   - `pts_ms` 被更新为目标位置附近的值
   - 此时 `GetCurrentTime()` 才开始返回正确值

## 解决方案

### 设计思路

分离两种 Reset 场景：

1. **完全重置**（Stop）：`Reset()` - 清空所有状态
2. **Seek 重置**：`ResetForSeek(target_pts_ms)` - 将时钟设置到目标位置

### 实现细节

#### 1. AVSyncController 添加新方法

```cpp
// av_sync_controller.h
void Reset();                          // Stop 时使用
void ResetForSeek(int64_t target_pts_ms);  // Seek 时使用
```

#### 2. ResetForSeek 实现

```cpp
void AVSyncController::ResetForSeek(int64_t target_pts_ms) {
  {
    std::lock_guard<std::mutex> lock(clock_mutex_);
    
    auto now = std::chrono::steady_clock::now();
    double target_ms = static_cast<double>(target_pts_ms);
    
    // ✅ 关键：设置时钟为目标位置，而不是 0
    audio_clock_.pts_ms.store(target_ms);
    audio_clock_.system_time = now;
    audio_clock_.drift = 0.0;

    video_clock_.pts_ms.store(target_ms);
    video_clock_.system_time = now;
    video_clock_.drift = 0.0;

    external_clock_.pts_ms.store(target_ms);
    external_clock_.system_time = now;
    external_clock_.drift = 0.0;

    // ✅ 更新 play_start_time_，使其偏移到目标位置
    // EXTERNAL_MASTER 模式下：now - play_start_time_ = target_ms
    // 所以：play_start_time_ = now - target_ms
    play_start_time_ = now - std::chrono::milliseconds(target_pts_ms);

    // ✅ 保持起始 PTS 基准不变
    // audio_start_pts_ms_ 和 video_start_pts_ms_ 不重置
    // 因为我们仍然使用相同的 normalization base
  }

  {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_ = SyncStats{};
    std::fill(sync_error_history_.begin(), sync_error_history_.end(), 0.0);
    sync_history_index_ = 0;
  }
}
```

#### 3. PlaybackController 调用新方法

```cpp
bool PlaybackController::ExecuteSeek(const SeekRequest& request) {
  // ... 前面的步骤 ...

  // === 步骤6: 重置同步控制器到目标位置 ===
  if (av_sync_controller_) {
    // ✅ 使用新的 ResetForSeek，传入目标位置
    av_sync_controller_->ResetForSeek(request.timestamp_ms);
  }

  // ... 后续步骤 ...
}
```

### 为什么保持 audio_start_pts_ms_ 不变？

假设场景：
- 文件第一个音频帧的原始 PTS = 1000ms
- `audio_start_pts_ms_` = 1000ms（用于归一化）
- Seek 到 5000ms

Seek 后：
- 下一个音频帧的原始 PTS ≈ 6000ms
- 归一化后：`6000 - 1000 = 5000ms`（正确！）

如果重置 `audio_start_pts_ms_` = 0：
- 下一个音频帧：`6000 - 0 = 6000ms`（错误！）

所以我们必须保持原始的 normalization base。

### play_start_time_ 的计算

`play_start_time_` 用于 EXTERNAL_MASTER 模式：

```cpp
case SyncMode::EXTERNAL_MASTER:
  return std::chrono::duration<double, std::milli>(now - play_start_time_).count();
```

为了让 `now - play_start_time_` 返回 `target_ms`：

```
play_start_time_ = now - target_ms
```

这样即使在 EXTERNAL_MASTER 模式下也能返回正确的播放位置。

## 测试验证

### 测试步骤

1. 打开一个视频文件
2. 播放一段时间
3. 拖动进度条 Seek 到任意位置
4. 观察进度条是否立即显示目标位置（而不是先跳到 0）

### 预期结果

- ✅ Seek 后进度条立即显示目标位置
- ✅ 进度条从目标位置开始递增
- ✅ 播放继续正常工作
- ✅ AV 同步保持正确

## 受影响的组件

### 直接修改

- `src/player/sync/av_sync_controller.h` - 添加 `ResetForSeek()` 声明
- `src/player/sync/av_sync_controller.cpp` - 实现 `ResetForSeek()`，修改 `Reset()` 回归完全重置
- `src/player/playback_controller.cpp` - 调用 `ResetForSeek()` 而不是 `Reset()`

### 间接影响

- `src/view/main_window.cpp` - `updatePlaybackProgress()` 现在能立即获取正确的播放时间
- 所有依赖 `GetCurrentPlayTime()` 的 UI 组件都会受益

## 设计原则

这个修复体现了以下设计原则：

1. **单一职责**：`Reset()` 只负责完全重置，`ResetForSeek()` 只负责 Seek 场景
2. **明确语义**：方法名清晰表达用途
3. **最小惊讶**：Seek 后时间应该在目标位置，而不是 0
4. **数据一致性**：保持 PTS normalization base 的一致性

## 相关文档

- [AV 同步架构设计](./audio_video_sync_design.md)
- [时间工具使用指南](./timer_util_guide.md)
- [完整播放器架构](./complete_player_architecture.md)
