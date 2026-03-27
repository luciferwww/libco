# libco 示例程序

这些示例程序展示了 libco 协程库的核心功能。

## 编译示例

示例程序会自动编译，位于 `build/examples/` 目录：

```bash
# Linux/macOS
cd build-linux
cmake --build .
./examples/demo_producer_consumer

# Windows
cd build
cmake --build . --config Release
.\examples\Release\demo_producer_consumer.exe
```

## 示例说明

### 1. demo_producer_consumer - 生产者-消费者模式

演示多个协程如何协作处理数据：
- 2个生产者协程生产数据
- 2个消费者协程消费数据
- 使用简单的环形队列通信
- 展示 `co_sleep()` 控制执行节奏

**运行：**
```bash
./examples/demo_producer_consumer
```

**预期输出：**
```
=== Producer-Consumer Demo ===
[Producer 1] Produced: 100
[Consumer 1] Consumed: 100
...
✓ All completed
```

---

### 2. demo_concurrent - 并发协程测试

压力测试调度器，验证大量协程并发执行：
- 可配置协程数量
- 每个协程执行多次计数
- 验证计数结果正确性
- 显示性能统计

**运行：**
```bash
# 创建 100 个协程，每个迭代 100 次
./examples/demo_concurrent 100 100
```

**性能参考（Linux）：**
- 50 个协程：**132万** 次上下文切换/秒
- 100 个协程：预计 **100万+** 次切换/秒

---

### 3. demo_scheduler - 定时任务调度

模拟 cron 风格的任务调度器：
- 4个定时任务（不同频率）
- 1个监控协程（定期报告进度）
- 展示 `co_sleep()` 精确定时
- 验证时间精度

**运行：**
```bash
./examples/demo_scheduler
```

**预期：**
- FastTask: 每 50ms 执行一次
- MediumTask: 每 100ms 执行一次  
- SlowTask: 每 200ms 执行一次
- 所有任务准时完成

---

### 4. demo_stack_pool - 栈池性能测试

验证栈内存复用效果：
- 批量创建和销毁协程
- 展示栈池的内存节省
- 性能统计

**运行：**
```bash
# 创建 1000 个协程，每批 100 个
./examples/demo_stack_pool 1000 100
```

**预期效果：**
- 无栈池：1000 个协程 = **125 MB** 内存
- 有栈池（容量16）：峰值 **~2 MB** 内存
- 内存节省：**~98%**

---

### 5. demo_echo_server - TCP Echo 服务器（Week 7）

演示协程式网络 I/O：
- 监听 TCP 端口接受连接
- 每个客户端一个协程
- 使用 `co_accept()`, `co_read()`, `co_write()`
- 并发处理多个客户端

**运行：**
```bash
# 启动服务器（监听 8080 端口）
./examples/demo_echo_server 8080

# 在另一个终端测试连接
telnet localhost 8080
# 或
echo "Hello libco!" | nc localhost 8080
```

**功能：**
- 接收客户端发送的数据
- 原样返回（echo）
- 30秒超时自动断开
- 显示连接统计

**平台支持：**
- ✅ Linux：epoll 完整实现
- ✅ Windows：IOCP 异步 I/O 完整实现
- ⚠️ macOS：kqueue 待实现（计划中）

---

## 性能基准

### Linux (GCC, Release, x86_64)

| 示例 | 指标 | 性能 |
|------|------|------|
| concurrent (50协程) | 上下文切换 | 132万/秒 |
| stack_pool (100协程) | 每协程耗时 | 0.029 ms |
| scheduler | 定时精度 | < 5 ms |
| bench_context_switch | 单次切换延迟 | ~10–50 ns |
| bench_spawn | 协程创建开销 | < 1 μs |
| bench_channel (cap=256) | 吞吐量 | > 10 M ops/s |

### Windows (MSVC, Release, x86_64)

| 示例 | 指标 | 性能 |
|------|------|------|
| concurrent (50协程) | 上下文切换 | 4.7万/秒 |
| producer_consumer | 总耗时 | ~0.3 秒 |

> 完整 benchmark 数据见 `benchmarks/` 目录，使用 `-DLIBCO_BUILD_BENCHMARKS=ON` 编译后运行。

---

## 验证功能

这些示例验证了：

✅ **调度器** - 多个协程正确调度和切换  
✅ **co_sleep()** - 精确的定时休眠（< 15 ms 误差）  
✅ **co_yield_now()** - 协程主动让出 CPU（C 代码可使用 `co_yield` 宏别名）  
✅ **栈池** - 内存复用减少 98% 内存占用  
✅ **I/O 多路复用** - 协程式网络 I/O（Linux epoll / Windows IOCP）  
✅ **Mutex / CondVar** - 协程级互斥锁与条件变量  
✅ **Channel** - Go 风格带缓冲/rendezvous channel  
✅ **C++17 封装** - libcoxx：Scheduler、Task、Mutex、CondVar、WaitGroup、Channel\<T\>  
✅ **跨平台** - Linux 和 Windows 全功能，macOS kqueue 计划中  

---

## 已知问题

1. **Windows 性能较低** - Fiber API 上下文切换开销高于 ucontext
2. **macOS 暂不支持** - kqueue I/O 多路复用待实现
3. **中文字符显示** - Windows 控制台可能显示乱码（建议设置 `chcp 65001`）

---

**当前状态：v2.0.0 / Week 14 完成 ✅**

核心功能稳定，可用于：
- CPU 密集型任务调度
- 定时任务处理
- 协程池实现
- 轻量级并发框架
- 网络服务器开发（Linux / Windows）
- 需要 RAII 风格并发原语的 C++17 项目
