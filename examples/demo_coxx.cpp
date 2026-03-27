/**
 * @file demo_coxx.cpp
 * @brief Comprehensive libcoxx C++ API demo
 *
 * Demonstrates the following features:
 *   1. Scheduler + Task::join()          -- wait for coroutines to finish
 *   2. WaitGroup                         -- barrier for concurrent tasks
 *   3. Mutex + std::lock_guard           -- critical-section protection
 *   4. Channel<T> (buffered and unbuffered) -- producer/consumer patterns
 *   5. CondVar + wait_for timeout        -- timed notification
 *   6. Exception propagation             -- Task::join() rethrows coroutine exceptions
 */

#include <coxx/coxx.hpp>
#include <libco/co.h>   // co_sleep
#include <cstdio>
#include <stdexcept>

// ============================================================================
// Demo 1: Task::join() waits for three concurrent coroutines in sequence
// ============================================================================

static void demo_task_join(co::Scheduler& sched) {
    std::puts("\n=== Demo 1: Task join ===");

    int results[3] = {};

    auto t0 = sched.spawn([&] { results[0] = 10; });
    auto t1 = sched.spawn([&] { results[1] = 20; });
    auto t2 = sched.spawn([&] { results[2] = 30; });

    // join() is called inside a coroutine: suspend until the target completes,
    // then resume the current coroutine.
    t0.join();
    t1.join();
    t2.join();

    std::printf("  10 + 20 + 30 = %d (expected 60)\n",
                results[0] + results[1] + results[2]);
}

// ============================================================================
// Demo 2: WaitGroup waits until all N concurrent tasks are complete
// ============================================================================

static void demo_waitgroup(co::Scheduler& sched) {
    std::puts("\n=== Demo 2: WaitGroup ===");

    const int N = 5;
    co::WaitGroup wg;
    int squares[N] = {};

    for (int i = 0; i < N; i++) {
        // Correct order: spawn first, then add.
        // If spawn itself throws, the WaitGroup count is not incremented and
        // wg.wait() cannot deadlock.
        sched.spawn([i, &squares, &wg] {
            // RAII guard: call done() during destruction whether the coroutine
            // returns normally or throws, preventing wg.wait() from deadlocking.
            struct DoneGuard {
                co::WaitGroup& wg;
                ~DoneGuard() { wg.done(); }
            } guard{wg};

            squares[i] = (i + 1) * (i + 1);
        });
        wg.add();   // Increment the count only after spawn succeeds
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
    // Demo 3: Mutex + std::lock_guard protect a shared counter during increments
// ============================================================================

static void demo_mutex(co::Scheduler& sched) {
    std::puts("\n=== Demo 3: Mutex ===");

    co::Mutex mtx;
    co::WaitGroup wg;
    int counter = 0;
    const int WORKERS = 4;
    const int ITERS   = 100;

    for (int i = 0; i < WORKERS; i++) {
        // Correct order: spawn first because it may throw, then add after success.
        // This keeps the WaitGroup count consistent and avoids deadlock.
        sched.spawn([&] {
            // RAII guard: done() is always called, even on exceptions.
            struct DoneGuard {
                co::WaitGroup& wg;
                ~DoneGuard() { wg.done(); }
            } guard{wg};

            for (int j = 0; j < ITERS; j++) {
                std::lock_guard<co::Mutex> lg(mtx);
                counter++;
            }
        });
        wg.add();   // Increment the count only after spawn succeeds
    }

    sched.spawn([&] {
        wg.wait();
        std::printf("  counter = %d (expected %d)\n", counter, WORKERS * ITERS);
    }).join();
}

// ============================================================================
// Demo 4: Buffered Channel<int> producer/consumer example
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
// Demo 5: Unbuffered Channel<int> rendezvous example
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
// Demo 6: CondVar + wait_for timeout for timed notifications
// ============================================================================

static void demo_condvar_timeout(co::Scheduler& sched) {
    std::puts("\n=== Demo 6: CondVar wait_for ===");

    co::Mutex   mtx;
    co::CondVar cv;
    bool        ready = false;

    // Notifier: send a signal after 50 ms
    auto notifier = sched.spawn([&] {
        co_sleep(50);
        {
            std::lock_guard<co::Mutex> lg(mtx);
            ready = true;
        }
        cv.notify_one();
    });

    // Waiter: wait at most 200 ms
    auto waiter = sched.spawn([&] {
        std::unique_lock<co::Mutex> ul(mtx);
        bool ok = cv.wait_for(ul, 200, [&] { return ready; });
        std::printf("  notified = %s (expected true)\n", ok ? "true" : "false");
    });

    waiter.join();
    notifier.join();  // Ensure the notifier finished before locals go out of scope
}

// ============================================================================
// Demo 7: Exception propagation through join()
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

    // Run all demos in one top-level coroutine because join() must be called
    // from coroutine context.
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
