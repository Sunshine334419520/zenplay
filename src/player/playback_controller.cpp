#include "player/playback_controller.h"

#include <chrono>

#include "loki/src/bind_util.h"
#include "loki/src/location.h"
#include "player/audio/audio_player.h"
#include "player/audio/audio_resampler.h"
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

  // ✅ 初始化音频播放器（先初始化，获取硬件支持的格式）
  audio_player_ = std::make_unique<AudioPlayer>(state_manager_.get(),
                                                av_sync_controller_.get());

  // ✅ 使用 AudioPlayer 的配置来设置重采样器
  // 原因：AudioPlayer::Init() 会根据硬件能力选择最佳配置
  AudioPlayer::AudioConfig audio_config;
  // 注意：这里可以从配置文件或用户设置中读取
  // 默认使用常见的 CD 音质配置
  audio_config.target_sample_rate = 44100;         // CD 音质标准
  audio_config.target_channels = 2;                // 立体声
  audio_config.target_format = AV_SAMPLE_FMT_S16;  // 16位整数
  audio_config.target_bits_per_sample = 16;
  audio_config.buffer_size = 1024;  // 缓冲区大小

  auto audio_init_result = audio_player_->Init(audio_config);
  if (!audio_init_result.IsOk()) {
    MODULE_ERROR(LOG_MODULE_PLAYER, "Failed to initialize audio player: {}",
                 audio_init_result.FullMessage());
    audio_player_.reset();
  }

  // ✅ 初始化音频重采样器（使用与 AudioPlayer 一致的配置）
  audio_resampler_ = std::make_unique<AudioResampler>();
  AudioResampler::ResamplerConfig resampler_config;
  resampler_config.target_sample_rate = audio_config.target_sample_rate;
  resampler_config.target_channels = audio_config.target_channels;
  resampler_config.target_format = audio_config.target_format;
  resampler_config.target_bits_per_sample = audio_config.target_bits_per_sample;
  resampler_config.enable_simd = true;  // 启用 SIMD 优化
  audio_resampler_->SetConfig(resampler_config);

  MODULE_INFO(LOG_MODULE_PLAYER,
              "Audio resampler configured: {}Hz, {} channels, {} bits",
              resampler_config.target_sample_rate,
              resampler_config.target_channels,
              resampler_config.target_bits_per_sample);

  // 根据音视频流的存在情况智能选择同步模式
  bool has_audio = audio_decoder_ && audio_decoder_->opened();
  bool has_video = video_decoder_ && video_decoder_->opened();

  if (has_audio && has_video) {
    // 场景 1：音视频都有 → 使用音频主时钟（标准播放）
    // 原因：音频硬件稳定，音频不能卡顿，视频通过丢帧/重复帧适应音频
    av_sync_controller_->SetSyncMode(AVSyncController::SyncMode::AUDIO_MASTER);
    MODULE_INFO(LOG_MODULE_PLAYER,
                "Audio + Video detected, using AUDIO_MASTER sync mode");

  } else if (has_audio && !has_video) {
    // 场景 2：只有音频 → 使用音频主时钟（音乐播放、播客等）
    // 原因：音频时钟由硬件驱动，最稳定
    av_sync_controller_->SetSyncMode(AVSyncController::SyncMode::AUDIO_MASTER);
    MODULE_INFO(LOG_MODULE_PLAYER,
                "Audio only detected, using AUDIO_MASTER sync mode");

  } else if (!has_audio && has_video) {
    // 场景 3：只有视频 → 使用外部时钟/系统时钟（GIF、静默视频等）
    // 原因：无音频时，系统时钟简单可靠，视频按固定帧率播放
    av_sync_controller_->SetSyncMode(
        AVSyncController::SyncMode::EXTERNAL_MASTER);
    MODULE_INFO(LOG_MODULE_PLAYER,
                "Video only detected, using EXTERNAL_MASTER sync mode");

  } else {
    // 场景 4：既无音频也无视频 → 错误情况
    // 使用外部时钟作为后备，避免崩溃
    MODULE_ERROR(LOG_MODULE_PLAYER,
                 "No audio and no video streams available, invalid media file");
    av_sync_controller_->SetSyncMode(
        AVSyncController::SyncMode::EXTERNAL_MASTER);
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

Result<void> PlaybackController::Start() {
  // 注意：不再需要 state_mutex_，状态由 PlayerStateManager 管理

  // ✅ 重置队列状态（如果之前调用过 Stop()）
  video_packet_queue_.Reset();
  audio_packet_queue_.Reset();
  seek_request_queue_.Reset();

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
  // 注意: time_base 现在通过 PushFrame(frame, timestamp) 传递，不再需要单独设置
  if (audio_player_) {
    auto audio_start_result = audio_player_->Start();
    if (!audio_start_result.IsOk()) {
      MODULE_ERROR(LOG_MODULE_PLAYER, "Failed to start AudioPlayer: {}",
                   audio_start_result.FullMessage());
      // 即使音频启动失败，也继续启动视频（只有视频的场景）
    }
  }

  // 启动视频播放器
  if (video_player_) {
    MODULE_INFO(LOG_MODULE_PLAYER, "Starting VideoPlayer");
    auto video_start_result = video_player_->Start();
    if (!video_start_result.IsOk()) {
      MODULE_ERROR(LOG_MODULE_PLAYER, "Failed to start VideoPlayer: {}",
                   video_start_result.FullMessage());
      // 视频启动失败，如果有音频可以继续（只有音频的场景）
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
  return Result<void>::Ok();
}

void PlaybackController::Stop() {
  MODULE_INFO(LOG_MODULE_PLAYER, "Stopping PlaybackController");

  // ✅ StopAllThreads 内部会调用 audio_player_->Stop() 和 video_player_->Stop()
  // 这样可以确保在 join 之前，播放器的队列已经停止
  StopAllThreads();

  // 清空所有队列（packet 队列需要手动清空）
  ClearAllQueues();

  MODULE_INFO(LOG_MODULE_PLAYER, "PlaybackController stopped");
}

void PlaybackController::Pause() {
  MODULE_INFO(LOG_MODULE_PLAYER, "Pausing PlaybackController");

  // 步骤 1：先暂停音视频播放（停止数据流）
  // 原因：确保暂停时钟时，不会有新的 UpdateClock 调用
  if (audio_player_) {
    audio_player_->Pause();  // 停止音频输出，音频回调停止
  }
  if (video_player_) {
    video_player_->Pause();  // VideoPlayer 通过 state_manager 停止渲染
  }

  // 步骤 2：再暂停同步控制器（记录暂停时间点）
  // 此时音视频已经停止，不会再调用 UpdateClock
  if (av_sync_controller_) {
    av_sync_controller_->Pause();
  }
}

void PlaybackController::Resume() {
  MODULE_INFO(LOG_MODULE_PLAYER, "Resuming PlaybackController");

  // 步骤 1：先恢复同步控制器（调整时钟基准）
  // 原因：确保播放器启动后，UpdateClock 使用的是调整后的 system_time
  if (av_sync_controller_) {
    av_sync_controller_->Resume();
  }

  // 步骤 2：再恢复音视频播放（开始数据流）
  // 此时时钟已经调整好，UpdateClock 会使用正确的 system_time
  if (audio_player_) {
    audio_player_->Resume();  // 启动音频输出，恢复音频回调
  }
  if (video_player_) {
    video_player_->Resume();  // 唤醒渲染线程
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
  if (!seek_request_queue_.Push(request)) {
    MODULE_ERROR(LOG_MODULE_PLAYER,
                 "Failed to queue seek request (queue stopped)");
    return;
  }

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

    // ✅ 移除队列大小检查和 sleep，BlockingQueue 会自动阻塞

    // 计算一下读取时间
    TIMER_START(demux_read);

    auto packet_result = demuxer_->ReadPacket();
    if (!packet_result.IsOk()) {
      // 读取失败，发送EOF信号
      if (video_decoder_ && video_decoder_->opened()) {
        if (!video_packet_queue_.Push(nullptr)) {
          break;  // 队列已停止
        }
      }
      if (audio_decoder_ && audio_decoder_->opened()) {
        if (!audio_packet_queue_.Push(nullptr)) {
          break;  // 队列已停止
        }
      }
      break;
    }

    AVPacket* packet = packet_result.Value();

    // ReadPacket 返回 nullptr 表示 EOF（不是错误）
    if (!packet) {
      // 发送EOF信号
      if (video_decoder_ && video_decoder_->opened()) {
        if (!video_packet_queue_.Push(nullptr)) {
          break;  // 队列已停止
        }
      }
      if (audio_decoder_ && audio_decoder_->opened()) {
        if (!audio_packet_queue_.Push(nullptr)) {
          break;  // 队列已停止
        }
      }
      break;
    }

    // MODULE_DEBUG(LOG_MODULE_PLAYER, "Demuxed packet, size: {}, pts: {}",
    //              packet->size, packet->pts);

    auto demux_time_ms = TIMER_END_MS_INT(demux_read);

    STATS_UPDATE_DEMUX(
        1, packet->size, demux_time_ms,
        packet->stream_index == demuxer_->active_video_stream_index());

    // ✅ BlockingQueue::Push 会自动阻塞直到有空间，无需手动检查
    // 分发packet到对应的解码队列
    if (packet->stream_index == demuxer_->active_video_stream_index() &&
        video_decoder_ && video_decoder_->opened()) {
      if (!video_packet_queue_.Push(packet)) {
        av_packet_free(&packet);
        break;  // 队列已停止
      }
    } else if (packet->stream_index == demuxer_->active_audio_stream_index() &&
               audio_decoder_ && audio_decoder_->opened()) {
      if (!audio_packet_queue_.Push(packet)) {
        av_packet_free(&packet);
        break;  // 队列已停止
      }
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

    // ✅ 使用条件变量等待，替代 sleep 轮询
    // PushFrameTimeout 内部会阻塞直到队列有空间

    // ✅ BlockingQueue::Pop 会阻塞直到有数据或队列停止
    if (!video_packet_queue_.Pop(packet)) {
      break;  // 队列已停止，退出循环
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
          video_player_->PushFrameTimeout(std::move(frame), timestamp, 100);
        }
      }
      break;
    }

    // 解码统计
    TIMER_START(video_decode);
    bool decode_success = video_decoder_->Decode(packet, &frames);
    auto decode_time = TIMER_END_MS(video_decode);

    // 更新解码统计（使用帧队列大小，而不是packet队列）
    uint32_t frame_queue_size =
        video_player_ ? video_player_->GetQueueSize() : 0;
    STATS_UPDATE_DECODE(true, decode_success, decode_time, frame_queue_size);

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

          // ✅ 使用 PushFrameTimeout，阻塞等待队列有空间（100ms超时）
          video_player_->PushFrameTimeout(std::move(frame), timestamp, 100);
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

    // ✅ 使用条件变量等待，替代 sleep 轮询

    // ✅ BlockingQueue::Pop 会阻塞直到有数据或队列停止
    if (!audio_packet_queue_.Pop(packet)) {
      break;  // 队列已停止，退出循环
    }

    if (!packet) {
      audio_decoder_->Flush(&frames);

      for (auto& frame : frames) {
        if (audio_player_ && audio_resampler_) {
          // 创建时间戳信息
          MediaTimestamp timestamp;
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

          // ✅ Flush时也使用相同的重采样流程
          ResampledAudioFrame resampled;
          if (audio_resampler_->Resample(frame.get(), timestamp, resampled)) {
            audio_player_->PushFrame(std::move(resampled));
          }
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
        if (audio_player_ && audio_resampler_) {
          // 创建时间戳信息
          MediaTimestamp timestamp;
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

          // ✅ 重构后的架构：职责分离
          // Step 1: AudioResampler 执行重采样（在解码线程）
          ResampledAudioFrame resampled;
          if (!audio_resampler_->Resample(frame.get(), timestamp, resampled)) {
            MODULE_ERROR(LOG_MODULE_AUDIO, "Audio resample failed");
            continue;
          }

          // Step 2: AudioPlayer 管理播放队列（BlockingQueue自动流控）
          audio_player_->PushFrame(std::move(resampled));
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

      // 通过 StatisticsManager 获取同步统计
      auto* stats_manager = stats::StatisticsManager::GetInstance();
      if (stats_manager) {
        auto& sync_stats = stats_manager->GetSyncStats();
        double sync_offset_ms = sync_stats.av_sync_offset_ms.load();

        // 如果偏移过大，可以通知播放器进行调整
        if (std::abs(sync_offset_ms) > 100.0) {  // 100ms阈值
          // 可以在这里实现一些校正逻辑
          // 比如通知video_player_调整播放速度
        }
      }
    }

    // 100Hz更新频率
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }
}

void PlaybackController::StopAllThreads() {
  // ✅ 第一步：停止所有队列（唤醒阻塞的线程）
  // 注意：必须在 join 之前停止，否则会死锁
  video_packet_queue_.Stop();
  audio_packet_queue_.Stop();
  seek_request_queue_.Stop();

  // ✅ 第二步：停止播放器的队列（解码线程可能在 PushFrame 时阻塞）
  // 这一步非常关键！否则解码线程会在 Push 时永久阻塞
  if (audio_player_) {
    audio_player_->Stop();  // 内部会调用 frame_queue_.Stop()
  }
  if (video_player_) {
    video_player_->Stop();  // 内部会调用 frame_queue_.Stop()
  }

  // ✅ 第三步：等待所有线程退出
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

// Undefine Windows macro to avoid conflict with our method name
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

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

    // ✅ BlockingQueue::Pop 会阻塞直到有数据或队列停止
    if (!seek_request_queue_.Pop(request)) {
      break;  // 队列已停止，退出循环
    }

    // 清空队列中的旧请求，只执行最新的
    SeekRequest latest_request = request;
    while (seek_request_queue_.TryPop(request)) {
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

    // === 步骤6.7: 清空音频硬件缓冲区 ===
    // ✅ 在队列预填充后才清空硬件缓冲区，确保有数据可以立即播放
    MODULE_DEBUG(LOG_MODULE_PLAYER, "Flushing audio hardware buffer");
    if (audio_player_) {
      audio_player_->Flush();
    }

    // === 步骤7: 恢复状态 ===
    MODULE_DEBUG(LOG_MODULE_PLAYER, "Restoring state: {}",
                 PlayerStateManager::GetStateName(request.restore_state));

    if (request.restore_state == PlayerStateManager::PlayerState::kPlaying) {
      // 1. 先转换状态，唤醒 DemuxTask 和 AudioDecodeTask
      state_manager_->TransitionToPlaying();

      // 2. 然后启动播放器（此时解码线程已开始准备数据）
      if (video_player_) {
        video_player_->Resume();
      }
      if (audio_player_) {
        audio_player_->Resume();
      }
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
