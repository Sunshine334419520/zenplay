#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <condition_variable>

#include "loki_export.h"
#include "location.h"
#include "threading/thread_define.h"
#include "callback.h"
#include "task_runner_util.h"
#include "single_thread_task_runner.h"

namespace loki {

namespace internal {
std::shared_ptr<loki::SingleThreadTaskRunner> GetTaskRunnerForThread(
    ID identifier);
}

LOKI_EXPORT bool PostTask(ID identifier,
                          const Location& from_here,
                          OnceClosure closure);

LOKI_EXPORT bool PostDelayedTask(ID identifier,
                                 const loki::Location& from_here,
                                 OnceClosure task,
                                 std::chrono::milliseconds delay);
LOKI_EXPORT bool PostNonNestableTask(ID identifier,
                                     const Location& from_here,
                                     OnceClosure task);
LOKI_EXPORT bool PostNonNestableDelayedTask(ID identifier,
                                            const Location& from_here,
                                            OnceClosure task,
                                            std::chrono::milliseconds delay);

LOKI_EXPORT bool PostTaskAndReply(ID identifier,
                                  const Location& from_here,
                                  OnceClosure task,
                                  OnceClosure reply);

template <typename ReturnType, typename ReplyArgType>
bool PostTaskAndReplyWithResult(ID identifier,
                                const Location& from_here,
                                Callback<ReturnType()> task,
                                Callback<void(ReplyArgType)> reply) {
  std::shared_ptr<loki::SingleThreadTaskRunner> task_runner =
      internal::GetTaskRunnerForThread(identifier);
  bool result = loki::PostTaskAndReplyWithResult<ReturnType, ReplyArgType>(
      task_runner.get(), from_here, std::move(task), std::move(reply));
  return result;
}

template <class ReturnT,
          typename =
              typename std::enable_if<!std::is_void<ReturnT>::value>::type>
ReturnT Invoke(ID identifier,
               const Location& from_here,
               FunctionView<ReturnT()> functor) {
  ReturnT result{};
  std::condition_variable condition;
  std::mutex mutex;
  bool taskCompleted = false;
  
  if (PostTask(identifier, from_here,
               loki::BindOnceClosure(
                   [functor, &result, &condition, &mutex, &taskCompleted]() {
                     std::unique_lock<std::mutex> lock(mutex);
                     result = functor();
                     taskCompleted = true;
                     condition.notify_one();
                   }))) {
    std::unique_lock<std::mutex> lock(mutex);
    condition.wait(lock, [&taskCompleted] { return taskCompleted; });
  }

  return result;
}

template <class ReturnT,
    typename = typename std::enable_if<std::is_void<ReturnT>::value>::type>
void Invoke(ID identifier,
            const Location& from_here,
            FunctionView<void()> functor) {
  std::condition_variable condition;
  bool taskCompleted = false;
  std::mutex mutex;
  if (PostTask(identifier, from_here,
               loki::BindOnceClosure(
                   [functor, &condition, &mutex, &taskCompleted]() {
                     std::unique_lock<std::mutex> lock(mutex);
                     functor();
                     taskCompleted = true;
                     condition.notify_one();
                   }))) {
    std::unique_lock<std::mutex> lock(mutex);
    condition.wait(lock, [&taskCompleted] { return taskCompleted; });
  }
}

LOKI_EXPORT bool CurrentlyOn(ID identifier);

}  // namespace loki

#if !defined(CURRENTLY_ON)
#define CURRENTLY_ON(identifier)  \
loki::CurrentlyOn(static_cast<loki::ID>(identifier))
#endif

#if !defined(POST_TASK)
#define POST_TASK(identifier, closure)  \
loki::PostTask(static_cast<loki::ID>(identifier), FROM_HERE, closure)
#endif
