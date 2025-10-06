#pragma once

#include <memory>
#include <string>

#include "player/common/player_state_manager.h"

namespace zenplay {

class VideoDecoder;
class AudioDecoder;
class Demuxer;
class Renderer;
class PlaybackController;

class ZenPlayer {
 public:
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

  int64_t GetDuration() const;         // 获取总时长（毫秒）
  int64_t GetCurrentPlayTime() const;  // 获取当前播放时间（毫秒）

  // 获取当前状态 - 直接返回 PlayerStateManager 的状态
  PlayerStateManager::PlayerState GetState() const;
  bool IsOpened() const { return is_opened_; }

 private:
  std::unique_ptr<Demuxer> demuxer_;
  std::unique_ptr<VideoDecoder> video_decoder_;
  std::unique_ptr<AudioDecoder> audio_decoder_;
  std::unique_ptr<Renderer> renderer_;
  std::unique_ptr<PlaybackController> playback_controller_;

  // 新：统一的状态管理器
  std::shared_ptr<PlayerStateManager> state_manager_;

  bool is_opened_ = false;
};

}  // namespace zenplay
