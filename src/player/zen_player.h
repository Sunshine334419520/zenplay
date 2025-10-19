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

  // 通知渲染器窗口大小变化
  void OnWindowResize(int width, int height);

  bool Play();
  bool Pause();
  bool Stop();

  /**
   * @brief 跳转到指定时间点（同步版本，已弃用）
   * @deprecated 使用 SeekAsync 替代
   */
  bool Seek(int64_t timestamp, bool backward = false);

  /**
   * @brief 异步跳转到指定时间点
   * @param timestamp_ms 目标时间戳（毫秒）
   * @param backward 是否向后搜索关键帧
   * @note 立即返回，不阻塞调用线程
   *       通过注册 StateChangeCallback 获取 Seek 结果：
   *       - kSeeking 状态：开始跳转
   *       - kPlaying/kPaused 状态：跳转成功
   *       - kError 状态：跳转失败
   */
  void SeekAsync(int64_t timestamp_ms, bool backward = true);

  /**
   * @brief 注册状态变更回调
   * @param callback 状态变更回调函数
   * @return 回调ID，用于取消注册
   */
  int RegisterStateChangeCallback(
      PlayerStateManager::StateChangeCallback callback);

  /**
   * @brief 取消注册状态变更回调
   * @param callback_id 回调ID
   */
  void UnregisterStateChangeCallback(int callback_id);

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
