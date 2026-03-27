# libcoxx C++ 封装设计草案

> **状态**：草案 v0.1 — 待评审确认后实施（Week 12）  
> **日期**：2026-03-25  
> **前置依赖**：Week 9（Mutex/Cond）、Week 10（Channel）均已完成

---

## 1. 设计目标

| 目标 | 说明 |
|------|------|
| **零开销抽象** | 不引入运行时分配（lambda 捕获除外），RAII 保证资源安全 |
| **风格一致** | 接口设计向 C++ 标准库（`<mutex>`、`<condition_variable>`）靠拢，降低学习成本 |
| **可组合** | `Mutex` 满足 `BasicLockable`，直接兼容 `std::lock_guard` / `std::unique_lock` |
| **显式错误** | 不滥用异常；协程体内异常由框架捕获后在 `Task::join()` 处重新抛出 |
| **可迭代 Channel** | 支持 range-for 遍历，对齐 Go 的 `for v := range ch` 语义 |

---

## 2. 参考调研

### 2.1 Go sync 包

Go 是影响最广的协程模型，libco 的 Channel 设计本身已经直接对标 Go。以下是 Go 对本次 C++ 封装的启发：

| Go 概念 | 对应 C++ 封装 |
|---|---|
| `go func()` | `Scheduler::spawn(lambda)` → `Task` |
| `sync.Mutex` | `co::Mutex`（BasicLockable） |
| `sync.WaitGroup` | `co::WaitGroup`（可纯 C++ 实现，无需新增 C 层） |
| `chan T`（带/不带缓冲） | `co::Channel<T>` |
| `select { case ... }` | `co::select()` 函数（草案中列为"待定"，见第 7 节） |
| `for v := range ch` | `Channel<T>::iterator` + range-for |
| `sync.Once` | `std::once_flag` + `std::call_once`（标准库已有，不重复造轮子） |

**Go 中没有的概念，但值得借鉴其他语言：**

---

### 2.2 Rust / Tokio

Tokio 是目前工程质量最高的异步运行时之一，关键设计参考：

| Tokio 概念 | 启发 |
|---|---|
| `tokio::spawn(async {})` → `JoinHandle<T>` | spawn 应返回可等待的**任务句柄**，而不是 void |
| `JoinHandle::await` / `.abort()` | `Task::join()` / `Task::cancel()` |
| `JoinHandle` drop = detach | `Task` 析构默认 detach（或 join，两种流派均有，见第 6.1 节） |
| `tokio::sync::Mutex<T>` 包裹数据 | libco 走"锁不包裹数据"路线（与 `std::mutex` 一致），不照搬 |
| `tokio::sync::mpsc::channel()` → `(Sender, Receiver)` | channel 拆分为读写两端（见第 5.5 节，列为可选优化） |
| `tokio::sync::Notify` | 类似 `CondVar` 但更轻量；Go 的 `sync.WaitGroup` 可以代替部分场景 |
| `tokio::sync::Semaphore` | `co::Semaphore`（见第 5.4 节，Week 12 优先级低，可留后续） |

---

### 2.3 cppcoro（Lewis Baker）

cppcoro 是基于 C++20 `co_await` 的无栈协程库，与 libco（有栈协程）的模型不同，但接口设计值得借鉴：

| cppcoro 概念 | 启发 |
|---|---|
| `cppcoro::task<T>` 返回泛型结果 | `Task` 初期可以不带 `T`（void），后续可扩展为 `Task<T>` |
| `when_all(tasks...)` | `TaskGroup` / 结构化并发（中长期目标） |
| `generator<T>` | 超出本期范围 |

---

### 2.4 Kotlin 协程

Kotlin 的结构化并发（Structured Concurrency）是目前最完善的模型之一：

| Kotlin 概念 | 启发 |
|---|---|
| `CoroutineScope` + `launch` | `Scheduler`（run 结束前等所有 coroutine 完成，行为已一致） |
| `async { }.await()` → Deferred<T> | `Task::join()` + 异常传播 |
| `Channel<T>` closed 后 for-each 自动终止 | `Channel<T>::iterator::operator!=` 检查 closed 状态 |
| `Mutex.withLock { }` | `std::lock_guard<co::Mutex>` 已满足 |
| `supervisorScope` | 暂不实现（异常隔离域） |

---

### 2.5 Python asyncio

asyncio 的接口最贴近教学级使用，适合参考"应该避免的设计"：

- `asyncio.get_event_loop()` 全局状态 → libco 显式传入 `scheduler`，**正确避免了这个问题**
- `asyncio.Queue` ≈ `co::Channel<T>`（有界、无界、put/get）

---

## 3. 与上一版设计的主要修订

| 内容 | 上一版建议 | 本草案修订 | 理由 |
|------|-----------|-----------|------|
| `spawn()` 返回值 | 未提及（隐含 void） | 返回 `Task`（可 join/detach/cancel） | 对标 Rust JoinHandle、Kotlin Job；join 是安全编程的基础 |
| lambda 传递方式 | `std::function<void()>*` 手动 new | 模板 + placement-new 到堆（类似 `std::thread`） | 消除 `std::function` 的虚调用与多余 allocation |
| WaitGroup | 跳过（需要 C 层新机制）| **纯 C++ 实现**（用 `co::Mutex` + `co::CondVar`） | 完全可在 C++ 层实现，无需修改 C 层 |
| `Task` 析构行为 | 未讨论 | 析构 = **detach**（不阻塞，与 `std::thread` 的 terminate-on-drop 不同） | 协程比线程轻量，detach 语义更自然；join 由用户显式调用 |
| `Channel<T>` 迭代 | 未提及 | 提供 `begin()`/`end()`，支持 range-for | Go 的 `range ch` 是最常见用法，易用性大幅提升 |
| 异常传播 | try/catch 在包装里（丢弃异常）| 捕获后存入 `Task`，`join()` 时 re-throw | 符合 `std::thread` 中对 `std::packaged_task` 的惯例 |

---

## 4. 头文件组织

```
libcoxx/
└── include/
    └── coxx/
        ├── coxx.hpp          ← 一键包含所有子头文件
        ├── scheduler.hpp     ← Scheduler + Task
        ├── sync.hpp          ← Mutex, CondVar, WaitGroup
        └── channel.hpp       ← Channel<T>
```

命名空间统一使用 `co`（与 C 层的 `co_` 前缀呼应，短而清晰）：

```cpp
namespace co {
    class Scheduler;
    class Task;
    class Mutex;
    class CondVar;
    class WaitGroup;
    template<typename T> class Channel;
}
```

---

## 5. API 设计

### 5.1 `Scheduler` + `Task`

#### 设计要点

- `Scheduler` 是 RAII 对象，析构时调用 `co_scheduler_destroy()`
- `spawn()` 是函数模板，接受任意可调用对象（lambda / 函数指针 / 仿函数）
- `spawn()` 返回 `Task`（不可复制，可移动）
- `Task` 析构 = detach；`Task::join()` 阻塞直到协程结束（或抛出其存储的异常）

```cpp
// scheduler.hpp
#pragma once
#include <functional>
#include <exception>
#include <libco/co.h>

namespace co {

class Task {
public:
    Task() noexcept = default;
    Task(Task&&) noexcept;
    Task& operator=(Task&&) noexcept;
    ~Task();                         // detach（不阻塞）

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    bool valid() const noexcept;     // 句柄是否持有协程
    void join();                     // 等待完成；若协程抛出异常则在此 re-throw
    void detach() noexcept;          // 显式 detach，之后 valid() == false
    void cancel() noexcept;          // 发出取消请求（best-effort）

private:
    friend class Scheduler;
    explicit Task(co_routine_t* routine) noexcept;

    co_routine_t* routine_ = nullptr;
    std::exception_ptr exception_;   // 协程体内的异常（由 trampoline 填写）
};

// ── Scheduler ────────────────────────────────────────────────────────────────

class Scheduler {
public:
    explicit Scheduler(void* config = nullptr);
    ~Scheduler();                    // 调用 co_scheduler_destroy()

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    /**
     * @brief 启动协程
     *
     * @tparam F  可调用类型（lambda、函数指针、仿函数）
     * @param  fn 协程体；函数签名为 void()
     * @param  stack_size 栈大小（0=默认）
     * @return Task 句柄
     *
     * @example
     *   auto t = sched.spawn([&]{ do_work(); });
     *   t.join();
     */
    template<typename F>
    Task spawn(F&& fn, size_t stack_size = 0);

    /**
     * @brief 运行调度器直到所有协程结束
     */
    void run();

    co_scheduler_t* native_handle() noexcept { return sched_; }

private:
    co_scheduler_t* sched_;
};

} // namespace co
```

#### 实现要点：lambda 传递

使用 **trampoline 函数 + 堆分配包装**，避免 `std::function` 的虚调用：

```cpp
template<typename F>
Task Scheduler::spawn(F&& fn, size_t stack_size) {
    // 类型擦除：将 F 包装为堆对象，trampoline 负责调用并捕获异常
    struct Wrapper {
        F fn;
        std::exception_ptr* ex;   // 指向 Task 内的 exception_
    };
    auto* w = new Wrapper{std::forward<F>(fn), /* 稍后绑定 */};

    auto trampoline = [](void* arg) noexcept {
        auto* w = static_cast<Wrapper*>(arg);
        try {
            w->fn();
        } catch (...) {
            *w->ex = std::current_exception();
        }
        delete w;
    };

    co_routine_t* r = co_spawn(sched_, trampoline, w, stack_size);
    Task t(r);
    w->ex = &t.exception_;   // 绑定异常存储指针
    return t;
}
```

> **注意**：`trampoline` 标记 `noexcept`，确保异常不会穿透到 C 层的调度器。

---

### 5.2 `Mutex`

满足 C++ 具名要求 `BasicLockable`，可直接用于 `std::lock_guard<co::Mutex>`：

```cpp
// sync.hpp（片段）
class Mutex {
public:
    Mutex();
    ~Mutex();

    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    void lock();       // 阻塞直到获得锁（让出 CPU 给其他协程）
    void unlock();
    bool try_lock();   // 非阻塞尝试，失败返回 false

    co_mutex_t* native_handle() noexcept { return mutex_; }

private:
    co_mutex_t* mutex_;
};
```

**使用示例**（与标准库完全兼容）：

```cpp
co::Mutex mtx;

sched.spawn([&]{
    std::lock_guard<co::Mutex> lg(mtx);
    // 临界区
});
```

---

### 5.3 `CondVar`

对标 `std::condition_variable`，接受 `std::unique_lock<co::Mutex>`：

```cpp
class CondVar {
public:
    CondVar();
    ~CondVar();

    CondVar(const CondVar&) = delete;
    CondVar& operator=(const CondVar&) = delete;

    void wait(std::unique_lock<Mutex>& lock);

    template<typename Predicate>
    void wait(std::unique_lock<Mutex>& lock, Predicate pred) {
        while (!pred()) wait(lock);
    }

    // 返回 true=条件满足，false=超时
    bool wait_for(std::unique_lock<Mutex>& lock, uint32_t timeout_ms);

    template<typename Predicate>
    bool wait_for(std::unique_lock<Mutex>& lock, uint32_t timeout_ms, Predicate pred);

    void notify_one();   // 对应 co_cond_signal
    void notify_all();   // 对应 co_cond_broadcast

    co_cond_t* native_handle() noexcept { return cond_; }

private:
    co_cond_t* cond_;
};
```

---

### 5.4 `WaitGroup`

Go `sync.WaitGroup` 的对应实现。**完全在 C++ 层实现，不需要修改 C 层。**

```cpp
class WaitGroup {
public:
    WaitGroup() = default;

    WaitGroup(const WaitGroup&) = delete;
    WaitGroup& operator=(const WaitGroup&) = delete;

    /**
     * @brief 增加计数（在 spawn 之前调用）
     * @param delta 增量（通常为 1）
     */
    void add(int delta = 1);

    /**
     * @brief 减少计数（在协程末尾调用）
     * 计数归零时自动唤醒所有 wait() 等待者
     */
    void done();

    /**
     * @brief 阻塞直到计数归零
     */
    void wait();

private:
    int count_ = 0;
    Mutex mtx_;
    CondVar cv_;
};
```

**实现逻辑**（全部在 `.hpp` 或 `sync.cpp` 中，不依赖新 C API）：

```cpp
void WaitGroup::add(int delta) {
    std::lock_guard<Mutex> lg(mtx_);
    count_ += delta;
}

void WaitGroup::done() {
    std::lock_guard<Mutex> lg(mtx_);
    if (--count_ == 0) cv_.notify_all();
}

void WaitGroup::wait() {
    std::unique_lock<Mutex> ul(mtx_);
    cv_.wait(ul, [this]{ return count_ == 0; });
}
```

---

### 5.5 `Channel<T>`

#### 接口设计

```cpp
// channel.hpp
template<typename T>
class Channel {
public:
    /**
     * @param capacity 缓冲区大小（0 = unbuffered rendezvous channel）
     */
    explicit Channel(size_t capacity = 0);
    ~Channel();

    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    // ── 发送端 ──────────────────────────────────────────────

    /**
     * @brief 发送值（阻塞直到对端接收或缓冲区有空间）
     * @return true=成功, false=channel 已关闭（值未被接收）
     */
    bool send(const T& value);
    bool send(T&& value);

    /**
     * @brief 非阻塞发送
     * @return true=已放入缓冲区, false=缓冲区满或 channel 已关闭
     */
    bool try_send(const T& value);

    /**
     * @brief 关闭 channel（之后 send 返回 false，recv 耗尽后返回 nullopt）
     */
    void close();

    // ── 接收端 ──────────────────────────────────────────────

    /**
     * @brief 接收值（阻塞）
     * @return 值；channel 关闭且缓冲区为空时返回 std::nullopt
     */
    std::optional<T> recv();

    /**
     * @brief 非阻塞接收
     * @return 值；无可用值时返回 std::nullopt（含关闭状态）
     */
    std::optional<T> try_recv();

    // ── 状态查询 ─────────────────────────────────────────────

    size_t len() const;        // 当前缓冲区中的元素数
    size_t capacity() const;   // 缓冲区容量
    bool   is_closed() const;

    // ── 迭代支持（range-for）────────────────────────────────

    struct iterator {
        Channel<T>* ch;
        std::optional<T> current;

        iterator& operator++() { current = ch->recv(); return *this; }
        T& operator*() { return *current; }
        bool operator!=(std::default_sentinel_t) const { return current.has_value(); }
    };

    iterator begin() { return iterator{this, recv()}; }
    std::default_sentinel_t end() { return {}; }

    co_channel_t* native_handle() noexcept { return ch_; }

private:
    co_channel_t* ch_;
};
```

**迭代使用示例**（对标 Go 的 `for v := range ch`）：

```cpp
co::Channel<int> ch(16);

for (int v : ch) {       // ch.close() 后循环自动退出
    process(v);
}
```

---

## 6. 关键技术决策

### 6.1 `Task` 析构行为：detach vs join

| 行为 | 优点 | 缺点 |
|------|------|------|
| 析构 = **detach**（本草案选择） | 协程轻量，detach 开销低；与 goroutine 语义一致 | 用户可能忘记 join，异常被静默丢弃 |
| 析构 = **join**（std::jthread 路线） | 更安全，自动等待 | 析构时阻塞（在协程上下文中析构 Task 可能死锁） |
| 析构 = **terminate**（std::thread 旧行为） | 纪律最严格 | 过于严苛，用户体验差 |

**结论**：选择 **detach**。libco 的应用场景下（Web 服务、事件驱动），大多数协程是 fire-and-forget 的，强制 join 反而不便。需要 join 的场景用 `Task::join()` 或 `WaitGroup` 显式表达。

---

### 6.2 异常传播策略

```
协程体 → 抛出异常
    │
    ▼
trampoline 的 catch(...) 捕获
    │
    ▼
存入 Task::exception_（std::exception_ptr）
    │
    ▼
Task::join() 调用时：std::rethrow_exception(exception_)
    │
    ▼
Task::~Task()（detach 路径）：异常被静默丢弃（与 goroutine panic 隔离语义不同，需文档说明）
```

**建议**：提供可选的全局"未处理异常回调"：

```cpp
namespace co {
    using UncaughtExceptionHandler = std::function<void(std::exception_ptr)>;
    void set_uncaught_exception_handler(UncaughtExceptionHandler h);
}
```

---

### 6.3 线程安全性声明

libco 调度器是**单线程**的，所有 `co::` 类只在同一个调度器线程内操作。文档中需明确：

> `co::Mutex` / `co::CondVar` / `co::Channel<T>` **不是** `std::mutex` 的替代品，不保护多线程并发访问；它们保护的是同一调度器内的多协程并发访问。

---

### 6.4 `native_handle()` 逃生舱口

每个封装类都提供 `native_handle()` 直接访问底层 C 指针，避免封装层无法表达某些高级 C API 时的困境（参考 `std::thread::native_handle()`）。

---

### 6.5 C++ 标准要求

| 特性 | 最低标准 |
|------|---------|
| `std::optional<T>` | C++17 |
| `std::default_sentinel_t` | C++20 |
| `std::exception_ptr` | C++11 |
| 模板折叠表达式等 | C++17 |

**结论**：要求 **C++17**（`std::default_sentinel_t` 用兼容替代即可，不强依赖 C++20）。

替代 `std::default_sentinel_t` 的写法（C++17 兼容）：

```cpp
// 用哨兵类型代替 std::default_sentinel_t
struct ChannelEndSentinel {};

bool operator!=(const iterator& it, ChannelEndSentinel) {
    return it.current.has_value();
}
```

---

## 7. 暂不实现（Out of Scope for Week 12）

| 功能 | 理由 | 后续规划 |
|------|------|---------|
| `Channel` 读写端拆分（`Sender<T>` / `Receiver<T>`） | 需要引用计数或 `shared_ptr`，增加复杂性 | Week 13+ |
| `co::select()` / `co::Select` | 需要 C 层 `co_select()` 先实现（已在 02-api-design.md 规划） | Phase 6 |
| `Task<T>`（有返回值的协程） | 当前 `co_spawn` 不支持返回值，需 C 层改造 | Phase 6 |
| 结构化并发 `TaskGroup` / `co::scope` | 语义复杂（生命周期传播、异常聚合） | Phase 6 |
| `co::Semaphore` | 可以纯 C++ 实现，但 Week 12 优先级低 | 如有需要可随时加 |
| `co::RWMutex` | C 层尚未实现 `co_rwmutex_t` | 后续 |

---

## 8. 文件清单（实施阶段）

| 文件 | 内容 |
|------|------|
| `libcoxx/include/coxx/coxx.hpp` | 总包含头 |
| `libcoxx/include/coxx/scheduler.hpp` | `Scheduler`、`Task` 声明 |
| `libcoxx/include/coxx/sync.hpp` | `Mutex`、`CondVar`、`WaitGroup` |
| `libcoxx/include/coxx/channel.hpp` | `Channel<T>`（模板，全部在头文件） |
| `libcoxx/src/scheduler.cpp` | `Scheduler`、`Task` 实现（非模板部分） |
| `libcoxx/src/sync.cpp` | `Mutex`、`CondVar` 实现 |
| `libcoxx/CMakeLists.txt` | 更新：添加 src/*.cpp，C++17 要求 |
| `tests/cxx/test_coxx.cpp` | Google Test 单元测试（全部组件）|
| `examples/cxx/demo_channel.cpp` | Channel + range-for 示例 |
| `examples/cxx/demo_scheduler.cpp` | Scheduler + WaitGroup 示例 |

---

## 9. 测试覆盖计划

| 测试名 | 覆盖内容 |
|--------|---------|
| `test_scheduler_spawn_join` | spawn + join，验证执行顺序 |
| `test_scheduler_spawn_detach` | detach 后 run() 正常退出 |
| `test_task_exception_propagation` | 协程体抛异常 → join() re-throw |
| `test_mutex_basic` | lock/unlock 互斥 |
| `test_mutex_lock_guard` | 与 `std::lock_guard` 组合 |
| `test_condvar_wait_notify` | wait + notify_one |
| `test_condvar_wait_for_timeout` | wait_for 超时 |
| `test_waitgroup_basic` | add/done/wait |
| `test_channel_send_recv` | 基础收发 |
| `test_channel_close_range_for` | close 后 range-for 自动退出 |
| `test_channel_optional_closed` | recv() 在关闭后返回 nullopt |
| `test_channel_trysend_tryrecv` | 非阻塞变体 |
