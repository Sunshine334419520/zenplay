/**
 * @file decode_stats_application_guide.md
 * @brief STATS_UPDATE_DECODE 在 PlaybackController 中的应用指南
 */

# STATS_UPDATE_DECODE 应用方案

## 方案一：基础统计（推荐）

```cpp
void PlaybackController::VideoDecodeTask() {
  // ... 现有代码 ...
  
  while (!should_stop_.load()) {
    // ... 暂停和队列检查代码 ...
    
    if (!packet) {
      // EOF处理 - 统计flush操作
      TIMER_START(video_flush);
      bool flush_success = video_decoder_->Flush(&frames);
      auto flush_time = TIMER_END_MS(video_flush);
      
      // 统计flush操作
      STATS_UPDATE_DECODE(true, flush_success, flush_time, 
                         video_player_ ? video_player_->GetQueueSize() : 0);
      
      // ... 处理flush的帧 ...
      break;
    }

    // 解码统计
    TIMER_START(video_decode);
    bool decode_success = video_decoder_->Decode(packet, &frames);
    auto decode_time = TIMER_END_MS(video_decode);
    
    // 获取当前队列大小
    uint32_t queue_size = video_player_ ? video_player_->GetQueueSize() : 0;
    
    // 更新解码统计
    STATS_UPDATE_DECODE(true, decode_success, decode_time, queue_size);
    
    // 处理解码后的帧
    if (decode_success) {
      for (auto& frame : frames) {
        // ... 现有的帧处理代码 ...
      }
    }
    
    av_packet_free(&packet);
  }
}
```

## 方案二：详细统计（更精确）

```cpp
void PlaybackController::VideoDecodeTask() {
  // ... 现有代码 ...
  
  while (!should_stop_.load()) {
    // ... 暂停和队列检查代码 ...
    
    if (!packet) {
      // EOF - flush decoder
      TIMER_START(video_flush);
      video_decoder_->Flush(&frames);
      auto flush_time = TIMER_END_MS(video_flush);
      
      // 统计flush操作（有帧输出就认为成功）
      bool flush_success = !frames.empty();
      uint32_t queue_size = video_player_ ? video_player_->GetQueueSize() : 0;
      STATS_UPDATE_DECODE(true, flush_success, flush_time, queue_size);
      
      // 处理flush的帧
      for (auto& frame : frames) {
        // ... 处理帧 ...
      }
      break;
    }

    // 记录解码前的队列大小
    uint32_t pre_decode_queue_size = video_player_ ? video_player_->GetQueueSize() : 0;
    
    // 解码
    TIMER_START(video_decode);
    bool decode_result = video_decoder_->Decode(packet, &frames);
    auto decode_time = TIMER_END_MS(video_decode);
    
    // 判断解码成功：API返回true且产生了帧
    bool decode_success = decode_result && !frames.empty();
    
    // 更新统计（使用解码前的队列大小更准确）
    STATS_UPDATE_DECODE(true, decode_success, decode_time, pre_decode_queue_size);
    
    // 处理解码的帧
    if (decode_result) {
      for (auto& frame : frames) {
        // ... 现有的帧处理代码 ...
      }
    }
    
    av_packet_free(&packet);
  }
}
```

## 方案三：错误处理增强

```cpp
void PlaybackController::VideoDecodeTask() {
  // ... 现有代码 ...
  
  while (!should_stop_.load()) {
    // ... 暂停和队列检查代码 ...
    
    uint32_t current_queue_size = video_player_ ? video_player_->GetQueueSize() : 0;
    
    if (!packet) {
      // EOF处理
      TIMER_START(video_flush);
      bool flush_result = false;
      try {
        flush_result = video_decoder_->Flush(&frames);
      } catch (const std::exception& e) {
        MODULE_ERROR(LOG_MODULE_PLAYER, "Video flush failed: {}", e.what());
        flush_result = false;
      }
      auto flush_time = TIMER_END_MS(video_flush);
      
      // 统计flush操作
      STATS_UPDATE_DECODE(true, flush_result, flush_time, current_queue_size);
      
      // ... 处理帧 ...
      break;
    }

    // 解码统计
    TIMER_START(video_decode);
    bool decode_result = false;
    try {
      decode_result = video_decoder_->Decode(packet, &frames);
    } catch (const std::exception& e) {
      MODULE_ERROR(LOG_MODULE_PLAYER, "Video decode failed: {}", e.what());
      decode_result = false;
    }
    auto decode_time = TIMER_END_MS(video_decode);
    
    // 更新统计
    STATS_UPDATE_DECODE(true, decode_result, decode_time, current_queue_size);
    
    // 处理结果
    if (decode_result) {
      for (auto& frame : frames) {
        // ... 处理帧 ...
      }
    }
    
    av_packet_free(&packet);
  }
}
```

## 音频解码统计（类似）

```cpp
void PlaybackController::AudioDecodeTask() {
  // ... 现有代码 ...
  
  while (!should_stop_.load()) {
    // ... 暂停和队列检查代码 ...
    
    uint32_t queue_size = audio_player_ ? audio_player_->GetQueueSize() : 0;
    
    if (!packet) {
      // EOF - flush decoder
      TIMER_START(audio_flush);
      bool flush_success = audio_decoder_->Flush(&frames);
      auto flush_time = TIMER_END_MS(audio_flush);
      
      STATS_UPDATE_DECODE(false, flush_success, flush_time, queue_size);
      
      // ... 处理帧 ...
      break;
    }

    // 解码
    TIMER_START(audio_decode);
    bool decode_success = audio_decoder_->Decode(packet, &frames);
    auto decode_time = TIMER_END_MS(audio_decode);
    
    // 更新音频解码统计
    STATS_UPDATE_DECODE(false, decode_success, decode_time, queue_size);
    
    if (decode_success) {
      for (auto& frame : frames) {
        // ... 处理帧 ...
      }
    }
    
    av_packet_free(&packet);
  }
}
```

## 推荐方案

我推荐使用 **方案一（基础统计）**，因为：

1. **简单易懂**：不会大幅修改现有逻辑
2. **性能开销小**：只增加必要的计时和统计
3. **信息充足**：提供了解码性能分析所需的基本信息
4. **容易维护**：代码变更最小，不容易引入bug

## 关键要点

1. **计时位置**：在 `Decode()` 调用前后进行计时
2. **成功判断**：直接使用 `Decode()` 的返回值
3. **队列大小**：从对应的 player 获取当前队列大小
4. **视频/音频区分**：第一个参数 `is_video` 用于区分
5. **异常处理**：如果需要，可以添加 try-catch 保护