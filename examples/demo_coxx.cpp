/**
 * @file demo_coxx.cpp
 * @brief libcoxx C++ API 综合演示
 *
 * 演示以下功能：
 *   1. Scheduler + Task::join()          -- 等待协程完成并获取结果
 *   2. WaitGroup                          -- 并发任务屏障
 *   3. Mutex + std::lock_guard            -- 临界区保护
 *   4. Channel<T>（带缓冲 + 无缓冲）      -- 生产者/消费者
 *   5. CondVar + wait_for 超时            -- 定时通知
 *   6. 异常传播                           -- Task::join() 重抛协程内异常
 */

#include <coxx/coxx.hpp>
#include <libco/co.h>   // co_sleep
#include <cstdio>
#include <stdexcept>

// ============================================================================
// 示例 1：Task::join()  顺序等待三个并发协程的结果
// ============================================================================

static void demo_task_join(co::Scheduler& sched) {
    std::puts("\n=== Demo 1: Task join ===");

    int results[3] = {};

    auto t0 = sched.spawn([&] { results[0] = 10; });
    auto t1 = sched.spawn([&] { results[1] = 20; });
    auto t2 = sched.spawn([&] { results[2] = 30; });

    // join() 在协程内部调用：挂起当前协程，等目标完成后再恢复
    t0.join();
    t1.join();
    t2.join();

    std::printf("  10 + 20 + 30 = %d (expected 60)\n",
                results[0] + results[1] + results[2]);
}

// ============================================================================
// 示例 2：WaitGroup  N 个并发任务全部完成后再继续
// ============================================================================

static void demo_waitgroup(co::Scheduler& sched) {
    std::puts("\n=== Demo 2: WaitGroup ===");

    const int N = 5;
    co::WaitGroup wg;
    int squares[N] = {};

    for (int i = 0; i < N; i++) {
        // 正确顺序：先 spawn 后 add。
        // 若 spawn 本身抛异常（内存不足等），wg 计数未递增，wg.wait() 不会死锁。
        sched.spawn([i, &squares, &wg] {
            // RAII guard：无论协程正常返回还是抛异常，析构时都保证调用 done()，
            // 防止 wg.wait() 因计数永不归零而死锁。
            struct DoneGuard {
                co::WaitGroup& wg;
                ~DoneGuard() { wg.done(); }
            } guard{wg};

            squares[i] = (i + 1) * (i + 1);
        });
        wg.add();   // spawn 成功后再递增计数
    }

    sched.spawn([&] {
        wg.wait();
        int sum = 0;
        for (int v : squares) sum += v;
        // 1^2 + 2^2 + 3^2 + 4^2 + 5^2 = 55
        std::printf("  sum of squares 1..%d = %d (expected 55)\n", N, sum);
    }).join();
}

// ============================================================================
// 示例 3：Mutex + std::lock_guard  并发递增共享计数器
// ============================================================================

static void demo_mutex(co::Scheduler& sched) {
    std::puts("\n=== Demo 3: Mutex ===");

    co::Mutex mtx;
    co::WaitGroup wg;
    int counter = 0;
    const int WORKERS = 4;
    const int ITERS   = 100;

    for (int i = 0; i < WORKERS; i++) {
        // 正确顺序：先 spawn（可能抛异常），成功后再 add。
        // 这样 spawn 失败时 wg 计数不会被污染，wg.wait() 不会死锁。
        sched.spawn([&] {
            // RAII guard：无论协程如何退出（正常/异常），done() 都会执行，
            // 防止 wg.wait() 因计数永不归零而死锁。
            struct DoneGuard {
                co::WaitGroup& wg;
                ~DoneGuard() { wg.done(); }
            } guard{wg};

            for (int j = 0; j < ITERS; j++) {
                std::lock_guard<co::Mutex> lg(mtx);
                counter++;
            }
        });
        wg.add();   // spawn 成功后再递增计数
    }

    sched.spawn([&] {
        wg.wait();
        std::printf("  counter = %d (expected %d)\n", counter, WORKERS * ITERS);
    }).join();
}

// ============================================================================
// 示例 4：Channel<int> 带缓冲  生产者/消费者
// ============================================================================

static void demo_channel_buffered(co::Scheduler& sched) {
    std::puts("\n=== Demo 4: Buffered Channel ===");

    co::Channel<int> ch(8);

    sched.spawn([&] {
        for (int i = 1; i <= 5; i++) ch.send(i);
        ch.close();
    });

    sched.spawn([&] {
        int sum = 0;
        for (int v : ch) sum += v;
        // 1+2+3+4+5 = 15
        std::printf("  buffered channel sum = %d (expected 15)\n", sum);
    }).join();
}

// ============================================================================
// 示例 5：Channel<int> 无缓冲（rendezvous）
// ============================================================================

static void demo_channel_rendezvous(co::Scheduler& sched) {
    std::puts("\n=== Demo 5: Rendezvous Channel ===");

    co::Channel<int> ch(0);

    sched.spawn([&] {
        for (int i = 0; i < 3; i++) {
            std::printf("  send %d\n", i);
            ch.send(i);
        }
        ch.close();
    });

    sched.spawn([&] {
        while (auto v = ch.recv()) {
            std::printf("  recv %d\n", *v);
        }
    }).join();
}

// ============================================================================
// 示例 6：CondVar + wait_for 超时  限时等待通知
// ============================================================================

static void demo_condvar_timeout(co::Scheduler& sched) {
    std::puts("\n=== Demo 6: CondVar wait_for ===");

    co::Mutex   mtx;
    co::CondVar cv;
    bool        ready = false;

    // 通知方：50ms 后发出信号
    auto notifier = sched.spawn([&] {
        co_sleep(50);
        {
            std::lock_guard<co::Mutex> lg(mtx);
            ready = true;
        }
        cv.notify_one();
    });

    // 等待方：最多等 200ms
    auto waiter = sched.spawn([&] {
        std::unique_lock<co::Mutex> ul(mtx);
        bool ok = cv.wait_for(ul, 200, [&] { return ready; });
        std::printf("  notified = %s (expected true)\n", ok ? "true" : "false");
    });

    waiter.join();
    notifier.join();  // 确保 notifier 已完成，避免局部变量悬空
}

// ============================================================================
// 示例 7：异常传播  join() 重新抛出协程内部异常
// ============================================================================

static void demo_exception(co::Scheduler& sched) {
    std::puts("\n=== Demo 7: Exception propagation ===");

    auto t = sched.spawn([&] {
        throw std::runtime_error("boom from coroutine");
    });

    try {
        t.join();
    } catch (const std::runtime_error& e) {
        std::printf("  caught: \"%s\"\n", e.what());
    }
}

// ============================================================================
// main
// ============================================================================

int main() {
    co::Scheduler sched;

    // 所有 demo 在同一顶层协程内顺序执行（join() 只能从协程上下文调用）
    sched.spawn([&] {
        demo_task_join(sched);
        demo_waitgroup(sched);
        demo_mutex(sched);
        demo_channel_buffered(sched);
        demo_channel_rendezvous(sched);
        demo_condvar_timeout(sched);
        demo_exception(sched);

        std::puts("\nAll demos completed.");
    });

    sched.run();
    return 0;
}
