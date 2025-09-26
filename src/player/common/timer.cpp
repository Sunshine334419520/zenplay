#include "timer.h"

#include <algorithm>

#include "log_manager.h"

#ifdef _WIN32
#include <windows.h>

#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#elif defined(__linux__)
#include <time.h>
#elif defined(__APPLE__)
#include <mach/mach_time.h>
#endif

namespace zenplay {

Timer::Timer(Duration interval,
             TimerType type,
             TimerPrecision precision,
             Callback callback)
    : interval_(interval),
      type_(type),
      precision_(precision),
      callback_(std::move(callback)) {
  // 初始化时间点为当前时间
  last_execution_time_.store(std::chrono::steady_clock::now());

  MODULE_DEBUG(
      LOG_MODULE_PLAYER, "Timer created: interval={}ms, type={}, precision={}",
      interval_.count(), type_ == TimerType::OneShot ? "OneShot" : "Repeating",
      precision_ == TimerPrecision::Standard ? "Standard" : "HighPrecision");
}

Timer::~Timer() {
  Stop();
  MODULE_DEBUG(LOG_MODULE_PLAYER, "Timer destroyed");
}

Timer::Timer(Timer&& other) noexcept {
  std::lock_guard<std::mutex> lock(other.config_mutex_);

  interval_ = other.interval_;
  type_ = other.type_;
  precision_ = other.precision_;
  callback_ = std::move(other.callback_);

  // 移动运行状态
  bool was_running = other.running_.exchange(false);
  other.should_stop_.store(true);

  // 等待其他线程结束
  if (other.timer_thread_ && other.timer_thread_->joinable()) {
    other.timer_thread_->join();
  }
  other.timer_thread_.reset();

  // 移动统计信息
  execution_count_.store(other.execution_count_.exchange(0));
  last_execution_time_.store(other.last_execution_time_.load());

  // 如果原定时器在运行，启动新定时器
  if (was_running) {
    Start();
  }
}

Timer& Timer::operator=(Timer&& other) noexcept {
  if (this != &other) {
    // 停止当前定时器
    Stop();

    std::lock_guard<std::mutex> lock1(config_mutex_);
    std::lock_guard<std::mutex> lock2(other.config_mutex_);

    interval_ = other.interval_;
    type_ = other.type_;
    precision_ = other.precision_;
    callback_ = std::move(other.callback_);

    // 移动运行状态
    bool was_running = other.running_.exchange(false);
    other.should_stop_.store(true);

    // 等待其他线程结束
    if (other.timer_thread_ && other.timer_thread_->joinable()) {
      other.timer_thread_->join();
    }
    other.timer_thread_.reset();

    // 移动统计信息
    execution_count_.store(other.execution_count_.exchange(0));
    last_execution_time_.store(other.last_execution_time_.load());

    // 如果原定时器在运行，启动新定时器
    if (was_running) {
      Start();
    }
  }
  return *this;
}

void Timer::SetCallback(Callback callback) {
  std::lock_guard<std::mutex> lock(config_mutex_);
  callback_ = std::move(callback);
}

void Timer::SetInterval(Duration interval) {
  std::lock_guard<std::mutex> lock(config_mutex_);
  interval_ = interval;

  MODULE_DEBUG(LOG_MODULE_PLAYER, "Timer interval updated: {}ms",
               interval_.count());

  // 如果正在运行，重新启动以应用新间隔
  if (running_.load()) {
    lock.~lock_guard();  // 释放锁避免死锁
    Restart();
  }
}

void Timer::SetType(TimerType type) {
  std::lock_guard<std::mutex> lock(config_mutex_);
  type_ = type;

  MODULE_DEBUG(LOG_MODULE_PLAYER, "Timer type updated: {}",
               type_ == TimerType::OneShot ? "OneShot" : "Repeating");
}

void Timer::SetPrecision(TimerPrecision precision) {
  std::lock_guard<std::mutex> lock(config_mutex_);
  precision_ = precision;

  MODULE_DEBUG(
      LOG_MODULE_PLAYER, "Timer precision updated: {}",
      precision_ == TimerPrecision::Standard ? "Standard" : "HighPrecision");

  // 如果正在运行，重新启动以应用新精度
  if (running_.load()) {
    lock.~lock_guard();  // 释放锁避免死锁
    Restart();
  }
}

bool Timer::Start() {
  std::lock_guard<std::mutex> lock(config_mutex_);

  if (running_.exchange(true)) {
    MODULE_WARN(LOG_MODULE_PLAYER, "Timer already running");
    return false;
  }

  if (!callback_) {
    MODULE_ERROR(LOG_MODULE_PLAYER, "Timer callback not set");
    running_.store(false);
    return false;
  }

  should_stop_.store(false);

  try {
    timer_thread_ =
        std::make_unique<std::thread>(&Timer::TimerThreadMain, this);
    MODULE_INFO(LOG_MODULE_PLAYER, "Timer started: interval={}ms",
                interval_.count());
    return true;
  } catch (const std::exception& e) {
    MODULE_ERROR(LOG_MODULE_PLAYER, "Failed to start timer thread: {}",
                 e.what());
    running_.store(false);
    return false;
  }
}

bool Timer::Stop() {
  if (!running_.exchange(false)) {
    return false;  // 已经停止
  }

  should_stop_.store(true);

  if (timer_thread_ && timer_thread_->joinable()) {
    timer_thread_->join();
  }
  timer_thread_.reset();

  MODULE_INFO(LOG_MODULE_PLAYER, "Timer stopped after {} executions",
              execution_count_.load());
  return true;
}

bool Timer::Restart() {
  Stop();
  return Start();
}

bool Timer::IsRunning() const {
  return running_.load();
}

Timer::Duration Timer::GetInterval() const {
  std::lock_guard<std::mutex> lock(config_mutex_);
  return interval_;
}

TimerType Timer::GetType() const {
  std::lock_guard<std::mutex> lock(config_mutex_);
  return type_;
}

TimerPrecision Timer::GetPrecision() const {
  std::lock_guard<std::mutex> lock(config_mutex_);
  return precision_;
}

uint64_t Timer::GetExecutionCount() const {
  return execution_count_.load();
}

void Timer::ResetExecutionCount() {
  execution_count_.store(0);
  MODULE_DEBUG(LOG_MODULE_PLAYER, "Timer execution count reset");
}

std::chrono::steady_clock::time_point Timer::GetLastExecutionTime() const {
  return last_execution_time_.load();
}

void Timer::TimerThreadMain() {
  MODULE_DEBUG(LOG_MODULE_PLAYER, "Timer thread started");

#ifdef _WIN32
  // Windows: 设置定时器分辨率以提高精度
  UINT period = 1;  // 1ms
  if (precision_ == TimerPrecision::HighPrecision) {
    timeBeginPeriod(period);
  }
#endif

  auto next_execution = std::chrono::steady_clock::now() + interval_;

  while (!should_stop_.load()) {
    auto now = std::chrono::steady_clock::now();

    if (now >= next_execution) {
      // 执行回调
      ExecuteCallback();

      // 更新执行计数和时间
      execution_count_.fetch_add(1);
      last_execution_time_.store(now);

      // 一次性定时器执行后退出
      if (type_ == TimerType::OneShot) {
        break;
      }

      // 计算下次执行时间
      next_execution += interval_;

      // 防止时间累积偏差过大
      if (next_execution < now) {
        next_execution = now + interval_;
      }
    }

    // 计算睡眠时间
    auto sleep_duration = next_execution - now;
    if (sleep_duration > std::chrono::milliseconds(0)) {
      auto sleep_ms = std::chrono::duration_cast<Duration>(sleep_duration);

      if (precision_ == TimerPrecision::HighPrecision) {
        HighPrecisionSleep(sleep_ms);
      } else {
        StandardSleep(sleep_ms);
      }
    }
  }

#ifdef _WIN32
  if (precision_ == TimerPrecision::HighPrecision) {
    timeEndPeriod(period);
  }
#endif

  running_.store(false);
  MODULE_DEBUG(LOG_MODULE_PLAYER, "Timer thread ended");
}

void Timer::HighPrecisionSleep(Duration duration) {
  if (duration <= Duration(0)) {
    return;
  }

  auto start = std::chrono::steady_clock::now();
  auto end = start + duration;

#ifdef _WIN32
  // Windows高精度睡眠：结合Sleep和自旋等待
  if (duration > std::chrono::milliseconds(2)) {
    // 长时间睡眠：先用Sleep减少CPU占用
    auto sleep_time = duration - std::chrono::milliseconds(1);
    Sleep(static_cast<DWORD>(sleep_time.count()));
  }

  // 短时间精确等待：自旋等待
  while (std::chrono::steady_clock::now() < end && !should_stop_.load()) {
    std::this_thread::yield();
  }

#elif defined(__linux__)
  // Linux高精度睡眠：使用nanosleep
  struct timespec req;
  auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(duration);
  req.tv_sec = ns.count() / 1000000000LL;
  req.tv_nsec = ns.count() % 1000000000LL;

  while (nanosleep(&req, &req) == -1 && !should_stop_.load()) {
    // 被信号中断，继续等待剩余时间
    if (std::chrono::steady_clock::now() >= end) {
      break;
    }
  }

#elif defined(__APPLE__)
  // macOS高精度睡眠：使用mach_wait_until
  static mach_timebase_info_data_t timebase;
  static bool timebase_initialized = false;

  if (!timebase_initialized) {
    mach_timebase_info(&timebase);
    timebase_initialized = true;
  }

  uint64_t ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
  uint64_t absolute_time = ns * timebase.denom / timebase.numer;
  uint64_t deadline = mach_absolute_time() + absolute_time;

  mach_wait_until(deadline);

#else
  // 其他平台：回退到标准睡眠
  StandardSleep(duration);
#endif
}

void Timer::StandardSleep(Duration duration) {
  if (duration <= Duration(0)) {
    return;
  }

  // 标准睡眠：使用条件变量实现可中断的睡眠
  std::mutex sleep_mutex;
  std::condition_variable sleep_cv;
  std::unique_lock<std::mutex> lock(sleep_mutex);

  sleep_cv.wait_for(lock, duration, [this] { return should_stop_.load(); });
}

void Timer::ExecuteCallback() {
  if (!callback_) {
    return;
  }

  try {
    callback_();
  } catch (const std::exception& e) {
    MODULE_ERROR(LOG_MODULE_PLAYER, "Timer callback exception: {}", e.what());
  } catch (...) {
    MODULE_ERROR(LOG_MODULE_PLAYER, "Timer callback unknown exception");
  }
}

void Timer::Cleanup() {
  Stop();
}

}  // namespace zenplay
