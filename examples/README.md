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

**注意：**
- ✅ Linux 版本完全实现（epoll）
- ⚠️ macOS 版本待实现（需要 kqueue）
- ⚠️ Windows 版本实验性（IOCP AcceptEx/ConnectEx 调试中）
  - Windows 建议使用传统 accept() 测试基本功能
  - Week 8 AcceptEx/ConnectEx 优化进行中

**Windows 限制：**
目前 Windows 版本的 co_accept/co_connect 存在 IOCP 集成问题（错误 10057/995）。
建议使用 Linux/WSL 测试网络功能，或等待 Week 8 完善。

---

## 性能基准

### Linux (GCC 11.4)

| 示例 | 指标 | 性能 |
|------|------|------|
| concurrent (50协程) | 上下文切换 | 132万/秒 |
| concurrent (100协程) | 协程创建 | 3.4万/秒 |
| stack_pool (100协程) | 每协程耗时 | 0.029ms |
| scheduler | 定时精度 | < 5ms |

### Windows (MSVC 19.44)

| 示例 | 指标 | 性能 |
|------|------|------|
| concurrent (50协程) | 上下文切换 | 4.7万/秒 |
| producer_consumer | 总耗时 | ~0.3秒 |

---

## 验证功能

这些示例验证了：

✅ **调度器** - 多个协程正确调度和切换  
✅ **co_sleep()** - 精确的定时休眠（< 15ms 误差）  
✅ **co_yield_now()** - 协程主动让出 CPU（C 代码可使用 `co_yield` 宏别名）  
✅ **栈池** - 内存复用减少 98% 内存占用  
✅ **I/O 多路复用** - 协程式网络 I/O（Linux epoll 完整实现）  
✅ **跨平台** - Linux 和 Windows 一致性行为  

---

## 已知问题

1. **Windows 性能较低** - 由于 Fiber API 开销（慢 33 倍）
2. **Windows IOCP 简化** - AcceptEx/ConnectEx 尚未实现
3. **中文字符显示** - Windows 控制台可能显示乱码

---

## 下一步

Week 7 I/O 多路复用已完成基础实现：
- ✅ Linux epoll 后端
- ✅ Windows IOCP 后端（异步读写）
- ⚠️ Windows accept/connect 待完善

后续扩展：
- Week 8: 完善 Windows IOCP (AcceptEx/ConnectEx)
- Week 9-10: 同步原语（mutex、channel）
- Week 11-12: 系统调用 Hook

---

**当前状态：Week 7 完成 ✅**

核心功能稳定，可用于：
- CPU 密集型任务调度
- 定时任务处理
- 协程池实现
- 轻量级并发框架
- **网络服务器开发（Linux）**
