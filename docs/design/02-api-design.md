# API 设计文档

## 设计原则

1. **命名一致性**：`<prefix>_<object>_<action>` 格式
2. **最小惊讶原则**：API 行为符合直觉
3. **资源管理清晰**：明确的创建和销毁配对
4. **错误处理统一**：返回错误码，避免 errno
5. **版本兼容性**：保持 ABI 稳定

## 核心 API

### 头文件组织

```c
#include <libco/co.h>           // 核心 API
#include <libco/co_sched.h>     // 调度器
#include <libco/co_sync.h>      // 同步原语（可选）
#include <libco/co_runtime.h>   // 运行时功能（可选）
```

### 1. 初始化和配置

```c
/**
 * @brief 全局库初始化（可选）
 * @return CO_OK on success, error code on failure
 */
co_error_t co_init(void);

/**
 * @brief 全局库清理（可选）
 */
void co_deinit(void);

/**
 * @brief 设置全局内存分配器
 * @param allocator 自定义分配器，NULL 表示使用默认
 */
void co_set_allocator(const co_allocator_t* allocator);

/**
 * @brief 分配器结构
 */
typedef struct co_allocator {
    void* (*malloc_fn)(size_t size, void* userdata);
    void* (*realloc_fn)(void* ptr, size_t size, void* userdata);
    void (*free_fn)(void* ptr, void* userdata);
    void* userdata;
} co_allocator_t;
```

### 2. 调度器 API

```c
/**
 * @brief 调度器配置
 */
typedef struct co_sched_config {
    size_t max_routines;         // 最大协程数，0 = 无限制
    size_t default_stack_size;   // 默认栈大小（字节），0 = 使用默认值
    co_allocator_t* allocator;   // 自定义分配器，NULL = 使用全局
    int enable_hooks;            // 是否启用系统调用 hook
    int flags;                   // 保留标志位
} co_sched_config_t;

/**
 * @brief 创建调度器
 * @param config 配置，NULL 表示使用默认配置
 * @return 调度器句柄，失败返回 NULL
 */
co_scheduler_t* co_scheduler_create(const co_sched_config_t* config);

/**
 * @brief 销毁调度器（会终止所有协程）
 * @param sched 调度器句柄
 */
void co_scheduler_destroy(co_scheduler_t* sched);

/**
 * @brief 运行调度器（阻塞直到所有协程结束）
 * @param sched 调度器句柄
 * @return CO_OK on success, error code on failure
 */
co_error_t co_scheduler_run(co_scheduler_t* sched);

/**
 * @brief 运行调度器一次迭代（非阻塞）
 * @param sched 调度器句柄
 * @param timeout_ms 最大等待时间（毫秒），-1 表示无限等待
 * @return CO_OK on success, error code on failure
 */
co_error_t co_scheduler_poll(co_scheduler_t* sched, int timeout_ms);

/**
 * @brief 停止调度器
 * @param sched 调度器句柄
 */
void co_scheduler_stop(co_scheduler_t* sched);

/**
 * @brief 获取当前线程的调度器
 * @return 调度器句柄，如果不在协程上下文中返回 NULL
 */
co_scheduler_t* co_scheduler_self(void);
```

### 3. 协程 API

```c
/**
 * @brief 协程入口函数类型
 */
typedef void (*co_entry_func_t)(void* arg);

/**
 * @brief 创建并启动协程（spawn）
 * @param sched 调度器句柄，NULL 表示使用当前线程的调度器
 * @param entry 入口函数
 * @param arg 传递给入口函数的参数
 * @param stack_size 栈大小，0 表示使用默认值
 * @return 协程句柄，失败返回 NULL
 */
co_routine_t* co_spawn(co_scheduler_t* sched,
                       co_entry_func_t entry,
                       void* arg,
                       size_t stack_size);

/**
 * @brief 让出 CPU（切换到其他协程）
 * @return CO_OK on success, error code on failure
 * @note C 代码可以使用 co_yield() 宏别名（定义为 #define co_yield co_yield_now）
 */
co_error_t co_yield_now(void);

/**
 * @brief 休眠指定毫秒数
 * @param msec 毫秒数
 * @return CO_OK on success, error code on failure
 */
co_error_t co_sleep(uint32_t msec);

/**
 * @brief 获取当前协程
 * @return 协程句柄，如果不在协程上下文中返回 NULL
 */
co_routine_t* co_self(void);

/**
 * @brief 设置协程名称（用于调试）
 * @param co 协程句柄
 * @param name 名称字符串（会被复制）
 */
void co_routine_set_name(co_routine_t* co, const char* name);

/**
 * @brief 获取协程名称
 * @param co 协程句柄
 * @return 名称字符串，未设置则返回 NULL
 */
const char* co_routine_name(co_routine_t* co);

/**
 * @brief 获取协程 ID
 * @param co 协程句柄
 * @return 唯一 ID
 */
uint64_t co_routine_id(co_routine_t* co);

/**
 * @brief 获取协程状态
 * @param co 协程句柄
 * @return 协程状态
 */
co_state_t co_routine_state(co_routine_t* co);

/**
 * @brief 等待协程结束并获取结果（await）
 * @param co 协程句柄
 * @param result 结果结构，可为 NULL
 * @param timeout_ms 超时时间（毫秒），-1 表示无限等待
 * @return CO_OK on success, CO_ERROR_TIMEOUT on timeout
 */
co_error_t co_await(co_routine_t* co, co_result_t* result, int timeout_ms);

/**
 * @brief 取消协程
 * @param co 协程句柄
 * @return CO_OK on success, error code on failure
 */
co_error_t co_routine_cancel(co_routine_t* co);

/**
 * @brief 分离协程（协程结束后自动释放资源）
 * @param co 协程句柄
 * @return CO_OK on success, error code on failure
 */
co_error_t co_routine_detach(co_routine_t* co);

/**
 * @brief 协程返回结果
 * @param value 返回值指针
 * @return 不返回（函数会导致协程结束）
 */
void co_return(void* value);

/**
 * @brief 协程结果结构
 */
typedef struct co_result {
    void* value;          // 返回值
    co_error_t error;     // 错误码
    int completed;        // 是否完成
    int cancelled;        // 是否被取消
} co_result_t;
```

### 4. I/O API（可选，需要启用）

```c
/**
 * @brief I/O 事件类型
 */
typedef enum co_io_event {
    CO_IO_READ = 0x01,
    CO_IO_WRITE = 0x02,
    CO_IO_ERROR = 0x04
} co_io_event_t;

/**
 * @brief 等待文件描述符就绪
 * @param fd 文件描述符
 * @param events 要等待的事件
 * @param timeout_ms 超时时间（毫秒），-1 表示无限等待
 * @return 就绪的事件，超时返回 0，错误返回负值
 */
int co_io_wait(int fd, co_io_event_t events, int timeout_ms);

/**
 * @brief 协程友好的 read
 * @note 只有启用 hooks 时才可用
 */
ssize_t co_read(int fd, void* buf, size_t count);

/**
 * @brief 协程友好的 write
 * @note 只有启用 hooks 时才可用
 */
ssize_t co_write(int fd, const void* buf, size_t count);
```

### 5. 同步原语 API

#### 5.1 互斥锁

```c
/**
 * @brief 创建互斥锁
 * @param attr 预留扩展参数（当前传 NULL，未来用于递归锁等属性）
 * @return 互斥锁句柄，失败返回 NULL
 */
co_mutex_t* co_mutex_create(const void* attr);

/**
 * @brief 销毁互斥锁
 * @param mutex 互斥锁句柄
 * @return CO_OK 成功，CO_ERROR_INVAL 参数无效
 * @note 销毁时必须确保没有协程正在等待此锁
 */
co_error_t co_mutex_destroy(co_mutex_t* mutex);

/**
 * @brief 锁定互斥锁（会挂起当前协程）
 * @param mutex 互斥锁句柄
 * @return CO_OK on success, error code on failure
 */
co_error_t co_mutex_lock(co_mutex_t* mutex);

/**
 * @brief 尝试锁定互斥锁（非阻塞）
 * @param mutex 互斥锁句柄
 * @return CO_OK 成功，CO_ERROR_BUSY 锁已被占用
 */
co_error_t co_mutex_trylock(co_mutex_t* mutex);

/**
 * @brief 解锁互斥锁
 * @param mutex 互斥锁句柄
 * @return CO_OK on success, error code on failure
 */
co_error_t co_mutex_unlock(co_mutex_t* mutex);
```

#### 5.2 条件变量

```c
/**
 * @brief 创建条件变量
 * @param attr 预留扩展参数（当前传 NULL）
 * @return 条件变量句柄，失败返回 NULL
 */
co_cond_t* co_cond_create(const void* attr);

/**
 * @brief 销毁条件变量
 * @param cond 条件变量句柄
 * @return CO_OK 成功，CO_ERROR_INVAL 参数无效
 */
co_error_t co_cond_destroy(co_cond_t* cond);

/**
 * @brief 等待条件变量
 * @param cond 条件变量句柄
 * @param mutex 关联的互斥锁
 * @return CO_OK on success, error code on failure
 */
co_error_t co_cond_wait(co_cond_t* cond, co_mutex_t* mutex);

/**
 * @brief 带超时的等待
 * @param cond 条件变量句柄
 * @param mutex 关联的互斥锁
 * @param timeout_ms 相对等待时长（毫秒，从调用时刻起计算）
 * @return CO_OK 成功，CO_ERROR_TIMEOUT 超时
 */
co_error_t co_cond_timedwait(co_cond_t* cond, 
                             co_mutex_t* mutex, 
                             uint32_t timeout_ms);

/**
 * @brief 唤醒一个等待的协程
 * @param cond 条件变量句柄
 * @return CO_OK on success, error code on failure
 */
co_error_t co_cond_signal(co_cond_t* cond);

/**
 * @brief 唤醒所有等待的协程
 * @param cond 条件变量句柄
 * @return CO_OK on success, error code on failure
 */
co_error_t co_cond_broadcast(co_cond_t* cond);
```

#### 5.3 Channel（Go 风格）

```c
/**
 * @brief 创建 channel
 * @param elem_size 元素大小（字节）
 * @param capacity 容量，0 表示无缓冲 channel
 * @return channel 句柄，失败返回 NULL
 */
co_channel_t* co_channel_create(size_t elem_size, size_t capacity);

/**
 * @brief 销毁 channel
 * @param ch channel 句柄
 */
void co_channel_destroy(co_channel_t* ch);

/**
 * @brief 发送数据到 channel
 * @param ch channel 句柄
 * @param data 数据指针
 * @return CO_OK on success, CO_ERROR_CLOSED if closed
 */
co_error_t co_channel_send(co_channel_t* ch, const void* data);

/**
 * @brief 从 channel 接收数据
 * @param ch channel 句柄
 * @param data 数据缓冲区
 * @return CO_OK on success, CO_ERROR_CLOSED if closed
 */
co_error_t co_channel_recv(co_channel_t* ch, void* data);

/**
 * @brief 尝试发送（非阻塞）
 * @param ch channel 句柄
 * @param data 数据指针
 * @return CO_OK on success, CO_ERROR_BUSY if full
 */
co_error_t co_channel_trysend(co_channel_t* ch, const void* data);

/**
 * @brief 尝试接收（非阻塞）
 * @param ch channel 句柄
 * @param data 数据缓冲区
 * @return CO_OK on success, CO_ERROR_BUSY if empty
 */
co_error_t co_channel_tryrecv(co_channel_t* ch, void* data);

/**
 * @brief 关闭 channel
 * @param ch channel 句柄
 */
void co_channel_close(co_channel_t* ch);

/**
 * @brief 获取 channel 当前元素数量
 * @param ch channel 句柄
 * @return 当前元素数量
 */
size_t co_channel_len(co_channel_t* ch);

/**
 * @brief 获取 channel 容量
 * @param ch channel 句柄
 * @return 容量
 */
size_t co_channel_cap(co_channel_t* ch);

/**
 * @brief 检查 channel 是否已关闭
 * @param ch channel 句柄
 * @return 1 表示已关闭，0 表示未关闭
 */
int co_channel_is_closed(co_channel_t* ch);
```

#### 5.4 Select（多路等待）

```c
/**
 * @brief Select case 类型
 */
typedef enum co_select_op {
    CO_SELECT_SEND,     // 发送操作
    CO_SELECT_RECV,     // 接收操作
    CO_SELECT_DEFAULT   // 默认分支
} co_select_op_t;

/**
 * @brief Select case 结构
 */
typedef struct co_select_case {
    co_select_op_t op;       // 操作类型
    co_channel_t* channel;   // channel 句柄
    void* data;              // 数据指针（发送时为源，接收时为目标）
    int selected;            // 是否被选中（输出参数）
} co_select_case_t;

/**
 * @brief 多路等待（类似 Go 的 select）
 * @param cases case 数组
 * @param count case 数量
 * @param timeout_ms 超时时间（毫秒），-1 表示无限等待
 * @return 被选中的 case 索引，超时返回 -1
 * 
 * @example
 * co_select_case_t cases[] = {
 *     { CO_SELECT_RECV, ch1, &data1, 0 },
 *     { CO_SELECT_SEND, ch2, &data2, 0 },
 *     { CO_SELECT_DEFAULT, NULL, NULL, 0 }  // 非阻塞
 * };
 * int selected = co_select(cases, 3, 1000);
 */
int co_select(co_select_case_t* cases, size_t count, int timeout_ms);
```

#### 5.5 协程本地存储（TLS）

```c
/**
 * @brief TLS key 类型
 */
typedef struct co_tls_key co_tls_key_t;

/**
 * @brief TLS 析构函数
 */
typedef void (*co_tls_destructor_t)(void* value);

/**
 * @brief 创建 TLS key
 * @param key key 指针
 * @param destructor 析构函数，协程结束时调用，可为 NULL
 * @return CO_OK on success, error code on failure
 */
co_error_t co_tls_key_create(co_tls_key_t** key, co_tls_destructor_t destructor);

/**
 * @brief 删除 TLS key
 * @param key TLS key
 */
void co_tls_key_delete(co_tls_key_t* key);

/**
 * @brief 设置 TLS 值
 * @param key TLS key
 * @param value 值
 * @return CO_OK on success, error code on failure
 */
co_error_t co_tls_set(co_tls_key_t* key, void* value);

/**
 * @brief 获取 TLS 值
 * @param key TLS key
 * @return 值，未设置返回 NULL
 */
void* co_tls_get(co_tls_key_t* key);
```

#### 5.6 WaitGroup

```c
/**
 * @brief 创建 WaitGroup
 * @return WaitGroup 句柄，失败返回 NULL
 */
co_waitgroup_t* co_waitgroup_create(void);

/**
 * @brief 销毁 WaitGroup
 * @param wg WaitGroup 句柄
 */
void co_waitgroup_destroy(co_waitgroup_t* wg);

/**
 * @brief 增加计数器
 * @param wg WaitGroup 句柄
 * @param delta 增加的数量
 */
void co_waitgroup_add(co_waitgroup_t* wg, int delta);

/**
 * @brief 减少计数器
 * @param wg WaitGroup 句柄
 */
void co_waitgroup_done(co_waitgroup_t* wg);

/**
 * @brief 等待计数器归零
 * @param wg WaitGroup 句柄
 * @return CO_OK on success, error code on failure
 */
co_error_t co_waitgroup_wait(co_waitgroup_t* wg);
```

### 6. 错误处理 API

```c
/**
 * @brief 错误码定义
 */
typedef enum co_error {
    CO_OK             =   0,
    CO_ERROR          =  -1,   // 通用错误
    CO_ERROR_NOMEM    =  -2,   // 内存不足
    CO_ERROR_INVAL    =  -3,   // 无效参数
    CO_ERROR_PLATFORM =  -4,   // 平台特定错误
    CO_ERROR_TIMEOUT  =  -5,   // 超时
    CO_ERROR_CANCELLED=  -6,   // 已取消
    CO_ERROR_BUSY     =  -7,   // 资源暂时不可用（如 trylock 时锁已被占用）
} co_error_t;

/**
 * @brief 获取错误描述
 * @param err 错误码
 * @return 错误描述字符串
 */
const char* co_strerror(co_error_t err);

/**
 * @brief 获取最后一个错误（线程本地）
 * @return 错误码
 */
co_error_t co_last_error(void);
```

### 7. 统计和调试 API

```c
/**
 * @brief 统计信息
 */
typedef struct co_stats {
    uint64_t total_switches;            // 总切换次数
    uint64_t total_routines_created;    // 总创建协程数
    uint64_t total_routines_destroyed;  // 总销毁协程数
    uint64_t active_routines;           // 当前活跃协程数
    uint64_t peak_routines;             // 峰值协程数
    uint64_t ready_routines;            // 就绪队列协程数
    uint64_t waiting_routines;          // 等待队列协程数
} co_stats_t;

/**
 * @brief 获取调度器统计信息
 * @param sched 调度器句柄
 * @param stats 统计信息结构
 */
void co_scheduler_get_stats(co_scheduler_t* sched, co_stats_t* stats);

/**
 * @brief 设置日志级别
 * @param level 日志级别 (0=OFF, 1=ERROR, 2=WARN, 3=INFO, 4=DEBUG)
 */
void co_set_log_level(int level);

/**
 * @brief 设置日志回调
 * @param callback 回调函数
 * @param userdata 用户数据
 */
typedef void (*co_log_callback_t)(int level, const char* msg, void* userdata);
void co_set_log_callback(co_log_callback_t callback, void* userdata);
```

## 使用示例

### 基础示例

```c
#include <libco/co.h>
#include <stdio.h>

void task_func(void* arg) {
    const char* name = (const char*)arg;
    for (int i = 0; i < 5; i++) {
        printf("%s: %d\n", name, i);
        co_yield_now();
    }
}

int main(void) {
    // 创建调度器
    co_scheduler_t* sched = co_scheduler_create(NULL);
    
    // 创建两个协程
    co_spawn(sched, task_func, "Task-A", 0);
    co_spawn(sched, task_func, "Task-B", 0);
    
    // 运行调度器
    co_scheduler_run(sched);
    
    // 清理
    co_scheduler_destroy(sched);
    
    return 0;
}
```

### 生产者-消费者示例

```c
#include <libco/co.h>
#include <libco/co_sync.h>
#include <stdio.h>

typedef struct {
    co_channel_t* ch;
    int count;
} task_arg_t;

void producer(void* arg) {
    task_arg_t* ta = (task_arg_t*)arg;
    for (int i = 0; i < ta->count; i++) {
        printf("Producing: %d\n", i);
        co_channel_send(ta->ch, &i);
        co_sleep(10);
    }
    co_channel_close(ta->ch);
}

void consumer(void* arg) {
    task_arg_t* ta = (task_arg_t*)arg;
    int value;
    while (co_channel_recv(ta->ch, &value) == CO_OK) {
        printf("Consuming: %d\n", value);
        co_sleep(20);
    }
}

int main(void) {
    co_scheduler_t* sched = co_scheduler_create(NULL);
    co_channel_t* ch = co_channel_create(sizeof(int), 10);
    
    task_arg_t arg = { ch, 20 };
    
    co_spawn(sched, producer, &arg, 0);
    co_spawn(sched, consumer, &arg, 0);
    
    co_scheduler_run(sched);
    
    co_channel_destroy(ch);
    co_scheduler_destroy(sched);
    
    return 0;
}
```

### Echo 服务器示例

```c
#include <libco/co.h>
#include <libco/co_runtime.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <unistd.h>

void handle_connection(void* arg) {
    int fd = *(int*)arg;
    char buf[1024];
    
    while (1) {
        // 协程友好的 read
        ssize_t n = co_read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        
        // 协程友好的 write
        co_write(fd, buf, n);
    }
    
    close(fd);
}

void server(void* arg) {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(8080),
        .sin_addr.s_addr = INADDR_ANY
    };
    
    bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(listen_fd, 128);
    
    printf("Server listening on port 8080...\n");
    
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);
        
        // 等待连接
        co_io_wait(listen_fd, CO_IO_READ, -1);
        int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &len);
        
        if (client_fd >= 0) {
            // 为每个连接创建一个协程
            co_spawn(NULL, handle_connection, &client_fd, 0);
        }
    }
}

int main(void) {
    // 启用系统调用 hook
    co_sched_config_t config = {
        .enable_hooks = 1
    };
    
    co_scheduler_t* sched = co_scheduler_create(&config);
    co_spawn(sched, server, NULL, 0);
    co_scheduler_run(sched);
    co_scheduler_destroy(sched);
    
    return 0;
}
```

### Join 和 Cancel 示例

```c
#include <libco/co.h>
#include <stdio.h>

void worker(void* arg) {
    int id = *(int*)arg;
    printf("Worker %d starting...\n", id);
    
    for (int i = 0; i < 10; i++) {
        printf("Worker %d: step %d\n", id, i);
        co_sleep(100);
    }
    
    printf("Worker %d done\n", id);
    // 返回结果
    int* result = malloc(sizeof(int));
    *result = id * 100;
    co_return(result);
}

int main(void) {
    co_scheduler_t* sched = co_scheduler_create(NULL);
    
    int id1 = 1, id2 = 2;
    co_routine_t* co1 = co_spawn(sched, worker, &id1, 0);
    co_routine_t* co2 = co_spawn(sched, worker, &id2, 0);
    
    // 在另一个协程中等待
    co_spawn(sched, [](void* arg) {
        co_routine_t** routines = (co_routine_t**)arg;
        
        // 等待第一个协程
        co_result_t result;
        if (co_await(routines[0], &result, 5000) == CO_OK) {
            printf("Worker 1 result: %d\n", *(int*)result.value);
            free(result.value);
        }
        
        // 取消第二个协程
        co_routine_cancel(routines[1]);
    }, (co_routine_t*[]){co1, co2}, 0);
    
    co_scheduler_run(sched);
    co_scheduler_destroy(sched);
    
    return 0;
}
```

### Select 多路等待示例

```c
#include <libco/co.h>
#include <libco/co_sync.h>
#include <stdio.h>

void select_example(void* arg) {
    co_channel_t* ch1 = co_channel_create(sizeof(int), 0);
    co_channel_t* ch2 = co_channel_create(sizeof(int), 0);
    
    // 启动发送者
    co_spawn(NULL, [](void* arg) {
        co_channel_t* ch = (co_channel_t*)arg;
        co_sleep(500);
        int value = 42;
        co_channel_send(ch, &value);
    }, ch1, 0);
    
    co_spawn(NULL, [](void* arg) {
        co_channel_t* ch = (co_channel_t*)arg;
        co_sleep(300);
        int value = 99;
        co_channel_send(ch, &value);
    }, ch2, 0);
    
    // Select 等待
    int data1, data2;
    co_select_case_t cases[] = {
        { CO_SELECT_RECV, ch1, &data1, 0 },
        { CO_SELECT_RECV, ch2, &data2, 0 },
    };
    
    int selected = co_select(cases, 2, 1000);
    
    if (selected == 0) {
        printf("Received from ch1: %d\n", data1);
    } else if (selected == 1) {
        printf("Received from ch2: %d\n", data2);
    } else {
        printf("Timeout\n");
    }
    
    co_channel_destroy(ch1);
    co_channel_destroy(ch2);
}

int main(void) {
    co_scheduler_t* sched = co_scheduler_create(NULL);
    co_spawn(sched, select_example, NULL, 0);
    co_scheduler_run(sched);
    co_scheduler_destroy(sched);
    return 0;
}
```

### 协程组示例

```c
#include <libco/co.h>
#include <stdio.h>

void parallel_task(void* arg) {
    int id = *(int*)arg;
    printf("Task %d running\n", id);
    co_sleep(100 * id);
    printf("Task %d done\n", id);
}

int main(void) {
    co_scheduler_t* sched = co_scheduler_create(NULL);
    co_group_t* group = co_group_create();
    
    // 创建多个协程并加入组
    for (int i = 0; i < 5; i++) {
        int* id = malloc(sizeof(int));
        *id = i;
        co_routine_t* co = co_spawn(sched, parallel_task, id, 0);
        co_group_add(group, co);
    }
    
    // 在主协程中等待所有任务完成
    co_spawn(sched, [](void* arg) {
        co_group_t* group = (co_group_t*)arg;
        printf("Waiting for all tasks...\n");
        co_group_wait(group, -1);
        printf("All tasks completed!\n");
    }, group, 0);
    
    co_scheduler_run(sched);
    co_group_destroy(group);
    co_scheduler_destroy(sched);
    
    return 0;
}
```

## C++ 扩展 API

```cpp
#include <libcoxx/coxx.hpp>

namespace cxx_co {

// RAII 调度器
class Scheduler {
public:
    explicit Scheduler(const co_sched_config_t* config = nullptr);
    ~Scheduler();
    
    // 创建协程（支持 lambda）
    template<typename Func>
    void spawn(Func&& func);
    
    // 运行调度器
    co_error_t run();
    void stop();
};

// 便利函数
inline void yield() { co_yield_now(); }
inline void sleep(uint32_t ms) { co_sleep(ms); }

// RAII 互斥锁
class Mutex {
public:
    Mutex();
    ~Mutex();
    void lock();
    void unlock();
    bool try_lock();
};

class LockGuard {
public:
    explicit LockGuard(Mutex& mtx);
    ~LockGuard();
};

// 类型安全的 Channel
template<typename T>
class Channel {
public:
    explicit Channel(size_t capacity = 0);
    ~Channel();
    
    co_error_t send(const T& value);
    co_error_t recv(T& value);
    void close();
};

} // namespace cxx_co

// 使用示例
int main() {
    cxx_co::Scheduler sched;
    
    sched.spawn([] {
        for (int i = 0; i < 5; i++) {
            std::cout << "Task " << i << std::endl;
            cxx_co::yield();
        }
    });
    
    sched.run();
    return 0;
}
```

## 高级功能

### 协程组管理

```c
/**
 * @brief 协程组（用于批量管理协程）
 */
typedef struct co_group co_group_t;

/**
 * @brief 创建协程组
 * @return 协程组句柄，失败返回 NULL
 */
co_group_t* co_group_create(void);

/**
 * @brief 销毁协程组
 * @param group 协程组句柄
 */
void co_group_destroy(co_group_t* group);

/**
 * @brief 添加协程到组
 * @param group 协程组句柄
 * @param co 协程句柄
 * @return CO_OK on success, error code on failure
 */
co_error_t co_group_add(co_group_t* group, co_routine_t* co);

/**
 * @brief 等待组中所有协程结束
 * @param group 协程组句柄
 * @param timeout_ms 超时时间（毫秒），-1 表示无限等待
 * @return CO_OK on success, error code on failure
 */
co_error_t co_group_wait(co_group_t* group, int timeout_ms);

/**
 * @brief 取消组中所有协程
 * @param group 协程组句柄
 * @return CO_OK on success, error code on failure
 */
co_error_t co_group_cancel(co_group_t* group);
```

### 调试和诊断

```c
/**
 * @brief 获取协程栈使用信息
 */
typedef struct co_stack_info {
    size_t total_size;       // 总大小
    size_t used_size;        // 已使用大小
    size_t peak_size;        // 峰值大小
    float usage_percent;     // 使用百分比
} co_stack_info_t;

/**
 * @brief 获取协程栈信息
 * @param co 协程句柄
 * @param info 输出信息
 * @return CO_OK on success, error code on failure
 */
co_error_t co_get_stack_info(co_routine_t* co, co_stack_info_t* info);

/**
 * @brief 转储协程信息到文件
 * @param co 协程句柄
 * @param fp 文件指针
 */
void co_dump_routine(co_routine_t* co, FILE* fp);

/**
 * @brief 转储调度器所有协程信息
 * @param sched 调度器句柄
 * @param fp 文件指针
 */
void co_dump_all(co_scheduler_t* sched, FILE* fp);

/**
 * @brief 检测死锁
 * @param sched 调度器句柄
 * @return 1 表示检测到死锁，0 表示正常
 */
int co_detect_deadlock(co_scheduler_t* sched);
```

## 下一步

参见：
- [03-implementation.md](./03-implementation.md) - 实现细节
- [04-testing.md](./04-testing.md) - 测试策略
