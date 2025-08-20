#include <iostream>
#include <memory>
#include <vector>
#include <thread>
#if defined(OS_WIN)
#include <Windows.h>
#else
#include <stdlib.h>
#include <unistd.h>
#endif

#include "post_task_interface.h"
#include "main_message_loop.h"
#include "main_message_loop_std.h"
#include "main_message_loop_with_not_main_thread.h"

constexpr int kWorkerThreadId = 8;

class MyDelegate : public loki::MainMessageLoop::Delegate {
 public:
  void OnSubThreadRegistry(
      std::vector<std::pair<loki::ID, std::string>>* subThreads)
      override {
    subThreads->emplace_back(loki::UI, "UI_Thread");
    subThreads->emplace_back(loki::IO, "IO_Thread");
    subThreads->emplace_back(static_cast<loki::ID>(kWorkerThreadId),
                             "Worker_Thread");
  }
};

class WorkerTest {
 public:
  void DoSomething(const std::string& something) {
    if (!CURRENTLY_ON(kWorkerThreadId)) {
      POST_TASK(kWorkerThreadId, loki::BindOnceClosure(&WorkerTest::DoSomething,
                                                       this, something));
      return;
    }

    std::cout << "worker thread id : " << std::this_thread::get_id() << std::endl;
    std::cout << something << std::endl;
  }

  std::string DoSomething2(const std::string& something) {
    std::cout << something << std::endl;
      return something; 
  }
};

class IOTest {
 public:
  static void DoSomething(const std::string& something) {
    // 假装写入某个数据到文件
    std::cout << "io thread id : " << std::this_thread::get_id() << std::endl;
    std::cout << "write " << something << " done." << std::endl;
  }
};

void Main() {
  WorkerTest workerTest;

  workerTest.DoSomething("workerTest DoSomething");

  std::string testPostAndRet = "dosomething2";

  loki::PostTaskAndReplyWithResult<std::string, std::string>(
      static_cast<loki::ID>(kWorkerThreadId), FROM_HERE,
      std::bind(&WorkerTest::DoSomething2, &workerTest, testPostAndRet),
      [](std::string str) { 
        // 这个回复函数执行在调用线程，就是执行在调用loki::PostTaskAndReplyWithResult这个函数的线程中.
       // 在这个列子就是执行在主线程.
        POST_TASK(loki::IO,
                  loki::BindOnceClosure(&IOTest::DoSomething, str));
      }
  );


  //Sleep(100000);
  //loki::MainMessageLoop::Get()->Get()->Quit();
}

int main(void) {
  loki::MainMessageLoop::Delegate* delegate = new MyDelegate();
  /*
  std::unique_ptr<loki::MainMessageLoop> mainMessageLoop =
      std::make_unique<loki::MainMessageLoopStd>(delegate);

  mainMessageLoop->Initialize();
  mainMessageLoop->PostTask(loki::BindOnceClosure(&Main));
  mainMessageLoop->Run();
  */

  /// 这下面的使用另一种消息循环，他不会帮你托管main线程
    std::unique_ptr<loki::MainMessageLoop> mainMessageLoop =
      std::make_unique<loki::MainMessageLoopWithNotMainThread>(delegate);

  mainMessageLoop->Initialize();
  //mainMessageLoop->PostTask(loki::BindOnceClosure(&Main));
  mainMessageLoop->Run();
  POST_TASK(loki::IO, loki::BindOnceClosure(&Main));

  while (true) {
#if defined(OS_WIN)
    Sleep(5000);
#else
    sleep(5);
#endif
  }
  return 0;
}
