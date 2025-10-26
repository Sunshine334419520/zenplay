#include "player/config/global_config.h"

#include <fstream>
#include <sstream>

namespace zenplay {

// ==================== ConfigValue 实现 ====================

bool ConfigValue::AsBool(bool default_value) const {
  return value_.is_boolean() ? value_.get<bool>() : default_value;
}

int ConfigValue::AsInt(int default_value) const {
  return value_.is_number_integer() ? value_.get<int>() : default_value;
}

int64_t ConfigValue::AsInt64(int64_t default_value) const {
  return value_.is_number_integer() ? value_.get<int64_t>() : default_value;
}

double ConfigValue::AsDouble(double default_value) const {
  return value_.is_number() ? value_.get<double>() : default_value;
}

std::string ConfigValue::AsString(const std::string& default_value) const {
  return value_.is_string() ? value_.get<std::string>() : default_value;
}

std::vector<std::string> ConfigValue::AsStringArray() const {
  std::vector<std::string> result;
  if (value_.is_array()) {
    for (const auto& item : value_) {
      if (item.is_string()) {
        result.push_back(item.get<std::string>());
      }
    }
  }
  return result;
}

// ==================== GlobalConfig 实现 ====================

GlobalConfig::GlobalConfig() {
  config_ = CreateDefaultConfig();
}

GlobalConfig* GlobalConfig::Instance() {
  static GlobalConfig instance;  // Meyer's Singleton（C++11 保证线程安全）
  return &instance;
}

nlohmann::json GlobalConfig::CreateDefaultConfig() const {
  return nlohmann::json{
      {"player",
       {{"audio",
         {{"buffer_size", 4096},
          {"sample_rate", 48000},
          {"channels", 2},
          {"volume", 1.0}}},
        {"video",
         {{"decoder_priority",
           nlohmann::json::array({"h264_cuvid", "h264_qsv", "h264"})},
          {"max_width", 3840},
          {"max_height", 2160}}},
        {"sync", {{"method", "audio"}, {"correction_threshold_ms", 100}}}}},
      {"render",
       {{"use_hardware_acceleration", true},
        {"backend_priority",
         nlohmann::json::array({"d3d11", "opengl", "software"})},
        {"vsync", true},
        {"max_fps", 60},
        {"hardware",
         {{"allow_d3d11va", true},
          {"allow_dxva2", true},
          {"allow_fallback", true}}}}},
      {"log",
       {{"level", "info"},
        {"outputs",
         nlohmann::json::array(
             {{{"type", "console"}, {"enabled", true}, {"color", true}},
              {{"type", "file"},
               {"enabled", true},
               {"path", "logs/zenplay.log"},
               {"max_size_mb", 100},
               {"max_files", 5},
               {"rotation", "daily"}}})},
        {"module_levels",
         {{"player", "info"},
          {"demuxer", "info"},
          {"decoder", "info"},
          {"renderer", "info"}}}}},
      {"statistics",
       {{"enabled", true},
        {"report_interval_ms", 1000},
        {"metrics", nlohmann::json::array({"fps", "bitrate", "dropped_frames",
                                           "audio_video_sync_offset"})},
        {"outputs",
         nlohmann::json::array({{{"type", "console"}, {"enabled", true}},
                                {{"type", "file"},
                                 {"enabled", false},
                                 {"path", "logs/statistics.csv"}}})}}},
      {"network",
       {{"timeout_ms", 5000},
        {"buffer_size_kb", 1024},
        {"user_agent", "ZenPlay/1.0"},
        {"proxy",
         {{"enabled", false},
          {"type", "http"},
          {"host", "127.0.0.1"},
          {"port", 7890}}}}},
      {"cache",
       {{"enabled", true},
        {"max_size_mb", 500},
        {"directory", "cache/zenplay"}}}};
}

Result<void> GlobalConfig::Load(const std::string& config_path) {
#if ZENPLAY_CONFIG_USE_LOCK

  std::unique_lock lock(mutex_);

#endif

  config_path_ = config_path;

  std::ifstream file(config_path);
  if (!file.is_open()) {
    config_ = CreateDefaultConfig();
    return Result<void>::Ok();
  }

  std::string content((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
  file.close();

  try {
    config_ = nlohmann::json::parse(content);
    return Result<void>::Ok();
  } catch (const nlohmann::json::parse_error& e) {
    return Result<void>::Err(ErrorCode::kConfigError,
                             std::string("JSON parse error: ") + e.what());
  }
}

Result<void> GlobalConfig::Save(const std::string& config_path) {
#if ZENPLAY_CONFIG_USE_LOCK

  std::shared_lock lock(mutex_);

#endif

  std::string path = config_path.empty() ? config_path_ : config_path;

  std::ofstream file(path);
  if (!file.is_open()) {
    return Result<void>::Err(ErrorCode::kFileError,
                             "Failed to open file for writing: " + path);
  }

  file << config_.dump(4);
  file.close();

  return Result<void>::Ok();
}

Result<void> GlobalConfig::Reload() {
  return Load(config_path_);
}

// ==================== 点号路径解析 ====================

const nlohmann::json* GlobalConfig::GetValuePtr(const std::string& key) const {
  std::vector<std::string> parts;
  std::stringstream ss(key);
  std::string part;
  while (std::getline(ss, part, '.')) {
    parts.push_back(part);
  }

  const nlohmann::json* current = &config_;
  for (const auto& p : parts) {
    if (current->contains(p)) {
      current = &(*current)[p];
    } else {
      return nullptr;
    }
  }

  return current;
}

nlohmann::json* GlobalConfig::GetValuePtr(const std::string& key) {
  std::vector<std::string> parts;
  std::stringstream ss(key);
  std::string part;
  while (std::getline(ss, part, '.')) {
    parts.push_back(part);
  }

  nlohmann::json* current = &config_;
  for (const auto& p : parts) {
    if (!current->contains(p)) {
      (*current)[p] = nlohmann::json::object();
    }
    current = &(*current)[p];
  }

  return current;
}

// ==================== Get 方法 ====================

bool GlobalConfig::GetBool(const std::string& key, bool default_value) const {
#if ZENPLAY_CONFIG_USE_LOCK

  std::shared_lock lock(mutex_);

#endif
  const auto* value = GetValuePtr(key);
  return (value && value->is_boolean()) ? value->get<bool>() : default_value;
}

int GlobalConfig::GetInt(const std::string& key, int default_value) const {
#if ZENPLAY_CONFIG_USE_LOCK

  std::shared_lock lock(mutex_);

#endif
  const auto* value = GetValuePtr(key);
  return (value && value->is_number_integer()) ? value->get<int>()
                                               : default_value;
}

int64_t GlobalConfig::GetInt64(const std::string& key,
                               int64_t default_value) const {
#if ZENPLAY_CONFIG_USE_LOCK

  std::shared_lock lock(mutex_);

#endif
  const auto* value = GetValuePtr(key);
  return (value && value->is_number_integer()) ? value->get<int64_t>()
                                               : default_value;
}

double GlobalConfig::GetDouble(const std::string& key,
                               double default_value) const {
#if ZENPLAY_CONFIG_USE_LOCK

  std::shared_lock lock(mutex_);

#endif
  const auto* value = GetValuePtr(key);
  return (value && value->is_number()) ? value->get<double>() : default_value;
}

std::string GlobalConfig::GetString(const std::string& key,
                                    const std::string& default_value) const {
#if ZENPLAY_CONFIG_USE_LOCK

  std::shared_lock lock(mutex_);

#endif
  const auto* value = GetValuePtr(key);
  return (value && value->is_string()) ? value->get<std::string>()
                                       : default_value;
}

std::vector<std::string> GlobalConfig::GetStringArray(
    const std::string& key) const {
#if ZENPLAY_CONFIG_USE_LOCK

  std::shared_lock lock(mutex_);

#endif
  const auto* value = GetValuePtr(key);

  std::vector<std::string> result;
  if (value && value->is_array()) {
    for (const auto& item : *value) {
      if (item.is_string()) {
        result.push_back(item.get<std::string>());
      }
    }
  }
  return result;
}

std::optional<ConfigValue> GlobalConfig::Get(const std::string& key) const {
#if ZENPLAY_CONFIG_USE_LOCK

  std::shared_lock lock(mutex_);

#endif
  const auto* value = GetValuePtr(key);
  if (value) {
    return ConfigValue(*value);
  }
  return std::nullopt;
}

bool GlobalConfig::Has(const std::string& key) const {
#if ZENPLAY_CONFIG_USE_LOCK

  std::shared_lock lock(mutex_);

#endif
  return GetValuePtr(key) != nullptr;
}

// ==================== Set 方法 ====================

void GlobalConfig::Set(const std::string& key, bool value) {
#if ZENPLAY_CONFIG_USE_LOCK

  std::unique_lock lock(mutex_);

#endif
  auto* ptr = GetValuePtr(key);
  nlohmann::json old_value = *ptr;
  *ptr = value;
  NotifyWatchers(key, old_value, value);
}

void GlobalConfig::Set(const std::string& key, int value) {
#if ZENPLAY_CONFIG_USE_LOCK

  std::unique_lock lock(mutex_);

#endif
  auto* ptr = GetValuePtr(key);
  nlohmann::json old_value = *ptr;
  *ptr = value;
  NotifyWatchers(key, old_value, value);
}

void GlobalConfig::Set(const std::string& key, int64_t value) {
#if ZENPLAY_CONFIG_USE_LOCK

  std::unique_lock lock(mutex_);

#endif
  auto* ptr = GetValuePtr(key);
  nlohmann::json old_value = *ptr;
  *ptr = value;
  NotifyWatchers(key, old_value, value);
}

void GlobalConfig::Set(const std::string& key, double value) {
#if ZENPLAY_CONFIG_USE_LOCK

  std::unique_lock lock(mutex_);

#endif
  auto* ptr = GetValuePtr(key);
  nlohmann::json old_value = *ptr;
  *ptr = value;
  NotifyWatchers(key, old_value, value);
}

void GlobalConfig::Set(const std::string& key, const std::string& value) {
#if ZENPLAY_CONFIG_USE_LOCK

  std::unique_lock lock(mutex_);

#endif
  auto* ptr = GetValuePtr(key);
  nlohmann::json old_value = *ptr;
  *ptr = value;
  NotifyWatchers(key, old_value, value);
}

void GlobalConfig::Set(const std::string& key,
                       const std::vector<std::string>& value) {
#if ZENPLAY_CONFIG_USE_LOCK

  std::unique_lock lock(mutex_);

#endif
  auto* ptr = GetValuePtr(key);
  nlohmann::json old_value = *ptr;
  *ptr = value;
  NotifyWatchers(key, old_value, value);
}

void GlobalConfig::Set(const std::string& key, const nlohmann::json& value) {
#if ZENPLAY_CONFIG_USE_LOCK

  std::unique_lock lock(mutex_);

#endif
  auto* ptr = GetValuePtr(key);
  nlohmann::json old_value = *ptr;
  *ptr = value;
  NotifyWatchers(key, old_value, value);
}

// ==================== 监听器 ====================

int GlobalConfig::Watch(const std::string& key, ConfigChangeCallback callback) {
#if ZENPLAY_CONFIG_USE_LOCK

  std::unique_lock lock(mutex_);

#endif
  int id = next_watcher_id_++;
  watchers_.push_back({id, key, std::move(callback)});
  return id;
}

void GlobalConfig::Unwatch(int watch_id) {
#if ZENPLAY_CONFIG_USE_LOCK

  std::unique_lock lock(mutex_);

#endif
  watchers_.erase(
      std::remove_if(watchers_.begin(), watchers_.end(),
                     [watch_id](const Watcher& w) { return w.id == watch_id; }),
      watchers_.end());
}

void GlobalConfig::NotifyWatchers(const std::string& key,
                                  const nlohmann::json& old_value,
                                  const nlohmann::json& new_value) {
  for (const auto& watcher : watchers_) {
    if (watcher.key == key) {
      try {
        watcher.callback(ConfigValue(old_value), ConfigValue(new_value));
      } catch (const std::exception&) {
        // 忽略回调中的异常
      }
    }
  }
}

// ==================== 验证 ====================

Result<void> GlobalConfig::Validate(
    const std::string& key,
    std::function<bool(const ConfigValue&)> validator) const {
  auto value = Get(key);
  if (!value) {
    return Result<void>::Err(ErrorCode::kConfigError,
                             "Config key not found: " + key);
  }

  if (!validator(*value)) {
    return Result<void>::Err(ErrorCode::kConfigError,
                             "Config validation failed for key: " + key);
  }

  return Result<void>::Ok();
}

// ==================== 其他 ====================

void GlobalConfig::ResetToDefaults() {
#if ZENPLAY_CONFIG_USE_LOCK

  std::unique_lock lock(mutex_);

#endif
  config_ = CreateDefaultConfig();
}

std::string GlobalConfig::Dump(int indent) const {
#if ZENPLAY_CONFIG_USE_LOCK

  std::shared_lock lock(mutex_);

#endif
  return config_.dump(indent);
}

}  // namespace zenplay
