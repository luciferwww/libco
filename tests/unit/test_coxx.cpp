/**
 * @file test_coxx.cpp
 * @brief Unit tests for the libcoxx C++ wrapper (Week 12).
 *
 * Tests cover: Scheduler, Task, Mutex, CondVar, WaitGroup, Channel<T>.
 * Each test creates its own co::Scheduler and calls run() to execute all
 * coroutines before asserting results.
 */

#include <gtest/gtest.h>
#include <coxx/coxx.hpp>
#include <libco/co.h>   // co_yield_now, co_sleep
#include <stdexcept>
#include <vector>
#include <string>

// ============================================================================
// Scheduler + Task
// ============================================================================

TEST(SchedulerTest, SpawnRun) {
    co::Scheduler sched;
    int executed = 0;
    sched.spawn([&] { executed++; });
    sched.run();
    EXPECT_EQ(executed, 1);
}

TEST(SchedulerTest, MultipleCoroutines) {
    co::Scheduler sched;
    int counter = 0;
    sched.spawn([&] { counter++; });
    sched.spawn([&] { counter++; });
    sched.spawn([&] { counter++; });
    sched.run();
    EXPECT_EQ(counter, 3);
}

TEST(TaskTest, JoinOrdering) {
    co::Scheduler sched;
    std::vector<int> order;

    sched.spawn([&] {
        // Inner coroutine is queued but not yet running.
        auto t = sched.spawn([&] {
            order.push_back(2);
        });
        order.push_back(1);
        t.join();           // suspend outer, let inner run, then resume outer
        order.push_back(3);
    });

    sched.run();
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

TEST(TaskTest, Detach) {
    co::Scheduler sched;
    int executed = 0;

    sched.spawn([&] {
        auto t = sched.spawn([&] { executed++; });
        t.detach();
        EXPECT_FALSE(t.valid());
        // coroutine continues running despite detach
    });

    sched.run();
    EXPECT_EQ(executed, 1);
}

TEST(TaskTest, ExceptionPropagation) {
    co::Scheduler sched;
    bool caught = false;

    sched.spawn([&] {
        auto t = sched.spawn([&] {
            throw std::runtime_error("test error");
        });
        try {
            t.join();
            FAIL() << "Expected exception not thrown";
        } catch (const std::runtime_error& e) {
            caught = true;
            EXPECT_STREQ(e.what(), "test error");
        }
    });

    sched.run();
    EXPECT_TRUE(caught);
}

// ============================================================================
// Mutex
// ============================================================================

TEST(MutexTest, BasicLockUnlock) {
    co::Scheduler sched;
    co::Mutex mtx;
    int counter = 0;

    // Two coroutines do: lock -> read -> yield -> write -> unlock
    // Without the mutex the yield would let the other coroutine in, causing
    // both to read 0 and write 1 (final = 1). With the mutex final = 2.
    auto worker = [&] {
        mtx.lock();
        int val = counter;
        co_yield_now();   // attempt to interleave - blocked by mutex
        counter = val + 1;
        mtx.unlock();
    };

    sched.spawn(worker);
    sched.spawn(worker);
    sched.run();
    EXPECT_EQ(counter, 2);
}

TEST(MutexTest, LockGuard) {
    co::Scheduler sched;
    co::Mutex mtx;
    int counter = 0;

    sched.spawn([&] {
        std::lock_guard<co::Mutex> lg(mtx);
        counter++;
    });
    sched.spawn([&] {
        std::lock_guard<co::Mutex> lg(mtx);
        counter++;
    });

    sched.run();
    EXPECT_EQ(counter, 2);
}

TEST(MutexTest, TryLock) {
    co::Scheduler sched;
    co::Mutex mtx;
    bool first_try = false;
    bool second_try = false;

    sched.spawn([&] {
        // Lock held; try_lock from same coroutine should still succeed
        // (co_mutex is not a reentrant mutex, but this is the same coroutine).
        // We test that try_lock works at all when the lock is free.
        first_try = mtx.try_lock();
        if (first_try) mtx.unlock();

        // Acquire and hold to let inner check fail.
        mtx.lock();
        auto t = sched.spawn([&] {
            second_try = mtx.try_lock();  // should be false: held by outer
        });
        t.join();
        mtx.unlock();
    });

    sched.run();
    EXPECT_TRUE(first_try);
    EXPECT_FALSE(second_try);
}

// ============================================================================
// CondVar
// ============================================================================

TEST(CondVarTest, WaitNotifyOne) {
    co::Scheduler sched;
    co::Mutex  mtx;
    co::CondVar cv;
    bool ready = false;
    int  value = 0;

    // Consumer: wait until ready, then record value.
    sched.spawn([&] {
        std::unique_lock<co::Mutex> ul(mtx);
        cv.wait(ul, [&] { return ready; });
        value = 42;
    });

    // Producer: set ready and notify.
    sched.spawn([&] {
        {
            std::lock_guard<co::Mutex> lg(mtx);
            ready = true;
        }
        cv.notify_one();
    });

    sched.run();
    EXPECT_EQ(value, 42);
}

TEST(CondVarTest, WaitNotifyAll) {
    co::Scheduler sched;
    co::Mutex   mtx;
    co::CondVar cv;
    bool ready = false;
    int  woken = 0;

    auto consumer = [&] {
        std::unique_lock<co::Mutex> ul(mtx);
        cv.wait(ul, [&] { return ready; });
        woken++;
    };

    sched.spawn(consumer);
    sched.spawn(consumer);
    sched.spawn(consumer);

    sched.spawn([&] {
        {
            std::lock_guard<co::Mutex> lg(mtx);
            ready = true;
        }
        cv.notify_all();
    });

    sched.run();
    EXPECT_EQ(woken, 3);
}

TEST(CondVarTest, WaitForTimeout) {
    co::Scheduler sched;
    co::Mutex   mtx;
    co::CondVar cv;
    bool timed_out = false;

    sched.spawn([&] {
        std::unique_lock<co::Mutex> ul(mtx);
        bool result = cv.wait_for(ul, 30);  // 30 ms, nobody signals
        timed_out = !result;
    });

    sched.run();
    EXPECT_TRUE(timed_out);
}

// ============================================================================
// WaitGroup
// ============================================================================

TEST(WaitGroupTest, Basic) {
    co::Scheduler sched;
    co::WaitGroup wg;
    int finished = 0;

    sched.spawn([&] {
        for (int i = 0; i < 4; i++) {
            wg.add();
            sched.spawn([&] {
                finished++;
                wg.done();
            });
        }
        wg.wait();
        // By here all 4 workers must have finished.
        EXPECT_EQ(finished, 4);
    });

    sched.run();
    EXPECT_EQ(finished, 4);
}

// ============================================================================
// Channel<T>
// ============================================================================

TEST(ChannelTest, BasicSendRecv) {
    co::Scheduler   sched;
    co::Channel<int> ch(4);
    std::vector<int> received;

    sched.spawn([&] {
        ch.send(10);
        ch.send(20);
        ch.send(30);
        ch.close();
    });

    sched.spawn([&] {
        while (auto v = ch.recv()) {
            received.push_back(*v);
        }
    });

    sched.run();
    EXPECT_EQ(received, (std::vector<int>{10, 20, 30}));
}

TEST(ChannelTest, CloseRangeFor) {
    co::Scheduler   sched;
    co::Channel<int> ch(8);
    int sum = 0;

    sched.spawn([&] {
        for (int i = 1; i <= 5; i++) ch.send(i);
        ch.close();
    });

    sched.spawn([&] {
        for (int v : ch) sum += v;
    });

    sched.run();
    EXPECT_EQ(sum, 15);
}

TEST(ChannelTest, TrySendTryRecv) {
    co::Scheduler   sched;

    sched.spawn([&] {
        co::Channel<int> ch(2);

        // Non-blocking send to buffered channel.
        EXPECT_TRUE(ch.try_send(1));
        EXPECT_TRUE(ch.try_send(2));
        EXPECT_FALSE(ch.try_send(3));  // buffer full

        // Non-blocking receive.
        auto v1 = ch.try_recv();
        ASSERT_TRUE(v1.has_value());
        EXPECT_EQ(*v1, 1);

        auto v2 = ch.try_recv();
        ASSERT_TRUE(v2.has_value());
        EXPECT_EQ(*v2, 2);

        auto v3 = ch.try_recv();
        EXPECT_FALSE(v3.has_value());  // empty

        // After close, send returns false.
        ch.close();
        EXPECT_TRUE(ch.is_closed());
        EXPECT_FALSE(ch.send(99));
    });

    sched.run();
}

TEST(ChannelTest, Rendezvous) {
    co::Scheduler   sched;
    co::Channel<int> ch(0);  // unbuffered
    int received = 0;

    // Sender and receiver must meet synchronously.
    sched.spawn([&] { ch.send(777); });
    sched.spawn([&] {
        auto v = ch.recv();
        ASSERT_TRUE(v.has_value());
        received = *v;
    });

    sched.run();
    EXPECT_EQ(received, 777);
}

// ============================================================================
// static_assert check (compile-time, comment out to verify)
// ============================================================================

// The following line must NOT compile - uncomment to verify the static_assert:
// co::Channel<std::string> bad_ch(1);
