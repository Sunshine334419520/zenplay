#pragma once

#include <functional>
#include <memory>
#include <optional>

#include "callback.h"
#include "player/config/global_config.h"
#include "post_task_interface.h"

/**
 * @file config_manager.h
 * @brief 基于 Loki 任务派遣的配置管理器
 *
 * 设计理念：
 * 1. 所有配置操作都派遣到 IO 线程执行，消除锁的需要
 * 2. 使用 Loki 内置的 Invoke() 实现同步调用（更简洁）
 * 3. 使用 Loki 的 PostTask() 实现异步调用
 * 4. 使用 PostTaskAndReplyWithResult() 实现异步带返回值的调用
 *
 * 使用方式：
 * ```cpp
 * auto* config = ConfigManager::Instance();
 *
 * // 读取（同步，使用 Loki::Invoke）
 * int size = config->GetInt("player.audio.buffer_size", 4096);
 *
 * // 写入（同步，使用 Loki::Invoke）
 * config->Set("player.audio.buffer_size", 8192);
 *
 * // 写入（异步，使用 Loki::PostTask）
 * config->SetAsync("player.audio.buffer_size", 8192, []() {
 *   // 写入完成回调
 * });
 * ```
 */

// 编译宏：是否使用 Loki 任务派遣
#ifndef ZENPLAY_CONFIG_USE_LOKI_DISPATCH
#define ZENPLAY_CONFIG_USE_LOKI_DISPATCH 0
#endif

namespace zenplay {

/**
 * @brief 自动保存策略
 */
enum class AutoSavePolicy {
  Manual,     // 手动保存（需要显式调用 Save）
  Immediate,  // 立即保存（每次修改都保存，IO 密集）
  Debounced,  // 防抖保存（延迟保存，批量修改只保存一次，推荐）
  OnExit      // 退出时保存（需要确保程序正常退出）
};

/**
 * @brief 配置管理器（基于 Loki 任务派遣）
 *
 * 特性：
 * 1. 线程安全：所有操作派遣到 IO 线程，单线程访问 GlobalConfig
 * 2. 无锁设计：IO 线程独占访问，无需读写锁
 * 3. 使用 Loki 内置 API：Invoke (同步), PostTask (异步)
 * 4. 自动保存：支持多种自动保存策略（防抖/立即/手动/退出时）
 */
class ConfigManager {
 public:
  /**
   * @brief 获取单例实例
   */
  static ConfigManager* Instance();

  // 禁用拷贝和赋值
  ConfigManager(const ConfigManager&) = delete;
  ConfigManager& operator=(const ConfigManager&) = delete;

  /**
   * @brief 初始化配置管理器
   * @param policy 自动保存策略（默认防抖保存）
   * @param debounce_delay 防抖延迟时间（仅对 Debounced 策略有效）
   * @note 必须在 IO 线程创建后调用
   */
  void Initialize(AutoSavePolicy policy = AutoSavePolicy::Debounced,
                  std::chrono::milliseconds debounce_delay =
                      std::chrono::milliseconds(1000));

  /**
   * @brief 设置自动保存策略
   */
  void SetAutoSavePolicy(AutoSavePolicy policy);

  /**
   * @brief 设置防抖延迟时间
   */
  void SetDebounceDelay(std::chrono::milliseconds delay);

  /**
   * @brief 获取当前自动保存策略
   */
  AutoSavePolicy GetAutoSavePolicy() const { return auto_save_policy_; }

  /**
   * @brief 获取当前防抖延迟时间
   */
  std::chrono::milliseconds GetDebounceDelay() const { return debounce_delay_; }

  // ==================== 文件操作 ====================

  /**
   * @brief 加载配置文件（同步）
   */
  Result<void> Load(const std::string& config_path = "config/zenplay.json");

  /**
   * @brief 保存配置文件（同步）
   */
  Result<void> Save();

  /**
   * @brief 保存配置文件（异步）
   */
  void SaveAsync(std::function<void(Result<void>)> callback = nullptr);

  // ==================== 读取操作（同步，使用 Loki::Invoke）
  // ====================

  /**
   * @brief 获取布尔值
   */
  bool GetBool(const std::string& key, bool default_value = false) const;

  /**
   * @brief 获取整数
   */
  int GetInt(const std::string& key, int default_value = 0) const;

  /**
   * @brief 获取 64 位整数
   */
  int64_t GetInt64(const std::string& key, int64_t default_value = 0) const;

  /**
   * @brief 获取浮点数
   */
  double GetDouble(const std::string& key, double default_value = 0.0) const;

  /**
   * @brief 获取字符串
   */
  std::string GetString(const std::string& key,
                        const std::string& default_value = "") const;

  /**
   * @brief 获取字符串数组
   */
  std::vector<std::string> GetStringArray(const std::string& key) const;

  /**
   * @brief 获取配置值
   */
  std::optional<ConfigValue> Get(const std::string& key) const;

  /**
   * @brief 检查配置键是否存在
   */
  bool Has(const std::string& key) const;

  // ==================== 写入操作（同步，使用 Loki::Invoke）
  // ====================

  /**
   * @brief 设置布尔值（同步）
   */
  void Set(const std::string& key, bool value);

  /**
   * @brief 设置整数（同步）
   */
  void Set(const std::string& key, int value);

  /**
   * @brief 设置 64 位整数（同步）
   */
  void Set(const std::string& key, int64_t value);

  /**
   * @brief 设置浮点数（同步）
   */
  void Set(const std::string& key, double value);

  /**
   * @brief 设置字符串（同步）
   */
  void Set(const std::string& key, const std::string& value);

  /**
   * @brief 设置字符串数组（同步）
   */
  void Set(const std::string& key, const std::vector<std::string>& value);

  // ==================== 写入操作（异步，使用 Loki::PostTask）
  // ====================

  /**
   * @brief 设置布尔值（异步）
   */
  void SetAsync(const std::string& key,
                bool value,
                std::function<void()> callback = nullptr);

  /**
   * @brief 设置整数（异步）
   */
  void SetAsync(const std::string& key,
                int value,
                std::function<void()> callback = nullptr);

  /**
   * @brief 设置 64 位整数（异步）
   */
  void SetAsync(const std::string& key,
                int64_t value,
                std::function<void()> callback = nullptr);

  /**
   * @brief 设置浮点数（异步）
   */
  void SetAsync(const std::string& key,
                double value,
                std::function<void()> callback = nullptr);

  /**
   * @brief 设置字符串（异步）
   */
  void SetAsync(const std::string& key,
                const std::string& value,
                std::function<void()> callback = nullptr);

  /**
   * @brief 设置字符串数组（异步）
   */
  void SetAsync(const std::string& key,
                const std::vector<std::string>& value,
                std::function<void()> callback = nullptr);

  // ==================== 配置监听 ====================

  /**
   * @brief 监听配置变化
   * @return 监听器 ID
   */
  int Watch(
      const std::string& key,
      std::function<void(const ConfigValue&, const ConfigValue&)> callback);

  /**
   * @brief 取消监听
   */
  void Unwatch(int watcher_id);

  // ==================== 配置验证 ====================

  /**
   * @brief 验证配置值
   */
  Result<void> Validate(
      const std::string& key,
      std::function<bool(const ConfigValue&)> validator) const;

 private:
  ConfigManager() = default;
  ~ConfigManager();

  /**
   * @brief 触发自动保存（根据策略）
   */
  void TriggerAutoSave();

  /**
   * @brief 取消待处理的防抖保存
   */
  void CancelDebouncedSave();

  GlobalConfig* config_ = nullptr;  // 指向 GlobalConfig 单例的指针
  bool initialized_ = false;

  // 自动保存配置
  AutoSavePolicy auto_save_policy_ = AutoSavePolicy::Manual;
  std::chrono::milliseconds debounce_delay_{1000};  // 默认 1 秒
  bool save_pending_ = false;                       // 是否有待处理的保存
};

}  // namespace zenplay
