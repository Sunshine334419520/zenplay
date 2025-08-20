/**
* @Author: YangGuang
* @Date:   2018-11-22
* @Email:  guang334419520@126.com
* @Filename: browser_process_sub_thread.h
* @Last modified by:  YangGuang
*/
#pragma once

#include <memory>
#include <vector>

#include "macor.h"
#include "threading/thread.h"
#include "threading/loki_thread.h"
#include "threading/loki_thread_impl.h"

namespace loki {

class LokiSubThread : public loki::Thread {
 public:
  explicit LokiSubThread(ID identifier,
                         const std::string& threadName);
  ~LokiSubThread() override;

  // 将当前的线程的 |identifier_|注册到LokiThread，这个方法必须调用在
  // 线程已经处于运行的状态,而且这个方法只能调用一次.
  void RegisterAsBrowserThread();

  // 创建并且开始这个IO thread.
  static std::unique_ptr<LokiSubThread> CreateIOThread();

 protected:
  void Init() override;
  void Run(loki::RunLoop* run_loop) override;
  void CleanUp() override;

 private:
  // 第二个初始化阶段，这个只应该在RegisterAsBrowserThread()上调用.
  void CompleteInitializationOnBrowserThread();

  void UIThreadRun(loki::RunLoop* run_loop);
  void IOThreadRun(loki::RunLoop* run_loop);

  void IOThreadCleanUp();

  const ID identifier_;

  std::unique_ptr<loki::LokiThreadImpl> browser_thread_;

  DISALLOW_COPY_AND_ASSIGN(LokiSubThread);
};

}   // namespace loki
