#include "wasapi_audio_output.h"

#ifdef _WIN32

#include <cassert>
#include <iostream>

// COM库和错误处理
#pragma comment(lib, "ole32.lib")

#include "player/common/log_manager.h"

namespace zenplay {

WasapiAudioOutput::WasapiAudioOutput()
    : device_enumerator_(nullptr),
      audio_device_(nullptr),
      audio_client_(nullptr),
      render_client_(nullptr),
      volume_control_(nullptr),
      wave_format_(nullptr),
      buffer_frame_count_(0),
      user_data_(nullptr),
      is_playing_(false),
      is_paused_(false),
      should_stop_(false),
      volume_(1.0f),
      com_initialized_(false) {}

WasapiAudioOutput::~WasapiAudioOutput() {
  Cleanup();
}

bool WasapiAudioOutput::Init(const AudioSpec& spec,
                             AudioOutputCallback callback,
                             void* user_data) {
  audio_spec_ = spec;
  audio_callback_ = callback;
  user_data_ = user_data;

  // 1. 初始化COM
  if (!InitializeCOM()) {
    std::cerr << "Failed to initialize COM" << std::endl;
    return false;
  }

  // 2. 获取默认音频设备
  if (!GetDefaultAudioDevice()) {
    std::cerr << "Failed to get default audio device" << std::endl;
    return false;
  }

  // 3. 创建音频客户端
  if (!CreateAudioClient()) {
    std::cerr << "Failed to create audio client" << std::endl;
    return false;
  }

  // 4. 配置音频格式
  if (!ConfigureAudioFormat()) {
    std::cerr << "Failed to configure audio format" << std::endl;
    return false;
  }

  return true;
}

bool WasapiAudioOutput::Start() {
  if (is_playing_.load()) {
    MODULE_WARN(LOG_MODULE_AUDIO, "WASAPI already playing");
    return true;
  }

  MODULE_INFO(LOG_MODULE_AUDIO, "Starting WASAPI audio output");

  if (!StartAudioService()) {
    MODULE_ERROR(LOG_MODULE_AUDIO, "Failed to start WASAPI audio service");
    return false;
  }

  should_stop_ = false;
  is_paused_ = false;

  // 启动音频播放线程
  audio_thread_ =
      std::make_unique<std::thread>(&WasapiAudioOutput::AudioThreadMain, this);

  is_playing_ = true;
  MODULE_INFO(LOG_MODULE_AUDIO, "WASAPI audio output started successfully");
  return true;
}

void WasapiAudioOutput::Stop() {
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

  // 停止音频客户端
  if (audio_client_) {
    audio_client_->Stop();
  }
}

void WasapiAudioOutput::Pause() {
  is_paused_ = true;
  if (audio_client_) {
    audio_client_->Stop();
  }
}

void WasapiAudioOutput::Resume() {
  is_paused_ = false;
  if (audio_client_) {
    audio_client_->Start();
  }
}

void WasapiAudioOutput::SetVolume(float volume) {
  volume_.store(std::max(0.0f, std::min(1.0f, volume)));

  std::lock_guard<std::mutex> lock(volume_mutex_);
  if (volume_control_) {
    volume_control_->SetMasterVolume(volume_.load(), nullptr);
  }
}

float WasapiAudioOutput::GetVolume() const {
  return volume_.load();
}

void WasapiAudioOutput::Cleanup() {
  Stop();
  ReleaseCOMResources();

  if (wave_format_) {
    CoTaskMemFree(wave_format_);
    wave_format_ = nullptr;
  }

  if (com_initialized_) {
    CoUninitialize();
    com_initialized_ = false;
  }
}

const char* WasapiAudioOutput::GetDeviceName() const {
  return device_name_.c_str();
}

bool WasapiAudioOutput::IsPlaying() const {
  return is_playing_.load();
}

bool WasapiAudioOutput::InitializeCOM() {
  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
    return false;
  }
  com_initialized_ = true;
  return true;
}

bool WasapiAudioOutput::GetDefaultAudioDevice() {
  HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                (void**)&device_enumerator_);

  if (FAILED(hr)) {
    return false;
  }

  hr = device_enumerator_->GetDefaultAudioEndpoint(eRender, eMultimedia,
                                                   &audio_device_);

  if (FAILED(hr)) {
    return false;
  }

  // 获取设备名称
  IPropertyStore* properties;
  if (SUCCEEDED(audio_device_->OpenPropertyStore(STGM_READ, &properties))) {
    PROPVARIANT device_name_prop;
    PropVariantInit(&device_name_prop);

    if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName,
                                       &device_name_prop))) {
      // 转换宽字符到多字节
      int buffer_size =
          WideCharToMultiByte(CP_UTF8, 0, device_name_prop.pwszVal, -1, nullptr,
                              0, nullptr, nullptr);
      if (buffer_size > 0) {
        device_name_.resize(buffer_size - 1);
        WideCharToMultiByte(CP_UTF8, 0, device_name_prop.pwszVal, -1,
                            &device_name_[0], buffer_size, nullptr, nullptr);
      }
    }

    PropVariantClear(&device_name_prop);
    properties->Release();
  }

  return true;
}

bool WasapiAudioOutput::CreateAudioClient() {
  HRESULT hr = audio_device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                                       nullptr, (void**)&audio_client_);

  return SUCCEEDED(hr);
}

bool WasapiAudioOutput::ConfigureAudioFormat() {
  wave_format_ = CreateWaveFormat(audio_spec_);
  if (!wave_format_) {
    return false;
  }

  // 检查设备是否支持该格式
  WAVEFORMATEX* closest_match = nullptr;
  HRESULT hr = audio_client_->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED,
                                                wave_format_, &closest_match);

  if (hr == S_FALSE && closest_match) {
    // 使用最接近的格式
    CoTaskMemFree(wave_format_);
    wave_format_ = closest_match;
  } else if (FAILED(hr)) {
    return false;
  }

  // 初始化音频客户端（使用轮询模式，不需要事件回调）
  hr = audio_client_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                 0,         // 不使用特殊标志，采用轮询模式
                                 10000000,  // 1秒的缓冲区（100ns 单位）
                                 0,         // 共享模式下为0
                                 wave_format_, nullptr);

  if (FAILED(hr)) {
    return false;
  }

  // 获取缓冲区大小
  hr = audio_client_->GetBufferSize(&buffer_frame_count_);
  if (FAILED(hr)) {
    return false;
  }

  // 获取渲染客户端
  hr = audio_client_->GetService(__uuidof(IAudioRenderClient),
                                 (void**)&render_client_);
  if (FAILED(hr)) {
    return false;
  }

  // 获取音量控制
  hr = audio_client_->GetService(__uuidof(ISimpleAudioVolume),
                                 (void**)&volume_control_);
  // 音量控制是可选的，不需要检查结果

  return true;
}

bool WasapiAudioOutput::StartAudioService() {
  if (!audio_client_) {
    MODULE_ERROR(LOG_MODULE_AUDIO, "WASAPI audio_client_ is null");
    return false;
  }

  HRESULT hr = audio_client_->Start();
  if (FAILED(hr)) {
    MODULE_ERROR(LOG_MODULE_AUDIO,
                 "WASAPI IAudioClient::Start() failed with HRESULT: 0x{:08X}",
                 static_cast<unsigned int>(hr));
    return false;
  }

  MODULE_INFO(LOG_MODULE_AUDIO, "WASAPI IAudioClient::Start() succeeded");
  return true;
}

void WasapiAudioOutput::AudioThreadMain() {
  MODULE_INFO(LOG_MODULE_AUDIO, "WASAPI audio thread started");

  // 设置线程优先级
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

  const UINT32 frame_size = wave_format_->nBlockAlign;
  const UINT32 buffer_duration_ms = 10;  // 10ms缓冲

  MODULE_INFO(
      LOG_MODULE_AUDIO,
      "WASAPI thread loop starting: buffer_frame_count={}, frame_size={}",
      buffer_frame_count_, frame_size);

  int callback_count = 0;

  while (!should_stop_.load()) {
    if (is_paused_.load()) {
      Sleep(10);
      continue;
    }

    // 获取当前填充的帧数
    UINT32 padding_frames;
    HRESULT hr = audio_client_->GetCurrentPadding(&padding_frames);
    if (FAILED(hr)) {
      MODULE_ERROR(LOG_MODULE_AUDIO, "GetCurrentPadding failed: 0x{:08X}",
                   static_cast<unsigned int>(hr));
      break;
    }

    // 计算可用的帧数
    UINT32 available_frames = buffer_frame_count_ - padding_frames;
    if (available_frames == 0) {
      Sleep(1);
      continue;
    }

    // 获取渲染缓冲区
    BYTE* render_buffer;
    hr = render_client_->GetBuffer(available_frames, &render_buffer);
    if (FAILED(hr)) {
      MODULE_ERROR(LOG_MODULE_AUDIO, "GetBuffer failed: 0x{:08X}",
                   static_cast<unsigned int>(hr));
      break;
    }

    // 计算需要的字节数
    UINT32 bytes_to_fill = available_frames * frame_size;

    // 调用用户回调获取音频数据
    int bytes_filled = 0;
    if (audio_callback_) {
      if (callback_count == 0) {
        MODULE_INFO(LOG_MODULE_AUDIO,
                    "About to call audio_callback_ for the first time");
      }

      bytes_filled = audio_callback_(user_data_, render_buffer, bytes_to_fill);

      if (callback_count == 0) {
        MODULE_INFO(LOG_MODULE_AUDIO,
                    "First audio_callback_ returned: {} bytes", bytes_filled);
      }

      if (++callback_count % 100 == 0) {
        MODULE_DEBUG(LOG_MODULE_AUDIO,
                     "WASAPI callback called {} times, last fill: {} bytes",
                     callback_count, bytes_filled);
      }
    } else {
      MODULE_ERROR(LOG_MODULE_AUDIO, "audio_callback_ is null!");
    }

    // 如果回调没有提供足够数据，用静音填充
    if (bytes_filled < (int)bytes_to_fill) {
      memset(render_buffer + bytes_filled, 0, bytes_to_fill - bytes_filled);
    }

    // 释放缓冲区
    hr = render_client_->ReleaseBuffer(available_frames, 0);
    if (FAILED(hr)) {
      MODULE_ERROR(LOG_MODULE_AUDIO, "ReleaseBuffer failed: 0x{:08X}",
                   static_cast<unsigned int>(hr));
      break;
    }

    // 短暂休眠
    Sleep(buffer_duration_ms);
  }

  MODULE_INFO(LOG_MODULE_AUDIO,
              "WASAPI audio thread exiting, total callbacks: {}",
              callback_count);
}

WAVEFORMATEX* WasapiAudioOutput::CreateWaveFormat(const AudioSpec& spec) {
  WAVEFORMATEX* wave_format =
      (WAVEFORMATEX*)CoTaskMemAlloc(sizeof(WAVEFORMATEX));
  if (!wave_format) {
    return nullptr;
  }

  wave_format->wFormatTag = WAVE_FORMAT_PCM;
  wave_format->nChannels = spec.channels;
  wave_format->nSamplesPerSec = spec.sample_rate;
  wave_format->wBitsPerSample = spec.bits_per_sample;
  wave_format->nBlockAlign =
      (wave_format->nChannels * wave_format->wBitsPerSample) / 8;
  wave_format->nAvgBytesPerSec =
      wave_format->nSamplesPerSec * wave_format->nBlockAlign;
  wave_format->cbSize = 0;

  return wave_format;
}

void WasapiAudioOutput::ReleaseCOMResources() {
  if (volume_control_) {
    volume_control_->Release();
    volume_control_ = nullptr;
  }

  if (render_client_) {
    render_client_->Release();
    render_client_ = nullptr;
  }

  if (audio_client_) {
    audio_client_->Release();
    audio_client_ = nullptr;
  }

  if (audio_device_) {
    audio_device_->Release();
    audio_device_ = nullptr;
  }

  if (device_enumerator_) {
    device_enumerator_->Release();
    device_enumerator_ = nullptr;
  }
}

}  // namespace zenplay

#endif  // _WIN32
