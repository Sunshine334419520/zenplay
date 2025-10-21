#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

#include "loki/src/callback.h"
#include "loki/src/threading/loki_thread.h"
#include "player/codec/decode.h"
#include "player/common/blocking_queue.h"
#include "player/common/error.h"
#include "player/common/player_state_manager.h"
#include "player/sync/av_sync_controller.h"

extern "C" {
#include <libavformat/avformat.h>
}

namespace zenplay {

class Demuxer;
class VideoDecoder;
class AudioDecoder;
class Renderer;
class VideoPlayer;
class AudioPlayer;
class PlayerStateManager;

/**
 * @brief 播放控制器 - 统一协调音视频播放和同步
 *
 * 职责：
 * 1. 管理解封装和解码线程
 * 2. 协调AudioPlayer和VideoPlayer
 * 3. 控制AVSyncController进行音视频同步
 * 4. 提供统一的播放控制接口
 */

// 播放控制器，管理所有播放线程
class PlaybackController {
 public:
  PlaybackController(std::shared_ptr<PlayerStateManager> state_manager,
                     Demuxer* demuxer,
                     VideoDecoder* video_decoder,
                     AudioDecoder* audio_decoder,
                     Renderer* renderer);
  ~PlaybackController();

  /**
   * @brief 启动播放
   * @return Result<void> 成功返回Ok，失败返回错误码
   */
  Result<void> Start();

  /**
   * @brief 停止播放
   */
  void Stop();

  /**
   * @brief 暂停播放
   */
  void Pause();

  /**
   * @brief 恢复播放
   */
  void Resume();

  /**
   * @brief 跳转到指定时间（同步版本，已弃用）
   * @param timestamp_ms 目标时间戳(毫秒)
   * @return 成功返回true
   * @deprecated 使用 SeekAsync 替代
   */
  bool Seek(int64_t timestamp_ms);

  /**
   * @brief 异步跳转到指定时间
   * @param timestamp_ms 目标时间戳(毫秒)
   * @param backward 是否向后搜索关键帧
   * @note 此方法立即返回，实际跳转在后台线程执行
   *       通过 PlayerStateManager 状态通知结果：
   *       - kSeeking: 开始跳转
   *       - kPlaying/kPaused: 跳转成功
   *       - kError: 跳转失败
   */
  void SeekAsync(int64_t timestamp_ms, bool backward = true);

  /**
   * @brief 设置音量
   * @param volume 音量值(0.0-1.0)
   */
  void SetVolume(float volume);

  /**
   * @brief 获取音量
   */
  float GetVolume() const;

  /**
   * @brief 获取当前播放时间
   * @return 当前播放时间（毫秒）
   */
  int64_t GetCurrentTime() const;

 private:
  /**
   * @brief Seek 请求结构
   */
  struct SeekRequest {
    int64_t timestamp_ms;
    bool backward;
    PlayerStateManager::PlayerState restore_state;

    SeekRequest(int64_t ts, bool bw, PlayerStateManager::PlayerState state)
        : timestamp_ms(ts), backward(bw), restore_state(state) {}
  };

  /**
   * @brief Seek 执行线程
   */
  void SeekTask();

  /**
   * @brief 执行单次 Seek 操作（内部方法）
   */
  bool ExecuteSeek(const SeekRequest& request);

  /**
   * @brief 清空所有队列（packet 和 frame）
   * @note 用于 Seek、Stop 等需要清空缓冲的场景
   */
  void ClearAllQueues();

  // 解封装任务 - 在专门的工作线程执行
  void DemuxTask();

  // 视频解码任务 - 在专门的解码线程执行
  void VideoDecodeTask();

  // 音频解码任务 - 在专门的解码线程执行
  void AudioDecodeTask();

  // 同步控制任务 - 定期更新时钟同步
  void SyncControlTask();

  // 停止所有线程
  void StopAllThreads();

 private:
  // 组件引用
  Demuxer* demuxer_;
  VideoDecoder* video_decoder_;
  AudioDecoder* audio_decoder_;
  Renderer* renderer_;

  // 播放器组件
  std::unique_ptr<AudioPlayer> audio_player_;
  std::unique_ptr<VideoPlayer> video_player_;
  std::unique_ptr<AVSyncController> av_sync_controller_;

  // ✅ 音频重采样器（在解码线程中使用）
  std::unique_ptr<class AudioResampler> audio_resampler_;

  // 状态管理器（共享）
  std::shared_ptr<PlayerStateManager> state_manager_;

  // 数据队列（使用 BlockingQueue 替代轮询）
  BlockingQueue<AVPacket*> video_packet_queue_{80};  // 视频包队列，容量 200
  BlockingQueue<AVPacket*> audio_packet_queue_{80};  // 音频包队列，容量 200

  // 解码线程（使用std::thread，因为需要持续运行）
  std::unique_ptr<std::thread> demux_thread_;
  std::unique_ptr<std::thread> video_decode_thread_;
  std::unique_ptr<std::thread> audio_decode_thread_;
  std::unique_ptr<std::thread> sync_control_thread_;

  // Seek 专用线程和队列
  std::unique_ptr<std::thread> seek_thread_;
  BlockingQueue<SeekRequest> seek_request_queue_{10};  // Seek 请求队列，容量 10
  std::atomic<bool> seeking_{false};
};

}  // namespace zenplay
