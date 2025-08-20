#include "main_message_loop_std.h"

#include "threading/loki_sub_thread.h"
#include "threading/loki_thread.h"
#include "run_loop.h"
#include "logging.h"

namespace loki {

MainMessageLoopStd::MainMessageLoopStd(Delegate* delegate)
    : _delegate(delegate),
      message_loop_(std::make_unique<MessageLoop>(MessageLoop::TYPE_DEFAULT)) {}

MainMessageLoopStd::MainMessageLoopStd() : MainMessageLoopStd(nullptr) {}

void MainMessageLoopStd::Initialize() {
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
bool MainMessageLoopStd::Run() {
  for (auto& subThread : _subThreads) {
    if (!subThread->Start()) {
      return false;
    }

    if (!subThread->WaitUntilThreadStarted()) {
      return false;
    }

    subThread->RegisterAsBrowserThread();
  }

  {
    std::lock_guard<std::mutex> lock(running_mutex_);
    running_ = true;
  }

  RunLoop runloop;
  run_loop_ = &runloop;
  run_loop_->Run();

  {
    std::lock_guard<std::mutex> lock(running_mutex_);
    running_ = false;
  }

  message_loop_ = nullptr;
  run_loop_ = nullptr;

  return true;
}

void MainMessageLoopStd::Quit() {
  for (auto& subThread : _subThreads) {
    subThread->Stop();
  }

  if (!message_loop_ || !running_) {
    return;
  }

  message_loop_->task_runner()->PostTask(
      FROM_HERE,
      loki::BindOnceClosure(&MainMessageLoopStd::ThreadQuitHelper, this));
}

void MainMessageLoopStd::PostTask(loki::OnceClosure closure) {
  message_loop_->task_runner()->PostTask(FROM_HERE, std::move(closure));
}

bool MainMessageLoopStd::RunsTasksOnCurrentThread() const {
  return message_loop_->task_runner()->RunsTasksInCurrentSequence();
}

void MainMessageLoopStd::ThreadQuitHelper() {
  DCHECK(run_loop_);
  run_loop_->QuitWhenIdle();
}

}  // namespace loki
