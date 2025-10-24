#pragma once

#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "player/common/error.h"

// 编译宏：是否使用锁保护
// 如果使用 ConfigManager（Loki 派遣），则不需要锁
#ifndef ZENPLAY_CONFIG_USE_LOCK
#define ZENPLAY_CONFIG_USE_LOCK 1
#endif

#if ZENPLAY_CONFIG_USE_LOCK
#include <shared_mutex>
#endif

namespace zenplay {

/**
 * @brief 配置值类型（支持多种类型）
 */
class ConfigValue {
 public:
  explicit ConfigValue(const nlohmann::json& value) : value_(value) {}

  // 类型转换方法
  bool AsBool(bool default_value = false) const;
  int AsInt(int default_value = 0) const;
  int64_t AsInt64(int64_t default_value = 0) const;
  double AsDouble(double default_value = 0.0) const;
  std::string AsString(const std::string& default_value = "") const;
  std::vector<std::string> AsStringArray() const;

  // 检查类型
  bool IsBool() const { return value_.is_boolean(); }
  bool IsInt() const { return value_.is_number_integer(); }
  bool IsDouble() const { return value_.is_number_float(); }
  bool IsString() const { return value_.is_string(); }
  bool IsArray() const { return value_.is_array(); }
  bool IsObject() const { return value_.is_object(); }

  // 获取原始 JSON
  const nlohmann::json& Raw() const { return value_; }

 private:
  nlohmann::json value_;
};

/**
 * @brief 配置变化监听器回调
 */
using ConfigChangeCallback = std::function<void(const ConfigValue& old_value,
                                                const ConfigValue& new_value)>;

/**
 * @brief 全局配置管理器（线程安全单例）
 *
 * 特性：
 * 1. 单例模式（Meyer's Singleton）
 * 2. 线程安全（读写锁）
 * 3. 热重载支持
 * 4. 配置监听
 * 5. 默认值支持
 * 6. 配置验证
 */
class GlobalConfig {
 public:
  /**
   * @brief 获取全局单例实例
   */
  static GlobalConfig& Instance();

  // 禁用拷贝和赋值
  GlobalConfig(const GlobalConfig&) = delete;
  GlobalConfig& operator=(const GlobalConfig&) = delete;

  /**
   * @brief 加载配置文件
   *
   * @param config_path 配置文件路径（.json）
   * @return Result<void> 成功或错误
   *
   * @note 如果文件不存在，使用默认配置
   */
  Result<void> Load(const std::string& config_path = "config/zenplay.json");

  /**
   * @brief 保存配置到文件
   */
  Result<void> Save(const std::string& config_path = "");

  /**
   * @brief 重新加载配置文件（热重载）
   */
  Result<void> Reload();

  /**
   * @brief 获取配置值（支持点号路径）
   *
   * @param key 配置键（如 "player.audio.buffer_size"）
   * @param default_value 默认值（如果键不存在）
   * @return 配置值
   *
   * @example
   *   int size = config.GetInt("player.audio.buffer_size", 4096);
   *   bool hw = config.GetBool("render.use_hardware_acceleration", true);
   */
  bool GetBool(const std::string& key, bool default_value = false) const;
  int GetInt(const std::string& key, int default_value = 0) const;
  int64_t GetInt64(const std::string& key, int64_t default_value = 0) const;
  double GetDouble(const std::string& key, double default_value = 0.0) const;
  std::string GetString(const std::string& key,
                        const std::string& default_value = "") const;
  std::vector<std::string> GetStringArray(const std::string& key) const;

  /**
   * @brief 获取配置值对象（高级用法）
   */
  std::optional<ConfigValue> Get(const std::string& key) const;

  /**
   * @brief 设置配置值
   *
   * @param key 配置键
   * @param value 配置值（支持 bool/int/double/string/vector）
   *
   * @note 设置后需要调用 Save() 持久化
   */
  void Set(const std::string& key, bool value);
  void Set(const std::string& key, int value);
  void Set(const std::string& key, int64_t value);
  void Set(const std::string& key, double value);
  void Set(const std::string& key, const std::string& value);
  void Set(const std::string& key, const std::vector<std::string>& value);
  void Set(const std::string& key, const nlohmann::json& value);

  /**
   * @brief 检查配置键是否存在
   */
  bool Has(const std::string& key) const;

  /**
   * @brief 监听配置变化
   *
   * @param key 配置键
   * @param callback 回调函数
   * @return 监听器 ID（用于取消监听）
   *
   * @example
   *   int id = config.Watch("render.use_hardware_acceleration",
   *                         [](auto old_val, auto new_val) {
   *                           if (new_val.AsBool()) {
   *                             // 切换到硬件渲染
   *                           }
   *                         });
   *   config.Unwatch(id);  // 取消监听
   */
  int Watch(const std::string& key, ConfigChangeCallback callback);
  void Unwatch(int watch_id);

  /**
   * @brief 验证配置值
   *
   * @param key 配置键
   * @param validator 验证函数（返回 true 表示合法）
   * @return Result<void> 验证结果
   *
   * @example
   *   config.Validate("player.audio.buffer_size", [](const ConfigValue& val) {
   *     int size = val.AsInt();
   *     return size >= 1024 && size <= 65536;
   *   });
   */
  Result<void> Validate(
      const std::string& key,
      std::function<bool(const ConfigValue&)> validator) const;

  /**
   * @brief 重置为默认配置
   */
  void ResetToDefaults();

  /**
   * @brief 获取配置文件路径
   */
  std::string GetConfigPath() const { return config_path_; }

  /**
   * @brief 导出配置为 JSON 字符串（用于调试）
   */
  std::string Dump(int indent = 2) const;

 private:
  GlobalConfig();
  ~GlobalConfig() = default;

  // 内部方法
  nlohmann::json* GetValuePtr(const std::string& key);
  const nlohmann::json* GetValuePtr(const std::string& key) const;
  void NotifyWatchers(const std::string& key,
                      const nlohmann::json& old_value,
                      const nlohmann::json& new_value);
  nlohmann::json CreateDefaultConfig() const;

  // 成员变量
  nlohmann::json config_;    // 配置数据
  std::string config_path_;  // 配置文件路径

#if ZENPLAY_CONFIG_USE_LOCK
  mutable std::shared_mutex mutex_;  // 读写锁（仅在直接使用时需要）
#endif

  // 监听器
  struct Watcher {
    int id;
    std::string key;
    ConfigChangeCallback callback;
  };
  std::vector<Watcher> watchers_;
  int next_watcher_id_ = 1;
};

}  // namespace zenplay
