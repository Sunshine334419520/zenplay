#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "player/common/blocking_queue.h"

namespace zenplay {

// ============================================================================
// 基础功能测试
// ============================================================================

TEST(BlockingQueueTest, BasicPushPop) {
  BlockingQueue<int> queue(5);

  EXPECT_TRUE(queue.Push(1));
  EXPECT_TRUE(queue.Push(2));
  EXPECT_TRUE(queue.Push(3));
  EXPECT_EQ(queue.Size(), 3);

  int val;
  EXPECT_TRUE(queue.Pop(val));
  EXPECT_EQ(val, 1);
  EXPECT_TRUE(queue.Pop(val));
  EXPECT_EQ(val, 2);
  EXPECT_TRUE(queue.Pop(val));
  EXPECT_EQ(val, 3);
  EXPECT_TRUE(queue.Empty());
}

TEST(BlockingQueueTest, TryPushPop) {
  BlockingQueue<int> queue(2);

  EXPECT_TRUE(queue.TryPush(1));
  EXPECT_TRUE(queue.TryPush(2));
  EXPECT_FALSE(queue.TryPush(3));  // 队列已满

  int val;
  EXPECT_TRUE(queue.TryPop(val));
  EXPECT_EQ(val, 1);
  EXPECT_TRUE(queue.TryPop(val));
  EXPECT_EQ(val, 2);
  EXPECT_FALSE(queue.TryPop(val));  // 队列已空
}

TEST(BlockingQueueTest, UnboundedQueue) {
  BlockingQueue<int> queue(0);  // 无限制容量

  for (int i = 0; i < 1000; ++i) {
    EXPECT_TRUE(queue.Push(i));
  }
  EXPECT_EQ(queue.Size(), 1000);

  for (int i = 0; i < 1000; ++i) {
    int val;
    EXPECT_TRUE(queue.Pop(val));
    EXPECT_EQ(val, i);
  }
  EXPECT_TRUE(queue.Empty());
}

TEST(BlockingQueueTest, Clear) {
  BlockingQueue<int> queue(10);

  queue.Push(1);
  queue.Push(2);
  queue.Push(3);
  EXPECT_EQ(queue.Size(), 3);

  queue.Clear();
  EXPECT_TRUE(queue.Empty());
  EXPECT_EQ(queue.Size(), 0);
}

TEST(BlockingQueueTest, StopAndReset) {
  BlockingQueue<int> queue(5);

  queue.Push(1);
  queue.Push(2);
  queue.Stop();

  EXPECT_TRUE(queue.Stopped());
  EXPECT_FALSE(queue.Push(3));  // Stop 后无法推入

  queue.Reset();
  EXPECT_FALSE(queue.Stopped());
  EXPECT_TRUE(queue.Empty());
  EXPECT_TRUE(queue.Push(4));  // Reset 后可以继续使用
}

// ============================================================================
// 阻塞与超时测试
// ============================================================================

TEST(BlockingQueueTest, PopBlocksUntilDataAvailable) {
  BlockingQueue<int> queue(5);
  std::atomic<bool> pop_completed{false};

  // 消费者线程：等待数据
  std::thread consumer([&]() {
    int val;
    EXPECT_TRUE(queue.Pop(val));
    EXPECT_EQ(val, 42);
    pop_completed = true;
  });

  // 主线程：稍后推入数据
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_FALSE(pop_completed);  // 消费者应该还在等待

  queue.Push(42);
  consumer.join();
  EXPECT_TRUE(pop_completed);  // 消费者应该已完成
}

TEST(BlockingQueueTest, PushBlocksWhenQueueFull) {
  BlockingQueue<int> queue(2);
  queue.Push(1);
  queue.Push(2);

  std::atomic<bool> push_completed{false};

  // 生产者线程：尝试推入第三个元素（应阻塞）
  std::thread producer([&]() {
    EXPECT_TRUE(queue.Push(3));
    push_completed = true;
  });

  // 主线程：等待一段时间，确认生产者被阻塞
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_FALSE(push_completed);

  // 弹出一个元素，释放空间
  int val;
  queue.Pop(val);
  producer.join();
  EXPECT_TRUE(push_completed);
}

TEST(BlockingQueueTest, PopTimeoutReturnsfalse) {
  BlockingQueue<int> queue(5);

  int val;
  auto start = std::chrono::steady_clock::now();
  EXPECT_FALSE(queue.PopTimeout(val, 100));  // 100ms 超时
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - start)
                     .count();

  EXPECT_GE(elapsed, 90);   // 至少等待 90ms（考虑误差）
  EXPECT_LE(elapsed, 150);  // 不超过 150ms
}

TEST(BlockingQueueTest, PushTimeoutReturnsfalse) {
  BlockingQueue<int> queue(1);
  queue.Push(1);  // 队列已满

  auto start = std::chrono::steady_clock::now();
  EXPECT_FALSE(queue.PushTimeout(2, 100));  // 100ms 超时
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - start)
                     .count();

  EXPECT_GE(elapsed, 90);
  EXPECT_LE(elapsed, 150);
}

TEST(BlockingQueueTest, PopTimeoutSucceedsWhenDataAvailable) {
  BlockingQueue<int> queue(5);

  // 异步推入数据
  std::thread producer([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    queue.Push(99);
  });

  int val;
  EXPECT_TRUE(queue.PopTimeout(val, 200));  // 应在超时前获得数据
  EXPECT_EQ(val, 99);
  producer.join();
}

// ============================================================================
// Stop() 行为测试
// ============================================================================

TEST(BlockingQueueTest, StopWakesUpBlockedConsumers) {
  BlockingQueue<int> queue(5);

  std::atomic<int> pop_count{0};

  // 启动多个消费者线程
  std::vector<std::thread> consumers;
  for (int i = 0; i < 3; ++i) {
    consumers.emplace_back([&]() {
      int val;
      if (!queue.Pop(val)) {
        pop_count++;  // Pop 返回 false（队列已停止）
      }
    });
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  queue.Stop();

  for (auto& t : consumers) {
    t.join();
  }

  EXPECT_EQ(pop_count, 3);  // 所有消费者都应被唤醒并返回 false
}

TEST(BlockingQueueTest, StopWakesUpBlockedProducers) {
  BlockingQueue<int> queue(1);
  queue.Push(1);  // 队列已满

  std::atomic<int> push_count{0};

  // 启动多个生产者线程
  std::vector<std::thread> producers;
  for (int i = 0; i < 3; ++i) {
    producers.emplace_back([&]() {
      if (!queue.Push(i + 2)) {
        push_count++;  // Push 返回 false（队列已停止）
      }
    });
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  queue.Stop();

  for (auto& t : producers) {
    t.join();
  }

  EXPECT_EQ(push_count, 3);  // 所有生产者都应被唤醒并返回 false
}

TEST(BlockingQueueTest, StopAllowsConsumingRemainingElements) {
  BlockingQueue<int> queue(10);

  queue.Push(1);
  queue.Push(2);
  queue.Push(3);
  queue.Stop();

  // Stop 后仍然可以消费剩余元素
  int val;
  EXPECT_TRUE(queue.Pop(val));
  EXPECT_EQ(val, 1);
  EXPECT_TRUE(queue.Pop(val));
  EXPECT_EQ(val, 2);
  EXPECT_TRUE(queue.Pop(val));
  EXPECT_EQ(val, 3);

  // 队列空后 Pop 返回 false
  EXPECT_FALSE(queue.Pop(val));
}

// ============================================================================
// 并发压力测试
// ============================================================================

TEST(BlockingQueueTest, MultiProducerMultiConsumer) {
  BlockingQueue<int> queue(100);
  constexpr int kNumProducers = 4;
  constexpr int kNumConsumers = 4;
  constexpr int kItemsPerProducer = 1000;

  std::atomic<int> total_produced{0};
  std::atomic<int> total_consumed{0};
  std::vector<int> consumed_values;
  std::mutex consumed_mutex;

  // 启动生产者
  std::vector<std::thread> producers;
  for (int i = 0; i < kNumProducers; ++i) {
    producers.emplace_back([&, i]() {
      for (int j = 0; j < kItemsPerProducer; ++j) {
        int val = i * kItemsPerProducer + j;
        while (!queue.Push(val)) {
          // 重试（理论上不会失败，除非 Stop）
        }
        total_produced++;
      }
    });
  }

  // 启动消费者
  std::vector<std::thread> consumers;
  for (int i = 0; i < kNumConsumers; ++i) {
    consumers.emplace_back([&]() {
      int val;
      while (total_consumed < kNumProducers * kItemsPerProducer) {
        if (queue.PopTimeout(val, 10)) {
          {
            std::lock_guard<std::mutex> lock(consumed_mutex);
            consumed_values.push_back(val);
          }
          total_consumed++;
        }
      }
    });
  }

  // 等待所有线程完成
  for (auto& t : producers) {
    t.join();
  }
  for (auto& t : consumers) {
    t.join();
  }

  EXPECT_EQ(total_produced, kNumProducers * kItemsPerProducer);
  EXPECT_EQ(total_consumed, kNumProducers * kItemsPerProducer);
  EXPECT_EQ(consumed_values.size(), kNumProducers * kItemsPerProducer);

  // 验证所有值都被消费（排序后应与生产的值一致）
  std::sort(consumed_values.begin(), consumed_values.end());
  for (int i = 0; i < kNumProducers * kItemsPerProducer; ++i) {
    EXPECT_EQ(consumed_values[i], i);
  }
}

TEST(BlockingQueueTest, StressTestWithStop) {
  BlockingQueue<int> queue(50);
  std::atomic<bool> should_stop{false};
  std::atomic<int> total_produced{0};
  std::atomic<int> total_consumed{0};

  // 生产者
  std::thread producer([&]() {
    for (int i = 0; i < 10000 && !should_stop; ++i) {
      if (queue.Push(i)) {
        total_produced++;
      }
    }
  });

  // 消费者
  std::thread consumer([&]() {
    int val;
    while (!should_stop || !queue.Empty()) {
      if (queue.PopTimeout(val, 1)) {
        total_consumed++;
      }
    }
  });

  // 运行一段时间后停止
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  should_stop = true;
  queue.Stop();

  producer.join();
  consumer.join();

  // 验证生产和消费数量一致
  EXPECT_EQ(total_produced, total_consumed);
}

// ============================================================================
// 移动语义测试（确保高效传递）
// ============================================================================

TEST(BlockingQueueTest, MoveSemantics) {
  struct MoveOnlyType {
    std::unique_ptr<int> data;
    MoveOnlyType(int val) : data(std::make_unique<int>(val)) {}
    MoveOnlyType(MoveOnlyType&&) = default;
    MoveOnlyType& operator=(MoveOnlyType&&) = default;
    MoveOnlyType(const MoveOnlyType&) = delete;
    MoveOnlyType& operator=(const MoveOnlyType&) = delete;
  };

  BlockingQueue<MoveOnlyType> queue(5);

  queue.Push(MoveOnlyType(42));
  queue.Push(MoveOnlyType(99));

  MoveOnlyType val1(0);
  EXPECT_TRUE(queue.Pop(val1));
  EXPECT_EQ(*val1.data, 42);

  MoveOnlyType val2(0);
  EXPECT_TRUE(queue.Pop(val2));
  EXPECT_EQ(*val2.data, 99);
}

// ============================================================================
// 边界条件测试
// ============================================================================

TEST(BlockingQueueTest, ZeroCapacityUnlimited) {
  BlockingQueue<int> queue(0);  // 无容量限制

  EXPECT_FALSE(queue.Full());
  for (int i = 0; i < 10000; ++i) {
    EXPECT_TRUE(queue.Push(i));
  }
  EXPECT_EQ(queue.Size(), 10000);
}

TEST(BlockingQueueTest, SingleElementQueue) {
  BlockingQueue<int> queue(1);

  EXPECT_TRUE(queue.Push(1));
  EXPECT_TRUE(queue.Full());
  EXPECT_FALSE(queue.TryPush(2));

  int val;
  EXPECT_TRUE(queue.Pop(val));
  EXPECT_EQ(val, 1);
  EXPECT_TRUE(queue.Empty());
}

TEST(BlockingQueueTest, PopFromEmptyQueueAfterStop) {
  BlockingQueue<int> queue(5);
  queue.Stop();

  int val;
  EXPECT_FALSE(queue.Pop(val));  // 队列空且已停止
}

// ============================================================================
// 性能基准测试（DISABLED，手动运行）
// ============================================================================

TEST(BlockingQueueTest, DISABLED_PerformanceBenchmark) {
  BlockingQueue<int> queue(1000);
  constexpr int kTotalItems = 1000000;

  auto start = std::chrono::steady_clock::now();

  std::thread producer([&]() {
    for (int i = 0; i < kTotalItems; ++i) {
      queue.Push(i);
    }
  });

  std::thread consumer([&]() {
    int val;
    for (int i = 0; i < kTotalItems; ++i) {
      queue.Pop(val);
    }
  });

  producer.join();
  consumer.join();

  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - start)
                     .count();

  double throughput = kTotalItems / (elapsed / 1000.0);
  std::cout << "BlockingQueue throughput: " << throughput << " items/sec"
            << std::endl;
  std::cout << "Elapsed time: " << elapsed << " ms" << std::endl;
}

}  // namespace zenplay
