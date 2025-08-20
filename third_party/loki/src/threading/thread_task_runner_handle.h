/**
* @Author: YangGuang
* @Date:   2018-10-25
* @Email:  guang334419520@126.com
* @Filename: thread_task_runner_handle.h
* @Last modified by:  YangGuang
*/
#pragma once

#include <memory>

#include "loki_export.h"
#include "macor.h"
#include "single_thread_task_runner.h"

namespace loki {

class LOKI_EXPORT ThreadTaskRunnerHandle {
 public:
  static std::shared_ptr<SingleThreadTaskRunner> Get();

  // 如果返回true的话是满足下列的条件的:
  // a) 一个SingleThreadTaskRunner是已经分配到当前的线程了，
  //	   通过SingleThreadTaskRunner实例化.
  // b) 当前线程有一个ThreadTaskRunnerHandle(包括任何
  //	   有MessageLoop关联的线程).
  static bool IsSet();

  explicit ThreadTaskRunnerHandle(
      std::shared_ptr<SingleThreadTaskRunner> task_runner);

  ThreadTaskRunnerHandle() = default;
  ~ThreadTaskRunnerHandle();

 private:
  std::shared_ptr<SingleThreadTaskRunner> task_runner_;

  DISALLOW_COPY_AND_ASSIGN(ThreadTaskRunnerHandle);
};

}   // namespace loki

