# 测试策略

## 测试金字塔

```
           /\
          /  \     E2E Tests (5%)
         /____\    - 完整应用场景
        /      \   
       /        \  Integration Tests (15%)
      /__________\ - 模块间交互
     /            \
    /              \ Unit Tests (80%)
   /________________\ - 单个函数/模块
```

## 测试框架

### C 代码测试

**框架选择**：Unity

```c
// tests/unit/test_context.c

#include "unity.h"
#include "co_context.h"

void setUp(void) {
    // 每个测试前执行
}

void tearDown(void) {
    // 每个测试后执行
}

void test_context_init_success(void) {
    co_context_t ctx;
    void* stack = malloc(8192);
    
    int result = co_context_init(&ctx, stack, 8192, dummy_entry, NULL);
    
    TEST_ASSERT_EQUAL_INT(CO_OK, result);
    
    free(stack);
}

void test_context_init_null_stack(void) {
    co_context_t ctx;
    
    int result = co_context_init(&ctx, NULL, 8192, dummy_entry, NULL);
    
    TEST_ASSERT_NOT_EQUAL(CO_OK, result);
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_context_init_success);
    RUN_TEST(test_context_init_null_stack);
    
    return UNITY_END();
}
```

### C++ 代码测试

**框架选择**：Google Test

```cpp
// tests/unit/test_scheduler.cpp

#include <gtest/gtest.h>
#include <libcoxx/coxx.hpp>

class SchedulerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 测试前准备
    }
    
    void TearDown() override {
        // 测试后清理
    }
};

TEST_F(SchedulerTest, CreateDestroy) {
    cxx_co::Scheduler sched;
    // 应该能正常创建和销毁
    SUCCEED();
}

TEST_F(SchedulerTest, SpawnSimpleCoroutine) {
    cxx_co::Scheduler sched;
    bool executed = false;
    
    sched.spawn([&] {
        executed = true;
    });
    
    sched.run();
    
    EXPECT_TRUE(executed);
}
```

## 单元测试

### 测试覆盖模块

#### 1. Context 测试

```c
// tests/unit/test_context.c

- test_context_init_success()
- test_context_init_null_stack()
- test_context_init_zero_size()
- test_context_swap_simple()
- test_context_swap_multiple()
- test_context_destroy()
```

#### 2. Scheduler 测试

```c
// tests/unit/test_scheduler.c

- test_scheduler_create_default()
- test_scheduler_create_with_config()
- test_scheduler_destroy_empty()
- test_scheduler_destroy_with_routines()
- test_scheduler_run_empty()
- test_scheduler_run_single_routine()
- test_scheduler_run_multiple_routines()
- test_scheduler_poll_timeout()
- test_scheduler_stop()
```

#### 3. Routine 测试

```c
// tests/unit/test_routine.c

- test_routine_create()
- test_routine_yield()
- test_routine_sleep()
- test_routine_state_transitions()
- test_routine_stack_overflow_detection()
```

#### 4. Timer 测试

```c
// tests/unit/test_timer.c

- test_timer_heap_push_pop()
- test_timer_heap_ordering()
- test_sleep_basic()
- test_sleep_multiple()
- test_sleep_zero()
- test_sleep_accuracy()
```

#### 5. I/O 测试

```c
// tests/unit/test_iomux.c

- test_iomux_create()
- test_iomux_add_fd()
- test_iomux_remove_fd()
- test_iomux_poll_timeout()
- test_iomux_poll_ready()
- test_io_wait_read()
- test_io_wait_write()
```

#### 6. Sync 测试

```c
// tests/unit/test_sync.c

- test_mutex_lock_unlock()
- test_mutex_recursive_lock()
- test_cond_signal()
- test_cond_broadcast()
- test_cond_wait_timeout()
- test_channel_send_recv()
- test_channel_buffered()
- test_channel_close()
- test_waitgroup()
```

#### 7. Memory 测试

```c
// tests/unit/test_memory.c

- test_stack_pool_alloc_free()
- test_stack_pool_reuse()
- test_custom_allocator()
- test_memory_leak_detection()
```

## 集成测试

### 场景测试

```c
// tests/integration/test_producer_consumer.c

void test_producer_consumer_pattern() {
    co_scheduler_t* sched = co_scheduler_create(NULL);
    co_channel_t* ch = co_channel_create(sizeof(int), 10);
    
    // 创建生产者和消费者
    producer_consumer_context_t ctx = { ch, 100 };
    co_spawn(sched, producer_routine, &ctx, 0);
    co_spawn(sched, consumer_routine, &ctx, 0);
    
    // 运行
    co_scheduler_run(sched);
    
    // 验证结果
    TEST_ASSERT_EQUAL_INT(100, ctx.consumed_count);
    
    co_channel_destroy(ch);
    co_scheduler_destroy(sched);
}
```

```c
// tests/integration/test_echo_server.c

void test_echo_server() {
    // 启动 echo 服务器协程
    // 创建客户端连接
    // 发送数据
    // 验证回显
}
```

### 压力测试

```c
// tests/integration/test_stress.c

void test_many_coroutines() {
    co_scheduler_t* sched = co_scheduler_create(NULL);
    
    // 创建 10000 个协程
    for (int i = 0; i < 10000; i++) {
        co_spawn(sched, dummy_routine, NULL, 0);
    }
    
    co_scheduler_run(sched);
    
    co_stats_t stats;
    co_scheduler_get_stats(sched, &stats);
    
    TEST_ASSERT_EQUAL_UINT64(10000, stats.total_routines_created);
    TEST_ASSERT_EQUAL_UINT64(10000, stats.total_routines_destroyed);
    
    co_scheduler_destroy(sched);
}

void test_rapid_create_destroy() {
    // 快速创建和销毁协程
}

void test_context_switch_performance() {
    // 测试上下文切换性能
}
```

## 性能基准测试

### 基准测试框架

使用 Google Benchmark：

```cpp
// tests/benchmark/bench_context_switch.cpp

#include <benchmark/benchmark.h>
#include <libco/co.h>

static void BM_ContextSwitch(benchmark::State& state) {
    co_scheduler_t* sched = co_scheduler_create(NULL);
    
    int count = 0;
    co_spawn(sched, [](void* arg) {
        int* p = (int*)arg;
        for (int i = 0; i < 1000; i++) {
            (*p)++;
            co_yield();
        }
    }, &count, 0);
    
    for (auto _ : state) {
        co_scheduler_poll(sched, 0);
    }
    
    co_scheduler_destroy(sched);
    
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ContextSwitch);

static void BM_ChannelSendRecv(benchmark::State& state) {
    // 测试 channel 性能
}
BENCHMARK(BM_ChannelSendRecv);

BENCHMARK_MAIN();
```

### 性能指标

| 指标 | 目标 | 测量方法 |
|------|------|----------|
| 上下文切换延迟 | < 50ns (x86_64) | 连续 yield 测量 |
| 协程创建开销 | < 1μs | 批量创建测量 |
| Channel 吞吐量 | > 10M ops/s | 生产者消费者测量 |
| 内存开销 | < 4KB/协程 | 栈大小 + 元数据 |
| 10K 协程调度延迟 | < 1ms | 唤醒所有协程测量 |

## 内存测试

### Valgrind 检测

```bash
# tests/scripts/run_valgrind.sh

valgrind \
    --leak-check=full \
    --show-leak-kinds=all \
    --track-origins=yes \
    --verbose \
    --log-file=valgrind-out.txt \
    ./build/tests/unit/test_all
```

### AddressSanitizer

```cmake
# CMakeLists.txt

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)

if(ENABLE_ASAN)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
endif()
```

```bash
# 运行测试
cmake -DENABLE_ASAN=ON ..
make
./build/tests/unit/test_all
```

### Stack Overflow 检测

```c
// 栈金丝雀值检测
#define STACK_CANARY 0xDEADBEEF

void co_routine_init_stack_canary(co_routine_t* co) {
    uint32_t* canary = (uint32_t*)co->stack_base;
    *canary = STACK_CANARY;
}

void co_routine_check_stack_canary(co_routine_t* co) {
    uint32_t* canary = (uint32_t*)co->stack_base;
    if (*canary != STACK_CANARY) {
        fprintf(stderr, "Stack overflow detected in routine %llu\n", co->id);
        abort();
    }
}
```

## 平台兼容性测试

### 测试矩阵

| OS | Compiler | Arch | 状态 |
|----|----------|------|------|
| Ubuntu 22.04 | GCC 11 | x86_64 | ✅ |
| Ubuntu 22.04 | Clang 14 | x86_64 | ✅ |
| macOS 13 | Clang | x86_64 | ✅ |
| macOS 13 | Clang | ARM64 | ✅ |
| Windows 11 | MSVC 2022 | x86_64 | ✅ |
| Windows 11 | MinGW-w64 | x86_64 | ✅ |

### 交叉编译测试

```bash
# tests/scripts/cross_compile.sh

# ARM64
cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-arm64.cmake ..
make

# ARM32
cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-arm32.cmake ..
make
```

## 回归测试

### 自动化测试流程

```yaml
# .github/workflows/test.yml

name: Tests

on: [push, pull_request]

jobs:
  test:
    runs-on: ${{ matrix.os }}
    strategy:
      matrix:
        os: [ubuntu-latest, macos-latest, windows-latest]
        build_type: [Debug, Release]
    
    steps:
      - uses: actions/checkout@v3
      
      - name: Configure
        run: cmake -B build -DCMAKE_BUILD_TYPE=${{ matrix.build_type }}
      
      - name: Build
        run: cmake --build build
      
      - name: Test
        run: ctest --test-dir build --output-on-failure
      
      - name: Upload coverage
        if: matrix.os == 'ubuntu-latest'
        uses: codecov/codecov-action@v3
```

### 测试报告

```bash
# 生成代码覆盖率报告
cmake -DENABLE_COVERAGE=ON ..
make
make test
lcov --capture --directory . --output-file coverage.info
genhtml coverage.info --output-directory coverage_report
```

## 测试数据生成

### 模糊测试（Fuzzing）

```c
// tests/fuzz/fuzz_channel.c

#include <stdint.h>
#include <stddef.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < sizeof(int)) return 0;
    
    co_scheduler_t* sched = co_scheduler_create(NULL);
    co_channel_t* ch = co_channel_create(sizeof(int), 10);
    
    int value = *(int*)data;
    co_channel_send(ch, &value);
    
    int received;
    co_channel_recv(ch, &received);
    
    assert(value == received);
    
    co_channel_destroy(ch);
    co_scheduler_destroy(sched);
    
    return 0;
}
```

```bash
# 编译和运行 fuzzer
clang -fsanitize=fuzzer,address fuzz_channel.c -o fuzz_channel
./fuzz_channel -max_total_time=60
```

## 测试最佳实践

### 1. 测试隔离

每个测试独立创建和销毁资源：

```c
void test_example(void) {
    // Setup
    co_scheduler_t* sched = co_scheduler_create(NULL);
    
    // Test
    // ...
    
    // Teardown
    co_scheduler_destroy(sched);
}
```

### 2. 确定性测试

避免时间依赖的测试：

```c
// Bad: 依赖实际时间
void test_sleep_bad(void) {
    uint64_t start = get_time_ms();
    co_sleep(100);
    uint64_t end = get_time_ms();
    TEST_ASSERT_EQUAL_UINT64(100, end - start); // 可能失败
}

// Good: 使用 mock 时间
void test_sleep_good(void) {
    mock_time_set(0);
    co_sleep(100);
    mock_time_advance(100);
    // 验证协程状态
}
```

### 3. 边界条件测试

```c
- 测试 NULL 指针
- 测试零值
- 测试最大值
- 测试极限情况（10000 个协程）
```

### 4. 错误路径测试

```c
void test_error_handling(void) {
    // 测试内存不足
    mock_malloc_fail_next();
    co_scheduler_t* sched = co_scheduler_create(NULL);
    TEST_ASSERT_NULL(sched);
    
    // 测试无效参数
    int result = co_yield(); // 不在协程上下文中
    TEST_ASSERT_NOT_EQUAL(CO_OK, result);
}
```

## 持续集成

### GitHub Actions 配置

```yaml
# .github/workflows/ci.yml

name: CI

on: [push, pull_request]

jobs:
  build-and-test:
    strategy:
      matrix:
        include:
          - os: ubuntu-latest
            cc: gcc
            cxx: g++
          - os: ubuntu-latest
            cc: clang
            cxx: clang++
          - os: macos-latest
            cc: clang
            cxx: clang++
          - os: windows-latest
            cc: cl
            cxx: cl
    
    runs-on: ${{ matrix.os }}
    
    steps:
      - uses: actions/checkout@v3
      
      - name: Install dependencies
        run: |
          # 安装依赖
      
      - name: Build
        run: |
          cmake -B build
          cmake --build build
      
      - name: Unit Tests
        run: |
          cd build
          ctest -R unit --output-on-failure
      
      - name: Integration Tests
        run: |
          cd build
          ctest -R integration --output-on-failure
      
      - name: Benchmark
        run: |
          cd build
          ./tests/benchmark/bench_all --benchmark_format=console
```

## 下一步

参见：
- [05-build.md](./05-build.md) - 构建配置
- [06-roadmap.md](./06-roadmap.md) - 实施路线图
