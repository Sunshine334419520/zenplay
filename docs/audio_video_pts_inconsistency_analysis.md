# AudioPlayer/VideoPlayer PTS 传递不一致问题分析

## 问题发现

用户发现了一个重要的设计不一致:

```cpp
// VideoPlayer - 传递完整的时间戳信息
struct FrameTimestamp {
  int64_t pts = AV_NOPTS_VALUE;
  int64_t dts = AV_NOPTS_VALUE;
  AVRational time_base{1, 1000000};
};

bool VideoPlayer::PushFrame(AVFramePtr frame, const FrameTimestamp& timestamp);

// AudioPlayer - 没有传递时间戳信息 ❌
bool AudioPlayer::PushFrame(AVFramePtr frame);  // ← 缺少 timestamp 参数!
```

## 根本原因

### VideoPlayer 的实现

**在 `PlaybackController::VideoDecodeTask()` 中**:
```cpp
// 创建时间戳信息
VideoPlayer::FrameTimestamp timestamp;
timestamp.pts = frame->pts;                    // ← 从 AVFrame 提取
timestamp.dts = frame->pkt_dts;                // ← 从 AVFrame 提取
// 从视频流获取时间基准
if (demuxer_ && demuxer_->active_video_stream_index() >= 0) {
  AVStream* stream = demuxer_->findStreamByIndex(
      demuxer_->active_video_stream_index());
  if (stream) {
    timestamp.time_base = stream->time_base;  // ← 从 AVStream 获取
  }
}
video_player_->PushFrame(std::move(frame), timestamp);
```

**优点**:
- ✅ 时间戳信息明确传递
- ✅ time_base 在解码线程就确定
- ✅ VideoPlayer 不需要访问 Demuxer
- ✅ 解耦清晰

### AudioPlayer 的实现

**在 `PlaybackController::AudioDecodeTask()` 中**:
```cpp
// 没有提取时间戳信息 ❌
if (decode_success) {
  for (auto& frame : frames) {
    if (audio_player_) {
      audio_player_->PushFrame(std::move(frame));  // ← 只传 frame!
    }
  }
}
```

**在 `AudioPlayer::PushFrame()` 中**:
```cpp
// ❌ 问题1: 直接从 AVFrame 读取 pts，但没有 time_base!
if (!base_pts_initialized_ && frame->pts != AV_NOPTS_VALUE) {
  std::lock_guard<std::mutex> pts_lock(pts_mutex_);
  if (!base_pts_initialized_) {
    base_audio_pts_ =
        static_cast<double>(frame->pts) * av_q2d(audio_time_base_);  // ← 使用成员变量的 time_base
    base_pts_initialized_ = true;
  }
}
```

**在 `PlaybackController::Start()` 中设置 time_base**:
```cpp
// ❌ 问题2: time_base 在 Start() 中通过单独调用设置
if (audio_player_) {
  if (demuxer_ && demuxer_->active_audio_stream_index() >= 0) {
    AVStream* audio_stream =
        demuxer_->findStreamByIndex(demuxer_->active_audio_stream_index());
    if (audio_stream) {
      audio_player_->SetTimeBase(audio_stream->time_base);  // ← 单独设置
    }
  }
  audio_player_->Start();
}
```

**缺点**:
- ❌ 时间戳信息隐式依赖 `AVFrame.pts`
- ❌ time_base 通过单独的 `SetTimeBase()` 设置，容易遗漏
- ❌ PTS 和 time_base 不是原子性传递，有时序风险
- ❌ AudioPlayer 需要内部成员变量 `audio_time_base_` 存储
- ❌ 不一致的 API 设计

## 时序风险分析

### 当前的危险时序

```
线程1 (PlaybackController::Start):
  T0: audio_player_->SetTimeBase(time_base)  // 设置 time_base
  T1: audio_player_->Start()

线程2 (PlaybackController::AudioDecodeTask):
  T2: audio_player_->PushFrame(frame)        // 使用 audio_time_base_
  
如果 T2 < T0，则 PushFrame 使用的是错误的 time_base!
```

**实际情况**:
- `Start()` 在主线程执行
- `AudioDecodeTask()` 在解码线程执行
- 存在理论上的竞态条件(虽然实际上 Start() 先执行)

### VideoPlayer 没有这个问题

```cpp
// time_base 和 pts 原子性传递
VideoPlayer::FrameTimestamp timestamp;
timestamp.pts = frame->pts;
timestamp.time_base = stream->time_base;  // 同时设置

video_player_->PushFrame(std::move(frame), timestamp);  // 原子性传递
```

## AVFrame 本身就有 PTS 信息

FFmpeg 的 `AVFrame` 结构:
```c
typedef struct AVFrame {
  int64_t pts;              // ← 解码后的显示时间戳
  int64_t pkt_dts;          // ← 原始 packet 的 DTS
  AVRational time_base;     // ← 时间基准 (FFmpeg 4.0+)
  // ...
} AVFrame;
```

**问题**: 
- VideoPlayer 使用 `frame->pts` 和 `frame->pkt_dts` + 外部 time_base
- AudioPlayer 只使用 `frame->pts` + 外部 time_base
- **为什么不直接使用 `frame->time_base`?**

**原因**:
- `AVFrame.time_base` 是 FFmpeg 4.0 新增的
- 在旧版本中，`AVFrame` 不包含 time_base
- **最佳实践**: 从 `AVStream.time_base` 获取，而不是依赖 `AVFrame.time_base`

## 应该如何修复?

### 方案1: AudioPlayer 使用与 VideoPlayer 一致的接口 (推荐)

**步骤1**: 定义 AudioPlayer 的时间戳结构

```cpp
// audio_player.h
class AudioPlayer {
 public:
  struct FrameTimestamp {
    int64_t pts = AV_NOPTS_VALUE;
    int64_t dts = AV_NOPTS_VALUE;
    AVRational time_base{1, 1000000};

    // 转换为秒
    double ToSeconds() const {
      if (pts == AV_NOPTS_VALUE) return -1.0;
      return pts * av_q2d(time_base);
    }
  };

  // 新接口
  bool PushFrame(AVFramePtr frame, const FrameTimestamp& timestamp);
  
  // ❌ 删除: SetTimeBase() - 不再需要
  // ❌ 删除: audio_time_base_ 成员变量 - 不再需要
};
```

**步骤2**: 修改 PlaybackController::AudioDecodeTask()

```cpp
void PlaybackController::AudioDecodeTask() {
  // ...
  if (decode_success) {
    for (auto& frame : frames) {
      if (audio_player_) {
        // 创建时间戳信息 (与 VideoPlayer 一致)
        AudioPlayer::FrameTimestamp timestamp;
        timestamp.pts = frame->pts;
        timestamp.dts = frame->pkt_dts;
        
        // 从音频流获取时间基准
        if (demuxer_ && demuxer_->active_audio_stream_index() >= 0) {
          AVStream* stream = demuxer_->findStreamByIndex(
              demuxer_->active_audio_stream_index());
          if (stream) {
            timestamp.time_base = stream->time_base;
          }
        }
        
        audio_player_->PushFrame(std::move(frame), timestamp);
      }
    }
  }
}
```

**步骤3**: 修改 AudioPlayer::PushFrame()

```cpp
bool AudioPlayer::PushFrame(AVFramePtr frame, const FrameTimestamp& timestamp) {
  if (!frame || state_manager_->ShouldStop()) {
    return false;
  }

  // 设置基础PTS（第一帧）
  if (!base_pts_initialized_ && timestamp.pts != AV_NOPTS_VALUE) {
    std::lock_guard<std::mutex> pts_lock(pts_mutex_);
    if (!base_pts_initialized_) {
      // ✅ 使用传入的 time_base
      base_audio_pts_ = timestamp.ToSeconds();
      base_pts_initialized_ = true;
      MODULE_INFO(LOG_MODULE_AUDIO,
                  "Audio base PTS set to: {:.3f}s (raw_pts={}, time_base={}/{})",
                  base_audio_pts_, timestamp.pts, 
                  timestamp.time_base.num, timestamp.time_base.den);
    }
  }

  // ... 其余逻辑不变
}
```

**步骤4**: 删除 SetTimeBase() 和 audio_time_base_

```cpp
// audio_player.h
// ❌ 删除这些
// void SetTimeBase(AVRational time_base);
// AVRational audio_time_base_{1, 1000000};

// audio_player.cpp
// ❌ 删除实现
// void AudioPlayer::SetTimeBase(AVRational time_base) { ... }
```

**步骤5**: 清理 PlaybackController::Start()

```cpp
// 启动音频播放器
if (audio_player_) {
  // ❌ 删除这部分 - 不再需要单独设置 time_base
  // if (demuxer_ && demuxer_->active_audio_stream_index() >= 0) {
  //   AVStream* audio_stream = ...
  //   audio_player_->SetTimeBase(audio_stream->time_base);
  // }
  audio_player_->Start();
}
```

### 方案2: VideoPlayer 也简化为隐式传递 (不推荐)

让 VideoPlayer 也像 AudioPlayer 一样，只传 frame，隐式使用 time_base。

**缺点**:
- ❌ 降低了接口的明确性
- ❌ 增加了内部状态管理复杂度
- ❌ 不利于代码维护

## 为什么方案1更好?

### 1. 接口一致性
- AudioPlayer 和 VideoPlayer 使用相同的模式
- 更容易理解和维护

### 2. 明确的时间戳传递
- PTS, DTS, time_base 原子性传递
- 没有时序竞态条件

### 3. 减少内部状态
- 不需要成员变量 `audio_time_base_`
- 不需要 `SetTimeBase()` 方法

### 4. 更好的解耦
- AudioPlayer 不需要在 Start() 时设置 time_base
- 解码线程独立管理时间戳转换

### 5. 更安全
```cpp
// 旧方式 - 可能的时序问题
T0: SetTimeBase(tb1)           // 线程A
T1: PushFrame(frame)           // 线程B - 可能使用旧的 tb
T2: SetTimeBase(tb2)           // 线程A - Seek后

// 新方式 - 没有时序问题
T0: PushFrame(frame, {pts, dts, tb1})  // 原子性
T1: PushFrame(frame, {pts, dts, tb2})  // 原子性
```

## 类比说明

### 旧方式(AudioPlayer 现状):
```
快递员: "这是你的包裹"
收件人: "我怎么知道这是什么时候寄出的?"
快递员: "你昨天应该收到一张时间表，自己查"
收件人: "如果我没收到时间表怎么办?"
```

### 新方式(VideoPlayer 现状):
```
快递员: "这是你的包裹，寄出时间是10月14日15:30，时区是UTC+8"
收件人: "好的，信息很清楚!"
```

## 总结

| 方面 | AudioPlayer (旧) | VideoPlayer (现状) | 修复后 AudioPlayer |
|------|------------------|-------------------|-------------------|
| PTS 传递 | ✅ 通过 AVFrame | ✅ 通过参数 | ✅ 通过参数 |
| DTS 传递 | ❌ 未传递 | ✅ 通过参数 | ✅ 通过参数 |
| time_base | ❌ 单独设置 | ✅ 随帧传递 | ✅ 随帧传递 |
| 接口一致性 | ❌ 不一致 | ✅ 标准 | ✅ 一致 |
| 线程安全性 | ⚠️ 有风险 | ✅ 安全 | ✅ 安全 |
| 代码简洁性 | ❌ 需要 SetTimeBase | ✅ 简洁 | ✅ 简洁 |

**结论**: 应该让 AudioPlayer 采用与 VideoPlayer 相同的接口设计，传递完整的 `FrameTimestamp` 结构体。✅
