# Pause/Resume Bug 修复报告

## 修复日期
2025-10-13

## 问题总结

1. ✅ `GetAccumulatedPauseDuration()` 函数未被使用
2. ✅ `PlaybackController` 中的 Pause/Resume 调用顺序不正确
3. ✅ `VideoPlayer` 重复管理暂停时长（与 `AVSyncController` 冲突）

---

## 修复内容

### 1. 删除未使用的 `GetAccumulatedPauseDuration()` 函数

#### 文件：`src/player/sync/av_sync_controller.h`

**修改前**：
```cpp
double GetAccumulatedPauseDuration(
    std::chrono::steady_clock::time_point current_time = 
        std::chrono::steady_clock::now()) const {
  auto total_duration = accumulated_pause_duration_;
  if (is_paused_) {
    total_duration += (current_time - pause_start_time_);
  }
  return std::chrono::duration<double, std::milli>(total_duration).count();
}
```

**修改后**：
```cpp
// 函数已删除
// accumulated_pause_duration_ 成员变量保留，用于 Resume() 中的日志记录
```

**原因**：
- 搜索整个代码库，该函数没有任何调用
- 暂停时长管理已经通过 `Resume()` 中的时钟调整完成
- 保留 `accumulated_pause_duration_` 成员变量仅用于日志统计

---

### 2. 修复 `PlaybackController` 的 Pause/Resume 调用顺序

#### 文件：`src/player/playback_controller.cpp`

#### 修改前（错误的顺序）：

```cpp
void PlaybackController::Pause() {
  // ❌ 错误：先暂停时钟，后暂停播放器
  av_sync_controller_->Pause();  // 记录 pause_start_time_
  audio_player_->Pause();        // 停止音频（可能有延迟）
  video_player_->Pause();
}

void PlaybackController::Resume() {
  // ❌ 错误：先恢复播放器，后恢复时钟
  audio_player_->Resume();       // 启动音频回调
  video_player_->Resume();       // 唤醒渲染线程
  av_sync_controller_->Resume(); // 调整时钟
}
```

**问题**：
1. **Pause 时**：在 `audio_player_->Pause()` 期间，音频回调可能还在运行并调用 `UpdateAudioClock()`，而此时 `pause_start_time_` 已经被记录，可能导致时间计算错误。
2. **Resume 时**：播放器先启动，可能立即调用 `UpdateClock()`，但此时时钟的 `system_time` 还没有被调整，导致时钟计算错误。

---

#### 修改后（正确的顺序）：

```cpp
void PlaybackController::Pause() {
  MODULE_INFO(LOG_MODULE_PLAYER, "Pausing PlaybackController");

  // ✅ 步骤 1：先暂停音视频播放（停止数据流）
  if (audio_player_) {
    audio_player_->Pause();  // 停止音频输出，音频回调停止
  }
  if (video_player_) {
    video_player_->Pause();  // VideoPlayer 通过 state_manager 停止渲染
  }

  // ✅ 步骤 2：再暂停同步控制器（记录暂停时间点）
  // 此时音视频已经停止，不会再调用 UpdateClock
  if (av_sync_controller_) {
    av_sync_controller_->Pause();
  }
}

void PlaybackController::Resume() {
  MODULE_INFO(LOG_MODULE_PLAYER, "Resuming PlaybackController");

  // ✅ 步骤 1：先恢复同步控制器（调整时钟基准）
  // 确保播放器启动后，UpdateClock 使用的是调整后的 system_time
  if (av_sync_controller_) {
    av_sync_controller_->Resume();
  }

  // ✅ 步骤 2：再恢复音视频播放（开始数据流）
  // 此时时钟已经调整好，UpdateClock 会使用正确的 system_time
  if (audio_player_) {
    audio_player_->Resume();  // 启动音频输出，恢复音频回调
  }
  if (video_player_) {
    video_player_->Resume();  // 唤醒渲染线程
  }
}
```

**正确性保证**：

**Pause 流程**：
```
1. audio_player_->Pause() → audio_client_->Stop() → 音频回调停止
2. video_player_->Pause() → state 变为 kPaused → 渲染线程进入 WaitForResume()
3. av_sync_controller_->Pause() → 记录 pause_start_time_
   此时已经没有线程会调用 UpdateClock()，安全！
```

**Resume 流程**：
```
1. av_sync_controller_->Resume()
   → audio_clock_.system_time += pause_duration
   → video_clock_.system_time += pause_duration
   时钟基准已调整！
   
2. audio_player_->Resume() → audio_client_->Start()
   → 音频回调开始 → UpdateAudioClock(pts, now)
   → GetCurrentTime(now) = pts + (now - system_time_adjusted) + drift
   使用调整后的 system_time，计算正确！
   
3. video_player_->Resume() → frame_available_.notify_all()
   → 渲染线程醒来 → UpdateVideoClock(pts, now)
   → GetCurrentTime(now) = pts + (now - system_time_adjusted) + drift
   使用调整后的 system_time，计算正确！
```

---

### 3. 删除 `VideoPlayer` 的冗余暂停时长管理

#### 文件：`src/player/video/video_player.h`

**删除的成员变量**：
```cpp
// 删除这些
mutable std::mutex pause_mutex_;
std::chrono::steady_clock::duration accumulated_pause_duration_;
std::chrono::steady_clock::time_point pause_start_time_;
```

**原因**：
- `VideoPlayer` 的暂停时长管理与 `AVSyncController` 重复
- `accumulated_pause_duration_` 从未被真正使用（只是记录但不参与计算）
- 暂停由 `PlayerStateManager` 统一管理（通过 `ShouldPause()` 和 `WaitForResume()`）
- 暂停时长由 `AVSyncController` 统一管理

---

#### 文件：`src/player/video/video_player.cpp`

**修改前**：
```cpp
bool VideoPlayer::Start() {
  play_start_time_ = std::chrono::steady_clock::now();
  {
    std::lock_guard<std::mutex> lock(pause_mutex_);
    accumulated_pause_duration_ = std::chrono::steady_clock::duration::zero();
    pause_start_time_ = play_start_time_;
  }
  // ...
}

void VideoPlayer::Stop() {
  if (state_manager_->GetState() == PlayerStateManager::PlayerState::kPaused) {
    auto stop_time = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(pause_mutex_);
    accumulated_pause_duration_ += stop_time - pause_start_time_;
  }
  // ...
  {
    std::lock_guard<std::mutex> lock(pause_mutex_);
    accumulated_pause_duration_ = std::chrono::steady_clock::duration::zero();
    pause_start_time_ = std::chrono::steady_clock::time_point{};
  }
}

void VideoPlayer::Pause() {
  std::lock_guard<std::mutex> lock(pause_mutex_);
  pause_start_time_ = std::chrono::steady_clock::now();
  MODULE_INFO(LOG_MODULE_VIDEO, "VideoPlayer paused");
}

void VideoPlayer::Resume() {
  auto resume_time = std::chrono::steady_clock::now();
  {
    std::lock_guard<std::mutex> lock(pause_mutex_);
    accumulated_pause_duration_ += resume_time - pause_start_time_;
  }
  MODULE_INFO(LOG_MODULE_VIDEO, "VideoPlayer resumed");
  frame_available_.notify_all();
}

double VideoPlayer::GetEffectiveElapsedTime(...) const {
  auto elapsed_time = current_time - play_start_time_;
  
  std::chrono::steady_clock::duration paused_duration_snapshot;
  {
    std::lock_guard<std::mutex> lock(pause_mutex_);
    paused_duration_snapshot = accumulated_pause_duration_;
  }
  
  if (state_manager_->GetState() == PlayerStateManager::PlayerState::kPaused) {
    paused_duration_snapshot += current_time - pause_start_time_;
  }
  
  auto effective_elapsed = elapsed_time - paused_duration_snapshot;
  return std::chrono::duration<double, std::milli>(effective_elapsed).count();
}
```

**修改后**：
```cpp
bool VideoPlayer::Start() {
  MODULE_INFO(LOG_MODULE_VIDEO, "VideoPlayer Start called");

  // 记录播放开始时间
  play_start_time_ = std::chrono::steady_clock::now();

  // 启动视频渲染线程
  render_thread_ =
      std::make_unique<std::thread>(&VideoPlayer::VideoRenderThread, this);

  MODULE_INFO(LOG_MODULE_VIDEO, "VideoPlayer started");
  return true;
}

void VideoPlayer::Stop() {
  MODULE_INFO(LOG_MODULE_VIDEO, "VideoPlayer Stop called");

  // 通知可能在等待的线程
  frame_available_.notify_all();

  // 等待渲染线程结束
  if (render_thread_ && render_thread_->joinable()) {
    render_thread_->join();
    render_thread_.reset();
  }

  // 清空队列
  ClearFrames();

  MODULE_INFO(LOG_MODULE_VIDEO, "VideoPlayer stopped");
}

void VideoPlayer::Pause() {
  // 暂停由 PlayerStateManager 统一管理
  // VideoRenderThread 会通过 ShouldPause() 和 WaitForResume() 自动暂停
  MODULE_INFO(LOG_MODULE_VIDEO, "VideoPlayer paused");
}

void VideoPlayer::Resume() {
  // 唤醒可能在 WaitForResume() 中阻塞的渲染线程
  frame_available_.notify_all();
  MODULE_INFO(LOG_MODULE_VIDEO, "VideoPlayer resumed");
}

double VideoPlayer::GetEffectiveElapsedTime(
    std::chrono::steady_clock::time_point current_time) const {
  // 此函数已废弃，应该使用 AVSyncController 的 EXTERNAL_MASTER 模式
  // 保留此函数仅为向后兼容，实际应该始终有 av_sync_controller_
  
  if (av_sync_controller_) {
    // 使用同步控制器的主时钟（会自动排除暂停时间）
    return av_sync_controller_->GetMasterClock(current_time);
  }
  
  // 后备方案：简单计算播放时长（不考虑暂停，已废弃）
  auto elapsed_time = current_time - play_start_time_;
  return std::chrono::duration<double, std::milli>(elapsed_time).count();
}
```

**关键改进**：
1. **简化 Pause/Resume**：不再自己管理暂停时长，依赖 `PlayerStateManager` 和 `AVSyncController`
2. **简化 Start/Stop**：移除暂停时长的初始化和清理代码
3. **修复 GetEffectiveElapsedTime**：直接使用 `AVSyncController::GetMasterClock()`，自动排除暂停时间

---

## 架构说明

### 暂停管理的职责分离

经过此次修复，暂停管理的职责明确分离：

| 组件 | 职责 | 机制 |
|------|------|------|
| **PlayerStateManager** | 播放状态管理 | `kPlaying`, `kPaused`, `kStopped` 等状态 |
| **AVSyncController** | 时钟同步和暂停时长 | 调整 `system_time`，排除暂停时间 |
| **AudioPlayer** | 音频播放控制 | `audio_output_->Pause/Resume()` |
| **VideoPlayer** | 视频渲染控制 | `WaitForResume()` 阻塞渲染线程 |
| **PlaybackController** | 协调所有组件 | 按正确顺序调用各组件的 Pause/Resume |

### 暂停/恢复的完整流程

```
用户点击暂停按钮
  ↓
MainWindow::OnPauseClicked()
  ↓
PlayerStateManager::SetState(kPaused)
  ↓
PlaybackController::Pause()
  ├─ AudioPlayer::Pause() → 停止音频回调
  ├─ VideoPlayer::Pause() → 日志记录（渲染线程会自己检测状态并暂停）
  └─ AVSyncController::Pause() → 记录 pause_start_time_
  
渲染线程和音频回调停止
  ↓
  
用户点击恢复按钮
  ↓
MainWindow::OnResumeClicked()
  ↓
PlayerStateManager::SetState(kPlaying)
  ↓
PlaybackController::Resume()
  ├─ AVSyncController::Resume()
  │   ├─ 计算 pause_duration
  │   ├─ accumulated_pause_duration_ += pause_duration
  │   ├─ audio_clock_.system_time += pause_duration
  │   ├─ video_clock_.system_time += pause_duration
  │   └─ play_start_time_ += pause_duration
  ├─ AudioPlayer::Resume() → 启动音频回调
  └─ VideoPlayer::Resume() → 唤醒渲染线程
  
音频回调和渲染线程恢复
  ↓
UpdateAudioClock/UpdateVideoClock 使用调整后的 system_time
  ↓
时钟推算自动正确！
```

---

## 测试验证

### 推荐测试用例

1. **基本暂停/恢复**
   - 播放视频 5 秒
   - 暂停 3 秒
   - 恢复播放
   - ✅ 验证：画面和声音从暂停处继续，音画同步正常

2. **快速连续暂停/恢复**
   - 播放视频
   - 10 次循环：暂停 100ms → 恢复 100ms
   - ✅ 验证：播放流畅，无卡顿或音画不同步

3. **暂停期间 Seek**
   - 播放到 5 秒
   - 暂停
   - Seek 到 10 秒
   - 恢复播放
   - ✅ 验证：从 10 秒位置正确播放

4. **长时间暂停**
   - 播放 1 分钟
   - 暂停 10 分钟
   - 恢复播放
   - ✅ 验证：音画同步正常，无时间漂移

---

## 修复总结

### 删除的代码
- ✅ `AVSyncController::GetAccumulatedPauseDuration()` - 未使用的函数
- ✅ `VideoPlayer::pause_mutex_` - 冗余的互斥锁
- ✅ `VideoPlayer::accumulated_pause_duration_` - 冗余的暂停时长
- ✅ `VideoPlayer::pause_start_time_` - 冗余的暂停开始时间
- ✅ `VideoPlayer` 中所有暂停时长计算逻辑

### 修改的代码
- ✅ `PlaybackController::Pause()` - 调整调用顺序（先停播放，后停时钟）
- ✅ `PlaybackController::Resume()` - 调整调用顺序（先启时钟，后启播放）
- ✅ `VideoPlayer::GetEffectiveElapsedTime()` - 使用 `AVSyncController`

### 架构改进
- ✅ 职责分离更清晰：状态管理、时钟同步、播放控制各司其职
- ✅ 消除重复代码：暂停时长由 `AVSyncController` 统一管理
- ✅ 提高可维护性：暂停逻辑集中，易于理解和修改
- ✅ 修复潜在 Bug：正确的调用顺序避免了竞态条件

---

**修复日期**: 2025-10-13  
**修复者**: GitHub Copilot  
**相关文档**: 
- `pause_resume_bug_analysis.md` - Bug 分析
- `pause_resume_optimization.md` - 性能优化说明
- `pause_resume_sync_design.md` - 原始设计文档
