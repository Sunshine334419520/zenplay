#include "main_message_loop_with_not_main_thread.h"
#include "threading/loki_sub_thread.h"
#include "logging.h"

namespace loki {
MainMessageLoopWithNotMainThread::MainMessageLoopWithNotMainThread(
    Delegate* delegate) : _delegate(delegate) {}

MainMessageLoopWithNotMainThread::~MainMessageLoopWithNotMainThread() = default;

void MainMessageLoopWithNotMainThread::Initialize() {
  std::vector<std::pair<loki::ID, std::string>> subThreadInfos;
  _delegate->OnSubThreadRegistry(&subThreadInfos);

  for (auto& subThreadInfo : subThreadInfos) {
    if (subThreadInfo.first >= loki::ID::ID_COUNT) {
      assert(false);
    }

    std::unique_ptr<LokiSubThread> subThread = std::make_unique<LokiSubThread>(
        subThreadInfo.first, subThreadInfo.second);
    _subThreads.push_back(std::move(subThread));
  }
}

bool MainMessageLoopWithNotMainThread::Run() {
  DCHECK(!running_)
  for (auto& subThread : _subThreads) {
    if (!subThread->Start()) {
      return false;
    }

    if (!subThread->WaitUntilThreadStarted()) {
      return false;
    }

    subThread->RegisterAsBrowserThread();
  }

  running_ = true;

  return true;
}

void MainMessageLoopWithNotMainThread::Quit() {
  DCHECK(running_);

  for (auto& subThread : _subThreads) {
    subThread->Stop();
  }

  running_ = false;
}

void MainMessageLoopWithNotMainThread::PostTask(loki::OnceClosure closure) {
  DCHECK(false);
}

bool MainMessageLoopWithNotMainThread::RunsTasksOnCurrentThread() const {
  return false;
}

}  // namespace loki
