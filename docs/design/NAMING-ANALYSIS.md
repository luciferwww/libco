# API 命名规范分析与优化建议

## 当前命名模式分析

### ✅ **命名一致的模块**

| 模块 | 模式 | 函数示例 | 评价 |
|------|------|----------|------|
| 调度器 | `co_scheduler_*` | `co_scheduler_create()`, `co_scheduler_run()` | ✅ 完美 |
| 互斥锁 | `co_mutex_*` | `co_mutex_lock()`, `co_mutex_unlock()` | ✅ 完美 |
| 条件变量 | `co_cond_*` | `co_cond_wait()`, `co_cond_signal()` | ✅ 完美 |
| Channel | `co_channel_*` | `co_channel_send()`, `co_channel_recv()` | ✅ 完美 |
| TLS | `co_tls_*` | `co_tls_set()`, `co_tls_get()` | ✅ 完美 |
| WaitGroup | `co_waitgroup_*` | `co_waitgroup_add()`, `co_waitgroup_wait()` | ✅ 完美 |
| 协程组 | `co_group_*` | `co_group_create()`, `co_group_wait()` | ✅ 完美 |

### ⚠️ **命名不一致的模块**

#### 1. 协程操作（最大问题）

| 函数 | 参数 | 问题 | 建议 |
|------|------|------|------|
| `co_create()` | 需要 sched | 缺少对象名 | `co_spawn()` 更语义化 |
| `co_yield()` | 无参数 | 操作隐式当前协程 | ✅ 保持（便利函数） |
| `co_sleep()` | msec | 操作隐式当前协程 | ✅ 保持（便利函数） |
| `co_self()` | 无参数 | 获取当前协程 | ✅ 保持 |
| `co_return()` | value | 操作隐式当前协程 | ✅ 保持 |
| **`co_join()`** | `co_routine_t*` | **需要句柄但缺少对象名** | `co_await()` 或 `co_routine_join()` |
| **`co_cancel()`** | `co_routine_t*` | **需要句柄但缺少对象名** | `co_routine_cancel()` |
| **`co_detach()`** | `co_routine_t*` | **需要句柄但缺少对象名** | `co_routine_detach()` |
| **`co_set_name()`** | `co_routine_t*` | **需要句柄但缺少对象名** | `co_routine_set_name()` |
| **`co_get_name()`** | `co_routine_t*` | **需要句柄但缺少对象名** | `co_routine_name()` |
| **`co_get_id()`** | `co_routine_t*` | **需要句柄但缺少对象名** | `co_routine_id()` |
| **`co_get_state()`** | `co_routine_t*` | **需要句柄但缺少对象名** | `co_routine_state()` |

#### 2. I/O 操作

| 函数 | 问题 | 建议 |
|------|------|------|
| `co_io_wait()` | ✅ 有对象名 | 保持 |
| `co_read()` | 缺少对象名 | 保持（便利函数，可选 hooks） |
| `co_write()` | 缺少对象名 | 保持（便利函数，可选 hooks） |

#### 3. 全局函数

| 函数 | 评价 |
|------|------|
| `co_init()`, `co_deinit()` | ✅ 全局函数，合理 |
| `co_set_allocator()` | ✅ 全局配置，合理 |
| `co_strerror()`, `co_last_error()` | ✅ 错误处理，合理 |

## 问题根源

**混合使用两种模式**：
1. **便利函数**：操作隐式对象（当前协程），简短命名
2. **对象方法**：需要显式传递句柄，应该包含对象名

**不一致的地方**：
- `co_yield()` - 操作当前协程，简短 ✅
- `co_join(co_routine_t*)` - 操作其他协程，却也简短 ❌

## 优化方案

### 🎯 **方案 A：严格遵循规则**（推荐用于长期维护）

#### 原则
1. **需要句柄的函数**：必须包含对象名 `co_<object>_<action>()`
2. **操作当前协程**：可以省略对象名 `co_<action>()`
3. **全局函数**：直接 `co_<action>()`

#### 具体改动

```c
// 1. 协程创建 - 使用更语义化的名称
co_routine_t* co_spawn(...)  // 代替 co_create()
// 或保持 co_create() 并添加别名
#define co_spawn co_create

// 2. 需要句柄的协程操作 - 添加对象名
co_error_t co_routine_join(co_routine_t* co, ...)     // 代替 co_join()
co_error_t co_routine_cancel(co_routine_t* co)        // 代替 co_cancel()
co_error_t co_routine_detach(co_routine_t* co)        // 代替 co_detach()

// 3. 协程属性访问 - 统一格式
void co_routine_set_name(co_routine_t* co, ...)       // 代替 co_set_name()
const char* co_routine_name(co_routine_t* co)         // 代替 co_get_name()
uint64_t co_routine_id(co_routine_t* co)              // 代替 co_get_id()
co_state_t co_routine_state(co_routine_t* co)         // 代替 co_get_state()

// 4. 操作当前协程 - 保持简短（这些是便利函数）
co_error_t co_yield(void)          // ✅ 保持
co_error_t co_sleep(uint32_t msec) // ✅ 保持
co_routine_t* co_self(void)        // ✅ 保持
void co_return(void* value)        // ✅ 保持

// 5. I/O 操作 - 保持现有设计
int co_io_wait(...)     // ✅ 保持
ssize_t co_read(...)    // ✅ 保持（可选 hook）
ssize_t co_write(...)   // ✅ 保持（可选 hook）
```

### 🎯 **方案 B：语义化命名**（推荐用于易用性）

使用更符合协程语义的特殊名称：

```c
// 1. 创建使用 spawn（参考 Rust, Erlang）
co_routine_t* co_spawn(...)  

// 2. 等待使用 await（参考 C++20, JavaScript）
co_error_t co_await(co_routine_t* co, ...)  // 代替 co_join()

// 3. 其他需要句柄的操作保持 co_routine_ 前缀
co_error_t co_routine_cancel(co_routine_t* co)
co_error_t co_routine_detach(co_routine_t* co)
```

### 🎯 **方案 C：折中方案**（推荐采用）

结合方案 A 和 B 的优点：

```c
// 1. 核心生命周期 - 使用语义化名称
co_routine_t* co_spawn(...)               // 创建（语义化）
co_error_t co_await(co_routine_t*, ...)   // 等待（语义化）
co_error_t co_routine_cancel(...)         // 取消（规范化）
co_error_t co_routine_detach(...)         // 分离（规范化）

// 2. 属性访问 - 使用规范命名
void co_routine_set_name(...)
const char* co_routine_name(...)           // 不用 get_
uint64_t co_routine_id(...)                // 不用 get_
co_state_t co_routine_state(...)           // 不用 get_

// 3. 当前协程操作 - 保持简短
co_yield(), co_sleep(), co_self(), co_return()  // ✅ 都保持

// 4. 提供便利别名（可选）
#define co_create co_spawn                 // 向前兼容
#define co_join   co_await                 // 语义对等
```

## 最终推荐：方案 C

### 优点
1. ✅ **一致性强**：需要句柄的一律带 `co_routine_` 或使用语义化名称
2. ✅ **易用性好**：核心函数使用直观的 `spawn`, `await`
3. ✅ **简洁性佳**：当前协程操作保持简短
4. ✅ **符合预期**：类似其他现代语言的协程 API

### 缺点
1. ⚠️ 需要更新部分函数名
2. ⚠️ 学习曲线略增（但更直观）

## 命名规则总结

### 📋 **最终规则**

1. **对象方法** - 必须包含对象名
   ```
   co_<object>_<action>(object_handle, ...)
   例如：co_scheduler_create(), co_mutex_lock()
   ```

2. **对象属性** - 省略 get/set 前缀
   ```
   co_<object>_<property>(object_handle)
   例如：co_routine_name(), co_routine_id()
   设置：co_<object>_set_<property>(object_handle, value)
   ```

3. **便利函数** - 操作当前协程，简短命名
   ```
   co_<action>()
   例如：co_yield(), co_sleep(), co_self()
   ```

4. **语义化函数** - 核心操作使用直观名称
   ```
   co_spawn()  - 创建协程
   co_await()  - 等待协程（join）
   ```

5. **全局函数** - 直接命名
   ```
   co_<action>()
   例如：co_init(), co_set_allocator()
   ```

## 修改清单

### 必须修改（高优先级）

| 旧名称 | 新名称 | 原因 |
|--------|--------|------|
| `co_create()` | `co_spawn()` | 更语义化 + 提供别名 |
| `co_join()` | `co_await()` | 更直观 + 提供别名 |
| `co_cancel()` | `co_routine_cancel()` | 规范化 |
| `co_detach()` | `co_routine_detach()` | 规范化 |
| `co_set_name()` | `co_routine_set_name()` | 规范化 |
| `co_get_name()` | `co_routine_name()` | 规范化 + 省略 get |
| `co_get_id()` | `co_routine_id()` | 规范化 + 省略 get |
| `co_get_state()` | `co_routine_state()` | 规范化 + 省略 get |

### 可选修改（低优先级）

| 旧名称 | 新名称 | 原因 |
|--------|--------|------|
| `co_scheduler_self()` | 保持 | 已经规范 |
| `co_strerror()` | 保持 | 全局函数 |
| `co_last_error()` | 保持 | 全局函数 |

### 添加别名（向前兼容）

```c
// libco/co_compat.h - 便利别名
#define co_create co_spawn    // 两个名称都可用
#define co_join   co_await    // 两个名称都可用
```

## 影响评估

- **文档更新**：需要更新所有 API 文档
- **示例更新**：需要更新所有示例代码
- **开发时间**：+1-2 天（仅改名）
- **收益**：命名一致性大幅提升，长期易维护

## 建议

✅ **采用方案 C（折中方案）**：
- 既保持了一致性
- 又兼顾了易用性和语义化
- 通过别名保持灵活性

🚀 **实施建议**：
1. 在开始编码前完成命名规范（现在）
2. 一次性更新所有文档
3. 在实现时严格遵守新规范

---

**日期**：2026-03-23  
**状态**：待审核  
**下一步**：根据反馈更新 API 设计文档
