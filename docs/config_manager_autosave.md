# ConfigManager 自动保存设计

## 问题分析

### ❌ 原设计的弊端

你发现的问题非常正确：

```cpp
// 问题 1：容易忘记保存
config.Set("player.audio.buffer_size", 8192);
config.Set("player.audio.sample_rate", 48000);
// 忘记调用 Save()，程序崩溃时配置丢失！
```

```cpp
// 问题 2：频繁保存性能差
for (int i = 0; i < 100; ++i) {
  config.Set("key" + std::to_string(i), i);
  config.Save();  // 写文件 100 次！
}
```

**弊端总结**：
- 😫 用户体验差：需要记住手动保存
- 😫 数据丢失风险：忘记保存或程序崩溃
- 😫 性能问题：频繁保存导致 IO 密集
- 😫 批量操作困难：不知道何时保存合适

## 改进方案：自动保存策略

### ✅ 方案对比

| 策略 | 触发时机 | 优点 | 缺点 | 适用场景 |
|------|---------|------|------|---------|
| **Manual** | 手动调用 Save() | 完全控制 | 容易忘记 | 需要精确控制保存时机 |
| **Immediate** | 每次 Set 后立即保存 | 不会丢失数据 | IO 密集，性能差 | 配置修改极少 |
| **Debounced** ⭐ | 最后一次修改后延迟保存 | 批量修改只保存一次 | 延迟保存（可能丢失） | 大多数场景（推荐） |
| **OnExit** | 程序退出时保存 | 无 IO 开销 | 崩溃会丢失数据 | 调试或测试 |

### 推荐：Debounced（防抖保存）

**原理**：最后一次修改后延迟 N 毫秒保存，如果在延迟期间又有修改，则重新计时。

```
时间轴: ─────────────────────────────────────────────>
操作:    Set1  Set2  Set3           (延迟1秒)    Save!
         │     │     │                            │
         ↓     ↓     ↓                            ↓
取消:          ×     ×                          ✓ 只保存一次
```

**优势**：
- ✅ 批量修改只保存一次（性能好）
- ✅ 不需要手动调用 Save（用户体验好）
- ✅ 可配置延迟时间（灵活）

## 使用示例

### 1. 初始化时配置策略

```cpp
#include "player/config/config_manager.h"

int main() {
  // 创建 IO 线程
  auto io_thread = loki::LokiSubThread::CreateIOThread();
  io_thread->Start();
  
  auto& config = ConfigManager::Instance();
  
  // 方式 1：使用默认策略（Debounced，延迟 1 秒）
  config.Initialize();
  
  // 方式 2：指定策略
  config.Initialize(AutoSavePolicy::Debounced, std::chrono::milliseconds(500));
  
  // 方式 3：手动保存模式
  config.Initialize(AutoSavePolicy::Manual);
  
  return 0;
}
```

### 2. Debounced 策略（推荐）

```cpp
// 初始化为防抖模式（默认 1 秒延迟）
config.Initialize(AutoSavePolicy::Debounced, std::chrono::milliseconds(1000));

// 批量修改
config.Set("player.audio.buffer_size", 8192);   // 不立即保存
config.Set("player.audio.sample_rate", 48000);  // 不立即保存
config.Set("player.audio.channels", 2);         // 不立即保存

// 1 秒后自动保存（只保存 1 次）
// 用户无需手动调用 Save()
```

### 3. Immediate 策略

```cpp
// 初始化为立即保存模式
config.Initialize(AutoSavePolicy::Immediate);

// 每次修改都立即保存
config.Set("player.audio.buffer_size", 8192);   // 立即保存（写文件 1 次）
config.Set("player.audio.sample_rate", 48000);  // 立即保存（写文件 2 次）
config.Set("player.audio.channels", 2);         // 立即保存（写文件 3 次）

// ⚠️ 注意：频繁写文件，性能差
```

### 4. Manual 策略

```cpp
// 初始化为手动保存模式
config.Initialize(AutoSavePolicy::Manual);

// 修改不会自动保存
config.Set("player.audio.buffer_size", 8192);
config.Set("player.audio.sample_rate", 48000);

// 需要手动保存
config.Save();
```

### 5. OnExit 策略

```cpp
// 初始化为退出时保存模式
config.Initialize(AutoSavePolicy::OnExit);

// 修改不会自动保存
config.Set("player.audio.buffer_size", 8192);
config.Set("player.audio.sample_rate", 48000);

// 程序正常退出时自动保存
// ⚠️ 注意：如果程序崩溃，配置会丢失
```

### 6. 运行时切换策略

```cpp
// 初始化
config.Initialize(AutoSavePolicy::Debounced);

// 某些场景下切换为手动模式
config.SetAutoSavePolicy(AutoSavePolicy::Manual);
config.Set("temp.key", 123);  // 不会自动保存
config.Save();  // 手动保存

// 切换回防抖模式
config.SetAutoSavePolicy(AutoSavePolicy::Debounced);
config.Set("player.volume", 80);  // 1 秒后自动保存
```

### 7. 调整防抖延迟

```cpp
// 初始化为防抖模式，延迟 500 毫秒
config.Initialize(AutoSavePolicy::Debounced, std::chrono::milliseconds(500));

// 运行时调整延迟
config.SetDebounceDelay(std::chrono::milliseconds(2000));  // 改为 2 秒
```

### 8. 异步操作也支持自动保存

```cpp
// 异步修改也会触发自动保存
config.SetAsync("player.audio.buffer_size", 8192, []() {
  std::cout << "配置已修改（自动保存中...）" << std::endl;
});

// 1 秒后自动保存
```

## 实现细节

### 防抖保存流程

```cpp
void ConfigManager::Set(const std::string& key, int value) {
  loki::Invoke<void>(loki::IO, FROM_HERE, [this, &key, value]() {
    // 1. 修改配置
    config_.Set(key, value);
    
    // 2. 触发自动保存
    TriggerAutoSave();
  });
}

void ConfigManager::TriggerAutoSave() {
  switch (auto_save_policy_) {
    case AutoSavePolicy::Debounced:
      // 取消之前的定时保存
      CancelDebouncedSave();
      
      // 重新调度保存（延迟 N 毫秒）
      save_pending_ = true;
      loki::PostDelayedTask(
          loki::IO, FROM_HERE,
          loki::BindOnceClosure([this]() {
            if (save_pending_) {
              config_.Save();
              save_pending_ = false;
            }
          }),
          debounce_delay_);
      break;
    
    case AutoSavePolicy::Immediate:
      config_.Save();  // 立即保存
      break;
    
    // ...
  }
}
```

### OnExit 保存流程

```cpp
ConfigManager::~ConfigManager() {
  // 如果策略是 OnExit，则在析构时保存
  if (auto_save_policy_ == AutoSavePolicy::OnExit && initialized_) {
    loki::Invoke<void>(loki::IO, FROM_HERE, [this]() {
      config_.Save();
    });
  }
}
```

## 性能对比

### 场景：批量修改 100 个配置

| 策略 | 写文件次数 | 总耗时 | 说明 |
|------|-----------|--------|------|
| **Manual** | 1 次（手动） | ~1-2 ms | 需要记住调用 Save() |
| **Immediate** | 100 次 | ~100-200 ms | 频繁 IO，性能差 |
| **Debounced** | 1 次 | ~1-2 ms | 自动保存，性能好 ⭐ |
| **OnExit** | 1 次 | ~1-2 ms | 崩溃会丢失 |

### 场景：单次修改

| 策略 | 延迟 | 数据安全性 | 说明 |
|------|-----|-----------|------|
| **Immediate** | 0 ms | ✅ 最安全 | 立即持久化 |
| **Debounced** | 1000 ms（可配置） | ⚠️ 较安全 | 延迟内崩溃会丢失 |
| **OnExit** | 退出时 | ❌ 不安全 | 崩溃必丢失 |

## 最佳实践

### ✅ 推荐做法

```cpp
// 1. 大多数应用：使用 Debounced（默认）
config.Initialize();  // 默认 Debounced, 1000ms

// 2. 配置很少修改：使用 Immediate
config.Initialize(AutoSavePolicy::Immediate);

// 3. 需要精确控制：使用 Manual + 事务模式
config.Initialize(AutoSavePolicy::Manual);
// 批量修改
config.Set("key1", 1);
config.Set("key2", 2);
config.Save();  // 显式保存

// 4. 测试环境：使用 OnExit
#ifdef DEBUG
config.Initialize(AutoSavePolicy::OnExit);
#else
config.Initialize(AutoSavePolicy::Debounced);
#endif
```

### ❌ 避免的做法

```cpp
// ❌ 不要：Immediate + 频繁修改
config.Initialize(AutoSavePolicy::Immediate);
for (int i = 0; i < 1000; ++i) {
  config.Set("key" + std::to_string(i), i);  // 写文件 1000 次！
}

// ❌ 不要：Debounced + 延迟太短
config.Initialize(AutoSavePolicy::Debounced, std::chrono::milliseconds(10));
// 10ms 太短，几乎等于 Immediate

// ❌ 不要：OnExit + 关键数据
config.Initialize(AutoSavePolicy::OnExit);
config.Set("user.password", "important");  // 崩溃会丢失！
```

## 数据安全性建议

### 不同场景的推荐策略

| 数据类型 | 推荐策略 | 延迟配置 | 原因 |
|---------|---------|---------|------|
| **用户偏好** | Debounced | 1-2 秒 | 允许短暂延迟，批量修改常见 |
| **播放状态** | Immediate | N/A | 需要立即持久化 |
| **临时缓存** | OnExit | N/A | 丢失无影响 |
| **关键设置** | Immediate | N/A | 数据安全第一 |
| **调试配置** | Manual | N/A | 开发者自行控制 |

## 总结

### 改进前 vs 改进后

| 方面 | 改进前 | 改进后 |
|------|--------|--------|
| **用户体验** | 😫 需要手动 Save | ✅ 自动保存 |
| **数据安全** | ⚠️ 容易忘记保存 | ✅ 自动持久化 |
| **性能** | ❓ 取决于用户 | ✅ 防抖优化 |
| **灵活性** | ❌ 只有手动模式 | ✅ 4 种策略可选 |
| **批量操作** | 😫 难以优化 | ✅ 自动合并 |

### 核心优势

✅ **防抖保存（推荐）**：批量修改只保存一次，性能和易用性最佳  
✅ **多种策略**：根据场景选择 Manual/Immediate/Debounced/OnExit  
✅ **运行时切换**：可以动态调整策略和延迟时间  
✅ **向后兼容**：仍然支持手动 Save()  
✅ **数据安全**：提供 Immediate 模式保证关键数据不丢失  

---

**版本**: 3.0（自动保存版）  
**更新时间**: 2025-10-24  
**改进**: 添加自动保存策略，解决手动保存的弊端
