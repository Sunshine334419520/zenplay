#include "player/zen_player.h"

#include <chrono>
#include <future>
#include <thread>

#include "player/codec/audio_decoder.h"
#include "player/codec/video_decoder.h"
#include "player/common/log_manager.h"
#include "player/demuxer/demuxer.h"
#include "player/playback_controller.h"
#include "player/video/render/renderer.h"

namespace zenplay {

ZenPlayer::ZenPlayer()
    : demuxer_(std::make_unique<Demuxer>()),
      video_decoder_(std::make_unique<VideoDecoder>()),
      audio_decoder_(std::make_unique<AudioDecoder>()),
      renderer_(std::unique_ptr<Renderer>(Renderer::CreateRenderer())) {}

ZenPlayer::~ZenPlayer() {
  Close();
}

bool ZenPlayer::Open(const std::string& url) {
  MODULE_INFO(LOG_MODULE_PLAYER, "Opening URL: {}", url.c_str());

  if (is_opened_) {
    Close();
  }

  if (!demuxer_->Open(url)) {
    MODULE_ERROR(LOG_MODULE_PLAYER, "Failed to open demuxer for URL: %s",
                 url.c_str());
    return false;  // Failed to open demuxer
  }

  // Open video decoder
  AVStream* video_stream =
      demuxer_->findStreamByIndex(demuxer_->active_video_stream_index());
  if (video_stream) {
    if (!video_decoder_->Open(video_stream->codecpar)) {
      demuxer_->Close();
      MODULE_ERROR(LOG_MODULE_PLAYER, "Failed to open video decoder");
      return false;  // Failed to open video decoder
    }
  }

  // Open audio decoder
  AVStream* audio_stream =
      demuxer_->findStreamByIndex(demuxer_->active_audio_stream_index());
  if (audio_stream) {
    if (!audio_decoder_->Open(audio_stream->codecpar)) {
      video_decoder_->Close();
      demuxer_->Close();
      MODULE_ERROR(LOG_MODULE_PLAYER, "Failed to open audio decoder");
      return false;  // Failed to open audio decoder
    }
  }

  // 创建播放控制器
  playback_controller_ = std::make_unique<PlaybackController>(
      demuxer_.get(), video_decoder_.get(), audio_decoder_.get(),
      renderer_.get());

  is_opened_ = true;
  state_ = PlayState::kStopped;
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
    renderer_->Init(window_handle, width, height);
    // 通过回调或信号通知初始化完成
  });
  init_thread.detach();  // 分离线程，不等待

  return true;  // 立即返回
}

void ZenPlayer::Close() {
  if (!is_opened_) {
    return;
  }

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
  state_ = PlayState::kStopped;
}

bool ZenPlayer::Play() {
  if (!is_opened_ || !playback_controller_) {
    return false;  // Not opened
  }

  if (state_ == PlayState::kPaused) {
    // 从暂停状态恢复
    playback_controller_->Resume();
    state_ = PlayState::kPlaying;
    return true;
  }

  if (state_ == PlayState::kPlaying) {
    return true;  // Already playing
  }

  // 开始播放
  if (playback_controller_->Start()) {
    state_ = PlayState::kPlaying;
    return true;
  }

  return false;
}

bool ZenPlayer::Pause() {
  if (!is_opened_ || !playback_controller_) {
    return false;
  }

  if (state_ != PlayState::kPlaying) {
    return false;  // Not playing
  }

  playback_controller_->Pause();
  state_ = PlayState::kPaused;
  return true;
}

bool ZenPlayer::Stop() {
  if (!is_opened_ || !playback_controller_) {
    return false;
  }

  if (state_ == PlayState::kStopped) {
    return true;  // Already stopped
  }

  playback_controller_->Stop();
  state_ = PlayState::kStopped;
  return true;
}

bool ZenPlayer::Seek(int64_t timestamp, bool backward) {
  if (!is_opened_ || !playback_controller_) {
    return false;
  }

  return playback_controller_->Seek(timestamp);
}

int ZenPlayer::GetDuration() const {
  if (!is_opened_ || !demuxer_) {
    return 0;
  }
  return demuxer_->GetDuration();
}

int ZenPlayer::GetCurrentPlayTime() const {
  if (!is_opened_ || !playback_controller_) {
    return 0;
  }

  // 从PlaybackController获取当前播放时间
  return playback_controller_->GetCurrentTime();
}

}  // namespace zenplay
