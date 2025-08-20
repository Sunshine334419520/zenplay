/**
* @Author: YangGuang
* @Date:   2018-10-10
* @Email:  guang334419520@126.com
* @Filename: platform_thread.h
* @Last modified by:  YangGuang
*/
#pragma once

#include <thread>
#include <chrono>
#include <string>
#include <tuple>

#include "loki_export.h"
#include "macor.h"

namespace loki {

using PlatformThreadId = std::thread::id;
using PlatformThreadHandle = std::thread::native_handle_type;

const PlatformThreadId kInvalidThreadId = std::thread::id();

enum class ThreadPriority : int { BACKGROUND, NORMAL, DISPLAY, REALTIME_AUDIO };

class LOKI_EXPORT PlatformThread {
 public:
  class LOKI_EXPORT Delegate {
   public:
    virtual void ThreadMain() = 0;

   protected:
    virtual ~Delegate() {}
  };

  static PlatformThreadId CurrentId();

  static PlatformThreadHandle CurrentHandle();

  static void YieldCurrentThread();

  template <typename Rep, typename Period>
  static void Sleep(const std::chrono::duration<Rep, Period>& duration);

  static void SetName(const std::string& name);

  static const char* GetName();

  static std::thread Create(size_t stack_size, Delegate* delegate) {
    return std::move(
        CreateWithPriority(stack_size, delegate, ThreadPriority::NORMAL));
  }

  static std::thread CreateWithPriority(size_t stack_size,
                                        Delegate* delegate,
                                        ThreadPriority priority);

  static std::thread CreateNonJoinable(size_t stack_size, Delegate* delegate);

  static std::thread CreateNonJoinableWithPriority(size_t stack_size,
                                                   Delegate* delegate,
                                                   ThreadPriority priority);

  static std::thread Join(std::thread&& thread);

  static std::thread Detch(std::thread&& thread);

  static bool CanIncreaseCurrentThreadPriority();

  static void SetThreadPriority(PlatformThreadHandle handle,
                                ThreadPriority priority);

  static ThreadPriority GetThreadPriority();

 private:
  PlatformThread() = delete;
  DISALLOW_COPY_AND_ASSIGN(PlatformThread);
};

template <typename Rep, typename Period>
inline void PlatformThread::Sleep(
    const std::chrono::duration<Rep, Period>& duration) {
  std::this_thread::sleep_for(duration);
}

}  // namespace loki.
