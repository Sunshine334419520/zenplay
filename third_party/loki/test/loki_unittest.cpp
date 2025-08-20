#include <gtest/gtest.h>
#include <mutex>
#include <thread>
#include <condition_variable>
#include "main_message_loop.h"
#include "main_message_loop_std.h"
#include "main_message_loop_with_not_main_thread.h"
#include "post_task_interface.h"

namespace {
constexpr loki::ID WORKER = static_cast<loki::ID>(8);
}
std::condition_variable gVar;
std::mutex gMutex;
bool done = false;

class WorkerTest {
 public:
  void DoSomething(const std::string& something) {
    if (!CURRENTLY_ON(WORKER)) {
      POST_TASK(WORKER, loki::BindOnceClosure(&WorkerTest::DoSomething, this,
                                              something));
      return;
    }

    EXPECT_TRUE(CURRENTLY_ON(WORKER));
    std::cout << "worker thread id : " << std::this_thread::get_id()
              << std::endl;
    std::cout << something << std::endl;
    _doSometingCount = ++_count;
  }

  void DoSomething2(const std::string& something) {
    if (!CURRENTLY_ON(WORKER)) {
      POST_TASK(WORKER, loki::BindOnceClosure(&WorkerTest::DoSomething, this,
                                              something));
      return;
    }

    EXPECT_TRUE(CURRENTLY_ON(WORKER));
    std::cout << "worker thread id : " << std::this_thread::get_id()
              << std::endl;
    std::cout << something << std::endl;
    _doSometing2Count = ++_count;
  }

  std::string DoSomething3(const std::string& something) {
    EXPECT_TRUE(CURRENTLY_ON(WORKER));
    std::cout << something << std::endl;
    return something;
  }

 public:
  int _doSometingCount = 0;
  int _doSometing2Count = 0;
  int _count = 0;
};

class IOTest {
 public:
  static void DoSomething(const std::string& something) {
    EXPECT_TRUE(CURRENTLY_ON(loki::IO));
    // 假装写入某个数据到文件
    std::cout << "io thread id : " << std::this_thread::get_id() << std::endl;
    std::cout << "write " << something << " done." << std::endl;
  }
};

void DoneNotify() {
    std::unique_lock<std::mutex> lock(gMutex);
    done = true;
    gVar.notify_all();
}

TEST(LokiTest, PostTask) {
  std::unique_lock<std::mutex> lock(gMutex);
  {
    done = false;
    WorkerTest workerTest;
    workerTest.DoSomething("Dosomething on WORKER");
    POST_TASK(WORKER,
              loki::BindOnceClosure(&WorkerTest::DoSomething2, &workerTest,
                                    "Dosometing2 on WORKER"));
    POST_TASK(WORKER, loki::BindOnceClosure(&DoneNotify));

    gVar.wait(lock, []() { return done; });
    EXPECT_EQ(workerTest._count, 2);
    EXPECT_EQ(workerTest._doSometingCount, 1);
    EXPECT_EQ(workerTest._doSometing2Count, 2);
  }

  { 
      done = false;
      WorkerTest workerTest;
      for (int i = 0; i < 10000; ++i) {
        workerTest.DoSomething("Dosomething on WORKER" + std::to_string(i));
      }

      for (int i = 10000; i < 20000; ++i) {
        POST_TASK(WORKER,
              loki::BindOnceClosure(&WorkerTest::DoSomething2, &workerTest,
                                        "Dosometing2 on WORKER " + std::to_string(i)));
      }

      POST_TASK(WORKER, loki::BindOnceClosure(&DoneNotify));
      gVar.wait(lock, []() { return done; });
      std::cout << "workerTest count " << workerTest._count << std::endl;
      EXPECT_EQ(workerTest._count, 20000);
  }
}

void Main() {
  if (!CURRENTLY_ON(loki::UI)) {
    POST_TASK(loki::UI, loki::BindOnceClosure(&Main));
    return;
  }

  EXPECT_TRUE(CURRENTLY_ON(loki::UI));
  auto valueResutl = loki::Invoke<bool>(WORKER, FROM_HERE, []() {
    EXPECT_TRUE(CURRENTLY_ON(WORKER));
    #if defined(OS_WIN)
    _sleep(5000);
    #else
    sleep(5);
    #endif
    return false; });
  EXPECT_FALSE(valueResutl);
  loki::Invoke<void>(WORKER, FROM_HERE, []() -> void { return; });

  WorkerTest workerTest;
  loki::PostTaskAndReplyWithResult<std::string, std::string>(
      static_cast<loki::ID>(WORKER), FROM_HERE,
      std::bind(&WorkerTest::DoSomething3, &workerTest, "dosometings3"),
      [](std::string str) {
        // 这个回复函数执行在调用线程，就是执行在调用loki::PostTaskAndReplyWithResult这个函数的线程中.
        // 在这个列子就是执行在主线程.
        EXPECT_TRUE(CURRENTLY_ON(loki::UI));
        EXPECT_EQ(str, "dosometings3");
        POST_TASK(loki::IO, loki::BindOnceClosure(&IOTest::DoSomething, str));
      });


}

TEST(LokiTest, PostTaskAndReplyWithResult) {
  Main();
}


void DelayMain(int count) {
  if (count < 10) {
    loki::PostDelayedTask(loki::IO,FROM_HERE, loki::BindOnceClosure(&DelayMain, ++count), std::chrono::milliseconds(3000));
  } else {
    POST_TASK(WORKER, loki::BindOnceClosure(&DoneNotify));
  }
}

TEST(LokiTest, PostTaskDelay) {
  std::unique_lock<std::mutex> lock(gMutex);
  done = false;
  auto start = std::chrono::steady_clock::now();
  DelayMain(0);
  gVar.wait(lock, []() { return done; });
  auto end = std::chrono::steady_clock::now();
  long long time = std::chrono::duration_cast<std::chrono::milliseconds>((end - start)).count();
  std::cout << time << std::endl;
  EXPECT_TRUE(time >= 28500  && time <= 31500);
}
