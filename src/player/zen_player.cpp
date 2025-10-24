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

void ZenPlayer::CleanupResources() {
  MODULE_DEBUG(LOG_MODULE_PLAYER, "Cleaning up resources...");

  // 🧹 按照依赖关系的逆序清理资源

  // 1. 先停止播放控制器（依赖所有其他资源）
  if (playback_controller_) {
    playback_controller_.reset();
  }

  // 2. 关闭解码器（依赖解封装器）
  if (audio_decoder_ && audio_decoder_->opened()) {
    audio_decoder_->Close();
  }
  if (video_decoder_ && video_decoder_->opened()) {
    video_decoder_->Close();
  }

  // 3. 最后关闭解封装器（底层资源）
  if (demuxer_) {
    demuxer_->Close();
  }

  MODULE_DEBUG(LOG_MODULE_PLAYER, "Resources cleaned up");
}

Result<void> ZenPlayer::Open(const std::string& url) {
  MODULE_INFO(LOG_MODULE_PLAYER, "Opening URL: {}", url.c_str());

  // 如果已打开，先关闭
  if (is_opened_) {
    Close();
  }

  state_manager_->TransitionToOpening();

  return demuxer_
      ->Open(url)
      // ✅ Step 1 成功：Demuxer 已打开
      .AndThen([this]() -> Result<void> {
        // 尝试打开视频解码器（如果有视频流）
        AVStream* video_stream =
            demuxer_->findStreamByIndex(demuxer_->active_video_stream_index());
        if (video_stream) {
          MODULE_INFO(LOG_MODULE_PLAYER, "Opening video decoder...");
          return video_decoder_->Open(video_stream->codecpar);
        }
        // 没有视频流，返回成功继续
        MODULE_INFO(LOG_MODULE_PLAYER, "No video stream found, skipping");
        return Result<void>::Ok();
      })
      // ✅ Step 2 成功：Video Decoder 已打开（或跳过）
      .AndThen([this]() -> Result<void> {
        // 尝试打开音频解码器（如果有音频流）
        AVStream* audio_stream =
            demuxer_->findStreamByIndex(demuxer_->active_audio_stream_index());
        if (audio_stream) {
          MODULE_INFO(LOG_MODULE_PLAYER, "Opening audio decoder...");
          return audio_decoder_->Open(audio_stream->codecpar);
        }
        // 没有音频流，返回成功继续
        MODULE_INFO(LOG_MODULE_PLAYER, "No audio stream found, skipping");
        return Result<void>::Ok();
      })
      // ✅ Step 3 成功：Audio Decoder 已打开（或跳过）
      .AndThen([this]() -> Result<void> {
        // 创建播放控制器
        MODULE_INFO(LOG_MODULE_PLAYER, "Creating playback controller...");
        playback_controller_ = std::make_unique<PlaybackController>(
            state_manager_, demuxer_.get(), video_decoder_.get(),
            audio_decoder_.get(), renderer_.get());

        is_opened_ = true;
        state_manager_->TransitionToStopped();
        MODULE_INFO(LOG_MODULE_PLAYER,
                    "✅ File opened successfully, state: Stopped");
        return Result<void>::Ok();
      })
      .MapErr([this, &url](ErrorCode code) -> ErrorCode {
        MODULE_ERROR(LOG_MODULE_PLAYER, "❌ Failed to open '{}': {} ({})", url,
                     ErrorCodeToString(code), static_cast<int>(code));

        CleanupResources();

        is_opened_ = false;
        state_manager_->TransitionToError();

        // 保持原错误码不变（也可以转换为其他错误码）
        return code;
      });
}

Result<void> ZenPlayer::SetRenderWindow(void* window_handle,
                                        int width,
                                        int height) {
  if (!is_opened_ || !renderer_) {
    return Result<void>::Err(ErrorCode::kNotInitialized,
                             "Player not opened or renderer not available");
  }

  if (!window_handle) {
    return Result<void>::Err(ErrorCode::kInvalidParameter,
                             "Window handle is null");
  }

  if (width <= 0 || height <= 0) {
    return Result<void>::Err(
        ErrorCode::kInvalidParameter,
        "Invalid window dimensions: " + std::to_string(width) + "x" +
            std::to_string(height));
  }

  MODULE_INFO(LOG_MODULE_PLAYER, "Starting renderer initialization ({}x{})",
              width, height);

  // 异步初始化渲染器（避免阻塞 UI 线程）
  std::thread init_thread([this, window_handle, width, height]() {
    auto result = renderer_->Init(window_handle, width, height);
    if (!result.IsOk()) {
      MODULE_ERROR(LOG_MODULE_PLAYER, "Renderer initialization failed: {}",
                   result.Message());
      state_manager_->TransitionToError();
    } else {
      MODULE_INFO(LOG_MODULE_PLAYER, "Renderer initialized successfully");
    }
  });
  init_thread.detach();  // 分离线程，不等待

  return Result<void>::Ok();  // 立即返回，表示异步初始化已启动
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

  // 停止播放（如果正在播放）
  Stop();

  CleanupResources();

  is_opened_ = false;
  state_manager_->TransitionToIdle();
  MODULE_INFO(LOG_MODULE_PLAYER, "Player closed");
}

Result<void> ZenPlayer::Play() {
  if (!is_opened_ || !playback_controller_) {
    return Result<void>::Err(
        ErrorCode::kNotInitialized,
        "Player not opened or playback controller not available");
  }

  // 如果已经在播放，直接返回成功
  if (state_manager_->IsPlaying()) {
    MODULE_INFO(LOG_MODULE_PLAYER, "Already playing");
    return Result<void>::Ok();
  }

  // 如果是暂停状态，恢复播放
  if (state_manager_->IsPaused()) {
    playback_controller_->Resume();
    state_manager_->TransitionToPlaying();
    MODULE_INFO(LOG_MODULE_PLAYER, "Resumed from pause");
    return Result<void>::Ok();
  }

  // 从停止状态开始播放
  // ⚠️ 关键修复：先转换状态，再启动线程，避免竞态条件
  state_manager_->TransitionToPlaying();

  auto start_result = playback_controller_->Start();
  if (!start_result.IsOk()) {
    // 启动失败，回滚状态
    state_manager_->TransitionToStopped();
    return start_result;  // 直接传播错误
  }

  MODULE_INFO(LOG_MODULE_PLAYER, "Playback started");
  return Result<void>::Ok();
}

Result<void> ZenPlayer::Pause() {
  if (!is_opened_ || !playback_controller_) {
    return Result<void>::Err(
        ErrorCode::kNotInitialized,
        "Player not opened or playback controller not available");
  }

  if (!state_manager_->IsPlaying()) {
    return Result<void>::Err(ErrorCode::kNotInitialized,
                             "Cannot pause: not in playing state");
  }

  playback_controller_->Pause();
  state_manager_->TransitionToPaused();
  MODULE_INFO(LOG_MODULE_PLAYER, "Playback paused");
  return Result<void>::Ok();
}

void ZenPlayer::Stop() {
  if (!is_opened_ || !playback_controller_) {
    MODULE_WARN(
        LOG_MODULE_PLAYER,
        "Cannot stop: player not opened or playback controller not available");
    return;
  }

  if (state_manager_->IsStopped()) {
    MODULE_INFO(LOG_MODULE_PLAYER, "Already stopped");
    return;
  }

  // ⚠️ 先转换状态，让工作线程看到停止信号
  state_manager_->TransitionToStopped();

  // 然后停止所有线程（线程会检查 ShouldStop() 并退出）
  playback_controller_->Stop();

  MODULE_INFO(LOG_MODULE_PLAYER, "Playback stopped");
  return;
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
