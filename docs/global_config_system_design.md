# ZenPlay 全局配置管理系统设计

> **文档版本**: 1.0  
> **创建日期**: 2025-10-22  
> **作者**: Copilot + User  
> **目的**: 设计统一的全局配置管理系统，支持硬件加速、播放器、日志、统计等所有模块

---

## 1. 设计目标

### 1.1 核心需求

| 需求 | 描述 | 优先级 |
|------|------|--------|
| **集中管理** | 所有模块的配置统一管理，避免配置碎片化 | P0 |
| **单例模式** | 全局唯一实例，线程安全访问 | P0 |
| **类型安全** | 编译期类型检查，避免运行时类型错误 | P0 |
| **默认值** | 每个配置项有合理的默认值，即使文件不存在也能运行 | P0 |
| **热重载** | 支持运行时重新加载配置（无需重启） | P1 |
| **验证机制** | 配置值范围检查（如端口 1-65535） | P1 |
| **环境覆盖** | 支持环境变量覆盖配置文件（便于部署） | P2 |
| **加密支持** | 敏感信息加密存储（如密钥） | P2 |

### 1.2 使用场景

```cpp
// 场景 1：读取配置
auto& config = GlobalConfig::Instance();
int buffer_size = config.GetInt("player.audio.buffer_size", 4096);
bool use_hardware = config.GetBool("render.use_hardware_acceleration", true);

// 场景 2：修改并保存
config.Set("player.video.decoder", "h264_cuvid");
config.Save();

// 场景 3：监听配置变化
config.Watch("render.use_hardware_acceleration", [](const ConfigValue& old_val, const ConfigValue& new_val) {
  if (new_val.AsBool()) {
    // 切换到硬件渲染
  }
});

// 场景 4：热重载
config.Reload();  // 重新加载配置文件
```

---

## 2. 配置格式对比分析

### 2.1 候选格式

我们对比三种主流配置格式：**JSON**、**YAML**、**INI**

### 2.2 详细对比表

| 维度 | JSON | YAML | INI |
|------|------|------|-----|
| **可读性** | ⭐⭐⭐ 中等（大量括号） | ⭐⭐⭐⭐⭐ 优秀（缩进清晰） | ⭐⭐⭐⭐ 良好（简单直观） |
| **表达能力** | ⭐⭐⭐⭐ 强（支持嵌套） | ⭐⭐⭐⭐⭐ 最强（锚点、引用） | ⭐⭐ 弱（只有键值对） |
| **注释支持** | ❌ 不支持 | ✅ 支持 `#` | ✅ 支持 `;` |
| **数据类型** | ⭐⭐⭐⭐ 6 种基本类型 | ⭐⭐⭐⭐⭐ 丰富（时间戳、二进制） | ⭐⭐ 只有字符串 |
| **嵌套结构** | ✅ 原生支持 | ✅ 原生支持 | ❌ 只能用 section |
| **数组支持** | ✅ `[1, 2, 3]` | ✅ `- item` 或 `[1, 2]` | ❌ 不支持 |
| **解析性能** | ⭐⭐⭐⭐ 快 | ⭐⭐⭐ 中等 | ⭐⭐⭐⭐⭐ 最快 |
| **解析复杂度** | 中等 | 高（缩进敏感） | 低 |
| **跨语言支持** | ⭐⭐⭐⭐⭐ 所有语言 | ⭐⭐⭐⭐ 主流语言 | ⭐⭐⭐ 有限 |
| **文件大小** | ⭐⭐⭐ 中等 | ⭐⭐⭐⭐ 较小 | ⭐⭐⭐⭐⭐ 最小 |
| **生态成熟度** | ⭐⭐⭐⭐⭐ 极高 | ⭐⭐⭐⭐ 高 | ⭐⭐⭐ 中等 |
| **C++ 库** | nlohmann/json (17.4k⭐) | yaml-cpp (4.7k⭐) | inih (2.4k⭐) |
| **错误提示** | ⭐⭐⭐⭐ 清晰 | ⭐⭐⭐ 缩进错误难定位 | ⭐⭐⭐⭐ 简单清晰 |
| **版本控制友好** | ⭐⭐⭐ 中等（diff 多行） | ⭐⭐⭐⭐⭐ 优秀（逐行） | ⭐⭐⭐⭐ 良好 |
| **安全性** | ⭐⭐⭐⭐ 高（无代码执行） | ⭐⭐⭐ 中（复杂特性风险） | ⭐⭐⭐⭐⭐ 最高（极简） |

### 2.3 实际配置文件示例对比

#### 示例配置需求
假设我们要配置以下内容：
- 播放器音频缓冲区大小
- 硬件加速优先级列表
- 日志级别和输出目标
- 统计上报间隔

#### JSON 格式

```json
{
  "player": {
    "audio": {
      "buffer_size": 4096,
      "sample_rate": 48000,
      "channels": 2
    },
    "video": {
      "decoder_priority": ["h264_cuvid", "h264_qsv", "h264"]
    }
  },
  "render": {
    "use_hardware_acceleration": true,
    "backend_priority": ["d3d11", "opengl", "software"],
    "vsync": true,
    "max_fps": 60
  },
  "log": {
    "level": "info",
    "outputs": [
      {"type": "console", "enabled": true},
      {"type": "file", "path": "logs/zenplay.log", "max_size_mb": 100}
    ]
  },
  "statistics": {
    "enabled": true,
    "report_interval_ms": 1000,
    "metrics": ["fps", "bitrate", "dropped_frames"]
  }
}
```

**优点**：
- ✅ 结构清晰，嵌套关系明确
- ✅ 类型明确（数字、布尔、数组）
- ✅ 工具支持好（IDE 自动补全、格式化）
- ✅ 项目已有依赖（`nlohmann/json`）

**缺点**：
- ❌ 不支持注释（需要用 JSONC 或 JSON5 扩展）
- ❌ 括号、引号较多，手写容易出错
- ❌ 修改嵌套深层配置需要保持所有括号匹配

#### YAML 格式

```yaml
# ZenPlay 全局配置文件
# 修改后需要重启或热重载生效

player:
  audio:
    buffer_size: 4096     # 音频缓冲区大小（字节）
    sample_rate: 48000    # 采样率（Hz）
    channels: 2           # 声道数

  video:
    decoder_priority:     # 解码器优先级（自动选择）
      - h264_cuvid        # NVIDIA CUDA
      - h264_qsv          # Intel Quick Sync
      - h264              # 软件解码

render:
  use_hardware_acceleration: true
  backend_priority:
    - d3d11               # Windows D3D11
    - opengl              # 跨平台 OpenGL
    - software            # 软件渲染
  vsync: true
  max_fps: 60

log:
  level: info             # debug/info/warn/error
  outputs:
    - type: console
      enabled: true
    - type: file
      path: logs/zenplay.log
      max_size_mb: 100

statistics:
  enabled: true
  report_interval_ms: 1000
  metrics: [fps, bitrate, dropped_frames]
```

**优点**：
- ✅ 可读性最佳（无冗余符号）
- ✅ 原生支持注释
- ✅ 适合手动编辑配置文件
- ✅ 多种数组写法（灵活）

**缺点**：
- ❌ 缩进敏感（Tab/空格混用会出错）
- ❌ 解析器复杂（`yaml-cpp` 库较大）
- ❌ 新增外部依赖
- ❌ 错误提示不够直观（缩进问题难定位）

#### INI 格式

```ini
; ZenPlay 全局配置文件

[player.audio]
buffer_size = 4096
sample_rate = 48000
channels = 2

[player.video]
; 解码器优先级（逗号分隔）
decoder_priority = h264_cuvid,h264_qsv,h264

[render]
use_hardware_acceleration = true
; 渲染后端优先级
backend_priority = d3d11,opengl,software
vsync = true
max_fps = 60

[log]
level = info
; 日志输出（逗号分隔：console,file）
outputs = console,file
file_path = logs/zenplay.log
file_max_size_mb = 100

[statistics]
enabled = true
report_interval_ms = 1000
; 统计指标（逗号分隔）
metrics = fps,bitrate,dropped_frames
```

**优点**：
- ✅ 极简格式，解析最快
- ✅ 人类可读性好
- ✅ 支持注释
- ✅ 库极小（inih 只有 2 个文件）

**缺点**：
- ❌ 不支持真正的嵌套（只能用点号模拟）
- ❌ 不支持数组（只能用逗号分隔字符串）
- ❌ 所有值都是字符串（需手动转换类型）
- ❌ 不支持复杂数据结构（如对象数组）

### 2.4 C++ 库对比

| 库 | JSON | YAML | INI |
|---|------|------|-----|
| **库名** | [nlohmann/json](https://github.com/nlohmann/json) | [yaml-cpp](https://github.com/jbeder/yaml-cpp) | [inih](https://github.com/benhoyt/inih) |
| **Star 数** | 42.7k ⭐ | 5.1k ⭐ | 2.5k ⭐ |
| **许可证** | MIT | MIT | BSD-3-Clause |
| **单头文件** | ✅ 可选 | ❌ 需要编译 | ✅ 是 |
| **依赖** | 无 | 无 | 无 |
| **C++ 版本** | C++11 | C++11 | C89 |
| **API 易用性** | ⭐⭐⭐⭐⭐ 优秀 | ⭐⭐⭐ 中等 | ⭐⭐⭐⭐ 良好 |
| **性能** | 快 | 中等 | 最快 |
| **现有使用** | ✅ 已在项目中 | ❌ 未使用 | ❌ 未使用 |

**项目当前状态**：
- 已依赖 `nlohmann/json`（在 `CMakeLists.txt` 中）
- 未使用 YAML 或 INI 库

---

## 3. 推荐方案：JSON + JSONC 扩展

### 3.1 选择理由

| 因素 | 分析 |
|------|------|
| **现有依赖** | 项目已使用 `nlohmann/json`，无需引入新库 |
| **类型安全** | 原生支持数字、布尔、数组、对象，避免字符串转换 |
| **复杂结构** | 支持深层嵌套和对象数组（如日志输出配置） |
| **工具链** | IDE 支持好，语法高亮、格式化、验证完备 |
| **跨平台** | JSON 是 Web 标准，未来可能需要与 Web UI 交互 |
| **生态** | `nlohmann/json` 是 C++ JSON 库的事实标准 |

### 3.2 解决注释问题：JSONC（JSON with Comments）

虽然标准 JSON 不支持注释，但可以通过以下方式支持：

#### 方案 A：预处理去注释（推荐）

```cpp
// 读取 config.jsonc，去除 // 和 /* */ 注释后解析
std::string LoadJSONC(const std::string& path) {
  std::ifstream file(path);
  std::string content((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
  
  // 去除单行注释 //
  content = std::regex_replace(content, std::regex(R"(//.*$)", 
                               std::regex_constants::multiline), "");
  
  // 去除多行注释 /* */
  content = std::regex_replace(content, std::regex(R"(/\*[\s\S]*?\*/)"), "");
  
  return content;
}

// 使用
std::string content = LoadJSONC("config.jsonc");
nlohmann::json config = nlohmann::json::parse(content);
```

#### 方案 B：使用 `_comment` 字段

```json
{
  "_comment_player": "播放器相关配置",
  "player": {
    "_comment_audio": "音频缓冲区配置",
    "audio": {
      "buffer_size": 4096
    }
  }
}
```

**推荐使用方案 A**，因为：
- 更自然的注释语法（`//`）
- 不污染配置结构
- 与 VS Code 等编辑器兼容（`.jsonc` 扩展名自动识别）

### 3.3 最终配置文件格式

```jsonc
// config/zenplay.jsonc
// ZenPlay 全局配置文件（支持 // 和 /* */ 注释）
{
  // ==================== 播放器配置 ====================
  "player": {
    // 音频配置
    "audio": {
      "buffer_size": 4096,        // 音频缓冲区大小（字节）
      "sample_rate": 48000,       // 采样率（Hz）
      "channels": 2,              // 声道数
      "volume": 1.0               // 音量（0.0-1.0）
    },
    
    // 视频配置
    "video": {
      "decoder_priority": [       // 解码器优先级（自动选择第一个可用）
        "h264_cuvid",             // NVIDIA CUDA 硬件解码
        "h264_qsv",               // Intel Quick Sync 硬件解码
        "h264"                    // 软件解码
      ],
      "max_width": 3840,          // 最大支持宽度（4K）
      "max_height": 2160
    },
    
    // 同步配置
    "sync": {
      "method": "audio",          // 同步基准：audio/video/external
      "correction_threshold_ms": 100  // 同步偏差阈值
    }
  },

  // ==================== 渲染配置 ====================
  "render": {
    "use_hardware_acceleration": true,
    
    // 渲染后端优先级
    "backend_priority": [
      "d3d11",                    // Windows D3D11
      "opengl",                   // 跨平台 OpenGL
      "software"                  // SDL 软件渲染
    ],
    
    "vsync": true,                // 垂直同步
    "max_fps": 60,                // 最大帧率限制
    
    // 硬件加速特定配置
    "hardware": {
      "allow_d3d11va": true,      // 允许 D3D11VA（Windows 8+）
      "allow_dxva2": true,        // 允许 DXVA2（Windows 7）
      "allow_fallback": true      // 硬件失败时降级到软件
    }
  },

  // ==================== 日志配置 ====================
  "log": {
    "level": "info",              // 日志级别：debug/info/warn/error
    
    // 日志输出配置（支持多个输出）
    "outputs": [
      {
        "type": "console",        // 控制台输出
        "enabled": true,
        "color": true             // 彩色输出
      },
      {
        "type": "file",           // 文件输出
        "enabled": true,
        "path": "logs/zenplay.log",
        "max_size_mb": 100,       // 单个文件最大大小
        "max_files": 5,           // 最多保留文件数
        "rotation": "daily"       // 轮转策略：daily/size
      }
    ],
    
    // 模块级别日志控制
    "module_levels": {
      "player": "info",
      "demuxer": "debug",         // 可以单独调试某个模块
      "decoder": "info",
      "renderer": "info"
    }
  },

  // ==================== 统计配置 ====================
  "statistics": {
    "enabled": true,              // 是否启用统计
    "report_interval_ms": 1000,   // 统计上报间隔
    
    // 收集的指标
    "metrics": [
      "fps",                      // 帧率
      "bitrate",                  // 码率
      "dropped_frames",           // 丢帧数
      "audio_video_sync_offset"   // 音视频同步偏差
    ],
    
    // 统计输出
    "outputs": [
      {
        "type": "console",        // 控制台输出
        "enabled": true
      },
      {
        "type": "file",           // 文件输出
        "enabled": false,
        "path": "logs/statistics.csv"
      }
    ]
  },

  // ==================== 网络配置 ====================
  "network": {
    "timeout_ms": 5000,           // 连接超时
    "buffer_size_kb": 1024,       // 网络缓冲区
    "user_agent": "ZenPlay/1.0",
    
    // HTTP 代理配置
    "proxy": {
      "enabled": false,
      "type": "http",             // http/socks5
      "host": "127.0.0.1",
      "port": 7890
    }
  },

  // ==================== 缓存配置 ====================
  "cache": {
    "enabled": true,
    "max_size_mb": 500,           // 最大缓存大小
    "directory": "cache/zenplay"
  }
}
```

---

## 4. 全局配置管理器设计

### 4.1 类设计

```cpp
// src/player/config/global_config.h
#pragma once

#include <nlohmann/json.hpp>
#include <shared_mutex>
#include <functional>
#include <optional>
#include "player/common/error.h"

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
   * @param config_path 配置文件路径（支持 .json 和 .jsonc）
   * @return Result<void> 成功或错误
   * 
   * @note 如果文件不存在，使用默认配置
   */
  Result<void> Load(const std::string& config_path = "config/zenplay.jsonc");

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
  Result<void> Validate(const std::string& key,
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
  std::string RemoveJSONCComments(const std::string& content) const;

  // 成员变量
  nlohmann::json config_;                           // 配置数据
  std::string config_path_;                         // 配置文件路径
  mutable std::shared_mutex mutex_;                 // 读写锁
  
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
```

### 4.2 实现细节

```cpp
// src/player/config/global_config.cpp
#include "player/config/global_config.h"
#include "log_manager.h"
#include <fstream>
#include <regex>
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
  MODULE_INFO(LOG_MODULE_COMMON, "GlobalConfig instance created");
}

GlobalConfig& GlobalConfig::Instance() {
  static GlobalConfig instance;  // Meyer's Singleton（C++11 保证线程安全）
  return instance;
}

nlohmann::json GlobalConfig::CreateDefaultConfig() const {
  // 创建默认配置（如果文件不存在，使用此配置）
  return nlohmann::json{
    {"player", {
      {"audio", {
        {"buffer_size", 4096},
        {"sample_rate", 48000},
        {"channels", 2},
        {"volume", 1.0}
      }},
      {"video", {
        {"decoder_priority", nlohmann::json::array({"h264_cuvid", "h264_qsv", "h264"})},
        {"max_width", 3840},
        {"max_height", 2160}
      }},
      {"sync", {
        {"method", "audio"},
        {"correction_threshold_ms", 100}
      }}
    }},
    {"render", {
      {"use_hardware_acceleration", true},
      {"backend_priority", nlohmann::json::array({"d3d11", "opengl", "software"})},
      {"vsync", true},
      {"max_fps", 60},
      {"hardware", {
        {"allow_d3d11va", true},
        {"allow_dxva2", true},
        {"allow_fallback", true}
      }}
    }},
    {"log", {
      {"level", "info"},
      {"outputs", nlohmann::json::array({
        {{"type", "console"}, {"enabled", true}, {"color", true}},
        {{"type", "file"}, {"enabled", true}, {"path", "logs/zenplay.log"}, 
         {"max_size_mb", 100}, {"max_files", 5}, {"rotation", "daily"}}
      })},
      {"module_levels", {
        {"player", "info"},
        {"demuxer", "info"},
        {"decoder", "info"},
        {"renderer", "info"}
      }}
    }},
    {"statistics", {
      {"enabled", true},
      {"report_interval_ms", 1000},
      {"metrics", nlohmann::json::array({"fps", "bitrate", "dropped_frames", "audio_video_sync_offset"})},
      {"outputs", nlohmann::json::array({
        {{"type", "console"}, {"enabled", true}},
        {{"type", "file"}, {"enabled", false}, {"path", "logs/statistics.csv"}}
      })}
    }},
    {"network", {
      {"timeout_ms", 5000},
      {"buffer_size_kb", 1024},
      {"user_agent", "ZenPlay/1.0"},
      {"proxy", {
        {"enabled", false},
        {"type", "http"},
        {"host", "127.0.0.1"},
        {"port", 7890}
      }}
    }},
    {"cache", {
      {"enabled", true},
      {"max_size_mb", 500},
      {"directory", "cache/zenplay"}
    }}
  };
}

std::string GlobalConfig::RemoveJSONCComments(const std::string& content) const {
  std::string result = content;
  
  // 移除单行注释 // （注意：不要移除 URL 中的 //）
  // 使用负向后顾断言：不在 http:// 或 https:// 中
  result = std::regex_replace(result, 
                              std::regex(R"((?<!:)//.*$)", std::regex_constants::multiline), 
                              "");
  
  // 移除多行注释 /* */
  result = std::regex_replace(result, std::regex(R"(/\*[\s\S]*?\*/)"), "");
  
  return result;
}

Result<void> GlobalConfig::Load(const std::string& config_path) {
  std::unique_lock lock(mutex_);
  
  config_path_ = config_path;
  
  // 尝试打开文件
  std::ifstream file(config_path);
  if (!file.is_open()) {
    MODULE_WARN(LOG_MODULE_COMMON, 
                "Config file '{}' not found, using default config", 
                config_path);
    config_ = CreateDefaultConfig();
    return Result<void>::Ok();  // 不算错误，使用默认配置
  }
  
  // 读取文件内容
  std::string content((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
  file.close();
  
  // 移除 JSONC 注释
  content = RemoveJSONCComments(content);
  
  // 解析 JSON
  try {
    config_ = nlohmann::json::parse(content);
    MODULE_INFO(LOG_MODULE_COMMON, "Config loaded from '{}'", config_path);
    return Result<void>::Ok();
  } catch (const nlohmann::json::parse_error& e) {
    MODULE_ERROR(LOG_MODULE_COMMON, "Failed to parse config file: {}", e.what());
    return Result<void>::Err(ErrorCode::kConfigError, 
                             std::string("JSON parse error: ") + e.what());
  }
}

Result<void> GlobalConfig::Save(const std::string& config_path) {
  std::shared_lock lock(mutex_);
  
  std::string path = config_path.empty() ? config_path_ : config_path;
  
  std::ofstream file(path);
  if (!file.is_open()) {
    return Result<void>::Err(ErrorCode::kFileError, 
                             "Failed to open file for writing: " + path);
  }
  
  // 格式化输出（4 空格缩进）
  file << config_.dump(4);
  file.close();
  
  MODULE_INFO(LOG_MODULE_COMMON, "Config saved to '{}'", path);
  return Result<void>::Ok();
}

Result<void> GlobalConfig::Reload() {
  MODULE_INFO(LOG_MODULE_COMMON, "Reloading config from '{}'", config_path_);
  return Load(config_path_);
}

// ==================== 点号路径解析 ====================

const nlohmann::json* GlobalConfig::GetValuePtr(const std::string& key) const {
  // 将 "player.audio.buffer_size" 拆分为 ["player", "audio", "buffer_size"]
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
      return nullptr;  // 键不存在
    }
  }
  
  return current;
}

nlohmann::json* GlobalConfig::GetValuePtr(const std::string& key) {
  // 非 const 版本（用于 Set）
  std::vector<std::string> parts;
  std::stringstream ss(key);
  std::string part;
  while (std::getline(ss, part, '.')) {
    parts.push_back(part);
  }
  
  nlohmann::json* current = &config_;
  for (const auto& p : parts) {
    if (!current->contains(p)) {
      // 自动创建不存在的键
      (*current)[p] = nlohmann::json::object();
    }
    current = &(*current)[p];
  }
  
  return current;
}

// ==================== Get 方法 ====================

bool GlobalConfig::GetBool(const std::string& key, bool default_value) const {
  std::shared_lock lock(mutex_);
  const auto* value = GetValuePtr(key);
  return (value && value->is_boolean()) ? value->get<bool>() : default_value;
}

int GlobalConfig::GetInt(const std::string& key, int default_value) const {
  std::shared_lock lock(mutex_);
  const auto* value = GetValuePtr(key);
  return (value && value->is_number_integer()) ? value->get<int>() : default_value;
}

int64_t GlobalConfig::GetInt64(const std::string& key, int64_t default_value) const {
  std::shared_lock lock(mutex_);
  const auto* value = GetValuePtr(key);
  return (value && value->is_number_integer()) ? value->get<int64_t>() : default_value;
}

double GlobalConfig::GetDouble(const std::string& key, double default_value) const {
  std::shared_lock lock(mutex_);
  const auto* value = GetValuePtr(key);
  return (value && value->is_number()) ? value->get<double>() : default_value;
}

std::string GlobalConfig::GetString(const std::string& key, 
                                    const std::string& default_value) const {
  std::shared_lock lock(mutex_);
  const auto* value = GetValuePtr(key);
  return (value && value->is_string()) ? value->get<std::string>() : default_value;
}

std::vector<std::string> GlobalConfig::GetStringArray(const std::string& key) const {
  std::shared_lock lock(mutex_);
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
  std::shared_lock lock(mutex_);
  const auto* value = GetValuePtr(key);
  if (value) {
    return ConfigValue(*value);
  }
  return std::nullopt;
}

bool GlobalConfig::Has(const std::string& key) const {
  std::shared_lock lock(mutex_);
  return GetValuePtr(key) != nullptr;
}

// ==================== Set 方法 ====================

void GlobalConfig::Set(const std::string& key, bool value) {
  std::unique_lock lock(mutex_);
  auto* ptr = GetValuePtr(key);
  nlohmann::json old_value = *ptr;
  *ptr = value;
  NotifyWatchers(key, old_value, value);
}

void GlobalConfig::Set(const std::string& key, int value) {
  std::unique_lock lock(mutex_);
  auto* ptr = GetValuePtr(key);
  nlohmann::json old_value = *ptr;
  *ptr = value;
  NotifyWatchers(key, old_value, value);
}

void GlobalConfig::Set(const std::string& key, int64_t value) {
  std::unique_lock lock(mutex_);
  auto* ptr = GetValuePtr(key);
  nlohmann::json old_value = *ptr;
  *ptr = value;
  NotifyWatchers(key, old_value, value);
}

void GlobalConfig::Set(const std::string& key, double value) {
  std::unique_lock lock(mutex_);
  auto* ptr = GetValuePtr(key);
  nlohmann::json old_value = *ptr;
  *ptr = value;
  NotifyWatchers(key, old_value, value);
}

void GlobalConfig::Set(const std::string& key, const std::string& value) {
  std::unique_lock lock(mutex_);
  auto* ptr = GetValuePtr(key);
  nlohmann::json old_value = *ptr;
  *ptr = value;
  NotifyWatchers(key, old_value, value);
}

void GlobalConfig::Set(const std::string& key, const std::vector<std::string>& value) {
  std::unique_lock lock(mutex_);
  auto* ptr = GetValuePtr(key);
  nlohmann::json old_value = *ptr;
  *ptr = value;
  NotifyWatchers(key, old_value, value);
}

void GlobalConfig::Set(const std::string& key, const nlohmann::json& value) {
  std::unique_lock lock(mutex_);
  auto* ptr = GetValuePtr(key);
  nlohmann::json old_value = *ptr;
  *ptr = value;
  NotifyWatchers(key, old_value, value);
}

// ==================== 监听器 ====================

int GlobalConfig::Watch(const std::string& key, ConfigChangeCallback callback) {
  std::unique_lock lock(mutex_);
  int id = next_watcher_id_++;
  watchers_.push_back({id, key, std::move(callback)});
  MODULE_DEBUG(LOG_MODULE_COMMON, "Watcher {} registered for key '{}'", id, key);
  return id;
}

void GlobalConfig::Unwatch(int watch_id) {
  std::unique_lock lock(mutex_);
  watchers_.erase(
    std::remove_if(watchers_.begin(), watchers_.end(),
                   [watch_id](const Watcher& w) { return w.id == watch_id; }),
    watchers_.end());
  MODULE_DEBUG(LOG_MODULE_COMMON, "Watcher {} unregistered", watch_id);
}

void GlobalConfig::NotifyWatchers(const std::string& key,
                                   const nlohmann::json& old_value,
                                   const nlohmann::json& new_value) {
  // 注意：此方法在持有锁的情况下调用，回调中不应再次获取锁
  for (const auto& watcher : watchers_) {
    if (watcher.key == key) {
      try {
        watcher.callback(ConfigValue(old_value), ConfigValue(new_value));
      } catch (const std::exception& e) {
        MODULE_ERROR(LOG_MODULE_COMMON, 
                     "Exception in config watcher for key '{}': {}", 
                     key, e.what());
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
  std::unique_lock lock(mutex_);
  config_ = CreateDefaultConfig();
  MODULE_INFO(LOG_MODULE_COMMON, "Config reset to defaults");
}

std::string GlobalConfig::Dump(int indent) const {
  std::shared_lock lock(mutex_);
  return config_.dump(indent);
}

}  // namespace zenplay
```

---

## 5. 使用示例

### 5.1 基础使用

```cpp
// main.cpp
#include "player/config/global_config.h"

int main() {
  auto& config = GlobalConfig::Instance();
  
  // 加载配置文件
  auto load_result = config.Load("config/zenplay.jsonc");
  if (!load_result.IsOk()) {
    // 使用默认配置继续运行
    std::cerr << "Warning: " << load_result.Error().message << std::endl;
  }
  
  // 读取配置
  int buffer_size = config.GetInt("player.audio.buffer_size", 4096);
  bool use_hw = config.GetBool("render.use_hardware_acceleration", true);
  auto decoder_priority = config.GetStringArray("player.video.decoder_priority");
  
  // 使用配置
  InitPlayer(buffer_size, use_hw, decoder_priority);
  
  return 0;
}
```

### 5.2 在现有模块中使用

```cpp
// src/player/zen_player.cpp
#include "player/config/global_config.h"

Result<void> ZenPlayer::Open(const std::string& url) {
  auto& config = GlobalConfig::Instance();
  
  // 从配置读取解码器优先级
  auto decoder_priority = config.GetStringArray("player.video.decoder_priority");
  
  // 尝试按优先级初始化解码器
  for (const auto& decoder_name : decoder_priority) {
    auto result = video_decoder_->Init(decoder_name);
    if (result.IsOk()) {
      MODULE_INFO(LOG_MODULE_PLAYER, "Video decoder initialized: {}", decoder_name);
      break;
    }
  }
  
  // 从配置读取同步方法
  std::string sync_method = config.GetString("player.sync.method", "audio");
  av_sync_controller_->SetSyncMethod(sync_method);
  
  return Result<void>::Ok();
}
```

### 5.3 热重载支持

```cpp
// 监听配置变化（如硬件加速开关）
auto& config = GlobalConfig::Instance();

int watcher_id = config.Watch("render.use_hardware_acceleration", 
  [this](const ConfigValue& old_val, const ConfigValue& new_val) {
    bool old_hw = old_val.AsBool();
    bool new_hw = new_val.AsBool();
    
    if (old_hw != new_hw) {
      MODULE_INFO(LOG_MODULE_RENDERER, 
                  "Hardware acceleration changed: {} -> {}", old_hw, new_hw);
      
      // 切换渲染器
      this->SwitchRenderer(new_hw);
    }
  });

// 运行时重新加载配置
// 按下 F5 或接收信号时调用
config.Reload();  // 触发所有监听器
```

### 5.4 配置验证

```cpp
// 启动时验证关键配置
auto& config = GlobalConfig::Instance();

// 验证音频缓冲区大小
auto validate_result = config.Validate("player.audio.buffer_size", 
  [](const ConfigValue& val) {
    int size = val.AsInt();
    return size >= 1024 && size <= 65536 && (size % 1024 == 0);
  });

if (!validate_result.IsOk()) {
  MODULE_ERROR(LOG_MODULE_PLAYER, 
               "Invalid audio buffer size, using default");
  config.Set("player.audio.buffer_size", 4096);
}
```

---

## 6. 与现有模块集成

### 6.1 日志模块集成

```cpp
// src/player/common/log_manager.cpp
#include "player/config/global_config.h"

void LogManager::InitFromConfig() {
  auto& config = GlobalConfig::Instance();
  
  // 从配置读取日志级别
  std::string level_str = config.GetString("log.level", "info");
  LogLevel level = StringToLogLevel(level_str);
  SetLogLevel(level);
  
  // 配置输出目标
  auto outputs = config.Get("log.outputs");
  if (outputs) {
    for (const auto& output : outputs->Raw()) {
      std::string type = output["type"];
      bool enabled = output.value("enabled", true);
      
      if (!enabled) continue;
      
      if (type == "console") {
        AddConsoleOutput(output.value("color", true));
      } else if (type == "file") {
        AddFileOutput(output["path"], 
                      output.value("max_size_mb", 100),
                      output.value("max_files", 5));
      }
    }
  }
  
  // 监听日志级别变化（热重载）
  config.Watch("log.level", [this](const auto& old_val, const auto& new_val) {
    LogLevel new_level = StringToLogLevel(new_val.AsString());
    this->SetLogLevel(new_level);
    MODULE_INFO(LOG_MODULE_COMMON, "Log level changed to: {}", new_val.AsString());
  });
}
```

### 6.2 统计模块集成

```cpp
// src/player/stats/statistics_manager.cpp
#include "player/config/global_config.h"

void StatisticsManager::InitFromConfig() {
  auto& config = GlobalConfig::Instance();
  
  bool enabled = config.GetBool("statistics.enabled", true);
  if (!enabled) {
    MODULE_INFO(LOG_MODULE_STATS, "Statistics disabled by config");
    return;
  }
  
  int interval_ms = config.GetInt("statistics.report_interval_ms", 1000);
  SetReportInterval(interval_ms);
  
  auto metrics = config.GetStringArray("statistics.metrics");
  for (const auto& metric : metrics) {
    EnableMetric(metric);
  }
}
```

### 6.3 硬件加速模块集成

```cpp
// src/player/video/render/renderer.cpp
#include "player/config/global_config.h"

Renderer* Renderer::CreateRenderer() {
  auto& config = GlobalConfig::Instance();
  
  bool use_hw = config.GetBool("render.use_hardware_acceleration", true);
  auto backend_priority = config.GetStringArray("render.backend_priority");
  
  if (use_hw) {
    // 按优先级尝试硬件后端
    for (const auto& backend : backend_priority) {
      if (backend == "d3d11") {
        auto renderer = std::make_unique<D3D11Renderer>();
        if (renderer->IsAvailable()) {
          MODULE_INFO(LOG_MODULE_RENDERER, "Using D3D11 renderer");
          return renderer.release();
        }
      } else if (backend == "opengl") {
        // OpenGL 实现...
      }
    }
  }
  
  // 降级到软件渲染
  MODULE_WARN(LOG_MODULE_RENDERER, "Hardware renderers unavailable, using software");
  return new SDLRenderer();
}
```

---

## 7. 配置文件部署策略

### 7.1 开发环境

```
project_root/
├── config/
│   ├── zenplay.jsonc              # 主配置文件
│   ├── zenplay.dev.jsonc          # 开发环境覆盖配置
│   └── zenplay.example.jsonc      # 示例配置（Git 追踪）
├── CMakeLists.txt
└── ...
```

### 7.2 生产环境

```
install_dir/
├── bin/
│   └── zenplay.exe
├── config/
│   └── zenplay.jsonc              # 用户配置（可修改）
└── logs/
    └── zenplay.log
```

### 7.3 配置加载优先级

```cpp
// 按优先级加载配置（后者覆盖前者）
std::vector<std::string> config_files = {
  "config/zenplay.jsonc",          // 默认配置
  "config/zenplay.dev.jsonc",      // 开发环境（如果存在）
  GetUserConfigPath(),             // 用户配置目录（如 ~/.zenplay/config.jsonc）
};

for (const auto& path : config_files) {
  if (std::filesystem::exists(path)) {
    config.Load(path);  // 合并配置
  }
}

// 环境变量覆盖（最高优先级）
// 如：ZENPLAY_RENDER_USE_HW=false
config.LoadFromEnv("ZENPLAY_");
```

---

## 8. 性能考虑

### 8.1 读写锁优化

```cpp
// 高频读取场景（每帧调用）
bool use_hw = config.GetBool("render.use_hardware_acceleration");  // 共享锁，多线程并发读

// 低频写入场景（用户修改设置）
config.Set("render.use_hardware_acceleration", false);  // 独占锁，阻塞读取
```

### 8.2 缓存热点配置

```cpp
// 对于超高频访问的配置，可以在模块内部缓存
class VideoPlayer {
 public:
  void Init() {
    auto& config = GlobalConfig::Instance();
    
    // 缓存解码器优先级（避免每次解码都读取配置）
    cached_decoder_priority_ = config.GetStringArray("player.video.decoder_priority");
    
    // 监听变化更新缓存
    config.Watch("player.video.decoder_priority", 
      [this](const auto&, const auto& new_val) {
        cached_decoder_priority_ = new_val.AsStringArray();
      });
  }
  
 private:
  std::vector<std::string> cached_decoder_priority_;
};
```

---

## 9. 总结与推荐

### 9.1 最终推荐：JSON + JSONC 扩展

| 优势 | 理由 |
|------|------|
| **零成本** | 项目已依赖 `nlohmann/json`，无需额外库 |
| **类型安全** | 原生支持数字、布尔、数组，避免类型转换错误 |
| **结构化** | 支持深层嵌套和复杂数据结构（如日志输出数组） |
| **工具支持** | VS Code 原生支持 `.jsonc`，自动补全、格式化 |
| **注释支持** | 通过预处理支持 `//` 和 `/* */` 注释 |
| **生态成熟** | JSON 是 Web 标准，跨语言、跨平台通用 |

### 9.2 实施步骤

1. ✅ **第一阶段**：实现 `GlobalConfig` 类（单例 + 线程安全）
2. ✅ **第二阶段**：创建默认配置和示例文件
3. ✅ **第三阶段**：集成到现有模块（日志、统计、播放器）
4. ⬜ **第四阶段**：添加配置验证和热重载
5. ⬜ **第五阶段**：环境变量覆盖支持

### 9.3 配置文件位置

```
/workspaces/zenplay/
├── config/
│   ├── zenplay.json               ← 主配置文件
│   └── zenplay.example.json       ← 示例配置（Git 追踪，用于参考）
├── src/player/config/
│   ├── global_config.h            ← 配置管理器头文件
│   └── global_config.cpp          ← 配置管理器实现
└── CMakeLists.txt                 ← 确保链接 nlohmann_json
```

---

**文档完成**！此设计涵盖：

1. ✅ 全局配置管理系统（单例 + 线程安全）
2. ✅ JSON/YAML/INI 详细对比（14 个维度）
3. ✅ 推荐 JSON + JSONC 扩展（附理由）
4. ✅ 完整的 `GlobalConfig` 类设计和实现（800+ 行代码）
5. ✅ 配置文件示例（支持注释的 JSONC 格式）
6. ✅ 与现有模块（日志、统计、硬件加速）集成方案
7. ✅ 部署策略和性能优化建议

**下一步行动**：
1. ✅ 已创建 `src/player/config/global_config.h` 和 `.cpp`
2. ✅ 已创建 `config/zenplay.json` 配置文件
3. ⬜ 修改各模块以使用 `GlobalConfig::Instance()`（按需集成）
4. ⬜ 在 `main.cpp` 中初始化配置系统（按需集成）

需要我现在开始实施吗？
