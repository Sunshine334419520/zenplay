#include "alsa_audio_output.h"

#ifdef __linux__

#include <algorithm>
#include <iostream>

namespace zenplay {

AlsaAudioOutput::AlsaAudioOutput()
    : pcm_handle_(nullptr),
      mixer_handle_(nullptr),
      volume_element_(nullptr),
      buffer_frames_(0),
      period_frames_(0),
      user_data_(nullptr),
      is_playing_(false),
      is_paused_(false),
      should_stop_(false),
      volume_(1.0f),
      volume_min_(0),
      volume_max_(100),
      device_name_("ALSA Default"),
      pcm_device_name_("default"),
      mixer_device_name_("default") {}

AlsaAudioOutput::~AlsaAudioOutput() {
  Cleanup();
}

bool AlsaAudioOutput::Init(const AudioSpec& spec,
                           AudioOutputCallback callback,
                           void* user_data) {
  audio_spec_ = spec;
  audio_callback_ = callback;
  user_data_ = user_data;

  // 1. 打开PCM设备
  if (!OpenPCMDevice()) {
    std::cerr << "Failed to open PCM device" << std::endl;
    return false;
  }

  // 2. 配置PCM参数
  if (!ConfigurePCMParams()) {
    std::cerr << "Failed to configure PCM parameters" << std::endl;
    return false;
  }

  // 3. 打开混音器(音量控制，可选)
  OpenMixer();

  return true;
}

bool AlsaAudioOutput::Start() {
  if (is_playing_.load()) {
    return true;
  }

  should_stop_ = false;
  is_paused_ = false;

  // 启动音频播放线程
  audio_thread_ =
      std::make_unique<std::thread>(&AlsaAudioOutput::AudioThreadMain, this);

  is_playing_ = true;
  return true;
}

void AlsaAudioOutput::Stop() {
  if (!is_playing_.load()) {
    return;
  }

  should_stop_ = true;
  is_playing_ = false;

  // 等待音频线程结束
  if (audio_thread_ && audio_thread_->joinable()) {
    audio_thread_->join();
    audio_thread_.reset();
  }

  // 停止并重置PCM
  if (pcm_handle_) {
    snd_pcm_drop(pcm_handle_);
    snd_pcm_prepare(pcm_handle_);
  }
}

void AlsaAudioOutput::Pause() {
  is_paused_ = true;
  if (pcm_handle_) {
    snd_pcm_pause(pcm_handle_, 1);
  }
}

void AlsaAudioOutput::Resume() {
  is_paused_ = false;
  if (pcm_handle_) {
    snd_pcm_pause(pcm_handle_, 0);
  }
}

void AlsaAudioOutput::SetVolume(float volume) {
  volume_.store(std::max(0.0f, std::min(1.0f, volume)));

  std::lock_guard<std::mutex> lock(volume_mutex_);
  if (volume_element_) {
    long alsa_volume =
        volume_min_ + (long)((volume_max_ - volume_min_) * volume_.load());
    snd_mixer_selem_set_playback_volume_all(volume_element_, alsa_volume);
  }
}

float AlsaAudioOutput::GetVolume() const {
  return volume_.load();
}

void AlsaAudioOutput::Cleanup() {
  Stop();
  CloseDevices();
}

const char* AlsaAudioOutput::GetDeviceName() const {
  return device_name_.c_str();
}

bool AlsaAudioOutput::IsPlaying() const {
  return is_playing_.load();
}

bool AlsaAudioOutput::OpenPCMDevice() {
  int err = snd_pcm_open(&pcm_handle_, pcm_device_name_.c_str(),
                         SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);

  if (err < 0) {
    std::cerr << "Cannot open PCM device " << pcm_device_name_ << ": "
              << snd_strerror(err) << std::endl;
    return false;
  }

  return true;
}

bool AlsaAudioOutput::ConfigurePCMParams() {
  snd_pcm_hw_params_t* hw_params;
  snd_pcm_sw_params_t* sw_params;
  int err;

  // 分配硬件参数结构
  snd_pcm_hw_params_alloca(&hw_params);
  snd_pcm_hw_params_any(pcm_handle_, hw_params);

  // 设置访问类型
  err = snd_pcm_hw_params_set_access(pcm_handle_, hw_params,
                                     SND_PCM_ACCESS_RW_INTERLEAVED);
  if (err < 0) {
    std::cerr << "Cannot set access type: " << snd_strerror(err) << std::endl;
    return false;
  }

  // 设置采样格式
  snd_pcm_format_t format =
      ConvertSampleFormat(audio_spec_.format, audio_spec_.bits_per_sample);
  err = snd_pcm_hw_params_set_format(pcm_handle_, hw_params, format);
  if (err < 0) {
    std::cerr << "Cannot set sample format: " << snd_strerror(err) << std::endl;
    return false;
  }

  // 设置声道数
  err = snd_pcm_hw_params_set_channels(pcm_handle_, hw_params,
                                       audio_spec_.channels);
  if (err < 0) {
    std::cerr << "Cannot set channel count: " << snd_strerror(err) << std::endl;
    return false;
  }

  // 设置采样率
  unsigned int actual_rate = audio_spec_.sample_rate;
  err =
      snd_pcm_hw_params_set_rate_near(pcm_handle_, hw_params, &actual_rate, 0);
  if (err < 0) {
    std::cerr << "Cannot set sample rate: " << snd_strerror(err) << std::endl;
    return false;
  }

  // 设置缓冲区大小
  buffer_frames_ = audio_spec_.buffer_size * 4;  // 4x buffer size
  err = snd_pcm_hw_params_set_buffer_size_near(pcm_handle_, hw_params,
                                               &buffer_frames_);
  if (err < 0) {
    std::cerr << "Cannot set buffer size: " << snd_strerror(err) << std::endl;
    return false;
  }

  // 设置周期大小
  period_frames_ = buffer_frames_ / 4;
  err = snd_pcm_hw_params_set_period_size_near(pcm_handle_, hw_params,
                                               &period_frames_, 0);
  if (err < 0) {
    std::cerr << "Cannot set period size: " << snd_strerror(err) << std::endl;
    return false;
  }

  // 应用硬件参数
  err = snd_pcm_hw_params(pcm_handle_, hw_params);
  if (err < 0) {
    std::cerr << "Cannot set hardware parameters: " << snd_strerror(err)
              << std::endl;
    return false;
  }

  // 配置软件参数
  snd_pcm_sw_params_alloca(&sw_params);
  snd_pcm_sw_params_current(pcm_handle_, sw_params);

  err = snd_pcm_sw_params_set_start_threshold(pcm_handle_, sw_params,
                                              period_frames_);
  if (err < 0) {
    std::cerr << "Cannot set start threshold: " << snd_strerror(err)
              << std::endl;
    return false;
  }

  err = snd_pcm_sw_params(pcm_handle_, sw_params);
  if (err < 0) {
    std::cerr << "Cannot set software parameters: " << snd_strerror(err)
              << std::endl;
    return false;
  }

  // 准备PCM
  err = snd_pcm_prepare(pcm_handle_);
  if (err < 0) {
    std::cerr << "Cannot prepare PCM: " << snd_strerror(err) << std::endl;
    return false;
  }

  return true;
}

bool AlsaAudioOutput::OpenMixer() {
  int err = snd_mixer_open(&mixer_handle_, 0);
  if (err < 0) {
    std::cerr << "Cannot open mixer: " << snd_strerror(err) << std::endl;
    return false;
  }

  err = snd_mixer_attach(mixer_handle_, mixer_device_name_.c_str());
  if (err < 0) {
    std::cerr << "Cannot attach mixer: " << snd_strerror(err) << std::endl;
    snd_mixer_close(mixer_handle_);
    mixer_handle_ = nullptr;
    return false;
  }

  err = snd_mixer_selem_register(mixer_handle_, nullptr, nullptr);
  if (err < 0) {
    std::cerr << "Cannot register mixer: " << snd_strerror(err) << std::endl;
    snd_mixer_close(mixer_handle_);
    mixer_handle_ = nullptr;
    return false;
  }

  err = snd_mixer_load(mixer_handle_);
  if (err < 0) {
    std::cerr << "Cannot load mixer: " << snd_strerror(err) << std::endl;
    snd_mixer_close(mixer_handle_);
    mixer_handle_ = nullptr;
    return false;
  }

  // 查找Master音量控制
  snd_mixer_selem_id_t* sid;
  snd_mixer_selem_id_alloca(&sid);
  snd_mixer_selem_id_set_index(sid, 0);
  snd_mixer_selem_id_set_name(sid, "Master");

  volume_element_ = snd_mixer_find_selem(mixer_handle_, sid);
  if (!volume_element_) {
    // 尝试PCM控制
    snd_mixer_selem_id_set_name(sid, "PCM");
    volume_element_ = snd_mixer_find_selem(mixer_handle_, sid);
  }

  if (volume_element_) {
    snd_mixer_selem_get_playback_volume_range(volume_element_, &volume_min_,
                                              &volume_max_);
  }

  return true;
}

void AlsaAudioOutput::AudioThreadMain() {
  const int frame_size =
      (audio_spec_.bits_per_sample / 8) * audio_spec_.channels;
  const int buffer_size = period_frames_ * frame_size;
  std::vector<uint8_t> buffer(buffer_size);

  while (!should_stop_.load()) {
    if (is_paused_.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    // 获取音频数据
    int bytes_filled = 0;
    if (audio_callback_) {
      bytes_filled = audio_callback_(user_data_, buffer.data(), buffer_size);
    }

    if (bytes_filled <= 0) {
      // 没有数据，填充静音
      memset(buffer.data(), 0, buffer_size);
      bytes_filled = buffer_size;
    } else if (bytes_filled < buffer_size) {
      // 填充不足，补充静音
      memset(buffer.data() + bytes_filled, 0, buffer_size - bytes_filled);
    }

    // 写入音频数据到ALSA
    snd_pcm_sframes_t frames_to_write = bytes_filled / frame_size;
    snd_pcm_sframes_t frames_written =
        snd_pcm_writei(pcm_handle_, buffer.data(), frames_to_write);

    if (frames_written < 0) {
      // 处理错误
      if (frames_written == -EPIPE) {
        // Buffer underrun
        std::cerr << "ALSA buffer underrun" << std::endl;
        snd_pcm_prepare(pcm_handle_);
      } else if (frames_written == -ESTRPIPE) {
        // Suspend
        while ((frames_written = snd_pcm_resume(pcm_handle_)) == -EAGAIN) {
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (frames_written < 0) {
          snd_pcm_prepare(pcm_handle_);
        }
      }
    }
  }
}

snd_pcm_format_t AlsaAudioOutput::ConvertSampleFormat(AVSampleFormat format,
                                                      int bits_per_sample) {
  switch (format) {
    case AV_SAMPLE_FMT_U8:
      return SND_PCM_FORMAT_U8;
    case AV_SAMPLE_FMT_S16:
      return SND_PCM_FORMAT_S16_LE;
    case AV_SAMPLE_FMT_S32:
      return SND_PCM_FORMAT_S32_LE;
    case AV_SAMPLE_FMT_FLT:
      return SND_PCM_FORMAT_FLOAT_LE;
    case AV_SAMPLE_FMT_DBL:
      return SND_PCM_FORMAT_FLOAT64_LE;
    default:
      // 根据位深度选择默认格式
      if (bits_per_sample == 8) {
        return SND_PCM_FORMAT_U8;
      }
      if (bits_per_sample == 16) {
        return SND_PCM_FORMAT_S16_LE;
      }
      if (bits_per_sample == 24) {
        return SND_PCM_FORMAT_S24_LE;
      }
      if (bits_per_sample == 32) {
        return SND_PCM_FORMAT_S32_LE;
      }
      return SND_PCM_FORMAT_S16_LE;  // 默认
  }
}

void AlsaAudioOutput::CloseDevices() {
  if (pcm_handle_) {
    snd_pcm_close(pcm_handle_);
    pcm_handle_ = nullptr;
  }

  if (mixer_handle_) {
    snd_mixer_close(mixer_handle_);
    mixer_handle_ = nullptr;
    volume_element_ = nullptr;
  }
}

}  // namespace zenplay

#endif  // __linux__
