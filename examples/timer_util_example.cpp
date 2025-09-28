/**
 * @file timer_util_example.cpp
 * @brief TimerUtil 使用示例
 */

#include <thread>

#include "player/common/log_manager.h"
#include "player/common/timer_util.h"

namespace zenplay {

void TimerUtilExamples() {
  // 示例1: 基本用法
  {
    MODULE_INFO(LOG_MODULE_PLAYER, "=== 基本用法示例 ===");

    TimerUtil timer;  // 构造时开始计时

    // 模拟一些耗时操作
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    double elapsed_ms = timer.ElapsedMs();          // 获取耗时（毫秒，带小数）
    int64_t elapsed_ms_int = timer.ElapsedMsInt();  // 获取耗时（毫秒，整数）
    int64_t elapsed_us = timer.ElapsedUs();         // 获取耗时（微秒）

    MODULE_INFO(LOG_MODULE_PLAYER, "操作耗时: {:.2f} ms ({} ms, {} us)",
                elapsed_ms, elapsed_ms_int, elapsed_us);
  }

  // 示例2: 宏定义用法（推荐）
  {
    MODULE_INFO(LOG_MODULE_PLAYER, "=== 宏定义用法示例 ===");

    TIMER_START(decode_operation);  // 开始计时

    // 模拟解码操作
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto ms = TIMER_END_MS(decode_operation);  // 结束并获取耗时
    MODULE_INFO(LOG_MODULE_PLAYER, "解码操作耗时: {:.2f} ms", ms);
  }

  // 示例3: 重置计时器
  {
    MODULE_INFO(LOG_MODULE_PLAYER, "=== 重置计时器示例 ===");

    TimerUtil timer;
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    MODULE_INFO(LOG_MODULE_PLAYER, "第一次测量: {:.2f} ms", timer.ElapsedMs());

    timer.Reset();  // 重置计时器
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    MODULE_INFO(LOG_MODULE_PLAYER, "重置后测量: {:.2f} ms", timer.ElapsedMs());
  }

  // 示例4: 在实际播放控制中的应用
  {
    MODULE_INFO(LOG_MODULE_PLAYER, "=== 实际应用示例 ===");

    // 模拟解封装操作
    TIMER_START(demux);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    auto demux_ms = TIMER_END_MS_INT(demux);
    MODULE_INFO(LOG_MODULE_PLAYER, "解封装耗时: {} ms", demux_ms);

    // 模拟解码操作
    TIMER_START(video_decode);
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    auto decode_ms = TIMER_END_MS(video_decode);
    MODULE_INFO(LOG_MODULE_PLAYER, "视频解码耗时: {:.2f} ms", decode_ms);

    // 模拟渲染操作
    TimerUtil render_timer;
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
    MODULE_INFO(LOG_MODULE_PLAYER, "渲染耗时: {:.1f} ms",
                render_timer.ElapsedMs());
  }
}

}  // namespace zenplay
