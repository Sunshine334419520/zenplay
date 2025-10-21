#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "player/common/error.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
}

namespace zenplay {

/**
 * @brief 音频输出回调函数类型
 * @param user_data 用户数据指针
 * @param buffer 音频数据缓冲区
 * @param buffer_size 缓冲区大小(字节)
 * @return 实际填充的字节数
 */
using AudioOutputCallback =
    std::function<int(void* user_data, uint8_t* buffer, int buffer_size)>;

/**
 * @brief 音频输出设备抽象接口
 *
 * 提供跨平台的音频播放功能，支持：
 * - Windows: WASAPI/DirectSound
 * - Linux: ALSA/PulseAudio
 * - macOS: Core Audio
 */
class AudioOutput {
 public:
  /**
   * @brief 音频参数配置
   */
  struct AudioSpec {
    int sample_rate = 44100;                    // 采样率
    int channels = 2;                           // 声道数
    int bits_per_sample = 16;                   // 位深度
    int buffer_size = 1024;                     // 缓冲区大小(采样点数)
    AVSampleFormat format = AV_SAMPLE_FMT_S16;  // 采样格式
  };

  /**
   * @brief 创建音频输出设备
   * @return 音频输出设备实例，失败返回nullptr
   */
  static std::unique_ptr<AudioOutput> Create();

  AudioOutput() = default;
  virtual ~AudioOutput() = default;

  /**
   * @brief 初始化音频输出设备
   * @param spec 音频参数配置
   * @param callback 音频数据回调函数
   * @param user_data 传递给回调的用户数据
   * @return Result<void> 成功返回Ok，失败返回错误码和消息
   */
  virtual Result<void> Init(const AudioSpec& spec,
                            AudioOutputCallback callback,
                            void* user_data) = 0;

  /**
   * @brief 开始音频播放
   * @return Result<void> 成功返回Ok，失败返回错误码和消息
   */
  virtual Result<void> Start() = 0;

  /**
   * @brief 停止音频播放
   */
  virtual void Stop() = 0;

  /**
   * @brief 暂停音频播放
   */
  virtual void Pause() = 0;

  /**
   * @brief 恢复音频播放
   */
  virtual void Resume() = 0;

  /**
   * @brief 设置音量
   * @param volume 音量值 (0.0 - 1.0)
   */
  virtual void SetVolume(float volume) = 0;

  /**
   * @brief 获取音量
   * @return 当前音量值 (0.0 - 1.0)
   */
  virtual float GetVolume() const = 0;

  /**
   * @brief 清理资源
   */
  virtual void Cleanup() = 0;

  /**
   * @brief 获取设备名称
   */
  virtual const char* GetDeviceName() const = 0;

  /**
   * @brief 检查是否正在播放
   */
  virtual bool IsPlaying() const = 0;

  /**
   * @brief 清空音频硬件缓冲区
   *
   * 用于 Seek 等场景，清除已写入硬件但未播放的音频数据。
   * 必须在音频流停止（Pause/Stop）时调用。
   *
   * @note 调用后，下次播放将从干净的缓冲区开始
   * @note 在 Seek 流程中：Pause() -> Flush() -> Seek -> Resume()
   */
  virtual void Flush() = 0;
};

}  // namespace zenplay
