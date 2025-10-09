# PlaybackController 队列清空逻辑封装

## 问题
在 `PlaybackController` 中，多处需要清空队列（packet 和 frame）：
- `ExecuteSeek()` - Seek 时清空缓冲
- `Stop()` - 停止时清空缓冲
- 未来可能的其他场景（如切换流、重新加载等）

**原始代码**存在重复：
```cpp
// ExecuteSeek 中
video_packet_queue_.Clear([](AVPacket* packet) {
  if (packet) {
    av_packet_free(&packet);
  }
});

audio_packet_queue_.Clear([](AVPacket* packet) {
  if (packet) {
    av_packet_free(&packet);
  }
});

if (video_player_) {
  video_player_->ClearFrames();
}
if (audio_player_) {
  audio_player_->ClearFrames();
}

// Stop 中可能也需要同样的代码...
```

---

## 解决方案：封装 `ClearAllQueues()` 方法

### 1. 头文件声明
```cpp
// playback_controller.h

private:
  /**
   * @brief 清空所有队列（packet 和 frame）
   * @note 用于 Seek、Stop 等需要清空缓冲的场景
   */
  void ClearAllQueues();
```

### 2. 实现
```cpp
// playback_controller.cpp

void PlaybackController::ClearAllQueues() {
  MODULE_DEBUG(LOG_MODULE_PLAYER, "Clearing all queues");

  // 清空 packet 队列（使用回调释放 AVPacket*）
  video_packet_queue_.Clear([](AVPacket* packet) {
    if (packet) {
      av_packet_free(&packet);
    }
  });

  audio_packet_queue_.Clear([](AVPacket* packet) {
    if (packet) {
      av_packet_free(&packet);
    }
  });

  // 清空 frame 队列
  if (video_player_) {
    video_player_->ClearFrames();
  }
  if (audio_player_) {
    audio_player_->ClearFrames();
  }

  MODULE_DEBUG(LOG_MODULE_PLAYER, "All queues cleared");
}
```

### 3. 使用场景

#### 场景1: Seek 时清空缓冲
```cpp
bool PlaybackController::ExecuteSeek(const SeekRequest& request) {
  try {
    // ... 暂停播放器 ...

    // === 步骤3: 清空所有队列 ===
    ClearAllQueues();  // ✅ 简洁清晰

    // === 步骤4: Demuxer Seek ===
    // ...
  }
}
```

#### 场景2: Stop 时清空缓冲
```cpp
void PlaybackController::Stop() {
  MODULE_INFO(LOG_MODULE_PLAYER, "Stopping PlaybackController");

  StopAllThreads();

  // 清空所有队列（packet 队列需要手动清空）
  ClearAllQueues();  // ✅ 确保资源释放

  // 停止播放器
  if (audio_player_) {
    audio_player_->Stop();  // 内部也会调用 ClearFrames()
  }
  if (video_player_) {
    video_player_->Stop();  // 内部也会调用 ClearFrames()
  }

  MODULE_INFO(LOG_MODULE_PLAYER, "PlaybackController stopped");
}
```

#### 未来场景3: 切换流
```cpp
void PlaybackController::SwitchStream(int new_stream_index) {
  // 暂停解码
  state_manager_->TransitionToPaused();
  
  // 清空旧数据
  ClearAllQueues();  // ✅ 复用现有逻辑
  
  // 切换到新流
  demuxer_->SelectStream(new_stream_index);
  
  // 恢复播放
  state_manager_->TransitionToPlaying();
}
```

---

## 设计优势

### ✅ 代码复用
- 避免重复的清空逻辑
- 统一管理，易于维护

### ✅ 可维护性
- 如果清空逻辑需要修改（如添加新队列），只需改一处
- 代码意图清晰（"清空所有队列"）

### ✅ 一致性
- 确保所有场景使用相同的清空逻辑
- 避免遗漏某个队列

### ✅ 扩展性
- 未来添加新队列时，只需在 `ClearAllQueues()` 中添加
- 调用者无需修改

---

## 队列清空层级

### 层级1: ThreadSafeQueue::Clear (基础层)
```cpp
// 无参版本 - 简单清空
void Clear();

// 回调版本 - 自定义清理
template <typename CleanupFunc>
void Clear(CleanupFunc cleanup_callback);
```

### 层级2: Player::ClearFrames (播放器层)
```cpp
// VideoPlayer
void VideoPlayer::ClearFrames() {
  std::lock_guard<std::mutex> lock(frame_queue_mutex_);
  std::queue<std::unique_ptr<VideoFrame>> empty_queue;
  frame_queue_.swap(empty_queue);
  // ...
}

// AudioPlayer
void AudioPlayer::ClearFrames() {
  std::lock_guard<std::mutex> lock(frame_queue_mutex_);
  std::queue<AVFramePtr> empty_queue;
  frame_queue_.swap(empty_queue);
  // ...
}
```

### 层级3: PlaybackController::ClearAllQueues (控制器层)
```cpp
void PlaybackController::ClearAllQueues() {
  // Packet 队列（需要手动释放）
  video_packet_queue_.Clear([](AVPacket* p) { av_packet_free(&p); });
  audio_packet_queue_.Clear([](AVPacket* p) { av_packet_free(&p); });
  
  // Frame 队列（委托给 Player）
  video_player_->ClearFrames();
  audio_player_->ClearFrames();
}
```

---

## 注意事项

### ⚠️ 调用顺序
在 `Stop()` 中：
1. 先 `StopAllThreads()` - 停止数据生产
2. 再 `ClearAllQueues()` - 清空现有数据
3. 最后 `player_->Stop()` - 停止播放器（会再次清空，但无害）

### ⚠️ 双重清空
`ClearAllQueues()` 会调用 `player_->ClearFrames()`，而 `player_->Stop()` 内部也会调用。这是**安全且推荐**的做法：
- PlaybackController 清空 packet 队列
- Player 清空 frame 队列
- 即使重复调用 `ClearFrames()`，也是幂等的（无副作用）

### ⚠️ 线程安全
`ClearAllQueues()` 应在：
- 所有解码线程已暂停/停止后调用
- 或在持有适当锁的情况下调用

---

## 对比总结

| 方面 | 封装前 | 封装后 |
|------|--------|--------|
| 代码行数 | 重复 20+ 行 | 调用 1 行 |
| 可维护性 | ❌ 多处修改 | ✅ 单点修改 |
| 可读性 | ⚠️ 细节暴露 | ✅ 意图清晰 |
| 一致性 | ❌ 易遗漏 | ✅ 统一逻辑 |
| 扩展性 | ❌ 每处都改 | ✅ 一处添加 |

---

## 最佳实践

### ✅ 推荐
```cpp
// 需要清空所有缓冲时
ClearAllQueues();
```

### ❌ 避免
```cpp
// 不要直接操作队列（除非有特殊需求）
video_packet_queue_.Clear(...);
audio_packet_queue_.Clear(...);
video_player_->ClearFrames();
audio_player_->ClearFrames();
```

---

## 总结

通过封装 `ClearAllQueues()` 方法：
1. ✅ 消除了代码重复
2. ✅ 提高了可维护性
3. ✅ 增强了代码可读性
4. ✅ 便于未来扩展

这是**经典的重构模式**：**提取方法（Extract Method）**！🎉
