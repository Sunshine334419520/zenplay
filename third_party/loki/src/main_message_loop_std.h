#pragma once

#include <memory>
#include <mutex>
#include "main_message_loop.h"

namespace loki {

class LokiSubThread;
class MessageLoop;
class RunLoop;

class MainMessageLoopStd : public MainMessageLoop {
 public:
  explicit MainMessageLoopStd(Delegate* delegate);
  MainMessageLoopStd();

  void Initialize() override;

  bool Run() override;

  void Quit() override;

  void PostTask(loki::OnceClosure closure) override;

  // Returns true if this message loop runs tasks on the current thread.
  bool RunsTasksOnCurrentThread() const override;

 private:
  void ThreadQuitHelper();

  Delegate* _delegate;
  // 这个对象不需要我们去释放，传入进来的对象是一个局部对象会自动释放
  RunLoop* run_loop_ = nullptr;
  std::unique_ptr<MessageLoop> message_loop_;
  std::vector<std::unique_ptr<loki::LokiSubThread>> _subThreads;

  bool running_ = false;
  mutable std::mutex running_mutex_;
};

}  // namespace loki
