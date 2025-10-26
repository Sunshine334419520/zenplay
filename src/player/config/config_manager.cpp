#include "player/config/config_manager.h"

#include "location.h"

namespace zenplay {

ConfigManager* ConfigManager::Instance() {
  static ConfigManager instance;
  return &instance;
}

ConfigManager::~ConfigManager() {
  // 如果策略是 OnExit，则在析构时保存
  if (auto_save_policy_ == AutoSavePolicy::OnExit && initialized_ && config_) {
    config_->Save();
  }
}

void ConfigManager::Initialize(AutoSavePolicy policy,
                               std::chrono::milliseconds debounce_delay) {
  config_ = GlobalConfig::Instance();
  auto_save_policy_ = policy;
  debounce_delay_ = debounce_delay;
  initialized_ = true;
}

void ConfigManager::SetAutoSavePolicy(AutoSavePolicy policy) {
  auto_save_policy_ = policy;

  // 如果切换到非防抖模式，取消待处理的保存
  if (policy != AutoSavePolicy::Debounced) {
    CancelDebouncedSave();
  }
}

void ConfigManager::SetDebounceDelay(std::chrono::milliseconds delay) {
  debounce_delay_ = delay;
}

void ConfigManager::TriggerAutoSave() {
  // 必须在 IO 线程调用
  switch (auto_save_policy_) {
    case AutoSavePolicy::Immediate:
      // 立即保存
      config_->Save();
      break;

    case AutoSavePolicy::Debounced:
      // 防抖保存：取消之前的，重新调度
      CancelDebouncedSave();
      save_pending_ = true;
      loki::PostDelayedTask(loki::IO, FROM_HERE,
                            loki::BindOnceClosure([this]() {
                              if (save_pending_) {
                                config_->Save();
                                save_pending_ = false;
                              }
                            }),
                            debounce_delay_);
      break;

    case AutoSavePolicy::Manual:
    case AutoSavePolicy::OnExit:
    default:
      // 不自动保存
      break;
  }
}

void ConfigManager::CancelDebouncedSave() {
  save_pending_ = false;
  // 注意：Loki 的 PostDelayedTask 没有返回取消句柄
  // 我们使用 save_pending_ 标志来避免执行
}

// ==================== 文件操作 ====================

Result<void> ConfigManager::Load(const std::string& config_path) {
#if ZENPLAY_CONFIG_USE_LOKI_DISPATCH
  // 使用 Loki::Invoke 同步调用
  return loki::Invoke<Result<void>>(
      loki::IO, FROM_HERE,
      [this, &config_path]() { return config_->Load(config_path); });
#else
  return config_->Load(config_path);
#endif
}

Result<void> ConfigManager::Save() {
#if ZENPLAY_CONFIG_USE_LOKI_DISPATCH
  // 使用 Loki::Invoke 同步调用
  return loki::Invoke<Result<void>>(loki::IO, FROM_HERE,
                                    [this]() { return config_->Save(); });
#else
  return config_->Save();
#endif
}

void ConfigManager::SaveAsync(std::function<void(Result<void>)> callback) {
#if ZENPLAY_CONFIG_USE_LOKI_DISPATCH
  // 使用 Loki 异步保存
  if (callback) {
    loki::PostTaskAndReply(
        loki::IO, FROM_HERE,
        loki::BindOnceClosure([this]() { config_->Save(); }),
        loki::BindOnceClosure([callback]() { callback(Result<void>::Ok()); }));
  } else {
    loki::PostTask(loki::IO, FROM_HERE,
                   loki::BindOnceClosure([this]() { config_->Save(); }));
  }
#else
  auto result = config_->Save();
  if (callback) {
    callback(result);
  }
#endif
}

// ==================== 读取操作（同步，使用 Loki::Invoke） ====================

bool ConfigManager::GetBool(const std::string& key, bool default_value) const {
#if ZENPLAY_CONFIG_USE_LOKI_DISPATCH
  // 使用 Loki::Invoke 同步调用
  return loki::Invoke<bool>(loki::IO, FROM_HERE, [this, &key, default_value]() {
    return config_->GetBool(key, default_value);
  });
#else
  return config_->GetBool(key, default_value);
#endif
}

int ConfigManager::GetInt(const std::string& key, int default_value) const {
#if ZENPLAY_CONFIG_USE_LOKI_DISPATCH
  return loki::Invoke<int>(loki::IO, FROM_HERE, [this, &key, default_value]() {
    return config_->GetInt(key, default_value);
  });
#else
  return config_->GetInt(key, default_value);
#endif
}

int64_t ConfigManager::GetInt64(const std::string& key,
                                int64_t default_value) const {
#if ZENPLAY_CONFIG_USE_LOKI_DISPATCH
  return loki::Invoke<int64_t>(loki::IO, FROM_HERE,
                               [this, &key, default_value]() {
                                 return config_->GetInt64(key, default_value);
                               });
#else
  return config_->GetInt64(key, default_value);
#endif
}

double ConfigManager::GetDouble(const std::string& key,
                                double default_value) const {
#if ZENPLAY_CONFIG_USE_LOKI_DISPATCH
  return loki::Invoke<double>(loki::IO, FROM_HERE,
                              [this, &key, default_value]() {
                                return config_->GetDouble(key, default_value);
                              });
#else
  return config_->GetDouble(key, default_value);
#endif
}

std::string ConfigManager::GetString(const std::string& key,
                                     const std::string& default_value) const {
#if ZENPLAY_CONFIG_USE_LOKI_DISPATCH
  return loki::Invoke<std::string>(
      loki::IO, FROM_HERE, [this, &key, &default_value]() {
        return config_->GetString(key, default_value);
      });
#else
  return config_->GetString(key, default_value);
#endif
}

std::vector<std::string> ConfigManager::GetStringArray(
    const std::string& key) const {
#if ZENPLAY_CONFIG_USE_LOKI_DISPATCH
  return loki::Invoke<std::vector<std::string>>(
      loki::IO, FROM_HERE,
      [this, &key]() { return config_->GetStringArray(key); });
#else
  return config_->GetStringArray(key);
#endif
}

std::optional<ConfigValue> ConfigManager::Get(const std::string& key) const {
#if ZENPLAY_CONFIG_USE_LOKI_DISPATCH
  return loki::Invoke<std::optional<ConfigValue>>(
      loki::IO, FROM_HERE, [this, &key]() { return config_->Get(key); });
#else
  return config_->Get(key);
#endif
}

bool ConfigManager::Has(const std::string& key) const {
#if ZENPLAY_CONFIG_USE_LOKI_DISPATCH
  return loki::Invoke<bool>(loki::IO, FROM_HERE,
                            [this, &key]() { return config_->Has(key); });
#else
  return config_->Has(key);
#endif
}

// ==================== 写入操作（同步，使用 Loki::Invoke） ====================

void ConfigManager::Set(const std::string& key, bool value) {
#if ZENPLAY_CONFIG_USE_LOKI_DISPATCH
  loki::Invoke<void>(loki::IO, FROM_HERE, [this, &key, value]() {
    config_->Set(key, value);
    TriggerAutoSave();  // 触发自动保存
  });
#else
  config_->Set(key, value);
  TriggerAutoSave();
#endif
}

void ConfigManager::Set(const std::string& key, int value) {
#if ZENPLAY_CONFIG_USE_LOKI_DISPATCH
  loki::Invoke<void>(loki::IO, FROM_HERE, [this, &key, value]() {
    config_->Set(key, value);
    TriggerAutoSave();  // 触发自动保存
  });
#else
  config_->Set(key, value);
  TriggerAutoSave();
#endif
}

void ConfigManager::Set(const std::string& key, int64_t value) {
#if ZENPLAY_CONFIG_USE_LOKI_DISPATCH
  loki::Invoke<void>(loki::IO, FROM_HERE, [this, &key, value]() {
    config_->Set(key, value);
    TriggerAutoSave();  // 触发自动保存
  });
#else
  config_->Set(key, value);
  TriggerAutoSave();
#endif
}

void ConfigManager::Set(const std::string& key, double value) {
#if ZENPLAY_CONFIG_USE_LOKI_DISPATCH
  loki::Invoke<void>(loki::IO, FROM_HERE, [this, &key, value]() {
    config_->Set(key, value);
    TriggerAutoSave();  // 触发自动保存
  });
#else
  config_->Set(key, value);
  TriggerAutoSave();
#endif
}

void ConfigManager::Set(const std::string& key, const std::string& value) {
#if ZENPLAY_CONFIG_USE_LOKI_DISPATCH
  loki::Invoke<void>(loki::IO, FROM_HERE, [this, &key, &value]() {
    config_->Set(key, value);
    TriggerAutoSave();  // 触发自动保存
  });
#else
  config_->Set(key, value);
  TriggerAutoSave();
#endif
}

void ConfigManager::Set(const std::string& key,
                        const std::vector<std::string>& value) {
#if ZENPLAY_CONFIG_USE_LOKI_DISPATCH
  loki::Invoke<void>(loki::IO, FROM_HERE, [this, &key, &value]() {
    config_->Set(key, value);
    TriggerAutoSave();  // 触发自动保存
  });
#else
  config_->Set(key, value);
  TriggerAutoSave();
#endif
}

// ==================== 写入操作（异步，使用 Loki::PostTask）
// ====================

void ConfigManager::SetAsync(const std::string& key,
                             bool value,
                             std::function<void()> callback) {
#if ZENPLAY_CONFIG_USE_LOKI_DISPATCH
  if (callback) {
    loki::PostTaskAndReply(loki::IO, FROM_HERE,
                           loki::BindOnceClosure([this, key, value]() {
                             config_->Set(key, value);
                             TriggerAutoSave();  // 触发自动保存
                           }),
                           loki::BindOnceClosure(callback));
  } else {
    loki::PostTask(loki::IO, FROM_HERE,
                   loki::BindOnceClosure([this, key, value]() {
                     config_->Set(key, value);
                     TriggerAutoSave();  // 触发自动保存
                   }));
  }
#else
  config_->Set(key, value);
  TriggerAutoSave();
  if (callback) {
    callback();
  }
#endif
}

void ConfigManager::SetAsync(const std::string& key,
                             int value,
                             std::function<void()> callback) {
#if ZENPLAY_CONFIG_USE_LOKI_DISPATCH
  if (callback) {
    loki::PostTaskAndReply(loki::IO, FROM_HERE,
                           loki::BindOnceClosure([this, key, value]() {
                             config_->Set(key, value);
                             TriggerAutoSave();  // 触发自动保存
                           }),
                           loki::BindOnceClosure(callback));
  } else {
    loki::PostTask(loki::IO, FROM_HERE,
                   loki::BindOnceClosure([this, key, value]() {
                     config_->Set(key, value);
                     TriggerAutoSave();  // 触发自动保存
                   }));
  }
#else
  config_->Set(key, value);
  TriggerAutoSave();
  if (callback) {
    callback();
  }
#endif
}

void ConfigManager::SetAsync(const std::string& key,
                             int64_t value,
                             std::function<void()> callback) {
#if ZENPLAY_CONFIG_USE_LOKI_DISPATCH
  if (callback) {
    loki::PostTaskAndReply(loki::IO, FROM_HERE,
                           loki::BindOnceClosure([this, key, value]() {
                             config_->Set(key, value);
                             TriggerAutoSave();  // 触发自动保存
                           }),
                           loki::BindOnceClosure(callback));
  } else {
    loki::PostTask(loki::IO, FROM_HERE,
                   loki::BindOnceClosure([this, key, value]() {
                     config_->Set(key, value);
                     TriggerAutoSave();  // 触发自动保存
                   }));
  }
#else
  config_->Set(key, value);
  TriggerAutoSave();
  if (callback) {
    callback();
  }
#endif
}

void ConfigManager::SetAsync(const std::string& key,
                             double value,
                             std::function<void()> callback) {
#if ZENPLAY_CONFIG_USE_LOKI_DISPATCH
  if (callback) {
    loki::PostTaskAndReply(loki::IO, FROM_HERE,
                           loki::BindOnceClosure([this, key, value]() {
                             config_->Set(key, value);
                             TriggerAutoSave();  // 触发自动保存
                           }),
                           loki::BindOnceClosure(callback));
  } else {
    loki::PostTask(loki::IO, FROM_HERE,
                   loki::BindOnceClosure([this, key, value]() {
                     config_->Set(key, value);
                     TriggerAutoSave();  // 触发自动保存
                   }));
  }
#else
  config_->Set(key, value);
  TriggerAutoSave();
  if (callback) {
    callback();
  }
#endif
}

void ConfigManager::SetAsync(const std::string& key,
                             const std::string& value,
                             std::function<void()> callback) {
#if ZENPLAY_CONFIG_USE_LOKI_DISPATCH
  if (callback) {
    loki::PostTaskAndReply(loki::IO, FROM_HERE,
                           loki::BindOnceClosure([this, key, value]() {
                             config_->Set(key, value);
                             TriggerAutoSave();  // 触发自动保存
                           }),
                           loki::BindOnceClosure(callback));
  } else {
    loki::PostTask(loki::IO, FROM_HERE,
                   loki::BindOnceClosure([this, key, value]() {
                     config_->Set(key, value);
                     TriggerAutoSave();  // 触发自动保存
                   }));
  }
#else
  config_->Set(key, value);
  TriggerAutoSave();
  if (callback) {
    callback();
  }
#endif
}

void ConfigManager::SetAsync(const std::string& key,
                             const std::vector<std::string>& value,
                             std::function<void()> callback) {
#if ZENPLAY_CONFIG_USE_LOKI_DISPATCH
  if (callback) {
    loki::PostTaskAndReply(loki::IO, FROM_HERE,
                           loki::BindOnceClosure([this, key, value]() {
                             config_->Set(key, value);
                             TriggerAutoSave();  // 触发自动保存
                           }),
                           loki::BindOnceClosure(callback));
  } else {
    loki::PostTask(loki::IO, FROM_HERE,
                   loki::BindOnceClosure([this, key, value]() {
                     config_->Set(key, value);
                     TriggerAutoSave();  // 触发自动保存
                   }));
  }
#else
  config_->Set(key, value);
  TriggerAutoSave();
  if (callback) {
    callback();
  }
#endif
}

// ==================== 配置监听 ====================

int ConfigManager::Watch(
    const std::string& key,
    std::function<void(const ConfigValue&, const ConfigValue&)> callback) {
#if ZENPLAY_CONFIG_USE_LOKI_DISPATCH
  return loki::Invoke<int>(loki::IO, FROM_HERE, [this, &key, &callback]() {
    return config_->Watch(key, callback);
  });
#else
  return config_->Watch(key, callback);
#endif
}

void ConfigManager::Unwatch(int watcher_id) {
#if ZENPLAY_CONFIG_USE_LOKI_DISPATCH
  loki::Invoke<void>(loki::IO, FROM_HERE,
                     [this, watcher_id]() { config_->Unwatch(watcher_id); });
#else
  config_->Unwatch(watcher_id);
#endif
}

// ==================== 配置验证 ====================

Result<void> ConfigManager::Validate(
    const std::string& key,
    std::function<bool(const ConfigValue&)> validator) const {
#if ZENPLAY_CONFIG_USE_LOKI_DISPATCH
  return loki::Invoke<Result<void>>(
      loki::IO, FROM_HERE,
      [this, &key, &validator]() { return config_->Validate(key, validator); });
#else
  return config_->Validate(key, validator);
#endif
}

}  // namespace zenplay
