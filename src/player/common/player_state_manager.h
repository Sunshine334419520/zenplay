#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <vector>

namespace zenplay {

/**
 * @brief 播放器状态管理器 - 统一管理所有播放状态
 *
 * 设计原则：
 * 1. 单一状态源（Single Source of Truth）
 * 2. 状态转换原子性
 * 3. 线程安全
 * 4. 观察者模式通知状态变更
 */
class PlayerStateManager {
 public:
  /**
   * @brief 播放器状态枚举
   */
  enum class PlayerState {
    kIdle,       // 空闲（未打开文件）
    kOpening,    // 正在打开文件
    kStopped,    // 已停止（文件已打开但未播放）
    kPlaying,    // 正在播放
    kPaused,     // 已暂停
    kSeeking,    // 正在跳转
    kBuffering,  // 缓冲中
    kError       // 错误状态
  };

  /**
   * @brief 组件状态（用于子组件标识自己）
   */
  enum class ComponentState {
    kIdle,     // 空闲
    kRunning,  // 运行中
    kPaused,   // 暂停
    kStopping  // 正在停止
  };

  /**
   * @brief 状态变更回调
   * @param old_state 旧状态
   * @param new_state 新状态
   */
  using StateChangeCallback =
      std::function<void(PlayerState old_state, PlayerState new_state)>;

  PlayerStateManager();
  ~PlayerStateManager();

  // ========== 状态查询 ==========

  /**
   * @brief 获取当前播放器状态
   */
  PlayerState GetState() const;

  /**
   * @brief 检查是否处于特定状态
   */
  bool IsIdle() const { return GetState() == PlayerState::kIdle; }
  bool IsOpening() const { return GetState() == PlayerState::kOpening; }
  bool IsStopped() const { return GetState() == PlayerState::kStopped; }
  bool IsPlaying() const { return GetState() == PlayerState::kPlaying; }
  bool IsPaused() const { return GetState() == PlayerState::kPaused; }
  bool IsSeeking() const { return GetState() == PlayerState::kSeeking; }
  bool IsBuffering() const { return GetState() == PlayerState::kBuffering; }
  bool IsError() const { return GetState() == PlayerState::kError; }

  /**
   * @brief 检查是否应该停止工作线程
   * @return true 表示线程应该退出
   */
  bool ShouldStop() const;

  /**
   * @brief 检查是否应该暂停工作
   * @return true 表示应该暂停处理
   */
  bool ShouldPause() const;

  /**
   * @brief 等待非暂停状态
   * @param timeout_ms 超时时间（毫秒），0表示无限等待
   * @return true 表示已恢复，false 表示超时或应该停止
   */
  bool WaitForResume(int timeout_ms = 0);

  // ========== 状态转换 ==========

  /**
   * @brief 请求状态转换
   * @param new_state 目标状态
   * @return true 表示转换成功，false 表示不允许此转换
   */
  bool RequestStateChange(PlayerState new_state);

  /**
   * @brief 转换到特定状态的便捷方法
   */
  bool TransitionToIdle();
  bool TransitionToOpening();
  bool TransitionToStopped();
  bool TransitionToPlaying();
  bool TransitionToPaused();
  bool TransitionToSeeking();
  bool TransitionToBuffering();
  bool TransitionToError();

  // ========== 状态通知 ==========

  /**
   * @brief 注册状态变更回调
   * @param callback 回调函数
   * @return 回调ID，用于取消注册
   */
  int RegisterStateChangeCallback(StateChangeCallback callback);

  /**
   * @brief 取消注册状态变更回调
   * @param callback_id 回调ID
   */
  void UnregisterStateChangeCallback(int callback_id);

  // ========== 调试辅助 ==========

  /**
   * @brief 获取状态名称字符串
   */
  static const char* GetStateName(PlayerState state);

 private:
  /**
   * @brief 检查状态转换是否合法
   */
  bool IsValidTransition(PlayerState from, PlayerState to) const;

  /**
   * @brief 通知所有观察者状态变更
   */
  void NotifyStateChange(PlayerState old_state, PlayerState new_state);

  // 当前状态（原子操作）
  std::atomic<PlayerState> current_state_;

  // 状态变更通知
  mutable std::mutex callbacks_mutex_;
  std::vector<std::pair<int, StateChangeCallback>> callbacks_;
  int next_callback_id_ = 0;

  // 暂停/恢复同步
  mutable std::mutex pause_mutex_;
  std::condition_variable pause_cv_;
};

}  // namespace zenplay
