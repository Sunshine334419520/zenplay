/**
* @Author: YangGuang
* @Date:   2018-11-22
* @Email:  guang334419520@126.com
* @Filename: browser_process_sub_thread.cc
* @Last modified by:  YangGuang
*/
#include "threading/loki_sub_thread.h"

#include "threading/loki_thread_impl.h"
#include "bind_util.h"
#include "logging.h"

namespace loki {

LokiSubThread::LokiSubThread(ID identifier,
                             const std::string& threadName)
    : loki::Thread(threadName), identifier_(identifier) {}

LokiSubThread::~LokiSubThread() {
  Stop();
}

void LokiSubThread::RegisterAsBrowserThread() {
  DCHECK(IsRunning());

  DCHECK(!browser_thread_);
  browser_thread_.reset(new LokiThreadImpl(identifier_, task_runner()));

  task_runner()->PostTask(
      FROM_HERE,
      loki::BindOnceClosure(
          &LokiSubThread::CompleteInitializationOnBrowserThread, this));
}

std::unique_ptr<LokiSubThread> LokiSubThread::CreateIOThread() {
  return std::unique_ptr<LokiSubThread>();
}

void LokiSubThread::Init() {}

void LokiSubThread::Run(loki::RunLoop* run_loop) {
  UIThreadRun(run_loop);
}

void LokiSubThread::CleanUp() {}

void LokiSubThread::CompleteInitializationOnBrowserThread() {}

void LokiSubThread::UIThreadRun(loki::RunLoop* run_loop) {
  Thread::Run(run_loop);
}

void LokiSubThread::IOThreadRun(loki::RunLoop* run_loop) {
  Thread::Run(run_loop);
}

void LokiSubThread::IOThreadCleanUp() {}

}  // namespace loki