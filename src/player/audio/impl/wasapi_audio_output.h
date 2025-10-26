#pragma once

#include "../audio_output.h"

#pragma once

// Windows Audio Session API (WASAPI) 音频输出实现
// 仅在 Windows 平台可用
#ifdef OS_WIN

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

// Windows WASAPI headers
#include <mmdeviceapi.h>
#include <windows.h>

#include <audioclient.h>
#include <audiopolicy.h>
#include <functiondiscoverykeys_devpkey.h>

namespace zenplay {

/**
 * @brief Windows WASAPI音频输出实现
 *
 * 使用Windows核心音频API (WASAPI) 进行低延迟音频播放
 * 支持独占模式和共享模式
 */
class WasapiAudioOutput : public AudioOutput {
 public:
  WasapiAudioOutput();
  ~WasapiAudioOutput() override;

  // AudioOutput接口实现
  Result<void> Init(const AudioSpec& spec,
                    AudioOutputCallback callback,
                    void* user_data) override;
  Result<void> Start() override;
  void Stop() override;
  void Pause() override;
  void Resume() override;
  void SetVolume(float volume) override;
  float GetVolume() const override;
  void Cleanup() override;
  const char* GetDeviceName() const override;
  bool IsPlaying() const override;
  void Flush() override;

 private:
  /**
   * @brief 初始化COM组件
   */
  bool InitializeCOM();

  /**
   * @brief 获取默认音频设备
   */
  bool GetDefaultAudioDevice();

  /**
   * @brief 创建音频客户端
   */
  bool CreateAudioClient();

  /**
   * @brief 配置音频格式
   */
  bool ConfigureAudioFormat();

  /**
   * @brief 启动音频服务
   */
  bool StartAudioService();

  /**
   * @brief 音频播放线程主函数
   */
  void AudioThreadMain();

  /**
   * @brief WASAPI格式转换
   */
  WAVEFORMATEX* CreateWaveFormat(const AudioSpec& spec);

  /**
   * @brief 释放COM资源
   */
  void ReleaseCOMResources();

 private:
  // WASAPI接口
  IMMDeviceEnumerator* device_enumerator_;
  IMMDevice* audio_device_;
  IAudioClient* audio_client_;
  IAudioRenderClient* render_client_;
  ISimpleAudioVolume* volume_control_;

  // 音频配置
  AudioSpec audio_spec_;
  WAVEFORMATEX* wave_format_;
  UINT32 buffer_frame_count_;

  // 回调和用户数据
  AudioOutputCallback audio_callback_;
  void* user_data_;

  // 线程控制
  std::unique_ptr<std::thread> audio_thread_;
  std::atomic<bool> is_playing_;
  std::atomic<bool> is_paused_;
  std::atomic<bool> should_stop_;

  // 音量控制
  mutable std::mutex volume_mutex_;
  std::atomic<float> volume_;

  // 设备信息
  std::string device_name_;

  // COM初始化状态
  bool com_initialized_;
};

}  // namespace zenplay

#endif  // _WIN32
