#ifndef ZENPLAY_PLAYER_COMMON_TIMER_UTIL_H_
#define ZENPLAY_PLAYER_COMMON_TIMER_UTIL_H_

#include <chrono>

namespace zenplay {

/**
 * @brief 简单易用的耗时统计工具
 *
 * 使用示例:
 * 1. 基本用法：
 *    TimerUtil timer;  // 构造时开始计时
 *    // ... 执行一些操作
 *    auto elapsed_ms = timer.ElapsedMs();  // 获取耗时（毫秒）
 *
 * 2. 宏定义，更方便：
 *    TIMER_START(decode);
 *    // ... 执行操作
 *    auto ms = TIMER_END_MS(decode);
 */
class TimerUtil {
 public:
  /**
   * @brief 构造函数，开始计时
   */
  TimerUtil() : start_time_(std::chrono::steady_clock::now()) {}

  /**
   * @brief 重新开始计时
   */
  void Reset() { start_time_ = std::chrono::steady_clock::now(); }

  /**
   * @brief 获取从开始计时到现在的耗时（毫秒）
   * @return 耗时毫秒数
   */
  double ElapsedMs() const {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        end_time - start_time_);
    return duration.count() / 1000.0;  // 转换为毫秒，保留小数
  }

  /**
   * @brief 获取从开始计时到现在的耗时（毫秒，整数）
   * @return 耗时毫秒数（整数）
   */
  int64_t ElapsedMsInt() const {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time_);
    return duration.count();
  }

  /**
   * @brief 获取从开始计时到现在的耗时（微秒）
   * @return 耗时微秒数
   */
  int64_t ElapsedUs() const {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        end_time - start_time_);
    return duration.count();
  }

 private:
  std::chrono::steady_clock::time_point start_time_;
};

}  // namespace zenplay

// 便利宏定义
#define TIMER_START(name) zenplay::TimerUtil timer_##name

#define TIMER_END_MS(name) timer_##name.ElapsedMs()

#define TIMER_END_MS_INT(name) timer_##name.ElapsedMsInt()

#define TIMER_END_US(name) timer_##name.ElapsedUs()

#endif  // ZENPLAY_PLAYER_COMMON_TIMER_UTIL_H_
