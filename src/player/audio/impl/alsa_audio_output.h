#pragma once

#include "../audio_output.h"

#ifdef __linux__

#include <alsa/asoundlib.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace zenplay {

/**
 * @brief Linux ALSA音频输出实现
 *
 * 使用ALSA (Advanced Linux Sound Architecture) 进行音频播放
 * 支持PCM播放和音量控制
 */
class AlsaAudioOutput : public AudioOutput {
 public:
  AlsaAudioOutput();
  ~AlsaAudioOutput() override;

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

 private:
  /**
   * @brief 打开ALSA PCM设备
   */
  bool OpenPCMDevice();

  /**
   * @brief 配置ALSA参数
   */
  bool ConfigurePCMParams();

  /**
   * @brief 打开ALSA混音器(用于音量控制)
   */
  bool OpenMixer();

  /**
   * @brief 音频播放线程主函数
   */
  void AudioThreadMain();

  /**
   * @brief 转换采样格式
   */
  snd_pcm_format_t ConvertSampleFormat(AVSampleFormat format,
                                       int bits_per_sample);

  /**
   * @brief 关闭ALSA设备
   */
  void CloseDevices();

 private:
  // ALSA句柄
  snd_pcm_t* pcm_handle_;
  snd_mixer_t* mixer_handle_;
  snd_mixer_elem_t* volume_element_;

  // 音频配置
  AudioSpec audio_spec_;
  snd_pcm_uframes_t buffer_frames_;
  snd_pcm_uframes_t period_frames_;

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
  long volume_min_;
  long volume_max_;

  // 设备信息
  std::string device_name_;
  std::string pcm_device_name_;
  std::string mixer_device_name_;
};

}  // namespace zenplay

#endif  // __linux__
