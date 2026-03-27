# libco  高性能 C/C++ 协程库

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Version](https://img.shields.io/badge/version-2.0.0-green.svg)](CHANGELOG.md)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-lightgrey.svg)]()

> [!NOTE]
> **项目背景**
>
> 本项目脱胎于作者十余年前未竟的旧项目
> [Lucifer-YU/coroutine](https://github.com/Lucifer-YU/coroutine)，
> 由 AI 全程重新设计并实现，人工参与以审核代码和确定关键决策为主。

> 一个现代化、跨平台、高性能的 C17 stackful 协程库，附带完整的 C++17 封装。

---

##  特性

- **高性能**：上下文切换约 1050 ns（x86_64），协程创建 < 1 μs
- **纯 C17 核心**：无外部依赖，可嵌入任何 C/C++ 项目
- **跨平台**：Linux（ucontext + epoll）、Windows（Fiber + IOCP）
- **Stackful**：可在任意调用深度挂起和恢复，无需 `async/await` 传染
- **完整同步原语**：Mutex、CondVar（含超时）、Channel（带缓冲 + rendezvous）
- **C++17 封装**（libcoxx）：RAII Scheduler/Task、Mutex、CondVar、WaitGroup、Channel\<T\>
- **协程式 I/O**：co_read / co_write / co_accept / co_connect（非阻塞，挂起当前协程）
- **定时器**：co_sleep，最小堆调度，精度约 1 ms
- **栈池**：复用栈内存，减少 mmap/VirtualAlloc 开销

---

## 快速开始

### 依赖

| 工具 | 最低版本 |
|------|----------|
| CMake | 3.15 |
| GCC / Clang | C17 支持 |
| MSVC | VS 2019+ |

### 构建

```bash
git clone https://github.com/yourname/libco.git
cd libco
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

### Hello Coroutine（C）

```c
#include <libco/co.h>
#include <stdio.h>

static void task(void *arg) {
    const char *name = (const char *)arg;
    for (int i = 0; i < 3; i++) {
        printf("%s: %d\n", name, i);
        co_yield();   /* 让出 CPU，调度器切换到下一个协程 */
    }
}

int main(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    co_spawn(sched, task, "A", 0);
    co_spawn(sched, task, "B", 0);
    co_scheduler_run(sched);
    co_scheduler_destroy(sched);
}
```

输出（A/B 交替执行）：
```
A: 0
B: 0
A: 1
B: 1
A: 2
B: 2
```

### Hello Coroutine（C++）

```cpp
#include <coxx/coxx.hpp>
#include <cstdio>

int main() {
    co::Scheduler sched;

    co::WaitGroup wg;
    for (int i = 0; i < 3; i++) {
        sched.spawn([i, &wg] {
            std::printf("worker %d done\n", i);
            wg.done();
        });
        wg.add();
    }

    sched.spawn([&] { wg.wait(); });
    sched.run();
}
```

---

## API 速查

### C API（`<libco/co.h>`）

```c
/* 调度器 */
co_scheduler_t *co_scheduler_create(void *config);   // config 传 NULL 使用默认值
co_error_t      co_scheduler_run(co_scheduler_t *);
void            co_scheduler_destroy(co_scheduler_t *);

/* 协程 */
co_routine_t *co_spawn(co_scheduler_t *, co_entry_func_t, void *arg, size_t stack_size);
co_error_t    co_yield_now(void);   // C 代码可用宏别名 co_yield()
co_error_t    co_sleep(uint32_t msec);

/* 同步（<libco/co_sync.h>） */
co_mutex_t   *co_mutex_create(NULL);
co_error_t    co_mutex_lock(co_mutex_t *);
co_error_t    co_mutex_unlock(co_mutex_t *);
co_error_t    co_mutex_trylock(co_mutex_t *);  // 非阻塞，失败返回 CO_ERROR_BUSY

co_cond_t    *co_cond_create(NULL);
co_error_t    co_cond_wait(co_cond_t *, co_mutex_t *);
co_error_t    co_cond_timedwait(co_cond_t *, co_mutex_t *, uint32_t timeout_ms);
co_error_t    co_cond_signal(co_cond_t *);
co_error_t    co_cond_broadcast(co_cond_t *);

co_channel_t *co_channel_create(size_t elem_size, size_t capacity);  // capacity=0: rendezvous
co_error_t    co_channel_send(co_channel_t *, const void *);
co_error_t    co_channel_recv(co_channel_t *, void *);
co_error_t    co_channel_trysend(co_channel_t *, const void *);
co_error_t    co_channel_tryrecv(co_channel_t *, void *);
co_error_t    co_channel_close(co_channel_t *);

/* 协程式 I/O */
ssize_t    co_read(co_socket_t fd, void *buf, size_t n, int64_t timeout_ms);
ssize_t    co_write(co_socket_t fd, const void *buf, size_t n, int64_t timeout_ms);
co_socket_t co_accept(co_socket_t, void *addr, socklen_t *, int64_t timeout_ms);
int        co_connect(co_socket_t, const void *addr, socklen_t, int64_t timeout_ms);
```

### C++ API（`<coxx/coxx.hpp>`，需链接 `coxx`）

```cpp
namespace co {
    // Scheduler
    class Scheduler {
        template<typename F>
        Task spawn(F&& fn, size_t stack_size = 0);
        void run();                // 启动主循环
    };

    // Task
    class Task {
        void join();               // 等待完成，重抛协程内异常
        void detach() noexcept;    // 分离（析构语义亦为 detach）
        bool valid() const;
    };

    // 同步
    class Mutex;       // 满足 BasicLockable，兼容 std::lock_guard
    class CondVar;     // wait / wait_for(ms) / wait(pred) / notify_one / notify_all
    class WaitGroup;   // add(n) / done() / wait()

    // Channel<T>（T 必须 trivially copyable）
    template<typename T>
    class Channel {
        explicit Channel(size_t capacity = 0);
        bool send(const T &);
        std::optional<T> recv();
        bool try_send(const T &);
        std::optional<T> try_recv();
        void close();
        // 支持 range-for（close() 后遍历剩余元素）
    };
}
```

---

## 项目结构

```
libco/
 libco/
    include/libco/    # 公开头文件：co.h, co_sync.h, config.h
    src/
        co_routine.c  # 协程生命周期
        co_scheduler.c
        co_timer.c    # 最小堆定时器
        co_stack_pool.c
        sync/         # co_mutex.c, co_cond.c, co_channel.c
        platform/
            linux/    # ucontext + epoll
            windows/  # Fiber + IOCP

 libcoxx/              # C++17 封装
    include/coxx/     # scheduler.hpp, sync.hpp, channel.hpp, coxx.hpp
    src/              # scheduler.cpp, sync.cpp

 tests/
    unit/             # Unity (C) + GTest (C++) 单元测试
    integration/      # 集成测试（生产者消费者等）

 benchmarks/           # 性能基准
    bench_context_switch.c
    bench_spawn.c
    bench_channel.c
    bench_stress.c    # 10K 协程压力测试

 examples/
     demo_scheduler.c
     demo_concurrent.c
     demo_producer_consumer.c
     demo_stack_pool.c
     demo_echo_server.c
     demo_echo_client.c
     demo_coxx.cpp     # C++ API 综合演示
```

---

## 错误码

| 错误码 | 值 | 含义 |
|--------|----|------|
| `CO_OK` | 0 | 成功 |
| `CO_ERROR` | -1 | 通用错误 |
| `CO_ERROR_NOMEM` | -2 | 内存不足 |
| `CO_ERROR_INVAL` | -3 | 无效参数 |
| `CO_ERROR_PLATFORM` | -4 | 平台错误 |
| `CO_ERROR_TIMEOUT` | -5 | 超时 |
| `CO_ERROR_CANCELLED` | -6 | 已取消 |
| `CO_ERROR_BUSY` | -7 | 资源暂不可用（锁已被占用） |
| `CO_ERROR_CLOSED` | -8 | Channel 已关闭 |

---

## 构建选项

| CMake 选项 | 默认 | 说明 |
|------------|------|------|
| `LIBCO_BUILD_TESTS` | ON | 构建单元/集成测试 |
| `LIBCO_BUILD_EXAMPLES` | ON | 构建示例程序 |
| `LIBCO_BUILD_BENCHMARKS` | OFF | 构建性能基准 |
| `LIBCO_BUILD_COXX` | ON | 构建 C++ 封装 libcoxx |
| `LIBCO_ENABLE_ASAN` | OFF | 开启 AddressSanitizer |
| `LIBCO_ENABLE_COVERAGE` | OFF | 开启代码覆盖率 |

---

## 设计文档

| 文档 | 内容 |
|------|------|
| [00-overview.md](docs/design/00-overview.md) | 项目定位与目标 |
| [01-architecture.md](docs/design/01-architecture.md) | 整体架构 |
| [02-api-design.md](docs/design/02-api-design.md) | API 详细设计 |
| [03-implementation.md](docs/design/03-implementation.md) | 关键算法与数据结构 |
| [04-testing.md](docs/design/04-testing.md) | 测试策略 |
| [05-build.md](docs/design/05-build.md) | 构建系统 |
| [06-roadmap.md](docs/design/06-roadmap.md) | 开发路线图 |

---

## License

[MIT](LICENSE)
