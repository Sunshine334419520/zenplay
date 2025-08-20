/**
* @Author: YangGuang
* @Date:   2018-11-16
* @Email:  guang334419520@126.com
* @Filename: browser_thread.h
* @Last modified by:  YangGuang
*/
#pragma once

#include "loki_export.h"
#include "threading/loki_thread.h"

namespace loki {

class LokiSubThread;

// LokiThreadImpl是一个作用域对象，它将一个SingleThreadTaskRunner
// 映射到LokiThread::ID. 在这个~LokiThreadImpl()上，将会将state
// 设置为SHUTDOWN(在这种情况下LokiThread::IsThreadInitialized()将会
// 返回false)而且这个映射还没有取消(任务的runner是已经释放并且停止接受任何
// 的任务)
class LOKI_EXPORT LokiThreadImpl : public LokiThread {
 public:
  ~LokiThreadImpl();

 private:
  friend class LokiSubThread;

  // 绑定这个|identifier| 到 |task_runner|.
  LokiThreadImpl(ID identifier,
                 std::shared_ptr<loki::SingleThreadTaskRunner> task_runner);

  // thraed的id.
  ID identifier_;
};

}  // namespace loki.
