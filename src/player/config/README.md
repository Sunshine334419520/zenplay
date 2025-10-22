# Player Config Module

此目录包含 ZenPlay 全局配置管理系统。

## 文件说明

- `global_config.h` - 全局配置管理器头文件
- `global_config.cpp` - 全局配置管理器实现

## 使用方法

```cpp
#include "player/config/global_config.h"

// 获取配置管理器实例（单例）
auto& config = GlobalConfig::Instance();

// 加载配置文件
config.Load("config/zenplay.json");

// 读取配置
int buffer_size = config.GetInt("player.audio.buffer_size", 4096);
bool use_hw = config.GetBool("render.use_hardware_acceleration", true);

// 修改配置
config.Set("player.audio.buffer_size", 8192);
config.Save();
```

## 详细文档

- [使用指南](../../../docs/global_config_usage.md)
- [设计文档](../../../docs/global_config_system_design.md)
- [使用示例](../../../examples/global_config_usage.cpp)
