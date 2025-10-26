#include "player/video/render/impl/sdl/sdl_manager.h"

#include "player/common/log_manager.h"

namespace zenplay {

SDLManager& SDLManager::Instance() {
  static SDLManager instance;
  return instance;
}

bool SDLManager::Initialize() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (ref_count_.load() == 0) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
      MODULE_ERROR(LOG_MODULE_RENDERER, "Failed to initialize SDL: {}",
                   SDL_GetError());
      return false;
    }
    initialized_ = true;
    MODULE_INFO(LOG_MODULE_RENDERER, "SDL initialized successfully");
  }

  ref_count_++;
  return true;
}

void SDLManager::Shutdown() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (ref_count_.load() > 0) {
    ref_count_--;

    if (ref_count_.load() == 0 && initialized_.load()) {
      SDL_Quit();
      initialized_ = false;
      MODULE_INFO(LOG_MODULE_RENDERER, "SDL shutdown completed");
    }
  }
}

SDLManager::~SDLManager() {
  if (initialized_.load()) {
    SDL_Quit();
  }
}

}  // namespace zenplay
