// 测试 PlayerStateManager 中 WaitForResume 在 Stop 时能否正确唤醒
//
// 测试场景：
// 1. 线程 A 调用 WaitForResume() 等待（状态为 Paused）
// 2. 线程 B 调用 TransitionToStopped()
// 3. 期望：线程 A 立即从 WaitForResume() 返回

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "player/common/player_state_manager.h"

using namespace zenplay;
using namespace std::chrono_literals;

TEST(PlayerStateManagerTest, WaitForResumeWakesUpOnStop) {
  PlayerStateManager state_manager;

  // 设置为 Paused 状态
  state_manager.TransitionToOpening();
  state_manager.TransitionToStopped();
  state_manager.TransitionToPlaying();
  state_manager.TransitionToPaused();

  EXPECT_EQ(state_manager.GetState(), PlayerStateManager::PlayerState::kPaused);

  // 标记线程是否从 WaitForResume 返回
  std::atomic<bool> wait_returned{false};
  auto start_time = std::chrono::steady_clock::now();

  // 线程 A：等待恢复
  std::thread thread_a(
      [&]() {
        bool result = state_manager.WaitForResume(5000);  // 5秒超时
        wait_returned.store(true);

        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                            end_time - start_time)
                            .count();

        // 应该在很短的时间内返回（< 100ms），而不是等待 5 秒超时
        EXPECT_LT(duration, 100)
            << "WaitForResume should return quickly, took " << duration << "ms";
        EXPECT_TRUE(result)
            << "WaitForResume should return true (predicate satisfied)";
      });

  // 等待线程 A 进入等待状态
  std::this_thread::sleep_for(50ms);

  // 线程 B：转换到 Stopped 状态
  EXPECT_TRUE(state_manager.TransitionToStopped());

  // 等待线程 A 返回
  thread_a.join();

  // 验证线程 A 已经返回
  EXPECT_TRUE(wait_returned.load());
}

TEST(PlayerStateManagerTest, WaitForResumeWakesUpOnIdle) {
  PlayerStateManager state_manager;

  // 设置为 Paused 状态
  state_manager.TransitionToOpening();
  state_manager.TransitionToStopped();
  state_manager.TransitionToPlaying();
  state_manager.TransitionToPaused();

  std::atomic<bool> wait_returned{false};
  auto start_time = std::chrono::steady_clock::now();

  std::thread thread_a([&]() {
    state_manager.WaitForResume(5000);
    wait_returned.store(true);

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start_time)
                        .count();
    EXPECT_LT(duration, 100);
  });

  std::this_thread::sleep_for(50ms);

  // 转换到 Idle（通过 Stopped）
  state_manager.TransitionToStopped();
  state_manager.TransitionToIdle();

  thread_a.join();
  EXPECT_TRUE(wait_returned.load());
}

TEST(PlayerStateManagerTest, WaitForResumeWakesUpOnError) {
  PlayerStateManager state_manager;

  state_manager.TransitionToOpening();
  state_manager.TransitionToStopped();
  state_manager.TransitionToPlaying();
  state_manager.TransitionToPaused();

  std::atomic<bool> wait_returned{false};
  auto start_time = std::chrono::steady_clock::now();

  std::thread thread_a([&]() {
    state_manager.WaitForResume(5000);
    wait_returned.store(true);

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start_time)
                        .count();
    EXPECT_LT(duration, 100);
  });

  std::this_thread::sleep_for(50ms);

  // 转换到 Error
  state_manager.TransitionToError();

  thread_a.join();
  EXPECT_TRUE(wait_returned.load());
}

TEST(PlayerStateManagerTest, WaitForResumeWakesUpOnPlaying) {
  PlayerStateManager state_manager;

  state_manager.TransitionToOpening();
  state_manager.TransitionToStopped();
  state_manager.TransitionToPlaying();
  state_manager.TransitionToPaused();

  std::atomic<bool> wait_returned{false};
  auto start_time = std::chrono::steady_clock::now();

  std::thread thread_a([&]() {
    bool result = state_manager.WaitForResume(5000);
    wait_returned.store(true);

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start_time)
                        .count();
    EXPECT_LT(duration, 100);
    EXPECT_TRUE(result);
  });

  std::this_thread::sleep_for(50ms);

  // 转换到 Playing（正常恢复）
  state_manager.TransitionToPlaying();

  thread_a.join();
  EXPECT_TRUE(wait_returned.load());
}

TEST(PlayerStateManagerTest, MultipleThreadsWaitingOnPause) {
  PlayerStateManager state_manager;

  state_manager.TransitionToOpening();
  state_manager.TransitionToStopped();
  state_manager.TransitionToPlaying();
  state_manager.TransitionToPaused();

  std::atomic<int> threads_returned{0};
  constexpr int NUM_THREADS = 5;

  std::vector<std::thread> threads;
  for (int i = 0; i < NUM_THREADS; ++i) {
    threads.emplace_back([&]() {
      state_manager.WaitForResume(5000);
      threads_returned.fetch_add(1);
    });
  }

  std::this_thread::sleep_for(100ms);

  // 转换到 Stopped 应该唤醒所有线程
  state_manager.TransitionToStopped();

  for (auto& t : threads) {
    t.join();
  }

  EXPECT_EQ(threads_returned.load(), NUM_THREADS)
      << "All threads should be woken up";
}

TEST(PlayerStateManagerTest, WaitForResumePredicateCorrect) {
  PlayerStateManager state_manager;

  // 测试 predicate：Playing 或 ShouldStop() 时应该返回

  // 场景 1：Playing 状态
  state_manager.TransitionToOpening();
  state_manager.TransitionToStopped();
  state_manager.TransitionToPlaying();

  bool result = state_manager.WaitForResume(100);
  EXPECT_TRUE(result) << "Should return true immediately when Playing";

  // 场景 2：Stopped 状态（ShouldStop() = true）
  state_manager.TransitionToPaused();
  state_manager.TransitionToStopped();

  result = state_manager.WaitForResume(100);
  EXPECT_TRUE(result) << "Should return true immediately when Stopped";

  // 场景 3：Idle 状态（ShouldStop() = true）
  state_manager.TransitionToIdle();

  result = state_manager.WaitForResume(100);
  EXPECT_TRUE(result) << "Should return true immediately when Idle";

  // 场景 4：Error 状态（ShouldStop() = true）
  state_manager.TransitionToOpening();
  state_manager.TransitionToError();

  result = state_manager.WaitForResume(100);
  EXPECT_TRUE(result) << "Should return true immediately when Error";
}
