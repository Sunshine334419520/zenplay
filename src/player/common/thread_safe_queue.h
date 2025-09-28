#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>

// 媒体数据队列，线程安全
template <typename T>
class ThreadSafeQueue {
 public:
  void Push(T item) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(std::move(item));
    condition_.notify_one();
  }

  bool Pop(T& item,
           std::chrono::milliseconds timeout = std::chrono::milliseconds(100)) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (condition_.wait_for(lock, timeout,
                            [this] { return !queue_.empty() || stop_; })) {
      if (!queue_.empty()) {
        item = std::move(queue_.front());
        queue_.pop();
        return true;
      }
    }
    return false;
  }

  void Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::queue<T> empty;
    queue_.swap(empty);
  }

  void Stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
    condition_.notify_all();
  }

  size_t Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
  }

 private:
  mutable std::mutex mutex_;
  std::queue<T> queue_;
  std::condition_variable condition_;
  std::atomic<bool> stop_{false};
};