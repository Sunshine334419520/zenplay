# ZenPlay 统一状态管理设计文档

## 📋 目录
1. [问题分析](#问题分析)
2. [设计方案](#设计方案)
3. [使用指南](#使用指南)
4. [迁移步骤](#迁移步骤)
5. [最佳实践](#最佳实践)

---

## 🔍 问题分析

### 当前问题

**状态分散在多个组件**：
```cpp
ZenPlayer:          state_ (enum)
PlaybackController: is_playing_, is_paused_, should_stop_
VideoPlayer:        is_playing_, is_paused_, should_stop_
AudioPlayer:        is_playing_, is_paused_, should_stop_
```

**导致的问题**：
1. ❌ 状态不一致：各组件状态可能不同步
2. ❌ 重复代码：每个组件都实现相同的状态管理逻辑
3. ❌ 难以维护：状态变更需要修改多处
4. ❌ 扩展困难：添加新状态（Seeking, Buffering）需要改动所有组件
5. ❌ 调试困难：状态转换路径不清晰

---

## 🎯 设计方案

### 架构概览

```
┌─────────────────────────────────────────────────┐
│              ZenPlayer (API层)                   │
│  - Open/Play/Pause/Stop/Seek 等用户接口          │
└──────────────────┬──────────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────────┐
│         PlayerStateManager (状态中心)            │
│                                                  │
│  职责：                                          │
│  ✓ 维护唯一的全局状态                            │
│  ✓ 控制状态转换合法性                            │
│  ✓ 通知状态变更（观察者模式）                     │
│  ✓ 提供线程安全的状态查询                        │
│                                                  │
│  状态：                                          │
│  - Idle      (空闲)                              │
│  - Opening   (正在打开)                          │
│  - Stopped   (已停止)                            │
│  - Playing   (正在播放)                          │
│  - Paused    (已暂停)                            │
│  - Seeking   (正在跳转)                          │
│  - Buffering (缓冲中)                            │
│  - Error     (错误)                              │
└──────────────────┬──────────────────────────────┘
                   │
         ┌─────────┼─────────┬─────────────┐
         │         │         │             │
         ▼         ▼         ▼             ▼
  ┌───────────┐ ┌────────┐ ┌──────────┐ ┌──────────┐
  │ Playback  │ │ Video  │ │  Audio   │ │  Demux   │
  │Controller │ │ Player │ │  Player  │ │  Thread  │
  └───────────┘ └────────┘ └──────────┘ └──────────┘
       监听状态，响应变更
```

### 核心设计原则

1. **Single Source of Truth（单一数据源）**
   - 只有 `PlayerStateManager` 维护状态
   - 其他组件通过查询接口获取状态

2. **状态机模式**
   - 明确定义状态转换规则
   - 非法转换会被拒绝

3. **观察者模式**
   - 组件注册回调监听状态变更
   - 解耦状态管理和业务逻辑

4. **线程安全**
   - 使用 `std::atomic` 和 `mutex` 保证并发安全
   - 提供阻塞式等待接口

---

## 📖 使用指南

### 1. 初始化

```cpp
// 在 ZenPlayer 中创建状态管理器
class ZenPlayer {
 public:
  ZenPlayer() 
      : state_manager_(std::make_shared<PlayerStateManager>()) {
    // 注册状态变更回调
    state_manager_->RegisterStateChangeCallback(
        [this](auto old_state, auto new_state) {
          OnStateChanged(old_state, new_state);
        });
  }

 private:
  std::shared_ptr<PlayerStateManager> state_manager_;
};
```

### 2. 状态转换

```cpp
// ZenPlayer::Play()
bool ZenPlayer::Play() {
  // 检查当前状态
  if (!state_manager_->IsStopped() && !state_manager_->IsPaused()) {
    MODULE_ERROR(LOG_MODULE_PLAYER, "Cannot play in current state: {}",
                 PlayerStateManager::GetStateName(state_manager_->GetState()));
    return false;
  }

  // 请求状态转换
  if (!state_manager_->TransitionToPlaying()) {
    return false;
  }

  // 通知 PlaybackController 开始播放
  playback_controller_->Start();
  return true;
}

// ZenPlayer::Pause()
bool ZenPlayer::Pause() {
  if (!state_manager_->IsPlaying()) {
    return false;
  }

  // 转换到暂停状态
  state_manager_->TransitionToPaused();
  
  // PlaybackController 会通过回调收到状态变更通知
  return true;
}
```

### 3. 工作线程使用状态

```cpp
// PlaybackController::DemuxTask() - 解封装线程
void PlaybackController::DemuxTask() {
  while (!state_manager_->ShouldStop()) {
    // 检查是否应该暂停
    if (state_manager_->ShouldPause()) {
      // 等待恢复或停止
      if (!state_manager_->WaitForResume()) {
        break;  // 应该停止
      }
    }

    // 执行解封装工作
    AVPacket* packet = nullptr;
    if (demuxer_->ReadPacket(&packet)) {
      // 处理 packet...
    }
  }
}
```

### 4. 状态变更通知

```cpp
// PlaybackController 注册回调
PlaybackController::PlaybackController(
    std::shared_ptr<PlayerStateManager> state_manager)
    : state_manager_(state_manager) {
  
  // 注册状态变更回调
  callback_id_ = state_manager_->RegisterStateChangeCallback(
      [this](auto old_state, auto new_state) {
        OnPlayerStateChanged(old_state, new_state);
      });
}

void PlaybackController::OnPlayerStateChanged(
    PlayerStateManager::PlayerState old_state,
    PlayerStateManager::PlayerState new_state) {
  
  using State = PlayerStateManager::PlayerState;
  
  switch (new_state) {
    case State::kPlaying:
      // 恢复解码和播放
      pause_cv_.notify_all();
      break;
      
    case State::kPaused:
      // 暂停处理已通过 ShouldPause() 自动处理
      break;
      
    case State::kStopped:
      // 停止所有线程
      StopAllThreads();
      break;
      
    case State::kSeeking:
      // 清空队列，准备跳转
      ClearAllQueues();
      break;
      
    default:
      break;
  }
}
```

---

## 🔄 迁移步骤

### 步骤1：创建 PlayerStateManager 实例

```cpp
// zen_player.h
class ZenPlayer {
 private:
  std::shared_ptr<PlayerStateManager> state_manager_;
  
  // 删除旧的状态变量
  // PlayState state_ = PlayState::kStopped;  // ❌ 删除
};

// zen_player.cpp
ZenPlayer::ZenPlayer()
    : state_manager_(std::make_shared<PlayerStateManager>()),
      demuxer_(std::make_unique<Demuxer>()),
      ... {
  // 初始化时状态自动为 Idle
}
```

### 步骤2：传递给子组件

```cpp
// playback_controller.h
class PlaybackController {
 public:
  PlaybackController(
      std::shared_ptr<PlayerStateManager> state_manager,  // 添加参数
      Demuxer* demuxer,
      ...);
      
 private:
  std::shared_ptr<PlayerStateManager> state_manager_;
  
  // 删除旧的状态变量
  // std::atomic<bool> is_playing_{false};   // ❌ 删除
  // std::atomic<bool> is_paused_{false};    // ❌ 删除
  // std::atomic<bool> should_stop_{false};  // ❌ 删除
};

// zen_player.cpp
ZenPlayer::ZenPlayer() {
  ...
  playback_controller_ = std::make_unique<PlaybackController>(
      state_manager_,  // 传递状态管理器
      demuxer_.get(),
      ...);
}
```

### 步骤3：替换状态检查

```cpp
// Before (VideoPlayer)
while (!should_stop_.load()) {
  if (is_paused_.load()) {
    std::unique_lock<std::mutex> lock(sync_mutex_);
    pause_cv_.wait(lock, [this] { 
      return !is_paused_.load() || should_stop_.load(); 
    });
  }
  // ...
}

// After
while (!state_manager_->ShouldStop()) {
  if (state_manager_->ShouldPause()) {
    state_manager_->WaitForResume();
  }
  // ...
}
```

### 步骤4：替换状态转换

```cpp
// Before (ZenPlayer::Play)
bool ZenPlayer::Play() {
  if (state_ == PlayState::kPaused) {
    playback_controller_->Resume();
    state_ = PlayState::kPlaying;
    return true;
  }
  
  if (state_ == PlayState::kPlaying) {
    return true;
  }
  
  if (!playback_controller_->Start()) {
    return false;
  }
  state_ = PlayState::kPlaying;
  return true;
}

// After
bool ZenPlayer::Play() {
  if (state_manager_->IsPlaying()) {
    return true;  // 已经在播放
  }
  
  // 状态转换由 StateManager 处理合法性检查
  if (!state_manager_->TransitionToPlaying()) {
    return false;
  }
  
  // 根据之前的状态决定操作
  if (old_state == PlayerStateManager::PlayerState::kPaused) {
    // 通过回调通知，无需手动调用
  } else {
    playback_controller_->Start();
  }
  
  return true;
}
```

### 步骤5：清理冗余代码

```cpp
// 删除各组件中的：
// - is_playing_, is_paused_, should_stop_ 成员变量
// - pause_cv_, state_mutex_ （除非有其他用途）
// - 重复的状态检查逻辑
```

---

## ✅ 最佳实践

### 1. 状态查询

```cpp
// ✅ 推荐：使用语义化的查询方法
if (state_manager_->IsPlaying()) {
  // ...
}

// ❌ 不推荐：直接比较枚举值
if (state_manager_->GetState() == PlayerStateManager::PlayerState::kPlaying) {
  // ...
}
```

### 2. 状态转换

```cpp
// ✅ 推荐：使用便捷方法
state_manager_->TransitionToPlaying();

// ⚠️ 可用但冗长
state_manager_->RequestStateChange(PlayerStateManager::PlayerState::kPlaying);
```

### 3. 线程等待

```cpp
// ✅ 推荐：使用内置的等待方法
while (!state_manager_->ShouldStop()) {
  if (state_manager_->ShouldPause()) {
    state_manager_->WaitForResume();  // 自动处理暂停/恢复
  }
  // 工作...
}

// ❌ 不推荐：自己实现等待逻辑
while (true) {
  if (state_manager_->IsPaused()) {
    std::this_thread::sleep_for(10ms);  // 低效
    continue;
  }
  // ...
}
```

### 4. 状态回调

```cpp
// ✅ 推荐：在构造函数中注册，析构函数中取消
class PlaybackController {
 public:
  PlaybackController(std::shared_ptr<PlayerStateManager> sm)
      : state_manager_(sm) {
    callback_id_ = state_manager_->RegisterStateChangeCallback(
        [this](auto old_s, auto new_s) { OnStateChanged(old_s, new_s); });
  }
  
  ~PlaybackController() {
    state_manager_->UnregisterStateChangeCallback(callback_id_);
  }
  
 private:
  int callback_id_ = -1;
};
```

### 5. 错误处理

```cpp
// ✅ 推荐：检查状态转换结果
if (!state_manager_->TransitionToPlaying()) {
  MODULE_ERROR(LOG_MODULE_PLAYER, 
               "Cannot start playing in current state: {}",
               PlayerStateManager::GetStateName(state_manager_->GetState()));
  return false;
}

// 状态转换失败会自动记录日志，但业务层也应该处理
```

---

## 🎯 核心优势

### Before（旧方案）

```cpp
// ❌ 状态分散，难以维护
class ZenPlayer {
  PlayState state_;  // ZenPlayer 的状态
};

class PlaybackController {
  std::atomic<bool> is_playing_;   // 重复的状态
  std::atomic<bool> is_paused_;    // 重复的状态
  std::atomic<bool> should_stop_;  // 重复的状态
};

class VideoPlayer {
  std::atomic<bool> is_playing_;   // 又一份重复
  std::atomic<bool> is_paused_;    // 又一份重复
  std::atomic<bool> should_stop_;  // 又一份重复
};

// 状态转换分散在各处
ZenPlayer::Play() {
  state_ = kPlaying;                    // 修改 ZenPlayer 状态
  playback_controller_->Start();        // 内部又修改 is_playing_
  video_player_->Start();               // 又修改一次 is_playing_
  audio_player_->Start();               // 再修改一次 is_playing_
}
```

### After（新方案）

```cpp
// ✅ 集中管理，清晰明了
class ZenPlayer {
  std::shared_ptr<PlayerStateManager> state_manager_;  // 唯一状态源
};

class PlaybackController {
  std::shared_ptr<PlayerStateManager> state_manager_;  // 共享状态
  // 无需自己的状态变量
};

class VideoPlayer {
  std::shared_ptr<PlayerStateManager> state_manager_;  // 共享状态
  // 无需自己的状态变量
};

// 状态转换集中处理
ZenPlayer::Play() {
  state_manager_->TransitionToPlaying();  // 一处修改
  // 其他组件通过回调自动响应
}
```

---

## 📊 状态转换图

```
     ┌──────┐
     │ Idle │
     └──┬───┘
        │ Open()
        ▼
    ┌─────────┐
    │ Opening │
    └────┬────┘
         │ success
         ▼
    ┌─────────┐ ◄──── Play() ──┐
    │ Stopped │                 │
    └────┬────┘                 │
         │ Play()               │
         ▼                      │
    ┌─────────┐                 │
┌───│ Playing │───┐             │
│   └─────────┘   │             │
│        │        │             │
│Pause() │        │ Seek()      │
│        │        │             │
▼        │        ▼             │
┌────────┐       ┌─────────┐   │
│ Paused │       │ Seeking │───┘
└────────┘       └─────────┘
   │                  │
   └─── Resume() ─────┘
          │
          ▼
     ┌─────────┐
     │ Playing │
     └─────────┘
```

---

## 🚀 总结

通过 `PlayerStateManager`，我们实现了：

1. ✅ **统一的状态管理**：单一数据源，消除不一致
2. ✅ **清晰的状态转换**：状态机模式，规则明确
3. ✅ **解耦的架构**：观察者模式，组件独立
4. ✅ **线程安全**：原子操作和同步原语
5. ✅ **易于扩展**：添加新状态只需修改一处
6. ✅ **便于调试**：集中的日志和状态追踪

这是一个**现代化、健壮、可维护**的状态管理方案！
