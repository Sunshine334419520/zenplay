# ZenPlay 全局配置系统使用指南

## 概述

ZenPlay 全局配置系统提供了统一的配置管理机制，支持：

- ✅ **单例模式**：全局唯一实例
- ✅ **线程安全**：读写锁保护，支持多线程并发访问
- ✅ **类型安全**：编译期类型检查
- ✅ **热重载**：运行时重新加载配置
- ✅ **配置监听**：监听配置变化并触发回调
- ✅ **默认值**：配置文件缺失时使用内置默认值
- ✅ **配置验证**：自定义验证规则

## 快速开始

### 1. 基础使用

```cpp
#include "player/config/global_config.h"

// 获取配置管理器实例
auto& config = GlobalConfig::Instance();

// 加载配置文件
config.Load("config/zenplay.json");

// 读取配置
int buffer_size = config.GetInt("player.audio.buffer_size", 4096);
bool use_hw = config.GetBool("render.use_hardware_acceleration", true);
std::string log_level = config.GetString("log.level", "info");
auto decoders = config.GetStringArray("player.video.decoder_priority");

// 修改配置
config.Set("player.audio.buffer_size", 8192);
config.Set("render.use_hardware_acceleration", false);

// 保存配置
config.Save();
```

### 2. 配置文件格式

配置文件使用标准 JSON 格式（`config/zenplay.json`）：

```json
{
  "player": {
    "audio": {
      "buffer_size": 4096,
      "sample_rate": 48000,
      "channels": 2,
      "volume": 1.0
    },
    "video": {
      "decoder_priority": ["h264_cuvid", "h264_qsv", "h264"]
    }
  },
  "render": {
    "use_hardware_acceleration": true,
    "backend_priority": ["d3d11", "opengl", "software"]
  },
  "log": {
    "level": "info"
  }
}
```

### 3. 点号路径访问

支持使用点号（`.`）访问嵌套配置：

```cpp
// 访问 config["player"]["audio"]["buffer_size"]
int size = config.GetInt("player.audio.buffer_size", 4096);

// 访问 config["render"]["use_hardware_acceleration"]
bool use_hw = config.GetBool("render.use_hardware_acceleration", true);

// 访问 config["player"]["video"]["decoder_priority"]
auto priority = config.GetStringArray("player.video.decoder_priority");
```

## 高级特性

### 1. 配置监听（热重载）

监听配置变化并自动响应：

```cpp
// 监听硬件加速开关
int watcher_id = config.Watch("render.use_hardware_acceleration",
  [this](const ConfigValue& old_val, const ConfigValue& new_val) {
    bool old_hw = old_val.AsBool();
    bool new_hw = new_val.AsBool();
    
    if (old_hw != new_hw) {
      // 切换渲染器
      this->SwitchRenderer(new_hw);
    }
  });

// 修改配置时会触发回调
config.Set("render.use_hardware_acceleration", false);

// 取消监听
config.Unwatch(watcher_id);
```

### 2. 配置验证

验证配置值的合法性：

```cpp
// 验证音频缓冲区大小范围
auto result = config.Validate("player.audio.buffer_size",
  [](const ConfigValue& val) {
    int size = val.AsInt();
    return size >= 1024 && size <= 65536 && (size % 1024 == 0);
  });

if (!result.IsOk()) {
  // 配置不合法，使用默认值
  config.Set("player.audio.buffer_size", 4096);
}
```

### 3. 运行时热重载

重新加载配置文件（会触发所有监听器）：

```cpp
// 按下 F5 或接收信号时
config.Reload();

// 所有注册的监听器会收到通知
```

### 4. 检查配置键是否存在

```cpp
if (config.Has("player.audio.buffer_size")) {
  int size = config.GetInt("player.audio.buffer_size");
} else {
  // 使用默认值
  int size = 4096;
}
```

### 5. 获取原始 JSON 对象

```cpp
auto value = config.Get("player.audio");
if (value) {
  // 获取原始 JSON 对象
  nlohmann::json audio_config = value->Raw();
  
  // 检查类型
  if (value->IsObject()) {
    // 处理对象类型配置
  }
}
```

### 6. 重置为默认配置

```cpp
// 重置所有配置为内置默认值
config.ResetToDefaults();
```

## 配置项说明

### player（播放器配置）

| 配置项 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `player.audio.buffer_size` | int | 4096 | 音频缓冲区大小（字节） |
| `player.audio.sample_rate` | int | 48000 | 音频采样率（Hz） |
| `player.audio.channels` | int | 2 | 声道数 |
| `player.audio.volume` | double | 1.0 | 音量（0.0-1.0） |
| `player.video.decoder_priority` | array | ["h264_cuvid", "h264_qsv", "h264"] | 解码器优先级 |
| `player.video.max_width` | int | 3840 | 最大支持宽度（像素） |
| `player.video.max_height` | int | 2160 | 最大支持高度（像素） |
| `player.sync.method` | string | "audio" | 同步基准（audio/video/external） |
| `player.sync.correction_threshold_ms` | int | 100 | 同步偏差阈值（毫秒） |

### render（渲染配置）

| 配置项 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `render.use_hardware_acceleration` | bool | true | 是否启用硬件加速 |
| `render.backend_priority` | array | ["d3d11", "opengl", "software"] | 渲染后端优先级 |
| `render.vsync` | bool | true | 是否启用垂直同步 |
| `render.max_fps` | int | 60 | 最大帧率限制 |
| `render.hardware.allow_d3d11va` | bool | true | 允许 D3D11VA（Windows 8+） |
| `render.hardware.allow_dxva2` | bool | true | 允许 DXVA2（Windows 7） |
| `render.hardware.allow_fallback` | bool | true | 硬件失败时降级到软件 |

### log（日志配置）

| 配置项 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `log.level` | string | "info" | 日志级别（debug/info/warn/error） |
| `log.outputs` | array | ... | 日志输出配置（console/file） |
| `log.module_levels` | object | ... | 模块级别日志控制 |

### statistics（统计配置）

| 配置项 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `statistics.enabled` | bool | true | 是否启用统计 |
| `statistics.report_interval_ms` | int | 1000 | 统计上报间隔（毫秒） |
| `statistics.metrics` | array | ["fps", ...] | 收集的指标列表 |

### network（网络配置）

| 配置项 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `network.timeout_ms` | int | 5000 | 连接超时（毫秒） |
| `network.buffer_size_kb` | int | 1024 | 网络缓冲区大小（KB） |
| `network.user_agent` | string | "ZenPlay/1.0" | User-Agent 字符串 |
| `network.proxy.enabled` | bool | false | 是否启用代理 |

### cache（缓存配置）

| 配置项 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `cache.enabled` | bool | true | 是否启用缓存 |
| `cache.max_size_mb` | int | 500 | 最大缓存大小（MB） |
| `cache.directory` | string | "cache/zenplay" | 缓存目录 |

## 线程安全

`GlobalConfig` 使用读写锁（`std::shared_mutex`）保护，支持：

- **多个线程并发读取**（共享锁）：
  ```cpp
  int size = config.GetInt("player.audio.buffer_size");  // 可并发
  ```

- **单个线程写入**（独占锁）：
  ```cpp
  config.Set("player.audio.buffer_size", 8192);  // 阻塞其他读写
  ```

## 性能优化建议

### 1. 缓存热点配置

对于超高频访问的配置（如每帧调用），建议在模块内部缓存：

```cpp
class VideoPlayer {
 public:
  void Init() {
    auto& config = GlobalConfig::Instance();
    
    // 缓存解码器优先级
    cached_decoder_priority_ = 
      config.GetStringArray("player.video.decoder_priority");
    
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

### 2. 避免在关键路径频繁读取

```cpp
// ❌ 不好：每次解码都读取配置
void DecodeFrame() {
  bool use_hw = config.GetBool("render.use_hardware_acceleration");
  // ...
}

// ✅ 好：初始化时读取并缓存
void Init() {
  use_hardware_ = config.GetBool("render.use_hardware_acceleration");
}

void DecodeFrame() {
  if (use_hardware_) {
    // ...
  }
}
```

## 示例代码

完整的使用示例请参考：
- `examples/global_config_usage.cpp` - 基础使用示例

## 文件位置

```
/workspaces/zenplay/
├── config/
│   ├── zenplay.json           # 主配置文件
│   └── zenplay.example.json   # 示例配置（Git 追踪）
├── src/player/config/
│   ├── global_config.h        # 配置管理器头文件
│   └── global_config.cpp      # 配置管理器实现
└── examples/
    └── global_config_usage.cpp # 使用示例
```

## 注意事项

1. **配置文件缺失**：如果 `config/zenplay.json` 不存在，`Load()` 仍会成功并使用内置默认值
2. **JSON 格式**：使用标准 JSON 格式（不支持注释）
3. **保存持久化**：调用 `Set()` 后需要调用 `Save()` 才能持久化到文件
4. **监听器异常**：监听器回调中的异常会被捕获并忽略，不会影响其他监听器
5. **默认值**：所有 `Get*()` 方法都支持默认值参数，确保代码健壮性

## 未来扩展

配置系统预留了以下扩展点（未来实现）：

- [ ] 环境变量覆盖（`ZENPLAY_*`）
- [ ] 多配置文件合并（开发/生产环境）
- [ ] 配置加密支持（敏感信息）
- [ ] 配置变更历史记录
- [ ] JSON Schema 验证

---

**版本**: 1.0  
**最后更新**: 2025-10-22
