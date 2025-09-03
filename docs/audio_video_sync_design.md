# 完整的音视频同步设计方案

## 🎯 **音视频同步核心思路**

### 1. **时钟系统设计**
```
主时钟(Master Clock) - 通常选择音频时钟
    │
    ├─ 音频时钟 (Audio Clock) - 基于音频播放进度
    ├─ 视频时钟 (Video Clock) - 基于视频帧PTS  
    └─ 系统时钟 (System Clock) - 系统时间参考
```

### 2. **同步策略**
- **音频为主时钟**：音频播放相对稳定，不易跳跃
- **视频跟随音频**：视频根据音频时钟调整显示时间
- **允许帧率调整**：丢帧/重复帧来维持同步

### 3. **同步阈值**
```cpp
const double SYNC_THRESHOLD = 40.0;        // 40ms内认为同步
const double DROP_FRAME_THRESHOLD = 80.0;   // 延迟80ms以上丢帧
const double REPEAT_FRAME_THRESHOLD = 20.0; // 超前20ms以上重复帧
```

## 🔧 **实现架构**

### 新的文件结构
```
src/player/
├── audio/
│   ├── audio_output.h/cpp          (跨平台音频输出)
│   ├── audio_player.h/cpp          (音频播放器)
│   └── impl/
│       ├── wasapi_audio_output.*   (Windows实现)
│       └── alsa_audio_output.*     (Linux实现)
├── video/
│   ├── video_player.h/cpp          (视频播放器) 
│   └── render/                     (渲染器移动到这里)
│       ├── renderer.h/cpp
│       ├── renderer_proxy.*
│       └── impl/
│           └── sdl_renderer.*
├── sync/
│   └── av_sync_controller.h/cpp    (音视频同步控制)
└── playback_controller.h/cpp       (总协调器)
```

### 组件职责分工
```cpp
PlaybackController {
    // 协调器，不直接处理音视频
    ├── DemuxTask()           // 解封装线程
    ├── VideoDecodeTask()     // 视频解码线程  
    ├── AudioDecodeTask()     // 音频解码线程
    └── SyncControlTask()     // 同步控制线程
}

AudioPlayer {
    // 独立音频播放
    ├── AudioOutputCallback() // 音频输出回调
    ├── ResampleFrame()       // 音频重采样
    └── FillAudioBuffer()     // 填充播放缓冲区
}

VideoPlayer {
    // 独立视频播放  
    ├── VideoRenderThread()   // 视频渲染线程
    ├── CalculateFrameDisplayTime() // 帧显示时间计算
    └── ShouldDropFrame()     // 丢帧判断
}

AVSyncController {
    // 音视频同步核心
    ├── UpdateAudioClock()    // 更新音频时钟
    ├── UpdateVideoClock()    // 更新视频时钟
    ├── GetMasterClock()      // 获取主时钟
    └── CalculateVideoDelay() // 计算视频延迟
}
```

## 📊 **同步算法详解**

### 时钟更新机制
```cpp
// 音频时钟更新 (在音频输出回调中)
void UpdateAudioClock(double audio_pts_ms) {
    auto now = std::chrono::steady_clock::now();
    sync_controller_->UpdateAudioClock(audio_pts_ms, now);
}

// 视频时钟更新 (在视频渲染前)  
void UpdateVideoClock(double video_pts_ms) {
    auto now = std::chrono::steady_clock::now();
    sync_controller_->UpdateVideoClock(video_pts_ms, now);
}
```

### 视频显示时间计算
```cpp
auto VideoPlayer::CalculateFrameDisplayTime(const VideoFrame& frame_info) {
    double video_pts_ms = frame_info.timestamp.ToMilliseconds();
    double master_clock_ms = sync_controller_->GetMasterClock(current_time);
    
    // 计算同步偏移
    double sync_offset_ms = video_pts_ms - master_clock_ms;
    
    // 限制延迟范围 [-100ms, +100ms]
    sync_offset_ms = std::clamp(sync_offset_ms, -100.0, 100.0);
    
    return current_time + std::chrono::milliseconds((int64_t)sync_offset_ms);
}
```

### 丢帧策略
```cpp
bool VideoPlayer::ShouldDropFrame(const VideoFrame& frame_info, auto current_time) {
    auto target_display_time = CalculateFrameDisplayTime(frame_info);
    auto delay_ms = std::chrono::duration<double, std::milli>(
        current_time - target_display_time).count();
    
    // 如果延迟超过丢帧阈值，丢弃此帧
    return delay_ms > DROP_FRAME_THRESHOLD;
}
```

## 🎮 **数据流向**

### 解码阶段
```
Media File → DemuxTask 
    ↓
┌─────────────────┐
│ Packet Queues   │ (Video/Audio分离)
└─────────────────┘
    ↓               ↓
VideoDecodeTask  AudioDecodeTask
    ↓               ↓
VideoPlayer      AudioPlayer
```

### 播放阶段  
```
VideoPlayer.RenderThread():
    获取视频帧
    ↓
    计算显示时间 ← AVSyncController.GetMasterClock()
    ↓
    检查是否丢帧 ← 同步偏移判断
    ↓
    等待到显示时间
    ↓
    RendererProxy.RenderFrame() → loki UI线程
    ↓
    更新视频时钟 → AVSyncController.UpdateVideoClock()

AudioPlayer.AudioOutputCallback():
    获取音频帧
    ↓ 
    重采样转换
    ↓
    填充播放缓冲区
    ↓
    更新音频时钟 → AVSyncController.UpdateAudioClock()
```

### 同步控制循环
```
SyncControlTask() {
    while (playing) {
        // 获取当前时钟状态
        audio_clock = GetAudioClock();
        video_clock = GetVideoClock(); 
        
        // 计算同步偏移
        sync_offset = video_clock - audio_clock;
        
        // 更新统计信息
        UpdateSyncStats(sync_offset);
        
        // 如果偏移过大，发出校正信号
        if (abs(sync_offset) > CORRECTION_THRESHOLD) {
            NotifyVideoPlayer(sync_offset);
        }
        
        sleep(10ms);  // 100Hz更新频率
    }
}
```

## 🎯 **关键优势**

### 1. **架构对称性**
- AudioPlayer和VideoPlayer地位平等
- 都有独立的线程和队列管理
- 统一的同步接口

### 2. **精确同步**  
- 基于PTS的精确时间戳
- 多级同步阈值控制
- 自适应丢帧/重复帧策略

### 3. **性能优化**
- 音频时钟为主，减少抖动
- 视频智能丢帧，保持流畅
- 统计信息实时监控

### 4. **易于调试**
- 每个组件职责清晰
- 同步状态可视化
- 详细的性能统计

这个设计完全解决了架构不对称问题，实现了专业级的音视频同步！🎉
