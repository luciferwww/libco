# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [2.0.0]  2026-03-27

### 里程碑概述

从头重写的现代化 C17 stackful 协程库，完整支持 Linux 和 Windows，
附带 C++17 封装（libcoxx）。

---

### Added

#### 核心库（libco）

**上下文切换（Week 3）**
- Linux：基于 `ucontext_t` 的上下文切换（`platform/linux/context.c`）
- Windows：基于 `Fiber` 的上下文切换（`platform/windows/context.c`）
- 跨平台统一接口：`co_context_init` / `co_context_swap`

**调度器（Week 4）**
- `co_scheduler_create` / `co_scheduler_run` / `co_scheduler_destroy`
- FIFO 就绪队列（双向链表，O(1) 入队出队）
- `co_spawn`：创建并入队协程
- `co_yield_now`：主动让出 CPU（C 代码可用 `co_yield` 宏别名）

**栈管理（Week 5）**
- `co_stack_pool`：栈内存复用池，减少 mmap / VirtualAlloc 开销
- 自定义分配器接口：`co_allocator_t` / `co_set_allocator`

**定时器与休眠（Week 6）**
- 最小堆定时器：`co_timer_heap`（push O(log n)，pop O(log n)，remove O(n)）
- `co_sleep(ms)`：挂起当前协程，到期后自动加回就绪队列

**I/O 多路复用（Week 78）**
- Linux：epoll（`platform/linux/iomux_epoll.c`）
- Windows：IOCP + `GetQueuedCompletionStatusEx` 批量处理（`platform/windows/iomux_iocp.c`）
- 协程式 I/O API：`co_read` / `co_write` / `co_accept` / `co_connect`
- `waiting_io_count` 计数器，空闲时精确控制 poll 超时

**同步原语（Week 9）**
- `co_mutex_t`：协程级互斥锁，FIFO 等待队列，锁转交语义（unlock 直接转给队首）
- `co_cond_t`：条件变量，`co_cond_wait` / `co_cond_timedwait` / `co_cond_signal` / `co_cond_broadcast`
- 非协程上下文（调度器启动前）可直接获取 mutex，兼容预锁场景

**Channel（Week 10）**
- `co_channel_t`：Go 风格有界 Channel，`capacity=0` 为无缓冲 rendezvous
- `co_channel_send` / `co_channel_recv`（阻塞）
- `co_channel_trysend` / `co_channel_tryrecv`（非阻塞）
- `co_channel_close`：关闭后唤醒所有等待方，返回 `CO_ERROR_CLOSED`
- 错误码 `CO_ERROR_CLOSED = -8`

**性能基准（Week 13）**
- `bench_context_switch`：测量上下文切换延迟（目标 < 50 ns）
- `bench_spawn`：测量协程创建开销（目标 < 1 μs）
- `bench_channel`：测量 Channel 吞吐量（目标 > 10 M ops/s）
- `bench_stress`：10K 协程并发压力测试

#### C++17 封装（libcoxx，Week 12）

- `co::Scheduler`：RAII 调度器，`spawn<F>()` 模板返回 `co::Task`
- `co::Task`：协程句柄，`join()` 等待完成（重抛异常），`detach()` 分离
- `co::Mutex`：满足 `BasicLockable`，兼容 `std::lock_guard` / `std::unique_lock`
- `co::CondVar`：`wait` / `wait_for(ms)` / 谓词版本 / `notify_one` / `notify_all`
- `co::WaitGroup`：`add(n)` / `done()` / `wait()`，纯 C++ 实现
- `co::Channel<T>`：类型安全封装，`static_assert(trivially_copyable)`，支持 range-for

---

### Fixed

**Debug 模式隐藏 Bug（发现于 Debug 测试阶段）**

- **`co_timer.c` heap_push 断言失败**  
  `heap_sift_up` 在 `count++` 之前调用，导致 `assert(idx < heap->count)` 必然失败。  
  修复：先记录 `insert_idx = heap->count`，`count++` 后再调用 `sift_up(insert_idx)`。

- **`co_mutex.c` 非协程上下文 unlock 断言失败**  
  从主线程（调度器启动前）调用 `co_mutex_lock` 时返回 `CO_ERROR_INVAL` 未获取锁，
  随后 `unlock` 断言 `mutex->locked` 失败。  
  修复：非协程上下文直接获取锁（`locked=true`），已占用返回 `CO_ERROR_BUSY`。

- **`co_cond_timedwait` signal 唤醒后悬空定时器导致调度器永久挂起**  
  `co_cond_signal` 提前唤醒协程后，定时器仍留在 `timer_heap`；
  协程执行完毕被 `free` 后，Debug 模式 MSVC 填充 `0xDD`，
  `wakeup_time = 0xDDDDDDDDDDDDDDDD`，调度器计算出天文数字等待时间冻结。  
  修复：新增 `co_timer_heap_remove`（O(n) + re-heapify），
  `co_cond_timedwait` signal 唤醒路径调用移除悬空定时器。

---

### Changed

- **`co_sched_yield`  `co_yield_now`**：函数名更贴近用途，避免与 POSIX `sched_yield` 混淆，
  C 代码的 `co_yield` 宏别名不变（`#define co_yield co_yield_now`）。
- **CMake 编译选项改用生成器表达式**：修复 MSVC 多配置生成器下
  `Debug` 构建混入 `/O2` 导致 `/RTC1` 冲突的问题。
- **`-Werror=implicit-function-declaration` 限定为仅 C 文件**（`$<$<COMPILE_LANGUAGE:C>:...>`），
  消除 C++ 编译单元的无效警告。

---

### Test Coverage

| 测试套件 | 平台 | 结果 |
|----------|------|------|
| 单元测试（48项） | Windows Debug |  48/48 |
| 单元测试（48项） | Linux Debug   |  48/48 |

---

## [Unreleased  Design Only]  2026-03-23

初始设计阶段文档（详见 `docs/design/`）。无可运行代码。

---

[2.0.0]: https://github.com/yourname/libco/releases/tag/v2.0.0
