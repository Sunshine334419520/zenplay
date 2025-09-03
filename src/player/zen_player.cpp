#include "player/zen_player.h"

#include "player/codec/audio_decoder.h"
#include "player/codec/video_decoder.h"
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
  if (is_opened_) {
    Close();
  }

  if (!demuxer_->Open(url)) {
    return false;  // Failed to open demuxer
  }

  // Open video decoder
  AVStream* video_stream =
      demuxer_->findStreamByIndex(demuxer_->active_video_stream_index());
  if (video_stream) {
    if (!video_decoder_->Open(video_stream->codecpar)) {
      demuxer_->Close();
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
    return false;
  }

  // 初始化渲染器与窗口句柄
  if (!renderer_->Init(window_handle, width, height)) {
    return false;
  }

  return true;
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

}  // namespace zenplay
