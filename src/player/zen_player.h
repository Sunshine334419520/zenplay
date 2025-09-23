#pragma once

#include <memory>
#include <string>

namespace zenplay {

class VideoDecoder;
class AudioDecoder;
class Demuxer;
class Renderer;
class PlaybackController;

class ZenPlayer {
 public:
  enum class PlayState { kStopped, kPlaying, kPaused };

  ZenPlayer();
  ~ZenPlayer();

  bool Open(const std::string& url);
  void Close();

  // 设置渲染窗口句柄
  bool SetRenderWindow(void* window_handle, int width, int height);

  bool Play();
  bool Pause();
  bool Stop();

  bool Seek(int64_t timestamp, bool backward = false);

  int GetDuration() const;
  int GetCurrentPlayTime()
      const;  // 获取当前播放时间（秒）- 避免Windows API冲突

  // 获取当前状态
  PlayState GetState() const { return state_; }
  bool IsOpened() const { return is_opened_; }

 private:
  std::unique_ptr<Demuxer> demuxer_;
  std::unique_ptr<VideoDecoder> video_decoder_;
  std::unique_ptr<AudioDecoder> audio_decoder_;
  std::unique_ptr<Renderer> renderer_;
  std::unique_ptr<PlaybackController> playback_controller_;

  PlayState state_ = PlayState::kStopped;
  bool is_opened_ = false;
};

}  // namespace zenplay
