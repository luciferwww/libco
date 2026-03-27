# 架构设计

## 整体架构

```
┌─────────────────────────────────────────────────────────┐
│                    Applications                         │
│            (User Code, Servers, Games, etc.)           │
└─────────────────────────────────────────────────────────┘
                           │
         ┌─────────────────┴─────────────────┐
         │                                   │
┌────────▼──────────┐              ┌────────▼──────────┐
│   C++ Wrapper     │              │   C Core API      │
│   (libcoxx)       │              │   (libco)         │
│  - RAII classes   │◄─────────────┤  - co_scheduler   │
│  - Templates      │              │  - co_routine     │
│  - std::function  │              │  - co_yield_now   │
└───────────────────┘              └───────┬───────────┘
                                           │
                    ┌──────────────────────┼──────────────────────┐
                    │                      │                      │
           ┌────────▼────────┐   ┌────────▼────────┐   ┌────────▼────────┐
           │   Scheduler     │   │    Context      │   │    Runtime      │
           │                 │   │                 │   │                 │
           │ - Task queue    │   │ - Save/restore  │   │ - Timer         │
           │ - Cooperative   │   │ - Stack mgmt    │   │ - I/O mux       │
           │ - Priority      │   │ - Platform impl │   │ - Sync prims    │
           └─────────────────┘   └─────────────────┘   └─────────────────┘
                    │                      │                      │
                    └──────────────────────┼──────────────────────┘
                                           │
                    ┌──────────────────────┼──────────────────────┐
                    │                      │                      │
           ┌────────▼────────┐   ┌────────▼────────┐   ┌────────▼────────┐
           │   Linux Impl    │   │  Windows Impl   │   │   macOS Impl    │
           │                 │   │                 │   │                 │
           │ - ucontext      │   │ - Fiber         │   │ - ucontext      │
           │ - epoll         │   │ - IOCP          │   │ - kqueue        │
           └─────────────────┘   └─────────────────┘   └─────────────────┘
```

## 核心模块

### 1. Context（上下文管理）

**职责**：管理协程的执行上下文（寄存器、栈指针等）

```c
// 核心接口
typedef struct co_context co_context_t;

// 初始化上下文
int co_context_init(co_context_t* ctx, 
                    void* stack_base, 
                    size_t stack_size,
                    co_entry_func_t entry,
                    void* arg);

// 切换上下文
int co_context_swap(co_context_t* from, co_context_t* to);

// 销毁上下文
void co_context_destroy(co_context_t* ctx);
```

**实现策略**：
- **Linux/macOS**：优先使用 `ucontext`，fallback 到汇编
- **Windows**：使用 `Fiber API`
- **性能优化**：提供手写汇编版本（x86_64, ARM64）

### 2. Scheduler（调度器）

**职责**：管理协程的调度和执行

```c
typedef struct co_scheduler co_scheduler_t;

// 调度策略
typedef enum co_sched_policy {
    CO_SCHED_FIFO,      // 先进先出
    CO_SCHED_PRIORITY,  // 优先级调度
    CO_SCHED_WFQ        // 加权公平队列
} co_sched_policy_t;

// 调度器配置
typedef struct co_sched_config {
    co_sched_policy_t policy;
    size_t max_routines;     // 最大协程数
    int enable_preempt;      // 是否启用抢占
    uint32_t time_slice_us;  // 时间片（微秒）
} co_sched_config_t;
```

**关键数据结构**：
```c
struct co_scheduler {
    // 调度策略
    co_sched_policy_t policy;
    
    // 运行队列（支持多级队列或优先级队列）
    union {
        co_queue_t fifo_queue;          // FIFO 模式
        co_priority_queue_t prio_queue; // 优先级模式
        co_cfs_runqueue_t cfs_queue;    // CFS 模式
    } ready_queue;
    
    // 当前运行的协程
    co_routine_t* current;
    
    // 主上下文
    co_context_t main_ctx;
    
    // 睡眠队列（定时器堆）
    co_timer_heap_t sleep_heap;
    
    // I/O 等待队列
    co_iomux_t* iomux;
    
    // 阻塞协程管理
    co_queue_t blocked_queue;
    
    // 调度配置
    uint64_t time_slice_us;     // 时间片
    uint64_t min_granularity;   // 最小调度粒度
    
    // 统计信息
    struct {
        uint64_t switch_count;
        uint64_t total_routines;
        uint64_t active_routines;
        uint64_t ready_routines;
        uint64_t blocked_routines;
    } stats;
};
```

**调度算法**：
1. 检查就绪队列，选择可运行协程
2. 检查定时器，唤醒到期协程
3. 检查 I/O 事件，唤醒就绪协程
4. 如果无协程就绪，等待事件或超时
5. 切换到选中的协程

### 3. Routine（协程）

**职责**：代表一个协程实例

```c
typedef struct co_routine co_routine_t;

typedef enum co_state {
    CO_STATE_READY,      // 就绪
    CO_STATE_RUNNING,    // 运行中
    CO_STATE_SUSPENDED,  // 挂起
    CO_STATE_WAITING,    // 等待（I/O、定时器等）
    CO_STATE_DEAD        // 已结束
} co_state_t;

struct co_routine {
    // 唯一 ID
    uint64_t id;
    
    // 状态
    co_state_t state;
    
    // 上下文
    co_context_t context;
    
    // 栈信息
    void* stack_base;
    size_t stack_size;
    
    // 入口函数
    co_entry_func_t entry;
    void* arg;
    
    // 所属调度器
    co_scheduler_t* scheduler;
    
    // 链表节点（用于队列）
    co_routine_t* next;
    co_routine_t* prev;
    
    // 调度信息
    int priority;               // 优先级（-20 到 19，类似 Linux nice）
    int nice_value;             // nice 值
    uint64_t vruntime;          // 虚拟运行时间（用于 CFS）
    uint64_t total_runtime_us;  // 总运行时间（微秒）
    uint64_t last_schedule_us;  // 上次调度时间
    
    // 生命周期管理
    co_result_t result;         // 协程结果
    int detached;               // 是否已分离
    co_routine_t* joiner;       // 等待此协程的协程
    
    // 等待信息（用于同步原语）
    void* wait_object;          // 等待的对象（mutex, channel 等）
    uint64_t wakeup_time;       // 唤醒时间（用于 sleep 和 timeout）
    
    // TLS
    void** tls_values;          // TLS 值数组
    size_t tls_count;           // TLS 槽位数量
    
    // 调试信息
    const char* name;
    co_stack_info_t stack_info; // 栈使用信息
    
#ifdef CO_DEBUG
    void** backtrace;           // 调用栈
    size_t backtrace_size;
#endif
};
```

### 4. Runtime（运行时）

#### 4.1 Timer（定时器）

```c
// 休眠指定毫秒
int co_sleep(uint32_t msec);

// 设置超时回调
typedef void (*co_timer_cb_t)(void* arg);
co_timer_t* co_timer_create(uint32_t msec, co_timer_cb_t cb, void* arg);
void co_timer_cancel(co_timer_t* timer);
```

**实现**：使用最小堆管理定时器

#### 4.2 I/O Multiplexing

```c
// 等待文件描述符就绪
typedef enum co_io_event {
    CO_IO_READ  = 0x01,
    CO_IO_WRITE = 0x02,
    CO_IO_ERROR = 0x04
} co_io_event_t;

int co_io_wait(int fd, co_io_event_t events, uint32_t timeout_ms);
```

**平台实现**：
- Linux: `epoll`
- macOS: `kqueue`
- Windows: `IOCP` 或 `WSAPoll`

#### 4.3 Synchronization（同步原语）

```c
// 互斥锁
typedef struct co_mutex co_mutex_t;
int co_mutex_init(co_mutex_t* mutex);
int co_mutex_lock(co_mutex_t* mutex);
int co_mutex_unlock(co_mutex_t* mutex);
void co_mutex_destroy(co_mutex_t* mutex);

// 条件变量
typedef struct co_cond co_cond_t;
int co_cond_init(co_cond_t* cond);
int co_cond_wait(co_cond_t* cond, co_mutex_t* mutex);
int co_cond_signal(co_cond_t* cond);
int co_cond_broadcast(co_cond_t* cond);
void co_cond_destroy(co_cond_t* cond);

// Channel（Go 风格）
typedef struct co_channel co_channel_t;
co_channel_t* co_channel_create(size_t elem_size, size_t capacity);
int co_channel_send(co_channel_t* ch, const void* data);
int co_channel_recv(co_channel_t* ch, void* data);
void co_channel_close(co_channel_t* ch);
void co_channel_destroy(co_channel_t* ch);
```

### 5. Memory（内存管理）

#### 5.1 Stack Pool

```c
typedef struct co_stack_pool co_stack_pool_t;

// 创建栈池
co_stack_pool_t* co_stack_pool_create(size_t stack_size, size_t initial_capacity);

// 分配栈
void* co_stack_pool_alloc(co_stack_pool_t* pool);

// 释放栈
void co_stack_pool_free(co_stack_pool_t* pool, void* stack);

// 销毁栈池
void co_stack_pool_destroy(co_stack_pool_t* pool);
```

#### 5.2 Custom Allocator

```c
typedef struct co_allocator {
    void* (*malloc_fn)(size_t size, void* userdata);
    void* (*realloc_fn)(void* ptr, size_t size, void* userdata);
    void (*free_fn)(void* ptr, void* userdata);
    void* userdata;
} co_allocator_t;

// 设置全局分配器
void co_set_allocator(const co_allocator_t* allocator);
```

## 平台抽象层

### 目录结构

```
src/
├── platform/
│   ├── platform.h          # 平台抽象接口
│   ├── linux/
│   │   ├── context.c       # Linux 上下文实现
│   │   ├── iomux_epoll.c   # epoll 实现
│   │   └── timer.c
│   ├── windows/
│   │   ├── context.c       # Windows Fiber 实现
│   │   ├── iomux_iocp.c    # IOCP 实现
│   │   └── timer.c
│   └── macos/
│       ├── context.c       # macOS 上下文实现
│       ├── iomux_kqueue.c  # kqueue 实现
│       └── timer.c
```

### 编译时选择

```c
// platform.h
#if defined(__linux__)
    #include "platform/linux/context.h"
    #include "platform/linux/iomux.h"
#elif defined(_WIN32)
    #include "platform/windows/context.h"
    #include "platform/windows/iomux.h"
#elif defined(__APPLE__)
    #include "platform/macos/context.h"
    #include "platform/macos/iomux.h"
#else
    #error "Unsupported platform"
#endif
```

## 线程安全

### 设计原则

1. **默认单线程**：每个 scheduler 绑定到一个线程
2. **可选的多线程支持**：通过多个 scheduler 实现
3. **线程本地存储**：使用 TLS 存储当前 scheduler

```c
// 获取当前线程的调度器
co_scheduler_t* co_scheduler_self(void);

// 获取当前协程
co_routine_t* co_self(void);
```

### Work-Stealing（可选）

```c
// 配置多调度器
typedef struct co_group_config {
    int num_schedulers;              // 调度器数量
    int enable_work_stealing;        // 是否启用工作窃取
    co_sched_policy_t policy;
} co_group_config_t;

// 调度器组
co_group_t* co_group_create(const co_group_config_t* config);
void co_group_destroy(co_group_t* group);

// 在组中创建协程（自动负载均衡）
co_routine_t* co_group_create_routine(co_group_t* group, 
                                      co_entry_func_t entry, 
                                      void* arg);
```

## 错误处理

### 错误码定义

```c
typedef enum co_error {
    CO_OK            =   0,
    CO_ERROR         =  -1,   // 通用错误
    CO_ERROR_NOMEM   =  -2,   // 内存不足
    CO_ERROR_INVAL   =  -3,   // 无效参数
    CO_ERROR_PLATFORM=  -4,   // 平台特定错误
    CO_ERROR_TIMEOUT =  -5,   // 超时
    CO_ERROR_CANCELLED= -6,   // 已取消
    CO_ERROR_BUSY    =  -7,   // 资源暂时不可用（锁已被占用）
} co_error_t;

// 获取错误描述
const char* co_strerror(co_error_t err);

// 获取最后一个错误（线程本地）
co_error_t co_last_error(void);
```

## 性能考虑

### 1. Fast Path 优化

```c
// 内联关键函数
static inline co_routine_t* co_self(void) {
    extern __thread co_scheduler_t* __tls_current_scheduler;
    return __tls_current_scheduler ? __tls_current_scheduler->current : NULL;
}
```

### 2. 零拷贝

- 参数通过寄存器或栈传递
- Channel 使用环形缓冲区
- 避免不必要的内存拷贝

### 3. 内存局部性

- 将热数据放在同一缓存行
- 使用对象池减少碎片

### 4. 分支预测

```c
#ifdef __GNUC__
    #define likely(x)   __builtin_expect(!!(x), 1)
    #define unlikely(x) __builtin_expect(!!(x), 0)
#else
    #define likely(x)   (x)
    #define unlikely(x) (x)
#endif
```

## 可观测性

### Debug Mode

```c
// 编译时开关
#ifdef CO_DEBUG
    #define CO_LOG(level, fmt, ...) \
        co_log_internal(level, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#else
    #define CO_LOG(level, fmt, ...) ((void)0)
#endif
```

### Statistics

```c
typedef struct co_stats {
    uint64_t total_switches;
    uint64_t total_routines_created;
    uint64_t total_routines_destroyed;
    uint64_t active_routines;
    uint64_t peak_routines;
    uint64_t total_sleep_time_us;
} co_stats_t;

// 获取统计信息
void co_scheduler_get_stats(co_scheduler_t* sched, co_stats_t* stats);
```

### Tracing

```c
// 钩子函数（可选）
typedef void (*co_trace_fn_t)(const char* event, co_routine_t* co, void* userdata);

void co_set_trace_callback(co_trace_fn_t callback, void* userdata);
```

## 下一步

参见：
- [02-api-design.md](./02-api-design.md) - 详细 API 设计
- [03-implementation.md](./03-implementation.md) - 实现细节
- [04-testing.md](./04-testing.md) - 测试策略
