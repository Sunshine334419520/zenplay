#include "player_state_manager.h"

#include "player/common/log_manager.h"

namespace zenplay {

PlayerStateManager::PlayerStateManager() : current_state_(PlayerState::kIdle) {}

PlayerStateManager::~PlayerStateManager() {
  // 确保所有等待的线程被唤醒
  {
    std::lock_guard<std::mutex> lock(pause_mutex_);
    current_state_.store(PlayerState::kIdle);
  }
  pause_cv_.notify_all();
}

// ========== 状态查询 ==========

PlayerStateManager::PlayerState PlayerStateManager::GetState() const {
  return current_state_.load(std::memory_order_acquire);
}

bool PlayerStateManager::ShouldStop() const {
  auto state = GetState();
  return state == PlayerState::kIdle || state == PlayerState::kStopped ||
         state == PlayerState::kError;
}

bool PlayerStateManager::ShouldPause() const {
  auto state = GetState();
  return state == PlayerState::kPaused || state == PlayerState::kBuffering ||
         state == PlayerState::kSeeking;
}

bool PlayerStateManager::WaitForResume(int timeout_ms) {
  std::unique_lock<std::mutex> lock(pause_mutex_);

  auto predicate = [this]() {
    auto state = GetState();
    // 继续执行的条件：正在播放 或 应该停止
    return state == PlayerState::kPlaying || ShouldStop();
  };

  if (timeout_ms > 0) {
    return pause_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                              predicate);
  } else {
    pause_cv_.wait(lock, predicate);
    return true;
  }
}

// ========== 状态转换 ==========

bool PlayerStateManager::RequestStateChange(PlayerState new_state) {
  PlayerState old_state = current_state_.load(std::memory_order_acquire);

  // 状态相同，无需转换
  if (old_state == new_state) {
    return true;
  }

  // 检查转换是否合法
  if (!IsValidTransition(old_state, new_state)) {
    MODULE_WARN(LOG_MODULE_PLAYER, "Invalid state transition: {} -> {}",
                GetStateName(old_state), GetStateName(new_state));
    return false;
  }

  // 原子地更新状态
  if (!current_state_.compare_exchange_strong(old_state, new_state,
                                              std::memory_order_release,
                                              std::memory_order_acquire)) {
    // CAS 失败，说明状态被其他线程修改了，重试
    MODULE_DEBUG(LOG_MODULE_PLAYER, "State change CAS failed, retrying...");
    return RequestStateChange(new_state);
  }

  MODULE_INFO(LOG_MODULE_PLAYER, "State changed: {} -> {}",
              GetStateName(old_state), GetStateName(new_state));

  // 通知状态变更
  NotifyStateChange(old_state, new_state);

  // ✅ 唤醒等待的线程（关键修复）
  // 1. 转换到 Playing：恢复播放
  // 2. 转换到 Stopped/Idle/Error：停止信号，让 WaitForResume() 返回
  if (new_state == PlayerState::kPlaying ||
      new_state == PlayerState::kStopped || new_state == PlayerState::kIdle ||
      new_state == PlayerState::kError) {
    pause_cv_.notify_all();
  }

  return true;
}

bool PlayerStateManager::TransitionToIdle() {
  return RequestStateChange(PlayerState::kIdle);
}

bool PlayerStateManager::TransitionToOpening() {
  return RequestStateChange(PlayerState::kOpening);
}

bool PlayerStateManager::TransitionToStopped() {
  return RequestStateChange(PlayerState::kStopped);
}

bool PlayerStateManager::TransitionToPlaying() {
  return RequestStateChange(PlayerState::kPlaying);
}

bool PlayerStateManager::TransitionToPaused() {
  return RequestStateChange(PlayerState::kPaused);
}

bool PlayerStateManager::TransitionToSeeking() {
  return RequestStateChange(PlayerState::kSeeking);
}

bool PlayerStateManager::TransitionToBuffering() {
  return RequestStateChange(PlayerState::kBuffering);
}

bool PlayerStateManager::TransitionToError() {
  return RequestStateChange(PlayerState::kError);
}

// ========== 状态通知 ==========

int PlayerStateManager::RegisterStateChangeCallback(
    StateChangeCallback callback) {
  std::lock_guard<std::mutex> lock(callbacks_mutex_);
  int id = next_callback_id_++;
  callbacks_.emplace_back(id, std::move(callback));
  return id;
}

void PlayerStateManager::UnregisterStateChangeCallback(int callback_id) {
  std::lock_guard<std::mutex> lock(callbacks_mutex_);
  callbacks_.erase(std::remove_if(callbacks_.begin(), callbacks_.end(),
                                  [callback_id](const auto& pair) {
                                    return pair.first == callback_id;
                                  }),
                   callbacks_.end());
}

// ========== 调试辅助 ==========

const char* PlayerStateManager::GetStateName(PlayerState state) {
  switch (state) {
    case PlayerState::kIdle:
      return "Idle";
    case PlayerState::kOpening:
      return "Opening";
    case PlayerState::kStopped:
      return "Stopped";
    case PlayerState::kPlaying:
      return "Playing";
    case PlayerState::kPaused:
      return "Paused";
    case PlayerState::kSeeking:
      return "Seeking";
    case PlayerState::kBuffering:
      return "Buffering";
    case PlayerState::kError:
      return "Error";
    default:
      return "Unknown";
  }
}

// ========== 私有方法 ==========

bool PlayerStateManager::IsValidTransition(PlayerState from,
                                           PlayerState to) const {
  // 状态转换规则表
  switch (from) {
    case PlayerState::kIdle:
      // Idle 只能转到 Opening
      return to == PlayerState::kOpening;

    case PlayerState::kOpening:
      // Opening 可以转到 Stopped/Error
      return to == PlayerState::kStopped || to == PlayerState::kError;

    case PlayerState::kStopped:
      // Stopped 可以转到 Playing/Idle/Seeking
      return to == PlayerState::kPlaying || to == PlayerState::kIdle ||
             to == PlayerState::kSeeking;

    case PlayerState::kPlaying:
      // Playing 可以转到 Paused/Stopped/Seeking/Buffering/Error
      return to == PlayerState::kPaused || to == PlayerState::kStopped ||
             to == PlayerState::kSeeking || to == PlayerState::kBuffering ||
             to == PlayerState::kError;

    case PlayerState::kPaused:
      // Paused 可以转到 Playing/Stopped/Seeking
      return to == PlayerState::kPlaying || to == PlayerState::kStopped ||
             to == PlayerState::kSeeking;

    case PlayerState::kSeeking:
      // Seeking 可以转到 Playing/Stopped/Paused/Buffering/Error
      return to == PlayerState::kPlaying || to == PlayerState::kStopped ||
             to == PlayerState::kPaused || to == PlayerState::kBuffering ||
             to == PlayerState::kError;

    case PlayerState::kBuffering:
      // Buffering 可以转到 Playing/Stopped/Error
      return to == PlayerState::kPlaying || to == PlayerState::kStopped ||
             to == PlayerState::kError;

    case PlayerState::kError:
      // Error 可以转到 Idle/Stopped
      return to == PlayerState::kIdle || to == PlayerState::kStopped;

    default:
      return false;
  }
}

void PlayerStateManager::NotifyStateChange(PlayerState old_state,
                                           PlayerState new_state) {
  std::lock_guard<std::mutex> lock(callbacks_mutex_);
  for (const auto& [id, callback] : callbacks_) {
    if (callback) {
      try {
        callback(old_state, new_state);
      } catch (const std::exception& e) {
        MODULE_ERROR(LOG_MODULE_PLAYER, "State change callback exception: {}",
                     e.what());
      }
    }
  }
}

}  // namespace zenplay
