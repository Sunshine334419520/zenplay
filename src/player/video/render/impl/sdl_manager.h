#pragma once

#include <atomic>
#include <mutex>

extern "C" {
#include <SDL2/SDL.h>
}

namespace zenplay {

/**
 * @brief SDL全局管理器 - 确保SDL正确初始化和清理
 */
class SDLManager {
 public:
  static SDLManager& Instance();

  bool Initialize();
  void Shutdown();

  bool IsInitialized() const { return initialized_.load(); }

 private:
  SDLManager() = default;
  ~SDLManager();

  std::atomic<bool> initialized_{false};
  std::atomic<int> ref_count_{0};
  mutable std::mutex mutex_;
};

}  // namespace zenplay
