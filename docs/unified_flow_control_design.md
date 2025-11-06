# ZenPlay 统一流控设计方案

## 问题分析

### 现状问题

当前代码存在三层背压机制，彼此重叠且目标不清：

```
DemuxTask
    ↓ (BlockingQueue<AVPacket*> size=40)
    ├─→ [阻塞等待] ← 当Packet队列满时
    ↓
VideoDecodeTask
    ├─→ WaitForQueueBelow(high_watermark) ← 等待视频帧队列降低
    ├─→ Decode(packet) → 得到AVFrame
    ├─→ PushFrameTimeout(frame) ← 又是一层超时等待
    ↓
VideoPlayer::frame_queue_ (size=30)
    ↓
RenderThread
    └─→ 消费帧
```

### 三层重复的问题

| 控制点 | 机制 | 目的 | 问题 |
|------|------|------|------|
| **DemuxTask** | BlockingQueue::Push() 阻塞 | 限制Packet队列 | ❌ 不知道解码速度，盲目限制 |
| **DecodeTask** | WaitForQueueBelow() | 等待帧队列下降 | ❌ 需要计算high_watermark，逻辑复杂 |
| **DecodeTask** | PushFrameTimeout() | 推送帧时超时 | ❌ 超时返回false，触发下一轮解码=继续压力 |

### 根本问题

```
当PushFrameTimeout超时返回false时：
    ↓
下一轮循环继续调用Pop()获取packet
    ↓
继续尝试Decode()
    ↓
继续尝试PushFrameTimeout()
    ↓
又是超时 → 无限循环浪费CPU
```

**关键洞察**：
- 背压应该发生在 **数据源**（demuxer）而非 **处理方**（decoder）
- Demuxer本身应该根据整个管道的背压来自动调节
- 中间的decode应该被动执行，而不是主动检查

---

## 三种解决方案

### 方案一：移除中间层（PushFrameTimeout），依赖BlockingQueue

**核心思想**：充分相信BlockingQueue的自动背压，简化DecodeTask

#### 架构图

```
DemuxTask [阻塞]
    ↓ (BlockingQueue::Push 满时自动阻塞)
VideoDecodeTask [被动]
    ↓ (Pop 阻塞等待packet)
    ├─ Decode(packet) → frames
    ├─ PushFrame(frame) [直接推送，不超时] ← 改为无超时
    ↓
VideoPlayer::frame_queue_ [被动]
    ↓
RenderThread [消费]
```

#### 优点
✅ 最简单，代码最少  
✅ 逻辑清晰：单向流动  
✅ DemuxTask 自动调节  

#### 缺点
❌ 如果RenderThread卡顿，整个管道会阻塞（包括Demux）  
❌ Packet队列和Frame队列的容量配置需要精心设计  
❌ 无法灵活处理decode产生多帧的情况  

#### 实现方案

```cpp
// playback_controller.cpp - VideoDecodeTask

void PlaybackController::VideoDecodeTask() {
  if (!video_decoder_ || !video_decoder_->opened()) {
    return;
  }

  AVPacket* packet = nullptr;
  std::vector<AVFramePtr> frames;

  while (!state_manager_->ShouldStop()) {
    if (state_manager_->ShouldPause()) {
      state_manager_->WaitForResume();
      continue;
    }

    // ✅ Pop 会自动阻塞，直到有packet或队列停止
    if (!video_packet_queue_.Pop(packet)) {
      break;  // 队列已停止，收到EOF
    }

    if (!packet) {
      // Flush packet
      video_decoder_->Flush(&frames);

      for (auto& frame : frames) {
        if (video_player_) {
          VideoPlayer::FrameTimestamp timestamp = PrepareTimestamp(frame);
          // ✅ 直接推送，不超时（阻塞方向统一在Packet队列）
          if (!video_player_->PushFrame(std::move(frame), timestamp)) {
            MODULE_DEBUG(LOG_MODULE_PLAYER, "PushFrame failed during flush");
          }
        }
      }
      break;
    }

    TIMER_START(video_decode);
    bool decode_success = video_decoder_->Decode(packet, &frames);
    auto decode_time = TIMER_END_MS(video_decode);

    STATS_UPDATE_DECODE(true, decode_success, decode_time, 
                        video_player_->GetQueueSize());

    // 推送所有解码得到的帧
    for (auto& frame : frames) {
      if (video_player_) {
        VideoPlayer::FrameTimestamp timestamp = PrepareTimestamp(frame);
        // ✅ PushFrame 是非阻塞的（快速路径）
        // 队列满时会丢帧（由config_.drop_frames控制）
        video_player_->PushFrame(std::move(frame), timestamp);
      }
    }
    frames.clear();
  }
}
```

**配置建议**：
```cpp
// playback_controller.h
BlockingQueue<AVPacket*> video_packet_queue_{80};  // ⬆️ 增大到80
BlockingQueue<AVPacket*> audio_packet_queue_{80};  // ⬆️ 增大到80

// video_player.h
struct VideoConfig {
  int max_frame_queue_size = 30;  // 保持30
  bool drop_frames = true;  // ⭐ 关键：启用丢帧处理
};
```

---

### 方案二：委托模式（推荐方案）✨

**核心思想**：让VideoPlayer管理自己的背压，DecodeTask只负责Push

#### 架构图

```
DemuxTask
    ↓ (BlockingQueue::Push 满时自动阻塞)
VideoDecodeTask [简化为被动处理]
    ↓
    ├─ Pop(packet)  [阻塞等待]
    ├─ Decode()
    └─ PushFrameBlocking(frame) ← 新方法，内部自动等待
        ↓
        [VideoPlayer自己负责背压逻辑]
        ├─ 检查队列满？
        └─ 满→等待 / 空→推入 [统一管理]
    ↓
VideoPlayer::frame_queue_
    ↓
RenderThread
```

#### 优点
✅ 关注点分离：VideoPlayer负责自己的背压  
✅ DecodeTask逻辑最简洁  
✅ VideoPlayer可独立测试  
✅ 灵活应对decode产生多帧/单帧/0帧的情况  
✅ 可添加统计：等待时长、丢帧数等  

#### 缺点
❌ 增加一个新方法 PushFrameBlocking  
❌ VideoPlayer职责增加  

#### 实现方案

```cpp
// video_player.h - 新增方法

class VideoPlayer {
 public:
  /**
   * @brief 推送视频帧（具有背压的阻塞版本）
   * 
   * 内部会自动等待队列有空间，符合背压模式。
   * 当队列满时，会阻塞解码线程，从而反向背压整个管道。
   * 
   * @param frame 视频帧指针
   * @param timestamp 时间戳信息
   * @param max_wait_ms 最大等待时间，0=无限等待，<0=无阻塞（快速路径）
   * @return true 成功推送，false 停止或超时
   * 
   * 使用场景：
   * - DecodeTask正常调用：PushFrameBlocking(frame, ts, 0)
   * - 特殊场景（Flush）：PushFrameBlocking(frame, ts, 100)
   * - 非阻塞快速路径：PushFrame(frame, ts)  [不存在时直接丢帧]
   */
  bool PushFrameBlocking(AVFramePtr frame,
                         const FrameTimestamp& timestamp,
                         int max_wait_ms = 0);

  /**
   * @brief 推送视频帧（快速路径，无阻塞）
   * 
   * 队列满时直接丢帧或返回false，不阻塞。
   * 用于非关键路径或确实空间不足时。
   * 
   * @return true 成功推送，false 队列满或已停止
   */
  bool PushFrame(AVFramePtr frame, const FrameTimestamp& timestamp);

 private:
  /**
   * @brief 内部：等待队列有空间（关键背压逻辑）
   */
  bool WaitForQueueSpace(int timeout_ms);
};
```

**video_player.cpp 实现**：

```cpp
bool VideoPlayer::PushFrameBlocking(AVFramePtr frame,
                                    const FrameTimestamp& timestamp,
                                    int max_wait_ms) {
  if (!frame || state_manager_->ShouldStop()) {
    return false;
  }

  std::unique_lock<std::mutex> lock(frame_queue_mutex_);

  // 阻塞等待队列有空间
  bool has_space = WaitForQueueSpace_Locked(max_wait_ms);
  
  if (!has_space || state_manager_->ShouldStop()) {
    MODULE_DEBUG(LOG_MODULE_VIDEO, 
                 "PushFrameBlocking failed: has_space={}, should_stop={}",
                 has_space, state_manager_->ShouldStop());
    return false;
  }

  // 有空间了，推送帧
  auto media_frame = std::make_unique<MediaFrame>(std::move(frame), timestamp);
  frame_queue_.push(std::move(media_frame));
  
  // 唤醒渲染线程
  frame_available_.notify_one();

  return true;
}

bool VideoPlayer::PushFrame(AVFramePtr frame, const FrameTimestamp& timestamp) {
  if (!frame || state_manager_->ShouldStop()) {
    return false;
  }

  std::lock_guard<std::mutex> lock(frame_queue_mutex_);

  // 快速路径：检查队列大小，满则丢帧
  if (frame_queue_.size() >= static_cast<size_t>(config_.max_frame_queue_size)) {
    if (config_.drop_frames) {
      // 丢弃最老的帧
      frame_queue_.pop();
      STATS_UPDATE_RENDER(true, false, true, 0.0);
      MODULE_DEBUG(LOG_MODULE_VIDEO, "Dropped old frame, queue full");
    }
    return false;
  }

  // 队列有空间，直接推送
  auto media_frame = std::make_unique<MediaFrame>(std::move(frame), timestamp);
  frame_queue_.push(std::move(media_frame));
  frame_available_.notify_one();

  return true;
}

// ✅ 关键内部方法：等待队列有空间
bool VideoPlayer::WaitForQueueSpace_Locked(int timeout_ms) {
  const size_t max_queue = GetMaxQueueSize();
  const size_t threshold = max_queue * 3 / 4;  // 75% 饱和度

  auto queue_ready = [this, threshold]() {
    return state_manager_->ShouldStop() ||
           frame_queue_.size() < threshold;
  };

  if (timeout_ms == 0) {
    // 无限等待
    frame_consumed_.wait(lock, queue_ready);
  } else if (timeout_ms > 0) {
    // 有限等待
    if (!frame_consumed_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                  queue_ready)) {
      return false;  // 超时
    }
  }
  // else: timeout_ms < 0，非阻塞，直接返回true

  return !state_manager_->ShouldStop() && frame_queue_.size() < threshold;
}
```

**playback_controller.cpp - 简化的 VideoDecodeTask**：

```cpp
void PlaybackController::VideoDecodeTask() {
  if (!video_decoder_ || !video_decoder_->opened()) {
    return;
  }

  AVPacket* packet = nullptr;
  std::vector<AVFramePtr> frames;

  while (!state_manager_->ShouldStop()) {
    if (state_manager_->ShouldPause()) {
      state_manager_->WaitForResume();
      continue;
    }

    // ✅ 第一层背压：Pop 自动阻塞
    if (!video_packet_queue_.Pop(packet)) {
      break;  // EOF
    }

    if (!packet) {
      // Flush
      video_decoder_->Flush(&frames);
      for (auto& frame : frames) {
        if (video_player_) {
          auto timestamp = PrepareTimestamp(frame);
          // ✅ 第二层背压：内部等待队列空间
          video_player_->PushFrameBlocking(std::move(frame), timestamp, 100);
        }
      }
      break;
    }

    // 解码
    TIMER_START(video_decode);
    bool decode_success = video_decoder_->Decode(packet, &frames);
    auto decode_time = TIMER_END_MS(video_decode);
    STATS_UPDATE_DECODE(true, decode_success, decode_time, 
                        video_player_->GetQueueSize());

    // ✅ 简洁：直接推送所有帧，由VideoPlayer自己处理背压
    for (auto& frame : frames) {
      if (video_player_) {
        auto timestamp = PrepareTimestamp(frame);
        // 无限等待，直到推送成功或停止
        video_player_->PushFrameBlocking(std::move(frame), timestamp, 0);
      }
    }
    frames.clear();
  }
}
```

---

### 方案三：高级背压框架（Future Plan）

**核心思想**：建立一个统一的背压观察者模式

#### 架构图

```
┌─────────────────────────────────────────┐
│     FlowControlManager (新建)           │
│  - 监听所有队列的负载                   │
│  - 动态调整各环节的处理速度             │
└──────────┬──────────────────────────────┘
           ↓
    ┌──────┴──────────┬────────────┐
    ↓                 ↓            ↓
DemuxTask         DecodeTask    RenderTask
 (受控)            (受控)        (受控)
```

#### 特点
- 可观测：实时获得整个管道的背压状态
- 可适应：根据不同场景调整策略
- 可扩展：添加新的处理阶段时无需修改现有代码

#### 实现复杂度
⏳ 中期计划，不推荐立即实施

---

## 推荐方案对比

| 指标 | 方案一 | 方案二 | 方案三 |
|------|-------|-------|-------|
| **实现复杂度** | 低 | 中 | 高 |
| **代码清晰度** | 中 | 高 | 最高 |
| **灵活性** | 低 | 中 | 高 |
| **可维护性** | 中 | 高 | 最高 |
| **性能开销** | 低 | 低 | 中 |
| **实施时间** | 立即 | 立即 | 中期 |
| **推荐度** | ⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ |

---

## 最终推荐：采用方案二（委托模式）

### 原因

1. **清晰的职责分工**
   - DemuxTask：简单的读取循环
   - DecodeTask：简单的解码循环
   - VideoPlayer：管理自己的队列和背压

2. **最小化改动**
   - 只需在 VideoPlayer 中添加 `PushFrameBlocking()` 方法
   - DecodeTask 中移除 `WaitForQueueBelow()` 和相关逻辑
   - 核心是让每个组件关注自己的职责

3. **最佳的扩展性**
   - 如果未来需要多个decode线程，只需多个Pop+Decode+PushFrameBlocking循环
   - 如果需要不同的背压策略，修改VideoPlayer即可
   - AudioPlayer可复用相同模式

4. **问题解决**
   - ❌ 移除 PushFrameTimeout 的三层嵌套
   - ❌ 移除 WaitForQueueBelow 的复杂计算
   - ✅ 统一在 VideoPlayer 内部处理背压
   - ✅ 超时不再产生"重试循环"

### 关键改动清单

#### 1. video_player.h 中添加

```cpp
// 新增公开方法
bool PushFrameBlocking(AVFramePtr frame,
                       const FrameTimestamp& timestamp,
                       int max_wait_ms = 0);

// 移除（保留但废弃）
// bool PushFrameTimeout(...)  // 保留向后兼容，但不再使用
// bool WaitForQueueBelow(...)  // 保留但标记为过时

// 新增私有方法
bool WaitForQueueSpace_Locked(int timeout_ms);
```

#### 2. video_player.cpp 中实现

```cpp
// 新增 PushFrameBlocking 实现
// 新增 WaitForQueueSpace_Locked 实现
// 保持现有 PushFrame 不变（快速路径）
```

#### 3. playback_controller.cpp 中修改 VideoDecodeTask

```cpp
// 移除：WaitForQueueBelow 相关逻辑
// 修改：PushFrameTimeout → PushFrameBlocking
// 简化：常量定义（只保留必要的日志相关）
```

---

## 配置参数建议

### 场景一：本地高清播放（默认）

```cpp
BlockingQueue<AVPacket*> video_packet_queue_{64};
VideoPlayer::VideoConfig {
  max_frame_queue_size = 30,
  drop_frames = true,
  vsync_enabled = true
}
```

### 场景二：低延迟直播

```cpp
BlockingQueue<AVPacket*> video_packet_queue_{32};
VideoPlayer::VideoConfig {
  max_frame_queue_size = 6,
  drop_frames = true,
  vsync_enabled = true
}
```

### 场景三：高吞吐量转码

```cpp
BlockingQueue<AVPacket*> video_packet_queue_{128};
VideoPlayer::VideoConfig {
  max_frame_queue_size = 60,
  drop_frames = false,  // 不丢帧
  vsync_enabled = false
}
```

---

## 后续验证点

1. ✅ 是否还有AVERROR_INVALIDDATA？（应该完全消除）
2. ✅ 是否还有绿屏？（应该完全消除）
3. ✅ AV同步漂移？（应该 <±20ms）
4. ✅ CPU使用率？（应该进一步降低）
5. ✅ 长时间播放稳定性？（>1小时无异常）

---

**文档版本**：1.0  
**更新日期**：2025年11月6日  
**作者**：GitHub Copilot + ZenPlay 团队  
**状态**：建议立即实施方案二
