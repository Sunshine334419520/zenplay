#pragma once

#include <mutex>
#include <stack>

#include "loki_export.h"
#include "callback.h"
#include "macor.h"

namespace loki {

// 这个类提供了一个在终止时处理atexit()的能力, 常常用在loki::Singleton.
// 使用起来非常简单，只要早期在main函数里面创建一个AtExitManager对象，然后在
// 这个stack自动释放时会调用析构函数，在析构函数里面会自动处理所有的atexit(）.
// for example :
//	int main(...) {
//		loki::AtExitManager exit_manager;
//
//	}
// 在这个exit_manager对象离开作用域时，注册的终止函数将全部被调用.
class LOKI_EXPORT AtExitManager {
 public:
  typedef void (*AtExitCallbackType)(void*);

  AtExitManager();

  // 析构函数里面会调用ProcessCallbacksNow
  //在结束作用域时对所有任务进行处理
  ~AtExitManager();

  // 注册一个需要清理数据的回调函数.
  static void RegisterCallback(AtExitCallbackType func, void* param);

  // 注册一个任务，用loki::Closure类型.
  static void RegisterTask(loki::Closure task);

  // 处理注册的所有任务.
  static void ProcessCallbacksNow();

  // Disable 所有的AtExitManager，这个在单个进程时使用.
  static void DisableAllAtExitManagers();

 protected:
  explicit AtExitManager(bool shadow);

 private:
  std::mutex mutex_;
  std::stack<loki::Closure> stack_;
  bool processing_callbacks_;
  AtExitManager* next_manager_;

  DISALLOW_COPY_AND_ASSIGN(AtExitManager);
};

}  // namespace loki.
