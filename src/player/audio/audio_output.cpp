#include "player/audio/audio_output.h"

#ifdef OS_WIN
#include "player/audio/impl/wasapi_audio_output.h"
#elif defined(OS_LINUX)
#include "player/audio/impl/alsa_audio_output.h"
#elif defined(OS_MAC)
// TODO: 添加Core Audio实现
#include "player/audio/impl/coreaudio_audio_output.h"
#endif

#include "player/common/log_manager.h"

namespace zenplay {

std::unique_ptr<AudioOutput> AudioOutput::Create() {
#ifdef OS_WIN
  return std::make_unique<WasapiAudioOutput>();
#elif defined(OS_LINUX)
  return std::make_unique<AlsaAudioOutput>();
#elif defined(OS_MAC)
  // TODO: 实现Core Audio
  // return std::make_unique<CoreAudioOutput>();
  MODULE_WARN(LOG_MODULE_AUDIO, "Core Audio not implemented yet");
  return nullptr;
#else
  MODULE_ERROR(LOG_MODULE_AUDIO, "Unsupported platform for audio output");
  return nullptr;
#endif
}

}  // namespace zenplay
