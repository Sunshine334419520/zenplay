/**
 * @file test_thread_safe_queue.cpp
 * @brief 单元测试 - ThreadSafeQueue (BlockingQueue 前身)
 *
 * 测试目标：
 * - 基本的 Push/Pop 操作
 * - 多线程并发安全性
 * - 超时与阻塞行为
 * - Stop 和 Clear 机制
 *
 * 参考：execution_plan_priority_features.md - 任务 4
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "player/common/thread_safe_queue.h"

using namespace std::chrono_literals;

// ============================================================================
// 基本功能测试
// ============================================================================

TEST(ThreadSafeQueueTest, BasicPushPop) {
  ThreadSafeQueue<int> queue;

  // Push 元素
  queue.Push(1);
  queue.Push(2);
  queue.Push(3);

  EXPECT_EQ(queue.Size(), 3);

  // Pop 元素并验证顺序（FIFO）
  int value;
  ASSERT_TRUE(queue.Pop(value, 100ms));
  EXPECT_EQ(value, 1);

  ASSERT_TRUE(queue.Pop(value, 100ms));
  EXPECT_EQ(value, 2);

  ASSERT_TRUE(queue.Pop(value, 100ms));
  EXPECT_EQ(value, 3);

  EXPECT_EQ(queue.Size(), 0);

  // 空队列 Pop 应该超时返回 false
  ASSERT_FALSE(queue.Pop(value, 50ms));
}

TEST(ThreadSafeQueueTest, Clear) {
  ThreadSafeQueue<int> queue;

  queue.Push(1);
  queue.Push(2);
  queue.Push(3);
  EXPECT_EQ(queue.Size(), 3);

  queue.Clear();
  EXPECT_EQ(queue.Size(), 0);

  // 清空后 Pop 应该超时
  int value;
  ASSERT_FALSE(queue.Pop(value, 50ms));
}

TEST(ThreadSafeQueueTest, ClearWithCallback) {
  ThreadSafeQueue<int*> queue;

  int* p1 = new int(1);
  int* p2 = new int(2);
  int* p3 = new int(3);

  queue.Push(p1);
  queue.Push(p2);
  queue.Push(p3);

  // 清空并释放所有指针
  int cleanup_count = 0;
  queue.Clear([&cleanup_count](int* ptr) {
    delete ptr;
    cleanup_count++;
  });

  EXPECT_EQ(cleanup_count, 3);
  EXPECT_EQ(queue.Size(), 0);
}

TEST(ThreadSafeQueueTest, Stop) {
  ThreadSafeQueue<int> queue;

  queue.Push(1);
  queue.Push(2);

  // 调用 Stop 后，队列仍有数据时 Pop 可以继续取出（graceful shutdown）
  queue.Stop();

  int value;
  // 第一次 Pop：成功取出 1
  ASSERT_TRUE(queue.Pop(value, 100ms));
  EXPECT_EQ(value, 1);

  // 第二次 Pop：成功取出 2
  ASSERT_TRUE(queue.Pop(value, 100ms));
  EXPECT_EQ(value, 2);

  // 第三次 Pop：队列空且已 Stop，立即返回 false
  ASSERT_FALSE(queue.Pop(value, 1000ms));

  EXPECT_EQ(queue.Size(), 0);
}

// ============================================================================
// 超时与阻塞行为测试
// ============================================================================

TEST(ThreadSafeQueueTest, PopTimeout) {
  ThreadSafeQueue<int> queue;

  auto start = std::chrono::steady_clock::now();

  int value;
  bool result = queue.Pop(value, 100ms);

  auto end = std::chrono::steady_clock::now();
  auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  ASSERT_FALSE(result);
  // 验证超时时间（允许一定误差）
  EXPECT_GE(elapsed.count(), 90);   // 至少 90ms
  EXPECT_LE(elapsed.count(), 150);  // 不超过 150ms
}

TEST(ThreadSafeQueueTest, PopBlockingUntilPush) {
  ThreadSafeQueue<int> queue;

  std::atomic<bool> thread_started{false};
  std::atomic<bool> pop_succeeded{false};
  int popped_value = 0;

  // 消费者线程：阻塞等待
  std::thread consumer([&]() {
    thread_started = true;
    int value;
    if (queue.Pop(value, 2000ms)) {  // 2秒超时
      popped_value = value;
      pop_succeeded = true;
    }
  });

  // 等待消费者线程启动
  while (!thread_started) {
    std::this_thread::sleep_for(1ms);
  }

  // 主线程延迟 200ms 后 Push 数据
  std::this_thread::sleep_for(200ms);
  queue.Push(42);

  consumer.join();

  ASSERT_TRUE(pop_succeeded);
  EXPECT_EQ(popped_value, 42);
}

// ============================================================================
// 多线程并发测试
// ============================================================================

TEST(ThreadSafeQueueTest, ConcurrentPushPop) {
  ThreadSafeQueue<int> queue;

  const int kNumProducers = 4;
  const int kNumConsumers = 4;
  const int kItemsPerProducer = 1000;

  std::atomic<int> total_produced{0};
  std::atomic<int> total_consumed{0};
  std::vector<int> consumed_values;
  std::mutex consumed_mutex;

  // 生产者线程
  std::vector<std::thread> producers;
  for (int i = 0; i < kNumProducers; ++i) {
    producers.emplace_back([&, i]() {
      for (int j = 0; j < kItemsPerProducer; ++j) {
        int value = i * kItemsPerProducer + j;
        queue.Push(value);
        total_produced++;
      }
    });
  }

  // 消费者线程
  std::vector<std::thread> consumers;
  for (int i = 0; i < kNumConsumers; ++i) {
    consumers.emplace_back([&]() {
      int value;
      while (total_consumed.load() < kNumProducers * kItemsPerProducer) {
        if (queue.Pop(value, 100ms)) {
          {
            std::lock_guard<std::mutex> lock(consumed_mutex);
            consumed_values.push_back(value);
          }
          total_consumed++;
        }
      }
    });
  }

  // 等待所有生产者完成
  for (auto& t : producers) {
    t.join();
  }

  // 等待所有消费者完成
  for (auto& t : consumers) {
    t.join();
  }

  // 验证结果
  EXPECT_EQ(total_produced.load(), kNumProducers * kItemsPerProducer);
  EXPECT_EQ(total_consumed.load(), kNumProducers * kItemsPerProducer);
  EXPECT_EQ(consumed_values.size(), kNumProducers * kItemsPerProducer);
  EXPECT_EQ(queue.Size(), 0);

  // 验证所有值都被消费且无重复
  std::sort(consumed_values.begin(), consumed_values.end());
  for (size_t i = 0; i < consumed_values.size(); ++i) {
    EXPECT_EQ(consumed_values[i], static_cast<int>(i));
  }
}

TEST(ThreadSafeQueueTest, ConcurrentStopSignal) {
  ThreadSafeQueue<int> queue;

  std::atomic<int> blocked_threads{0};
  std::atomic<int> unblocked_threads{0};

  const int kNumThreads = 10;
  std::vector<std::thread> threads;

  // 创建多个阻塞在 Pop 的线程
  for (int i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([&]() {
      blocked_threads++;
      int value;
      if (!queue.Pop(value, 10000ms)) {  // 长时间等待
        unblocked_threads++;             // Stop 后解除阻塞
      }
    });
  }

  // 等待所有线程开始阻塞
  while (blocked_threads.load() < kNumThreads) {
    std::this_thread::sleep_for(1ms);
  }

  // 调用 Stop，所有线程应该立即解除阻塞
  auto start = std::chrono::steady_clock::now();
  queue.Stop();

  for (auto& t : threads) {
    t.join();
  }
  auto end = std::chrono::steady_clock::now();
  auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  // 验证所有线程都被解除阻塞
  EXPECT_EQ(unblocked_threads.load(), kNumThreads);

  // Stop 应该快速唤醒所有线程（< 100ms）
  EXPECT_LT(elapsed.count(), 100);
}

// ============================================================================
// 边界条件测试
// ============================================================================

TEST(ThreadSafeQueueTest, EmptyQueue) {
  ThreadSafeQueue<int> queue;

  EXPECT_EQ(queue.Size(), 0);

  int value;
  ASSERT_FALSE(queue.Pop(value, 10ms));
}

TEST(ThreadSafeQueueTest, SingleElement) {
  ThreadSafeQueue<std::string> queue;

  queue.Push("hello");
  EXPECT_EQ(queue.Size(), 1);

  std::string value;
  ASSERT_TRUE(queue.Pop(value, 10ms));
  EXPECT_EQ(value, "hello");
  EXPECT_EQ(queue.Size(), 0);
}

TEST(ThreadSafeQueueTest, MoveSemantics) {
  ThreadSafeQueue<std::unique_ptr<int>> queue;

  auto ptr = std::make_unique<int>(42);
  queue.Push(std::move(ptr));

  EXPECT_EQ(ptr, nullptr);  // 已被移动

  std::unique_ptr<int> popped;
  ASSERT_TRUE(queue.Pop(popped, 10ms));
  ASSERT_NE(popped, nullptr);
  EXPECT_EQ(*popped, 42);
}

// ============================================================================
// 性能测试（验收标准：相比 busy-wait 降低 CPU 占用）
// ============================================================================

TEST(ThreadSafeQueueTest, DISABLED_PerformanceBenchmark) {
  // 注意：此测试默认禁用（DISABLED_前缀），需要手动运行
  // 用途：对比改造前后 CPU 占用，验证任务 4 的目标

  ThreadSafeQueue<int> queue;

  const int kNumItems = 1000000;

  auto start = std::chrono::steady_clock::now();

  // 生产者
  std::thread producer([&]() {
    for (int i = 0; i < kNumItems; ++i) {
      queue.Push(i);
    }
  });

  // 消费者
  std::thread consumer([&]() {
    int value;
    for (int i = 0; i < kNumItems; ++i) {
      while (!queue.Pop(value, 10ms)) {
        // 阻塞等待
      }
    }
  });

  producer.join();
  consumer.join();

  auto end = std::chrono::steady_clock::now();
  auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  std::cout << "ThreadSafeQueue benchmark: " << kNumItems
            << " items processed in " << elapsed.count() << "ms\n";
  std::cout << "Throughput: " << (kNumItems * 1000.0 / elapsed.count())
            << " items/sec\n";

  // 对比基准：
  // - 使用 sleep_for 轮询：预期 CPU 占用 5-15%
  // - 使用 condition_variable：预期 CPU 占用 < 3%
  // 测试方法：使用 top/htop 观察进程 CPU 占用
}
