#include "player/zen_player.h"

#include <chrono>
#include <future>
#include <thread>

#include "player/codec/audio_decoder.h"
#include "player/codec/video_decoder.h"
#include "player/common/log_manager.h"
#include "player/common/player_state_manager.h"
#include "player/demuxer/demuxer.h"
#include "player/playback_controller.h"
#include "player/video/render/renderer.h"

namespace zenplay {

// 直接返回 PlayerStateManager 的状态
PlayerStateManager::PlayerState ZenPlayer::GetState() const {
  return state_manager_->GetState();
}

ZenPlayer::ZenPlayer()
    : demuxer_(std::make_unique<Demuxer>()),
      video_decoder_(std::make_unique<VideoDecoder>()),
      audio_decoder_(std::make_unique<AudioDecoder>()),
      renderer_(std::unique_ptr<Renderer>(Renderer::CreateRenderer())),
      state_manager_(std::make_shared<PlayerStateManager>()) {
  MODULE_INFO(LOG_MODULE_PLAYER,
              "ZenPlayer created with unified state management");
}

ZenPlayer::~ZenPlayer() {
  Close();
}

bool ZenPlayer::Open(const std::string& url) {
  MODULE_INFO(LOG_MODULE_PLAYER, "Opening URL: {}", url.c_str());

  if (is_opened_) {
    Close();
  }

  state_manager_->TransitionToOpening();

  // Open demuxer
  auto demux_result = demuxer_->Open(url);
  if (!demux_result.IsOk()) {
    MODULE_ERROR(LOG_MODULE_PLAYER, "Failed to open demuxer: {}",
                 demux_result.Error().message);
    return false;
  }

  // Open video decoder
  AVStream* video_stream =
      demuxer_->findStreamByIndex(demuxer_->active_video_stream_index());
  if (video_stream) {
    auto video_result = video_decoder_->Open(video_stream->codecpar);
    if (!video_result.IsOk()) {
      demuxer_->Close();
      MODULE_ERROR(LOG_MODULE_PLAYER, "Failed to open video decoder: {}",
                   video_result.Error().message);
      return false;
    }
  }

  // Open audio decoder
  AVStream* audio_stream =
      demuxer_->findStreamByIndex(demuxer_->active_audio_stream_index());
  if (audio_stream) {
    auto audio_result = audio_decoder_->Open(audio_stream->codecpar);
    if (!audio_result.IsOk()) {
      video_decoder_->Close();
      demuxer_->Close();
      MODULE_ERROR(LOG_MODULE_PLAYER, "Failed to open audio decoder: {}",
                   audio_result.Error().message);
      return false;
    }
  }

  // 创建播放控制器（传递状态管理器）
  playback_controller_ = std::make_unique<PlaybackController>(
      state_manager_, demuxer_.get(), video_decoder_.get(),
      audio_decoder_.get(), renderer_.get());

  is_opened_ = true;
  state_manager_->TransitionToStopped();
  MODULE_INFO(LOG_MODULE_PLAYER, "File opened successfully, state: Stopped");
  return true;  // Successfully opened
}

bool ZenPlayer::SetRenderWindow(void* window_handle, int width, int height) {
  if (!is_opened_ || !renderer_) {
    MODULE_ERROR(LOG_MODULE_PLAYER,
                 "Player not opened or renderer not available");
    return false;
  }

  MODULE_INFO(LOG_MODULE_PLAYER, "Starting renderer initialization ({}x{})",
              width, height);

  std::thread init_thread([this, window_handle, width, height]() {
    auto result = renderer_->Init(window_handle, width, height);
    if (!result.IsOk()) {
      MODULE_ERROR(LOG_MODULE_PLAYER, "Renderer initialization failed: {}",
                   result.Error().message);
    }
    // 通过回调或信号通知初始化完成
  });
  init_thread.detach();  // 分离线程，不等待

  return true;  // 立即返回
}

void ZenPlayer::OnWindowResize(int width, int height) {
  if (!is_opened_ || !renderer_) {
    MODULE_DEBUG(LOG_MODULE_PLAYER,
                 "Resize ignored: player not opened or renderer not available");
    return;
  }

  MODULE_DEBUG(LOG_MODULE_PLAYER, "Window resized to {}x{}", width, height);
  renderer_->OnResize(width, height);
}

void ZenPlayer::Close() {
  if (!is_opened_) {
    return;
  }

  MODULE_INFO(LOG_MODULE_PLAYER, "Closing player");

  // 停止播放
  Stop();

  // 关闭播放控制器
  playback_controller_.reset();

  // 关闭解码器
  if (video_decoder_) {
    video_decoder_->Close();
  }
  if (audio_decoder_) {
    audio_decoder_->Close();
  }

  // 关闭解封装器
  if (demuxer_) {
    demuxer_->Close();
  }

  is_opened_ = false;
  state_manager_->TransitionToIdle();
  MODULE_INFO(LOG_MODULE_PLAYER, "Player closed");
}

bool ZenPlayer::Play() {
  if (!is_opened_ || !playback_controller_) {
    MODULE_WARN(LOG_MODULE_PLAYER, "Cannot play: player not opened");
    return false;
  }

  // 如果已经在播放，直接返回
  if (state_manager_->IsPlaying()) {
    return true;
  }

  // 如果是暂停状态，恢复播放
  if (state_manager_->IsPaused()) {
    playback_controller_->Resume();
    state_manager_->TransitionToPlaying();
    MODULE_INFO(LOG_MODULE_PLAYER, "Resumed from pause");
    return true;
  }

  // 从停止状态开始播放
  // ⚠️ 关键修复：先转换状态，再启动线程，避免竞态条件
  state_manager_->TransitionToPlaying();

  auto start_result = playback_controller_->Start();
  if (!start_result.IsOk()) {
    // 启动失败，回滚状态
    state_manager_->TransitionToStopped();
    MODULE_ERROR(LOG_MODULE_PLAYER, "Failed to start playback: {}",
                 start_result.Error().message);
    return false;
  }

  MODULE_INFO(LOG_MODULE_PLAYER, "Playback started");
  return true;
}

bool ZenPlayer::Pause() {
  if (!is_opened_ || !playback_controller_) {
    MODULE_WARN(LOG_MODULE_PLAYER, "Cannot pause: player not opened");
    return false;
  }

  if (!state_manager_->IsPlaying()) {
    MODULE_WARN(LOG_MODULE_PLAYER, "Cannot pause: not playing");
    return false;
  }

  playback_controller_->Pause();
  state_manager_->TransitionToPaused();
  MODULE_INFO(LOG_MODULE_PLAYER, "Playback paused");
  return true;
}

bool ZenPlayer::Stop() {
  if (!is_opened_ || !playback_controller_) {
    MODULE_WARN(LOG_MODULE_PLAYER, "Cannot stop: player not opened");
    return false;
  }

  if (state_manager_->IsStopped()) {
    return true;  // Already stopped
  }

  // ⚠️ 先转换状态，让工作线程看到停止信号
  state_manager_->TransitionToStopped();

  // 然后停止所有线程（线程会检查 ShouldStop() 并退出）
  playback_controller_->Stop();

  MODULE_INFO(LOG_MODULE_PLAYER, "Playback stopped");
  return true;
}

bool ZenPlayer::Seek(int64_t timestamp, bool backward) {
  MODULE_WARN(LOG_MODULE_PLAYER,
              "Sync Seek is deprecated, use SeekAsync instead");
  // 为了向后兼容，调用异步版本
  SeekAsync(timestamp, backward);
  return true;  // 立即返回，不等待结果
}

void ZenPlayer::SeekAsync(int64_t timestamp_ms, bool backward) {
  MODULE_INFO(LOG_MODULE_PLAYER, "ZenPlayer::SeekAsync to {}ms", timestamp_ms);

  // 验证前提条件
  if (!is_opened_ || !playback_controller_) {
    MODULE_ERROR(LOG_MODULE_PLAYER, "Cannot seek: player not opened");
    // 通过状态通知错误
    state_manager_->TransitionToError();
    return;
  }

  // 验证时间戳范围
  int64_t duration = GetDuration();
  if (timestamp_ms < 0 || (duration > 0 && timestamp_ms > duration)) {
    MODULE_ERROR(LOG_MODULE_PLAYER,
                 "Invalid seek timestamp: {}ms (duration: {}ms)", timestamp_ms,
                 duration);
    state_manager_->TransitionToError();
    return;
  }

  // 调用 PlaybackController 的异步 Seek
  playback_controller_->SeekAsync(timestamp_ms, backward);
}

int ZenPlayer::RegisterStateChangeCallback(
    PlayerStateManager::StateChangeCallback callback) {
  if (!state_manager_) {
    MODULE_ERROR(LOG_MODULE_PLAYER,
                 "Cannot register callback: state_manager is null");
    return -1;
  }
  return state_manager_->RegisterStateChangeCallback(std::move(callback));
}

void ZenPlayer::UnregisterStateChangeCallback(int callback_id) {
  if (!state_manager_) {
    MODULE_ERROR(LOG_MODULE_PLAYER,
                 "Cannot unregister callback: state_manager is null");
    return;
  }
  state_manager_->UnregisterStateChangeCallback(callback_id);
}

int64_t ZenPlayer::GetDuration() const {
  if (!is_opened_ || !demuxer_) {
    return 0;
  }
  return demuxer_->GetDuration();  // 现在返回毫秒
}

int64_t ZenPlayer::GetCurrentPlayTime() const {
  if (!is_opened_ || !playback_controller_) {
    return 0;
  }

  // 从PlaybackController获取当前播放时间（毫秒）
  return playback_controller_->GetCurrentTime();
}

}  // namespace zenplay
