/**
* @Author: YangGuang
* @Date:   2018-11-13
* @Email:  guang334419520@126.com
* @Filename: browser_thread.h
* @Last modified by:  YangGuang
*/
#pragma once

#include <memory>
#include <string>
#include <utility>
#include <chrono>

#include "loki_export.h"
#include "thread_define.h"
#include "callback.h"
#include "location.h"
#include "macor.h"
#include "single_thread_task_runner.h"
#include "post_task_and_reply_with_result_internal.h"
#include "task_runner_util.h"

namespace loki {

class LokiThreadImpl;

class LOKI_EXPORT LokiThread {
 public:

  // 这些函数都是一些与message_loop上的post task是一样的，他们会发送任务到
  // 他们自己的线程的消息循环incoming queue, 如果消息循环还存在的话，任务会
  // 发送成功，并且会返回true，否则失败，false，就算是发送成功了，也不能保证
  // 这个任务一定会运行，因为又可能在之前发送过退出任务，那么这个退出任务将会
  // 在它前面执行.
  static bool PostTask(ID identifier,
                       const loki::Location& from_here,
                       loki::OnceClosure task);
  static bool PostDelayedTask(ID identifier,
                              const loki::Location& from_here,
                              loki::OnceClosure task,
                              std::chrono::milliseconds delay);
  static bool PostNonNestableTask(ID identifier,
                                  const loki::Location& from_here,
                                  loki::OnceClosure task);
  static bool PostNonNestableDelayedTask(ID identifier,
                                         const loki::Location& from_here,
                                         loki::OnceClosure task,
                                         std::chrono::milliseconds delay);

  static bool PostTaskAndReply(ID identifier,
                               const loki::Location& from_here,
                               loki::OnceClosure task,
                               loki::OnceClosure reply);

  template <typename ReturnType, typename ReplyArgType>
  static bool PostTaskAndReplyWithResult(
      ID identifier,
      const loki::Location& from_here,
      loki::Callback<ReturnType()> task,
      loki::Callback<void(ReplyArgType)> reply) {
    std::shared_ptr<loki::SingleThreadTaskRunner> task_runner =
        GetTaskRunnerForThread(identifier);
    bool result = loki::PostTaskAndReplyWithResult<ReturnType, ReplyArgType>(
        task_runner.get(), from_here, std::move(task), std::move(reply));
    return result;
  }

  template <typename T>
  static bool DeleteSoon(ID identifier,
                         const loki::Location& from_here,
                         const T* object) {
    return GetTaskRunnerForThread(identifier)->DeleteSoon(from_here, object);
  }

  template <typename T>
  static bool DeleteSoon(ID identifier,
                         const loki::Location& from_here,
                         std::unique_ptr<T> object) {
    return DeleteSoon(identifier, from_here, object.release());
  }

  template <typename T>
  static bool ReleaseSoon(ID identifier,
                          const loki::Location& from_here,
                          const T* object) {
    return false;
  }

  static void PostAfterStartupTask(
      const loki::Location& from_here,
      const std::shared_ptr<loki::TaskRunner>& task_runner,
      loki::OnceClosure task);

  // 线程是否初始化，可以调用在任何线程上面.
  static bool IsThreadinitialized(ID identifier) WARN_UNUSED_RESULT;

  // 如果当前的线程与你给的|identifier|是同一个线程,返回true,
  // 可以调用在任务线程上
  static bool CurrentlyOn(ID identifier) WARN_UNUSED_RESULT;

  // 如果当前消息循环是已知的线程中的一个，那么就返回true，并且设置ID到|identifier|
  // 否则返回false.
  static bool GetCurrentThreadIdentifier(ID* identifier) WARN_UNUSED_RESULT;

  // 调用者可以在线程的生命周期之外持有一个被重新计算的任务运行器.
  static std::shared_ptr<loki::SingleThreadTaskRunner> GetTaskRunnerForThread(
      ID identifier);

  template <ID thread>
  struct DeleteOnThread {
    template <typename T>
    static void Destruct(const T* x) {
      if (CurrentlyOn(thread)) {
        delete x;
      } else {
        if (!DeleteSoon(thread, FROM_HERE, x)) {
          /*
          LOG(ERROR) <<
                  "DeleteSoon failed on thread" << thread;
                  */
        }
      }
    }

    template <typename T>
    inline void operator()(T* ptr) const {
      enum { type_must_be_complete = sizeof(T) };
      Destruct(ptr);
    }
  };

 private:
  friend class LokiThreadImpl;

  LokiThread() = default;
  DISALLOW_COPY_AND_ASSIGN(LokiThread);
};
}   // namesapce loki

