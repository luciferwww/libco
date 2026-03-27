# 实施路线图

## 总体规划

**目标**：在 16 周内完成 libco 2.0 的开发和发布（优化设计后）

**原始计划**：14 周  
**设计优化后**：16 周（+2 周）

**增加时间用于**：
- 协程生命周期管理（join、cancel、detach）
- Select 多路等待实现
- 协程本地存储（TLS）
- 调试和诊断工具

**原则**：
1. 增量开发，每个阶段都有可运行的成果
2. 测试驱动，确保代码质量
3. 文档先行，设计清晰明确
4. 持续集成，自动化测试

> **注**：基于设计评审，已移除向后兼容性约束，采用更现代的设计。详见 [IMPROVEMENTS.md](./IMPROVEMENTS.md)

## Phase 1: 基础设施 (2 周)

### Week 1: 项目搭建

**目标**：搭建完整的项目框架

- [ ] 创建目录结构
  ```
  libco/
  ├── libco/
  ├── libcoxx/
  ├── tests/
  ├── examples/
  ├── docs/
  └── tools/
  ```

- [ ] 编写根 CMakeLists.txt
  - 设置 C/C++ 标准
  - 配置编译选项
  - 平台检测逻辑

- [ ] 创建配置文件 config.h.in

- [ ] 编写 README.md
  - 项目简介
  - 快速开始
  - 编译说明

- [ ] 设置 .gitignore

- [ ] 选择许可证 (建议 MIT)

**验收标准**：
- ✅ 项目可以在 Linux/macOS/Windows 上配置成功
- ✅ CMake 生成构建文件无错误

### Week 2: CI/CD 和测试框架

**目标**：建立自动化测试和持续集成

- [ ] 集成 Unity 测试框架
  - 添加为子模块或使用 FetchContent
  - 创建示例测试 tests/unit/test_example.c

- [ ] 集成 Google Test (C++ 部分)
  - 创建示例测试 tests/unit/test_example.cpp

- [ ] 配置 GitHub Actions
  - Linux (GCC, Clang)
  - macOS (Clang)
  - Windows (MSVC)

- [ ] 添加代码格式化
  - .clang-format 配置
  - 格式化检查脚本

- [ ] 添加静态分析
  - cppcheck 或 clang-tidy

**验收标准**：
- ✅ CI 流水线在所有平台上绿色通过
- ✅ 示例测试运行成功

**里程碑 1**：✨ 项目框架完成

---

## Phase 2: 核心实现 (5 周)

> **注**：优化设计后增加 1 周，用于实现协程生命周期管理（join、cancel、detach）

### Week 3: 上下文切换

**目标**：实现跨平台的上下文切换

- [ ] Linux 实现
  - src/platform/linux/context.c (ucontext)
  - 单元测试 tests/unit/test_context_linux.c

- [ ] macOS 实现
  - src/platform/macos/context.c (ucontext)
  - 单元测试 tests/unit/test_context_macos.c

- [ ] Windows 实现
  - src/platform/windows/context.c (Fiber)
  - 单元测试 tests/unit/test_context_windows.c

- [ ] 性能基准
  - benchmarks/bench_context_switch.cpp

**验收标准**：
- ✅ 上下文切换在所有平台上工作正常
- ✅ 单元测试覆盖率 > 80%
- ✅ 上下文切换 < 100ns (x86_64)

### Week 4: 调度器基础

**目标**：实现基本的协程调度器

- [ ] 数据结构
  - co_scheduler_t
  - co_routine_t
  - 就绪队列 (双向链表)

- [ ] 核心 API
  - co_scheduler_create()
  - co_scheduler_destroy()
  - co_scheduler_run()
  - co_spawn()
  - co_yield_now()   （C 代码可用 co_yield 宏别名）

- [ ] 单元测试
  - tests/unit/test_scheduler.c
  - tests/unit/test_routine.c

**验收标准**：
- ✅ 可以创建和运行简单协程
- ✅ 多个协程能正确调度
- ✅ 无内存泄漏 (Valgrind 检查)

### Week 5: 栈管理和内存分配

**目标**：实现栈池和自定义分配器

- [ ] 栈池实现
  - src/co_stack_pool.c
  - 栈分配和回收

- [ ] 自定义分配器接口
  - co_allocator_t
  - co_set_allocator()

- [ ] 栈保护页 (可选)
  - 检测栈溢出

- [ ] 单元测试
  - tests/unit/test_stack_pool.c
  - tests/unit/test_allocator.c

**验收标准**：
- ✅ 栈可以复用，减少分配开销
- ✅ 支持自定义分配器
- ✅ 栈溢出能被检测到

### Week 6: 定时器和休眠

**目标**：实现定时器和 co_sleep

- [ ] 定时器堆实现
  - src/co_timer_heap.c
  - 最小堆数据结构

- [ ] co_sleep() 实现
  - 将协程加入睡眠队列
  - 定时唤醒

- [ ] 单元测试
  - tests/unit/test_timer.c
  - tests/unit/test_sleep.c

- [ ] 精度测试
  - 测试休眠精度

**验收标准**：
- ✅ co_sleep() 工作正常
- ✅ 多个协程可以同时休眠
- ✅ 休眠精度 < 10ms

**里程碑 2**：✨ 核心功能完成

---

## Phase 3: 平台完善 (2 周)

### Week 7: I/O 多路复用 - Linux

**目标**：实现 Linux epoll 支持

- [ ] epoll 封装
  - src/platform/linux/iomux_epoll.c
  - co_iomux_t 结构

- [ ] co_io_wait() 实现

- [ ] 单元测试
  - tests/unit/test_iomux_epoll.c

- [ ] 集成测试
  - tests/integration/test_echo_server.c

**验收标准**：
- ✅ 可以等待文件描述符就绪
- ✅ Echo 服务器示例工作正常

### Week 8: I/O 多路复用 - Windows（macOS 待续）

**目标**：完成 Windows 平台的 I/O 支持；macOS kqueue 推迟实现

- [ ] macOS kqueue 实现（尚未实现）
  - src/platform/macos/iomux_kqueue.c

- [x] Windows IOCP 实现（AcceptEx/ConnectEx/ReadFile/WriteFile）
  - src/platform/windows/iomux_iocp.c
  - 使用 `co_context_swap()` 直接切回调度器，不用 `co_yield_now()`
  - 需要 `associated_sockets[]` 跟踪已关联到 IOCP 的套接字，避免重复调用 `CreateIoCompletionPort`

- [x] 跨平台测试（Linux + Windows）
  - Linux epoll + Windows IOCP 均通过 5 并发客户端回归测试

**关键设计决策**：

1. **`co_wait_io()` 必须用 `co_context_swap()`，不能用 `co_yield_now()`**  
   `co_yield_now()` 会无条件将协程加回 `ready_queue`，导致调度器无法感知 WAITING 状态，永远不调用 `co_iomux_poll()`。

2. **调度器增加 `waiting_io_count` 字段**  
   跟踪当前处于 `CO_STATE_WAITING` 状态的协程数量。调度器主循环在 `ready_queue` 为空时，当且仅当 `waiting_io_count > 0` 才调用 `co_iomux_poll()`，否则直接退出。

3. **`co_iomux_poll()` 唤醒协程时通过 `routine->scheduler` 获取调度器**  
   比 `co_current_scheduler()` 语义更精确，不依赖线程局部全局变量，为未来 work-stealing 打基础。

4. **Windows IOCP 改用 `GetQueuedCompletionStatusEx` 实现批量完成包处理**  
   原 `GetQueuedCompletionStatus` 每次只能取出一个完成包，高并发时成为瓶颈。改用 Vista+ 的 `GetQueuedCompletionStatusEx` 后，单次 `co_iomux_poll()` 最多取出 `max_events` 个完成包，与 Linux `epoll_wait` 的批量语义对齐。错误检测从 `!ret` 改为读取 `entries[i].Internal`（NTSTATUS）。`struct co_iomux` 新增 `OVERLAPPED_ENTRY *entries` 批量缓冲区字段。

**验收标准**：
- ⏳ 所有平台的 I/O 测试通过
- ⏳ Echo 服务器在所有平台上工作

**实际进展**：
- ✅ Linux 9/9 单元测试 + 集成测试通过
- ✅ Windows 9/9 单元测试 + 集成测试通过
- ✅ Echo 服务器在 Linux 和 Windows 上均正常工作
- ⏳ macOS kqueue 未实现（需要 macOS 环境），验收标准未完全达成

**里程碑 3**：🔄 平台支持进行中（Linux ✅ + Windows ✅，macOS I/O ⏳）

---

## Phase 4: 运行时功能 (4 周)

> **注**：优化设计后增加 1 周，用于实现 Select、TLS 和调试工具

### Week 9: 互斥锁和条件变量

**目标**：实现基本同步原语

- [x] Mutex 实现
  - src/sync/co_mutex.c
  - co_mutex_lock/unlock/trylock()

- [x] 条件变量实现
  - src/sync/co_cond.c
  - co_cond_wait/timedwait/signal/broadcast()

- [x] 单元测试
  - tests/unit/test_mutex.c
  - tests/unit/test_cond.c

- [x] 集成测试
  - tests/integration/test_producer_consumer.c

**关键设计决策**：

1. **`co_mutex_trylock` 返回 `CO_ERROR_BUSY`，不用 `CO_ERROR_TIMEOUT`**  
   两者语义不同：TIMEOUT 表示等待超时，BUSY 表示资源暂时不可得。调用者逻辑更清晰。

2. **`destroy` 返回 `co_error_t`**  
   与其他 API 统一，NULL 传参可返回 `CO_ERROR_INVAL`，而非触发 assert 崩溃。

3. **`create` 接受 `const void *attr` 预留参数**  
   当前传 NULL 忽略，为未来递归锁等属性保留 ABI 扩展空间。

4. **`co_cond_wait` 原子性由单线程调度器天然保证**  
   释放 mutex 和挂起协程之间不会有其他协程插入，无需额外同步。

5. **`co_mutex_unlock` 直接转交锁给队首等待者**  
   `locked` 保持 `true`，避免在释放和重新获取之间产生竞争窗口。

**验收标准**：
- ✅ 互斥锁正确保护临界区
- ✅ 条件变量正确同步
- ✅ 无死锁

**实际进展**：
- ✅ co_mutex.c：create/destroy/lock/trylock/unlock 全部实现
- ✅ co_cond.c：create/destroy/wait/timedwait/signal/broadcast 全部实现
- ✅ test_mutex.c：4个单元测试通过（basic、fifo_order、trylock、critical_section）
- ✅ test_cond.c：3个单元测试通过（signal、broadcast、no_waiter）
- ✅ test_producer_consumer.c：3个集成测试通过（basic、multi_consumer、full_block）

### Week 10: Channel

**目标**：实现 Go 风格的 Channel

- [x] Channel 实现
  - src/sync/co_channel.c
  - 环形缓冲区（有界）+ rendezvous（无缓冲 capacity=0）
  - send/recv 等待队列，复用协程的 queue_node

- [x] 单元测试
  - tests/unit/test_channel.c
  - 有缓冲 basic、满时阻塞、FIFO 顺序、无缓冲 rendezvous、close 排空、trysend/tryrecv

**关键设计决策**：

1. **等待队列复用 `queue_node`**  
   挂起的协程（WAITING 状态）不在 `ready_queue` 中，`queue_node` 可安全复用给 `send_queue` / `recv_queue`。

2. **`chan_data` 指针临时存数据位置**  
   send 时指向调用者栈上的源数据；recv 时指向调用者的目标缓冲区。底层实现负责 `memcpy`，不存储值本身（零拷贝在发送者栈上）。

3. **send 优先级**：直接交付等待的 receiver → 写缓冲区 → 挂起

4. **recv 优先级**：从缓冲区取 + 唤醒 sender → 直接从 sender 读（rendezvous） → CLOSED → 挂起

5. **close 广播**：唤醒所有等待的 recv/send 协程，它们醒来后读到 `CO_ERROR_CLOSED`

6. **新增 `CO_ERROR_CLOSED = -8`**  
   独立错误码，语义清晰，不与 BUSY/TIMEOUT 混用。

**验收标准**：
- ✅ Channel 发送接收正确
- ✅ 阻塞和非阻塞模式都工作
- ✅ 无缓冲 rendezvous 工作

**实际进展**：
- ✅ co_channel.h + co_channel.c：全部 API 实现（create/destroy/send/recv/trysend/tryrecv/close/len/cap/is_closed）
- ✅ co_routine.h 新增 chan_data 字段
- ✅ co.h 新增 CO_ERROR_CLOSED = -8
- ✅ test_channel.c：6个单元测试在 Windows 和 Linux 上全部通过

### Week 11: 系统调用 Hook (可选)

**目标**：实现透明的系统调用 hook

- [ ] Hook 框架
  - src/hooks/unix/co_hooks.c
  - Hook read/write/connect 等

- [ ] 测试
  - tests/integration/test_hooks.c

- [ ] 示例
  - examples/http_client/

**验收标准**：
- ✅ 阻塞调用能自动让出 CPU
- ✅ 现有阻塞代码无需修改即可协程化

**里程碑 4**：✨ 运行时完成

---

## Phase 5: C++ 扩展 (1 周)

### Week 12: C++ 包装器 ✅

**目标**：提供现代 C++ 接口

- [x] Scheduler 类
  - libcoxx/src/scheduler.cpp
  - RAII 管理

- [x] Lambda 支持
  - spawn() 模板函数

- [x] 同步原语包装
  - Mutex, CondVar, LockGuard
  - Channel<T>
  - WaitGroup

- [x] 单元测试
  - tests/unit/test_coxx.cpp（16 个 GTest 用例，Windows/Linux Debug 全部通过）

- [x] 示例
  - examples/demo_coxx.cpp（7 个场景：Task/WaitGroup/Mutex/Channel/CondVar/异常传播）

**验收标准**：
- ✅ C++ API 使用方便
- ✅ 支持 lambda 和 std::function
- ✅ 异常安全

**里程碑 5**：✨ C++ 扩展完成

---

## Phase 6: 优化和发布 (2 周)

### Week 13: 性能优化

**目标**：达到性能目标

- [ ] 性能基准测试
  - 上下文切换 < 50ns
  - 协程创建 < 1μs
  - Channel 吞吐量 > 10M ops/s

- [ ] 性能瓶颈分析
  - 使用 perf/VTune/Instruments

- [ ] 优化实施
  - 内存对齐优化
  - 缓存友好的数据布局
  - 热路径优化

- [ ] 压力测试
  - 10K 协程测试
  - 长时间运行测试

**验收标准**：
- ✅ 所有性能指标达标
- ✅ 压力测试稳定运行

### Week 14: 文档和发布准备 ✅

**目标**：完善文档，准备发布

- [x] README.md 全面更新
  - 反映实际已实现的 API（co_yield_now、co_spawn、libcoxx 等）
  - 正确的 C/C++ 快速开始示例
  - API 速查表、项目结构、构建选项、错误码表

- [x] CHANGELOG.md 编写
  - v2.0.0 完整发布日志
  - 所有已实现功能（Week 3–13）
  - Debug 阶段修复的三个隐藏 Bug
  - API 重命名记录（co_sched_yield → co_yield_now）
  - 测试覆盖结果

- [x] 示例补充（7 个）
  - demo_scheduler.c、demo_concurrent.c、demo_producer_consumer.c
  - demo_stack_pool.c、demo_echo_server.c、demo_echo_client.c
  - demo_coxx.cpp（C++ API 综合演示，7 个场景）

- [ ] Doxygen 文档生成（待配置）
- [ ] 包管理器集成（vcpkg / Conan，待提交）

**验收标准**：
- ✅ 文档完整且准确
- ✅ 所有示例运行正常
- ⏳ 发布检查清单待最终确认

**里程碑 6**：🎉 版本 2.0.0 发布准备完成

---

## 发布后计划

### v2.1.0 (发布后 1-2 个月)

**功能增强**：
- [ ] 更多同步原语 (Semaphore, RWLock)
- [ ] Select 语句 (多路等待)
- [ ] 协程本地存储 (TLS)
- [ ] 调试工具增强

### v2.2.0 (发布后 3-4 个月)

**性能和可观测性**：
- [ ] 工作窃取调度器 (多线程)
- [ ] 协程分析工具
- [ ] 自适应栈大小
- [ ] 更多平台支持 (FreeBSD, OpenBSD)

### v3.0.0 (长期规划)

**突破性改进**：
- [ ] 汇编优化的上下文切换
- [ ] 零拷贝优化
- [ ] 协程迁移支持
- [ ] 分布式协程 (实验性)

---

## 风险评估和缓解

### 技术风险

| 风险 | 影响 | 概率 | 缓解措施 |
|------|------|------|----------|
| 平台兼容性问题 | 高 | 中 | 早期在所有平台测试，使用 CI |
| 性能不达标 | 中 | 低 | 参考成熟项目，使用性能分析工具 |
| 内存泄漏 | 高 | 低 | 使用 Valgrind/ASan，严格测试 |
| API 设计缺陷 | 高 | 中 | 设计阶段充分讨论，参考最佳实践 |

### 进度风险

| 风险 | 影响 | 概率 | 缓解措施 |
|------|------|------|----------|
| 关键功能延期 | 中 | 中 | 预留缓冲时间，优先核心功能 |
| 测试不充分 | 高 | 低 | 严格的测试覆盖率要求 |
| 文档不完善 | 中 | 中 | 文档与代码同步进行 |

---

## 资源需求

### 人力

- **核心开发**：1 人全职
- **测试**：共享资源
- **文档**：兼职

### 时间

- **总计**：16 周 (约 4 个月) - **优化设计后增加 2 周**
- **每周工作时间**：40 小时
- **时间分配**：
  - Phase 1: 基础设施 (2 周)
  - Phase 2: 核心实现 (5 周) ⬆️ +1 周
  - Phase 3: 平台支持 (2 周)
  - Phase 4: 运行时功能 (4 周) ⬆️ +1 周
  - Phase 5: C++ 扩展 (1 周)
  - Phase 6: 优化和发布 (2 周)

### 工具和基础设施

- **开发环境**：Linux/macOS/Windows
- **CI/CD**：GitHub Actions (免费)
- **测试工具**：Unity, Google Test, Valgrind, ASan (开源免费)
- **性能分析**：perf, Instruments, VTune (免费社区版)

---

## 成功标准检查清单

### 功能性
- [x] 基础协程创建和切换
- [ ] 调度器正常工作
- [ ] 所有平台支持（Linux ✅ + Windows ✅，macOS I/O ⏳）
- [ ] I/O 多路复用
- [ ] 同步原语完整
- [ ] C++ 扩展可用

### 质量
- [ ] 单元测试覆盖率 > 80%
- [ ] 所有平台 CI 绿色
- [ ] 无内存泄漏
- [ ] 性能基准达标
- [ ] 静态分析无严重问题

### 文档
- [ ] API 文档完整
- [ ] 教程清晰易懂
- [ ] 示例代码丰富
- [ ] README 完善

### 发布
- [ ] 版本号正确
- [ ] CHANGELOG 完整
- [ ] 许可证明确
- [ ] 包管理器集成

---

## 下一步行动

1. **评审设计文档**
   - 收集反馈
   - 修订设计

2. **开始 Phase 1**
   - 创建项目结构
   - 设置构建系统

3. **定期同步**
   - 每周检查进度
   - 及时调整计划

**准备好开始了吗？让我们开始实施！** 🚀
