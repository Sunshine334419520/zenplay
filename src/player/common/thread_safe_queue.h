#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <type_traits>

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
    condition_.notify_all();
  }

  /**
   * @brief 清空队列并对每个元素调用清理回调
   * @param cleanup_callback 清理回调函数，接收队列中的每个元素
   * @note 适用于需要自定义清理逻辑的场景（如释放指针）
   */
  template <typename CleanupFunc>
  void Clear(CleanupFunc cleanup_callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!queue_.empty()) {
      T item = std::move(queue_.front());
      queue_.pop();
      cleanup_callback(item);  // 调用者决定如何清理
    }
    condition_.notify_all();
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