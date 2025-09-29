#include "player/playback_controller.h"

#include <chrono>

#include "loki/src/bind_util.h"
#include "loki/src/location.h"
#include "player/audio/audio_player.h"
#include "player/codec/audio_decoder.h"
#include "player/codec/video_decoder.h"
#include "player/common/log_manager.h"
#include "player/common/timer_util.h"
#include "player/demuxer/demuxer.h"
#include "player/stats/statistics_manager.h"
#include "player/sync/av_sync_controller.h"
#include "player/video/render/renderer.h"
#include "player/video/render/renderer_proxy.h"
#include "player/video/video_player.h"

namespace zenplay {

PlaybackController::PlaybackController(Demuxer* demuxer,
                                       VideoDecoder* video_decoder,
                                       AudioDecoder* audio_decoder,
                                       Renderer* renderer)
    : demuxer_(demuxer),
      video_decoder_(video_decoder),
      audio_decoder_(audio_decoder),
      renderer_(renderer) {
  // 初始化音视频同步控制器
  av_sync_controller_ = std::make_unique<AVSyncController>();

  // 初始化音频播放器
  audio_player_ = std::make_unique<AudioPlayer>(av_sync_controller_.get());
  if (!audio_player_->Init()) {
    MODULE_ERROR(LOG_MODULE_PLAYER, "Failed to initialize audio player");
    audio_player_.reset();
  }

  if (!audio_decoder_ || !audio_decoder_->opened()) {
    av_sync_controller_->SetSyncMode(AVSyncController::SyncMode::VIDEO_MASTER);
  }

  // 初始化视频播放器 (如果有视频流)
  if (video_decoder_ && video_decoder_->opened()) {
    MODULE_INFO(LOG_MODULE_PLAYER,
                "Video decoder is opened, creating VideoPlayer");

    // 创建VideoPlayer并传递AVSyncController
    video_player_ = std::make_unique<VideoPlayer>(av_sync_controller_.get());

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
  std::lock_guard<std::mutex> lock(state_mutex_);

  if (is_playing_.load()) {
    return true;  // Already playing
  }

  should_stop_ = false;
  is_playing_ = true;
  is_paused_ = false;

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

  return true;
}

void PlaybackController::Stop() {
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    should_stop_ = true;
    is_playing_ = false;
    is_paused_ = false;
  }

  pause_cv_.notify_all();
  StopAllThreads();

  // 停止播放器
  if (audio_player_) {
    audio_player_->Stop();
  }
  if (video_player_) {
    video_player_->Stop();
  }
}

void PlaybackController::Pause() {
  std::lock_guard<std::mutex> lock(state_mutex_);

  if (!is_playing_.load()) {
    return;  // Not playing
  }

  is_paused_ = true;

  // 暂停播放器
  if (audio_player_) {
    audio_player_->Pause();
  }
  if (video_player_) {
    video_player_->Pause();
  }
}

void PlaybackController::Resume() {
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (!is_playing_.load() || !is_paused_.load()) {
      return;  // Not in pause state
    }
    is_paused_ = false;
  }

  // 恢复播放器
  if (audio_player_) {
    audio_player_->Resume();
  }
  if (video_player_) {
    video_player_->Resume();
  }

  pause_cv_.notify_all();
}

bool zenplay::PlaybackController::Seek(int64_t timestamp_ms) {
  return false;
}

void PlaybackController::DemuxTask() {
  MODULE_INFO(LOG_MODULE_PLAYER, "Demux thread started");
  if (!demuxer_) {
    return;
  }

  while (!should_stop_.load()) {
    // 检查暂停状态
    if (is_paused_.load()) {
      std::unique_lock<std::mutex> lock(state_mutex_);
      pause_cv_.wait(
          lock, [this] { return !is_paused_.load() || should_stop_.load(); });
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

    MODULE_DEBUG(LOG_MODULE_PLAYER, "Demuxed packet, size: {}, pts: {}",
                 packet->size, packet->pts);

    auto demux_time_ms = TIMER_END_MS_INT(demux_read);

    STATS_UPDATE_DEMUX(
        1, packet->size, demux_time_ms,
        packet->stream_index == demuxer_->active_video_stream_index());

    // 分发packet到对应的解码队列
    if (packet->stream_index == demuxer_->active_video_stream_index() &&
        video_decoder_ && video_decoder_->opened()) {
      MODULE_DEBUG(LOG_MODULE_PLAYER, "Demuxed video packet, size: {}, pts: {}",
                   packet->size, packet->pts);
      video_packet_queue_.Push(packet);
    } else if (packet->stream_index == demuxer_->active_audio_stream_index() &&
               audio_decoder_ && audio_decoder_->opened()) {
      MODULE_DEBUG(LOG_MODULE_PLAYER, "Demuxed audio packet, size: {}, pts: {}",
                   packet->size, packet->pts);
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

  while (!should_stop_.load()) {
    // 检查暂停状态
    if (is_paused_.load()) {
      std::unique_lock<std::mutex> lock(state_mutex_);
      pause_cv_.wait(
          lock, [this] { return !is_paused_.load() || should_stop_.load(); });
      continue;
    }

    // 检查视频播放器的队列大小，避免内存积压
    if (video_player_ && video_player_->GetQueueSize() > 30) {
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

  while (!should_stop_.load()) {
    // 检查暂停状态
    if (is_paused_.load()) {
      std::unique_lock<std::mutex> lock(state_mutex_);
      pause_cv_.wait(
          lock, [this] { return !is_paused_.load() || should_stop_.load(); });
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
  while (!should_stop_.load()) {
    // 检查暂停状态
    if (is_paused_.load()) {
      std::unique_lock<std::mutex> lock(state_mutex_);
      pause_cv_.wait(
          lock, [this] { return !is_paused_.load() || should_stop_.load(); });
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

PlaybackController::PlaybackStats PlaybackController::GetStats() const {
  PlaybackStats stats = {};

  if (av_sync_controller_) {
    auto sync_stats = av_sync_controller_->GetSyncStats();
    stats.sync_offset_ms = sync_stats.sync_offset_ms;
    stats.sync_quality = sync_stats.is_in_sync() ? "Good" : "Poor";
    stats.is_in_sync = sync_stats.is_in_sync();
  }

  stats.audio_queue_size = audio_player_ ? audio_player_->GetQueueSize() : 0;
  stats.video_queue_size = video_player_ ? video_player_->GetQueueSize() : 0;

  return stats;
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

}  // namespace zenplay
