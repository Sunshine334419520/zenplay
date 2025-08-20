
#include <gtest/gtest.h>
#if !defined(_WIN32)
#include <unistd.h>
#endif

#include "main_message_loop.h"
#include "main_message_loop_std.h"
#include "main_message_loop_with_not_main_thread.h"
#include "post_task_interface.h"

constexpr loki::ID WORKER = static_cast<loki::ID>(8);

class MyDelegate : public loki::MainMessageLoop::Delegate {
 public:
  void OnSubThreadRegistry(
      std::vector<std::pair<loki::ID, std::string>>* subThreads) override {
    subThreads->emplace_back(loki::UI, "UI_Thread");
    subThreads->emplace_back(loki::IO, "IO_Thread");
    subThreads->emplace_back(WORKER, "Worker_Thread");
  }
};

int main(int argc, char* argv[]) {
  loki::MainMessageLoop::Delegate* delegate = new MyDelegate();
  //std::unique_lock<std::mutex> lock(gMutex);
  std::unique_ptr<loki::MainMessageLoop> mainMessageLoop =
      std::make_unique<loki::MainMessageLoopWithNotMainThread>(delegate);
  mainMessageLoop->Initialize();
  mainMessageLoop->Run();
  testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  mainMessageLoop->Quit();
#if defined(_WIN32)
  _sleep(1000);
#else
  sleep(1);
#endif
  return ret;
}
