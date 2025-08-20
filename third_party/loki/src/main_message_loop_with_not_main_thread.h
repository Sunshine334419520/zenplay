#pragma once

#include <atomic>

#include "main_message_loop.h"

namespace loki {

class LokiSubThread;

class MainMessageLoopWithNotMainThread : public MainMessageLoop {
 public:
  explicit MainMessageLoopWithNotMainThread(Delegate* delegate);
  ~MainMessageLoopWithNotMainThread();

  void Initialize() override;

  bool Run() override;

  void Quit() override;

  void PostTask(loki::OnceClosure closure) override;

  // Returns true if this message loop runs tasks on the current thread.
  bool RunsTasksOnCurrentThread() const override;

  private:
  Delegate* _delegate;

  std::vector<std::unique_ptr<loki::LokiSubThread>> _subThreads;

  std::atomic<bool> running_ = {false};
};

}   // namesapce loki
