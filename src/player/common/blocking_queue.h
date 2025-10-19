#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>

namespace zenplay {

/**
 * @brief 线程安全的阻塞队列，支持生产者/消费者模式
 *
 * 特性：
 * - 线程安全：使用 mutex 和 condition_variable 保护
 * - 容量限制：可设置最大容量，Push 时队列满则阻塞
 * - 阻塞/超时操作：支持阻塞式和超时式 Push/Pop
 * - 优雅关闭：Stop() 后唤醒所有等待线程并拒绝新操作
 * - 高效唤醒：使用两个条件变量分别通知生产者/消费者
 *
 * 使用场景：
 * - 解码线程生产帧，渲染线程消费帧
 * - Demux 线程生产包，解码线程消费包
 * - 替换 busy-wait 和 sleep 轮询模式
 *
 * @tparam T 队列元素类型（建议使用移动语义高效的类型）
 */
template <typename T>
class BlockingQueue {
 public:
  /**
   * @brief 构造阻塞队列
   * @param max_size 最大容量（0 表示无限制）
   */
  explicit BlockingQueue(size_t max_size = 0)
      : max_size_(max_size), stopped_(false) {}

  /**
   * @brief 析构函数，自动调用 Stop()
   */
  ~BlockingQueue() { Stop(); }

  // 禁止拷贝和赋值
  BlockingQueue(const BlockingQueue&) = delete;
  BlockingQueue& operator=(const BlockingQueue&) = delete;

  /**
   * @brief 向队列尾部推入元素（阻塞版本）
   *
   * 如果队列已满，阻塞直到有空间或队列被停止
   *
   * @param item 要推入的元素（支持左值和右值）
   * @return true 成功推入，false 队列已停止
   */
  bool Push(const T& item) {
    std::unique_lock<std::mutex> lock(mutex_);

    // 等待直到有空间或队列停止
    not_full_cv_.wait(lock, [this] {
      return stopped_ || (max_size_ == 0 || queue_.size() < max_size_);
    });

    if (stopped_) {
      return false;
    }

    queue_.push_back(item);
    not_empty_cv_.notify_one();  // 通知消费者
    return true;
  }

  /**
   * @brief 向队列尾部推入元素（移动版本）
   */
  bool Push(T&& item) {
    std::unique_lock<std::mutex> lock(mutex_);

    // 等待直到有空间或队列停止
    not_full_cv_.wait(lock, [this] {
      return stopped_ || (max_size_ == 0 || queue_.size() < max_size_);
    });

    if (stopped_) {
      return false;
    }

    queue_.push_back(std::move(item));
    not_empty_cv_.notify_one();  // 通知消费者
    return true;
  }

  /**
   * @brief 向队列尾部推入元素（超时版本，左值）
   *
   * @param item 要推入的元素
   * @param timeout_ms 超时时间（毫秒）
   * @return true 成功推入，false 超时或队列已停止
   */
  bool PushTimeout(const T& item, int64_t timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);

    // 等待直到有空间、超时或队列停止
    bool success = not_full_cv_.wait_for(
        lock, std::chrono::milliseconds(timeout_ms), [this] {
          return stopped_ || (max_size_ == 0 || queue_.size() < max_size_);
        });

    if (stopped_ || !success) {
      return false;
    }

    queue_.push_back(item);
    not_empty_cv_.notify_one();
    return true;
  }

  /**
   * @brief 向队列尾部推入元素（超时版本，右值）
   */
  bool PushTimeout(T&& item, int64_t timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);

    // 等待直到有空间、超时或队列停止
    bool success = not_full_cv_.wait_for(
        lock, std::chrono::milliseconds(timeout_ms), [this] {
          return stopped_ || (max_size_ == 0 || queue_.size() < max_size_);
        });

    if (stopped_ || !success) {
      return false;
    }

    queue_.push_back(std::move(item));
    not_empty_cv_.notify_one();
    return true;
  }

  /**
   * @brief 从队列头部弹出元素（阻塞版本）
   *
   * 如果队列为空，阻塞直到有元素或队列被停止
   *
   * @param out 输出参数，接收弹出的元素
   * @return true 成功弹出，false 队列已停止且为空
   */
  bool Pop(T& out) {
    std::unique_lock<std::mutex> lock(mutex_);

    // 等待直到有元素或队列停止
    not_empty_cv_.wait(lock, [this] { return stopped_ || !queue_.empty(); });

    if (queue_.empty()) {
      return false;  // 队列已停止且为空
    }

    out = std::move(queue_.front());
    queue_.pop_front();
    not_full_cv_.notify_one();  // 通知生产者
    return true;
  }

  /**
   * @brief 从队列头部弹出元素（超时版本）
   *
   * @param out 输出参数
   * @param timeout_ms 超时时间（毫秒）
   * @return true 成功弹出，false 超时或队列已停止且为空
   */
  bool PopTimeout(T& out, int64_t timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);

    // 等待直到有元素、超时或队列停止
    bool success =
        not_empty_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                               [this] { return stopped_ || !queue_.empty(); });

    if (queue_.empty() || !success) {
      return false;
    }

    out = std::move(queue_.front());
    queue_.pop_front();
    not_full_cv_.notify_one();
    return true;
  }

  /**
   * @brief 尝试推入元素（非阻塞版本，左值）
   *
   * @param item 要推入的元素
   * @return true 成功推入，false 队列已满或已停止
   */
  bool TryPush(const T& item) {
    std::unique_lock<std::mutex> lock(mutex_);

    if (stopped_ || (max_size_ > 0 && queue_.size() >= max_size_)) {
      return false;
    }

    queue_.push_back(item);
    not_empty_cv_.notify_one();
    return true;
  }

  /**
   * @brief 尝试推入元素（非阻塞版本，右值）
   */
  bool TryPush(T&& item) {
    std::unique_lock<std::mutex> lock(mutex_);

    if (stopped_ || (max_size_ > 0 && queue_.size() >= max_size_)) {
      return false;
    }

    queue_.push_back(std::move(item));
    not_empty_cv_.notify_one();
    return true;
  }

  /**
   * @brief 尝试弹出元素（非阻塞版本）
   *
   * @param out 输出参数
   * @return true 成功弹出，false 队列为空或已停止
   */
  bool TryPop(T& out) {
    std::unique_lock<std::mutex> lock(mutex_);

    if (queue_.empty()) {
      return false;
    }

    out = std::move(queue_.front());
    queue_.pop_front();
    not_full_cv_.notify_one();
    return true;
  }

  /**
   * @brief 停止队列，唤醒所有等待的线程
   *
   * 调用后：
   * - 所有阻塞的 Push/Pop 操作将返回 false
   * - 新的 Push 操作将立即返回 false
   * - Pop 操作仍可以消费剩余元素
   */
  void Stop() {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      stopped_ = true;
    }
    not_empty_cv_.notify_all();
    not_full_cv_.notify_all();
  }

  /**
   * @brief 清空队列并重置停止状态
   *
   * 警告：此操作不是线程安全的，应在确保无其他线程访问时调用
   */
  void Reset() {
    std::unique_lock<std::mutex> lock(mutex_);
    queue_.clear();
    stopped_ = false;
  }

  /**
   * @brief 清空队列中的所有元素
   */
  void Clear() {
    std::unique_lock<std::mutex> lock(mutex_);
    queue_.clear();
    not_full_cv_.notify_all();  // 通知可能阻塞的生产者
  }

  /**
   * @brief 清空队列并对每个元素调用清理回调
   *
   * @param cleanup_callback 清理回调函数，接收队列中的每个元素
   * @note 适用于需要自定义清理逻辑的场景（如释放指针）
   *
   * 示例：
   * @code
   * BlockingQueue<AVPacket*> queue(100);
   * queue.Clear([](AVPacket* packet) {
   *   if (packet) {
   *     av_packet_free(&packet);
   *   }
   * });
   * @endcode
   */
  template <typename CleanupFunc>
  void Clear(CleanupFunc cleanup_callback) {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!queue_.empty()) {
      T item = std::move(queue_.front());
      queue_.pop_front();
      cleanup_callback(item);  // 调用者决定如何清理
    }
    not_full_cv_.notify_all();  // 通知可能阻塞的生产者
  }

  /**
   * @brief 获取队列当前大小
   */
  size_t Size() const {
    std::unique_lock<std::mutex> lock(mutex_);
    return queue_.size();
  }

  /**
   * @brief 检查队列是否为空
   */
  bool Empty() const {
    std::unique_lock<std::mutex> lock(mutex_);
    return queue_.empty();
  }

  /**
   * @brief 检查队列是否已满
   */
  bool Full() const {
    std::unique_lock<std::mutex> lock(mutex_);
    return max_size_ > 0 && queue_.size() >= max_size_;
  }

  /**
   * @brief 检查队列是否已停止
   */
  bool Stopped() const {
    std::unique_lock<std::mutex> lock(mutex_);
    return stopped_;
  }

  /**
   * @brief 获取队列最大容量
   */
  size_t MaxSize() const { return max_size_; }

 private:
  mutable std::mutex mutex_;
  std::condition_variable not_empty_cv_;  // 队列非空条件变量（消费者等待）
  std::condition_variable not_full_cv_;   // 队列未满条件变量（生产者等待）
  std::deque<T> queue_;
  size_t max_size_;
  bool stopped_;
};

}  // namespace zenplay
