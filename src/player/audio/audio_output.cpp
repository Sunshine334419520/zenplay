#include "player/audio/audio_output.h"

#ifdef _WIN32
#include "player/audio/impl/wasapi_audio_output.h"
#elif defined(__linux__)
#include "player/audio/impl/alsa_audio_output.h"
#elif defined(__APPLE__)
// TODO: 添加Core Audio实现
#include "player/audio/impl/coreaudio_audio_output.h"
#endif

#include <iostream>

namespace zenplay {

std::unique_ptr<AudioOutput> AudioOutput::Create() {
#ifdef _WIN32
  return std::make_unique<WasapiAudioOutput>();
#elif defined(__linux__)
  return std::make_unique<AlsaAudioOutput>();
#elif defined(__APPLE__)
  // TODO: 实现Core Audio
  // return std::make_unique<CoreAudioOutput>();
  std::cerr << "Core Audio not implemented yet" << std::endl;
  return nullptr;
#else
  std::cerr << "Unsupported platform for audio output" << std::endl;
  return nullptr;
#endif
}

}  // namespace zenplay
