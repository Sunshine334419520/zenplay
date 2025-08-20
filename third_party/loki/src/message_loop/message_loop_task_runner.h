/**
* @Author: YangGuang
* @Date:   2018-10-25
* @Email:  guang334419520@126.com
* @Filename: message_loop_task_runner.h
* @Last modified by:  YangGuang
*/
#pragma once

#include <memory>
#include <mutex>

#include "loki_export.h"
#include "single_thread_task_runner.h"
#include "threading/platform_thread.h"

namespace loki {

namespace internal {

class IncomingTaskQueue;

class LOKI_EXPORT MessageLoopTaskRunner : public SingleThreadTaskRunner {
 public:
  explicit MessageLoopTaskRunner(
      std::shared_ptr<IncomingTaskQueue> incoming_queue);

  // Initialize this message loop task runner on the current thread.
  void BindToCurrentThread();

  bool PostDelayedTask(const Location& from_here,
                       OnceClosure Task,
                       std::chrono::milliseconds delay) OVERRIDE;

  bool PostNonNestableDelayedTask(const Location& from_here,
                                  OnceClosure task,
                                  std::chrono::milliseconds delay) OVERRIDE;

  virtual bool RunsTasksInCurrentSequence() OVERRIDE;
  ~MessageLoopTaskRunner() OVERRIDE;

 private:
  template <class T>
  friend struct DefaultDestroyTraits;

  //~MessageLoopTaskRunner() OVERRIDE;

  std::shared_ptr<IncomingTaskQueue> incoming_queue_;

  PlatformThreadId valid_thread_id_;
  std::mutex valid_thread_id_lock_;
};

}  // namespace internal.

}  // namespace loki.
