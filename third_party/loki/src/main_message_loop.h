#pragma once

#include <vector>
#include <utility>
#include <string>

#include "macor.h"
#include "threading/thread_define.h"
#include "callback.h"

namespace loki {

class MainMessageLoop {
 public:
  class Delegate {
   public:
    virtual void OnSubThreadRegistry(
        std::vector<std::pair<loki::ID, std::string>>* subThreads) {
    }
    virtual void OnBeforeRun() {}
    virtual void OnAfterRun() {}
    virtual ~Delegate() = default;
  };

  static MainMessageLoop* Get();

  virtual void Initialize() = 0;

  virtual bool Run() = 0;

  virtual void Quit() = 0;

  virtual void PostTask(loki::OnceClosure closure) = 0;

  // Returns true if this message loop runs tasks on the current thread.
  virtual bool RunsTasksOnCurrentThread() const = 0;

  MainMessageLoop();
  virtual ~MainMessageLoop();
};

}  // namespace loki
