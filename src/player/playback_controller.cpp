#include "player/playback_controller.h"

#include <chrono>

#include "loki/src/bind_util.h"
#include "loki/src/location.h"
#include "player/audio/audio_player.h"
#include "player/codec/audio_decoder.h"
#include "player/codec/video_decoder.h"
#include "player/common/log_manager.h"
#include "player/common/player_state_manager.h"
#include "player/common/timer_util.h"
#include "player/demuxer/demuxer.h"
#include "player/stats/statistics_manager.h"
#include "player/sync/av_sync_controller.h"
#include "player/video/render/renderer.h"
#include "player/video/render/renderer_proxy.h"
#include "player/video/video_player.h"

namespace zenplay {

PlaybackController::PlaybackController(
    std::shared_ptr<PlayerStateManager> state_manager,
    Demuxer* demuxer,
    VideoDecoder* video_decoder,
    AudioDecoder* audio_decoder,
    Renderer* renderer)
    : demuxer_(demuxer),
      video_decoder_(video_decoder),
      audio_decoder_(audio_decoder),
      renderer_(renderer),
      state_manager_(state_manager) {
  MODULE_INFO(LOG_MODULE_PLAYER,
              "PlaybackController created with unified state management");
  // 初始化音视频同步控制器
  av_sync_controller_ = std::make_unique<AVSyncController>();

  // 初始化音频播放器并传递state_manager和sync_controller
  audio_player_ = std::make_unique<AudioPlayer>(state_manager_.get(),
                                                av_sync_controller_.get());
  if (!audio_player_->Init()) {
    MODULE_ERROR(LOG_MODULE_PLAYER, "Failed to initialize audio player");
    audio_player_.reset();
  }

  // 根据音频解码器状态设置同步模式
  if (!audio_decoder_ || !audio_decoder_->opened()) {
    // 仅视频播放：使用外部时钟（系统时钟）避免循环依赖
    av_sync_controller_->SetSyncMode(
        AVSyncController::SyncMode::EXTERNAL_MASTER);
    MODULE_INFO(LOG_MODULE_PLAYER,
                "Audio not available, using EXTERNAL_MASTER sync mode");
  } else {
    // 音视频播放：使用音频主时钟
    av_sync_controller_->SetSyncMode(AVSyncController::SyncMode::AUDIO_MASTER);
    MODULE_INFO(LOG_MODULE_PLAYER,
                "Audio available, using AUDIO_MASTER sync mode");
  }

  // 初始化视频播放器 (如果有视频流)
  if (video_decoder_ && video_decoder_->opened()) {
    MODULE_INFO(LOG_MODULE_PLAYER,
                "Video decoder is opened, creating VideoPlayer");

    // 创建VideoPlayer并传递state_manager和AVSyncController
    video_player_ = std::make_unique<VideoPlayer>(state_manager_.get(),
                                                  av_sync_controller_.get());

    // 创建线程安全的渲染代理
    if (!video_player_->Init(renderer_)) {
      MODULE_ERROR(LOG_MODULE_PLAYER, "Failed to initialize video player");
      video_player_.reset();
    } else {
      MODULE_INFO(LOG_MODULE_PLAYER, "VideoPlayer initialized successfully");
    }
  } else {
    MODULE_WARN(LOG_MODULE_PLAYER, "Video decoder not opened or not available");
  }
}

PlaybackController::~PlaybackController() {
  Stop();
}

bool PlaybackController::Start() {
  // 注意：不再需要 state_mutex_，状态由 PlayerStateManager 管理

  // 启动解封装线程 - 使用专门的工作线程
  demux_thread_ =
      std::make_unique<std::thread>(&PlaybackController::DemuxTask, this);

  // 启动视频解码线程
  if (video_decoder_ && video_decoder_->opened()) {
    video_decode_thread_ = std::make_unique<std::thread>(
        &PlaybackController::VideoDecodeTask, this);
  }

  // 启动音频解码线程
  if (audio_decoder_ && audio_decoder_->opened()) {
    audio_decode_thread_ = std::make_unique<std::thread>(
        &PlaybackController::AudioDecodeTask, this);
  }

  // 启动音频播放器
  if (audio_player_) {
    audio_player_->Start();
  }

  // 启动视频播放器
  if (video_player_) {
    MODULE_INFO(LOG_MODULE_PLAYER, "Starting VideoPlayer");
    if (!video_player_->Start()) {
      MODULE_ERROR(LOG_MODULE_PLAYER, "Failed to start VideoPlayer");
    } else {
      MODULE_INFO(LOG_MODULE_PLAYER, "VideoPlayer started successfully");
    }
  } else {
    MODULE_WARN(LOG_MODULE_PLAYER, "VideoPlayer not available for start");
  }

  // 启动同步控制任务
  sync_control_thread_ =
      std::make_unique<std::thread>(&PlaybackController::SyncControlTask, this);

  // 启动 Seek 专用线程
  seek_thread_ =
      std::make_unique<std::thread>(&PlaybackController::SeekTask, this);

  MODULE_INFO(LOG_MODULE_PLAYER, "PlaybackController started");
  return true;
}

void PlaybackController::Stop() {
  MODULE_INFO(LOG_MODULE_PLAYER, "Stopping PlaybackController");

  StopAllThreads();

  // 清空所有队列（packet 队列需要手动清空）
  ClearAllQueues();

  // 停止播放器
  if (audio_player_) {
    audio_player_->Stop();
  }
  if (video_player_) {
    video_player_->Stop();
  }

  MODULE_INFO(LOG_MODULE_PLAYER, "PlaybackController stopped");
}

void PlaybackController::Pause() {
  MODULE_INFO(LOG_MODULE_PLAYER, "Pausing PlaybackController");

  // 暂停播放器
  if (audio_player_) {
    audio_player_->Pause();
  }
  if (video_player_) {
    video_player_->Pause();
  }
}

void PlaybackController::Resume() {
  MODULE_INFO(LOG_MODULE_PLAYER, "Resuming PlaybackController");

  // 恢复播放器
  if (audio_player_) {
    audio_player_->Resume();
  }
  if (video_player_) {
    video_player_->Resume();
  }
}

bool zenplay::PlaybackController::Seek(int64_t timestamp_ms) {
  // 同步版本已弃用，请使用 SeekAsync
  MODULE_WARN(LOG_MODULE_PLAYER,
              "Sync Seek is deprecated, use SeekAsync instead");
  return false;
}

void PlaybackController::SeekAsync(int64_t timestamp_ms, bool backward) {
  MODULE_INFO(LOG_MODULE_PLAYER, "SeekAsync requested: {}ms (backward: {})",
              timestamp_ms, backward);

  // 保存当前状态，用于 Seek 完成后恢复
  auto current_state = state_manager_->GetState();
  auto restore_state = PlayerStateManager::PlayerState::kStopped;

  if (current_state == PlayerStateManager::PlayerState::kPlaying) {
    restore_state = PlayerStateManager::PlayerState::kPlaying;
  } else if (current_state == PlayerStateManager::PlayerState::kPaused) {
    restore_state = PlayerStateManager::PlayerState::kPaused;
  }

  // 创建 Seek 请求
  SeekRequest request(timestamp_ms, backward, restore_state);

  // 添加到请求队列（如果队列中已有请求，新请求会替代旧请求）
  seek_request_queue_.Push(request);

  MODULE_INFO(LOG_MODULE_PLAYER, "Seek request queued");
}

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

void PlaybackController::DemuxTask() {
  if (!demuxer_) {
    return;
  }

  while (!state_manager_->ShouldStop()) {
    // 检查暂停状态
    if (state_manager_->ShouldPause()) {
      state_manager_->WaitForResume();
      continue;
    }

    // 检查队列大小，避免内存积压
    if (video_packet_queue_.Size() > 100 || audio_packet_queue_.Size() > 100) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    AVPacket* packet = av_packet_alloc();
    if (!packet) {
      break;
    }

    // 计算一下读取时间
    TIMER_START(demux_read);

    if (!demuxer_->ReadPacket(&packet)) {
      // 发送EOF信号
      if (video_decoder_ && video_decoder_->opened()) {
        video_packet_queue_.Push(nullptr);
      }
      if (audio_decoder_ && audio_decoder_->opened()) {
        audio_packet_queue_.Push(nullptr);
      }
      av_packet_free(&packet);
      break;
    }

    // MODULE_DEBUG(LOG_MODULE_PLAYER, "Demuxed packet, size: {}, pts: {}",
    //              packet->size, packet->pts);

    auto demux_time_ms = TIMER_END_MS_INT(demux_read);

    STATS_UPDATE_DEMUX(
        1, packet->size, demux_time_ms,
        packet->stream_index == demuxer_->active_video_stream_index());

    // 分发packet到对应的解码队列
    if (packet->stream_index == demuxer_->active_video_stream_index() &&
        video_decoder_ && video_decoder_->opened()) {
      video_packet_queue_.Push(packet);
    } else if (packet->stream_index == demuxer_->active_audio_stream_index() &&
               audio_decoder_ && audio_decoder_->opened()) {
      audio_packet_queue_.Push(packet);
    } else {
      av_packet_free(&packet);
    }
  }
}

void PlaybackController::VideoDecodeTask() {
  if (!video_decoder_ || !video_decoder_->opened()) {
    return;
  }

  AVPacket* packet = nullptr;
  std::vector<AVFramePtr> frames;

  while (!state_manager_->ShouldStop()) {
    // 检查暂停状态
    if (state_manager_->ShouldPause()) {
      state_manager_->WaitForResume();
      continue;
    }

    // 检查视频播放器的队列大小，避免内存积压
    if (video_player_ && video_player_->GetQueueSize() > 25) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    // 从队列获取packet
    if (!video_packet_queue_.Pop(packet)) {
      continue;
    }

    if (!packet) {
      video_decoder_->Flush(&frames);

      for (auto& frame : frames) {
        if (video_player_) {
          // 创建时间戳信息
          VideoPlayer::FrameTimestamp timestamp;
          timestamp.pts = frame->pts;
          timestamp.dts = frame->pkt_dts;
          // 从视频流获取时间基准
          if (demuxer_ && demuxer_->active_video_stream_index() >= 0) {
            AVStream* stream = demuxer_->findStreamByIndex(
                demuxer_->active_video_stream_index());
            if (stream) {
              timestamp.time_base = stream->time_base;
            }
          }
          video_player_->PushFrame(std::move(frame), timestamp);
        }
      }
      break;
    }

    // 解码统计
    TIMER_START(video_decode);
    bool decode_success = video_decoder_->Decode(packet, &frames);
    auto decode_time = TIMER_END_MS(video_decode);

    // 更新解码统计
    STATS_UPDATE_DECODE(true, decode_success, decode_time,
                        video_packet_queue_.Size());

    if (decode_success) {
      for (auto& frame : frames) {
        if (video_player_) {
          // 创建时间戳信息
          VideoPlayer::FrameTimestamp timestamp;
          timestamp.pts = frame->pts;
          timestamp.dts = frame->pkt_dts;
          // 从视频流获取时间基准
          if (demuxer_ && demuxer_->active_video_stream_index() >= 0) {
            AVStream* stream = demuxer_->findStreamByIndex(
                demuxer_->active_video_stream_index());
            if (stream) {
              timestamp.time_base = stream->time_base;
            }
          }

          video_player_->PushFrame(std::move(frame), timestamp);
        }
      }
    }

    av_packet_free(&packet);
  }
}

void PlaybackController::AudioDecodeTask() {
  if (!audio_decoder_ || !audio_decoder_->opened()) {
    return;
  }

  AVPacket* packet = nullptr;
  std::vector<AVFramePtr> frames;

  while (!state_manager_->ShouldStop()) {
    // 检查暂停状态
    if (state_manager_->ShouldPause()) {
      state_manager_->WaitForResume();
      continue;
    }

    // 检查音频播放器的队列大小，避免内存积压
    if (audio_player_ && audio_player_->GetQueueSize() > 50) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    // 从队列获取packet
    if (!audio_packet_queue_.Pop(packet)) {
      continue;
    }

    if (!packet) {
      audio_decoder_->Flush(&frames);

      for (auto& frame : frames) {
        if (audio_player_) {
          audio_player_->PushFrame(std::move(frame));
        }
      }
      break;
    }

    TIMER_START(audio_decode);
    bool decode_success = audio_decoder_->Decode(packet, &frames);

    STATS_UPDATE_DECODE(false, decode_success, TIMER_END_MS(audio_decode),
                        audio_packet_queue_.Size());

    if (decode_success) {
      for (auto& frame : frames) {
        if (audio_player_) {
          audio_player_->PushFrame(std::move(frame));
        }
      }
    }

    av_packet_free(&packet);
  }
}

void PlaybackController::SyncControlTask() {
  while (!state_manager_->ShouldStop()) {
    // 检查暂停状态
    if (state_manager_->ShouldPause()) {
      state_manager_->WaitForResume();
      continue;
    }

    // 更新同步统计信息
    if (av_sync_controller_) {
      // 这里可以添加额外的同步逻辑
      // 比如检测大的时钟偏移并发出警告或校正信号
      auto stats = av_sync_controller_->GetSyncStats();

      // 如果偏移过大，可以通知播放器进行调整
      if (std::abs(stats.sync_offset_ms) > 100.0) {  // 100ms阈值
        // 可以在这里实现一些校正逻辑
        // 比如通知video_player_调整播放速度
      }
    }

    // 100Hz更新频率
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

void PlaybackController::StopAllThreads() {
  // 停止队列
  video_packet_queue_.Stop();
  audio_packet_queue_.Stop();
  seek_request_queue_.Stop();

  if (seek_thread_ && seek_thread_->joinable()) {
    seek_thread_->join();
    seek_thread_.reset();
  }

  // 等待线程结束
  if (demux_thread_ && demux_thread_->joinable()) {
    demux_thread_->join();
    demux_thread_.reset();
  }

  if (video_decode_thread_ && video_decode_thread_->joinable()) {
    video_decode_thread_->join();
    video_decode_thread_.reset();
  }

  if (audio_decode_thread_ && audio_decode_thread_->joinable()) {
    audio_decode_thread_->join();
    audio_decode_thread_.reset();
  }

  if (sync_control_thread_ && sync_control_thread_->joinable()) {
    sync_control_thread_->join();
    sync_control_thread_.reset();
  }
}

int64_t PlaybackController::GetCurrentTime() const {
  if (!av_sync_controller_) {
    return 0;
  }

  auto current_time = std::chrono::steady_clock::now();
  double master_clock_ms = av_sync_controller_->GetMasterClock(current_time);

  // 直接返回毫秒
  return static_cast<int64_t>(master_clock_ms);
}

void PlaybackController::SeekTask() {
  MODULE_INFO(LOG_MODULE_PLAYER, "SeekTask started");

  while (!state_manager_->ShouldStop()) {
    SeekRequest request(0, false, PlayerStateManager::PlayerState::kStopped);

    // 从队列获取 Seek 请求
    if (!seek_request_queue_.Pop(request, std::chrono::milliseconds(500))) {
      continue;
    }

    // 清空队列中的旧请求，只执行最新的
    SeekRequest latest_request = request;
    while (seek_request_queue_.Pop(request, std::chrono::milliseconds(0))) {
      MODULE_DEBUG(LOG_MODULE_PLAYER, "Discarding old seek request: {}ms",
                   request.timestamp_ms);
      latest_request = request;
    }

    // 执行 Seek
    MODULE_INFO(LOG_MODULE_PLAYER, "Executing seek to {}ms (backward: {})",
                latest_request.timestamp_ms, latest_request.backward);

    bool success = ExecuteSeek(latest_request);

    if (success) {
      MODULE_INFO(LOG_MODULE_PLAYER, "Seek completed successfully");
    } else {
      MODULE_ERROR(LOG_MODULE_PLAYER, "Seek failed");
    }
  }

  MODULE_INFO(LOG_MODULE_PLAYER, "SeekTask stopped");
}

bool PlaybackController::ExecuteSeek(const SeekRequest& request) {
  // 防止并发
  if (seeking_.exchange(true)) {
    MODULE_WARN(LOG_MODULE_PLAYER, "Already seeking, skipping");
    return false;
  }

  try {
    // === 步骤1: 转换到 Seeking 状态 ===
    if (!state_manager_->TransitionToSeeking()) {
      MODULE_ERROR(LOG_MODULE_PLAYER, "Failed to transition to Seeking state");
      seeking_.store(false);
      return false;
    }

    // === 步骤2: 暂停数据处理 ===
    MODULE_DEBUG(LOG_MODULE_PLAYER, "Pausing players");
    if (video_player_) {
      video_player_->Pause();
    }
    if (audio_player_) {
      audio_player_->Pause();
    }

    // 等待解码线程进入暂停状态（最多100ms）
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // === 步骤3: 清空所有队列 ===
    ClearAllQueues();

    // === 步骤4: Demuxer Seek ===
    MODULE_DEBUG(LOG_MODULE_PLAYER, "Demuxer seeking");

    // FFmpeg 使用微秒为单位
    int64_t timestamp_us = request.timestamp_ms * 1000;

    if (!demuxer_->Seek(timestamp_us, request.backward)) {
      MODULE_ERROR(LOG_MODULE_PLAYER, "Demuxer seek failed");
      state_manager_->TransitionToError();
      seeking_.store(false);
      return false;
    }

    // === 步骤5: 刷新解码器缓冲区 ===
    MODULE_DEBUG(LOG_MODULE_PLAYER, "Flushing decoders");

    if (video_decoder_ && video_decoder_->opened()) {
      video_decoder_->FlushBuffers();
    }
    if (audio_decoder_ && audio_decoder_->opened()) {
      audio_decoder_->FlushBuffers();
    }

    // === 步骤6: 重置同步控制器到目标位置 ===
    MODULE_DEBUG(LOG_MODULE_PLAYER,
                 "Resetting sync controller to target position");

    if (av_sync_controller_) {
      // ✅ 使用新的 ResetForSeek，传入目标位置
      av_sync_controller_->ResetForSeek(request.timestamp_ms);
    }

    // 重置播放器时间戳
    if (video_player_) {
      video_player_->ResetTimestamps();
    }
    if (audio_player_) {
      audio_player_->ResetTimestamps();
    }

    // === 步骤7: 恢复状态 ===
    MODULE_DEBUG(LOG_MODULE_PLAYER, "Restoring state: {}",
                 PlayerStateManager::GetStateName(request.restore_state));

    // 转换到目标状态
    if (request.restore_state == PlayerStateManager::PlayerState::kPlaying) {
      if (video_player_) {
        video_player_->Resume();
      }
      if (audio_player_) {
        audio_player_->Resume();
      }
      state_manager_->TransitionToPlaying();
    } else if (request.restore_state ==
               PlayerStateManager::PlayerState::kPaused) {
      state_manager_->TransitionToPaused();
    } else {
      state_manager_->TransitionToStopped();
    }

    seeking_.store(false);
    return true;

  } catch (const std::exception& e) {
    MODULE_ERROR(LOG_MODULE_PLAYER, "Seek exception: {}", e.what());
    state_manager_->TransitionToError();
    seeking_.store(false);
    return false;
  }
}

}  // namespace zenplay
