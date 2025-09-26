/**
 * @file timer.h
 * @brief 通用定时器类 - 支持高精度和普通精度模式
 *
 * 提供简单易用的定时器接口，支持一次性定时器和重复定时器，
 * 可配置高精度或普通精度模式以适应不同的性能需求。
 */

#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace zenplay {

/**
 * @brief 定时器精度模式
 */
enum class TimerPrecision {
  Standard,      // 标准精度 (±1ms)，资源消耗较低
  HighPrecision  // 高精度 (±0.1ms)，资源消耗较高
};

/**
 * @brief 定时器类型
 */
enum class TimerType {
  OneShot,   // 一次性定时器
  Repeating  // 重复定时器
};

/**
 * @brief 通用定时器类
 *
 * 特性：
 * - 支持高精度和标准精度两种模式
 * - 支持一次性和重复定时器
 * - 线程安全的启动、停止、重置操作
 * - 自动资源管理，无需手动清理
 * - 支持lambda、函数指针、成员函数等多种回调方式
 *
 * 使用示例：
 * @code
 * // 创建一个标准精度的重复定时器，每1000ms执行一次
 * Timer timer(std::chrono::milliseconds(1000), TimerType::Repeating,
 *             TimerPrecision::Standard, []() {
 *   std::cout << "Timer fired!" << std::endl;
 * });
 *
 * timer.Start();  // 启动定时器
 * // ... 一段时间后
 * timer.Stop();   // 停止定时器
 * @endcode
 */
class Timer {
 public:
  using Callback = std::function<void()>;
  using Duration = std::chrono::milliseconds;

  /**
   * @brief 构造函数
   * @param interval 定时间隔
   * @param type 定时器类型（一次性或重复）
   * @param precision 精度模式（标准或高精度）
   * @param callback 定时器回调函数
   */
  Timer(Duration interval,
        TimerType type = TimerType::Repeating,
        TimerPrecision precision = TimerPrecision::Standard,
        Callback callback = nullptr);

  /**
   * @brief 析构函数 - 自动停止定时器并清理资源
   */
  ~Timer();

  // 禁止拷贝构造和赋值
  Timer(const Timer&) = delete;
  Timer& operator=(const Timer&) = delete;

  // 支持移动构造和赋值
  Timer(Timer&& other) noexcept;
  Timer& operator=(Timer&& other) noexcept;

  /**
   * @brief 设置回调函数
   * @param callback 回调函数
   */
  void SetCallback(Callback callback);

  /**
   * @brief 设置定时间隔
   * @param interval 新的时间间隔
   * @note 如果定时器正在运行，会重新启动以应用新间隔
   */
  void SetInterval(Duration interval);

  /**
   * @brief 设置定时器类型
   * @param type 定时器类型
   */
  void SetType(TimerType type);

  /**
   * @brief 设置精度模式
   * @param precision 精度模式
   * @note 如果定时器正在运行，会重新启动以应用新精度
   */
  void SetPrecision(TimerPrecision precision);

  /**
   * @brief 启动定时器
   * @return 成功启动返回true，已经在运行返回false
   */
  bool Start();

  /**
   * @brief 停止定时器
   * @return 成功停止返回true，未在运行返回false
   */
  bool Stop();

  /**
   * @brief 重启定时器
   * @return 重启是否成功
   */
  bool Restart();

  /**
   * @brief 检查定时器是否正在运行
   * @return 正在运行返回true，否则返回false
   */
  bool IsRunning() const;

  /**
   * @brief 获取当前配置的时间间隔
   * @return 时间间隔
   */
  Duration GetInterval() const;

  /**
   * @brief 获取定时器类型
   * @return 定时器类型
   */
  TimerType GetType() const;

  /**
   * @brief 获取精度模式
   * @return 精度模式
   */
  TimerPrecision GetPrecision() const;

  /**
   * @brief 获取定时器已运行次数（仅对重复定时器有意义）
   * @return 运行次数
   */
  uint64_t GetExecutionCount() const;

  /**
   * @brief 重置执行计数
   */
  void ResetExecutionCount();

  /**
   * @brief 获取上次执行时间
   * @return 上次执行的时间点
   */
  std::chrono::steady_clock::time_point GetLastExecutionTime() const;

 private:
  /**
   * @brief 定时器线程主函数
   */
  void TimerThreadMain();

  /**
   * @brief 高精度睡眠实现
   * @param duration 睡眠时长
   */
  void HighPrecisionSleep(Duration duration);

  /**
   * @brief 标准精度睡眠实现
   * @param duration 睡眠时长
   */
  void StandardSleep(Duration duration);

  /**
   * @brief 执行回调函数（带异常保护）
   */
  void ExecuteCallback();

  /**
   * @brief 清理定时器资源
   */
  void Cleanup();

 private:
  // 配置参数
  mutable std::mutex config_mutex_;
  Duration interval_;
  TimerType type_;
  TimerPrecision precision_;
  Callback callback_;

  // 运行状态
  std::atomic<bool> running_{false};
  std::atomic<bool> should_stop_{false};

  // 统计信息
  std::atomic<uint64_t> execution_count_{0};
  std::atomic<std::chrono::steady_clock::time_point> last_execution_time_{};

  // 线程管理
  std::unique_ptr<std::thread> timer_thread_;
};

/**
 * @brief 定时器工厂类 - 提供便捷的创建方法
 */
class TimerFactory {
 public:
  /**
   * @brief 创建标准精度重复定时器
   * @param interval_ms 间隔毫秒数
   * @param callback 回调函数
   * @return 定时器智能指针
   */
  static std::unique_ptr<Timer> CreateRepeating(int interval_ms,
                                                Timer::Callback callback) {
    return std::make_unique<Timer>(
        std::chrono::milliseconds(interval_ms), TimerType::Repeating,
        TimerPrecision::Standard, std::move(callback));
  }

  /**
   * @brief 创建高精度重复定时器
   * @param interval_ms 间隔毫秒数
   * @param callback 回调函数
   * @return 定时器智能指针
   */
  static std::unique_ptr<Timer> CreateHighPrecisionRepeating(
      int interval_ms,
      Timer::Callback callback) {
    return std::make_unique<Timer>(
        std::chrono::milliseconds(interval_ms), TimerType::Repeating,
        TimerPrecision::HighPrecision, std::move(callback));
  }

  /**
   * @brief 创建一次性定时器
   * @param delay_ms 延迟毫秒数
   * @param callback 回调函数
   * @return 定时器智能指针
   */
  static std::unique_ptr<Timer> CreateOneShot(int delay_ms,
                                              Timer::Callback callback) {
    return std::make_unique<Timer>(std::chrono::milliseconds(delay_ms),
                                   TimerType::OneShot, TimerPrecision::Standard,
                                   std::move(callback));
  }

  /**
   * @brief 创建高精度一次性定时器
   * @param delay_ms 延迟毫秒数
   * @param callback 回调函数
   * @return 定时器智能指针
   */
  static std::unique_ptr<Timer> CreateHighPrecisionOneShot(
      int delay_ms,
      Timer::Callback callback) {
    return std::make_unique<Timer>(
        std::chrono::milliseconds(delay_ms), TimerType::OneShot,
        TimerPrecision::HighPrecision, std::move(callback));
  }
};

/**
 * @brief 简化的定时器宏定义
 */
#define ZENPLAY_TIMER_REPEATING(interval_ms, callback) \
  zenplay::TimerFactory::CreateRepeating(interval_ms, callback)

#define ZENPLAY_TIMER_ONESHOT(delay_ms, callback) \
  zenplay::TimerFactory::CreateOneShot(delay_ms, callback)

#define ZENPLAY_TIMER_HIGH_PRECISION(interval_ms, callback) \
  zenplay::TimerFactory::CreateHighPrecisionRepeating(interval_ms, callback)

}  // namespace zenplay
