/**
 * @file sync.hpp
 * @brief co::Mutex, co::CondVar, co::WaitGroup - C++17 wrappers for libco sync primitives.
 *
 * co::Mutex satisfies the C++ named requirement BasicLockable and is compatible
 * with std::lock_guard<co::Mutex> and std::unique_lock<co::Mutex>.
 *
 * co::CondVar mirrors the std::condition_variable interface.
 *
 * co::WaitGroup is a pure C++ implementation on top of Mutex + CondVar;
 * it does not require any additional C-layer primitives.
 *
 * THREAD SAFETY: All types in this header synchronize *coroutines* within
 * a single scheduler thread. They do NOT protect against concurrent access
 * from multiple OS threads and are NOT replacements for std::mutex.
 */
#pragma once

#include <mutex>    // for std::unique_lock, std::lock_guard (types only)
#include <stdexcept>
#include <libco/co_sync.h>

namespace co {

// ============================================================================
// Mutex
// ============================================================================

/**
 * @brief Cooperative mutex for coroutines.
 *
 * Satisfies BasicLockable - compatible with std::lock_guard<co::Mutex>
 * and std::unique_lock<co::Mutex>.
 *
 * When lock() would block, the calling *coroutine* is suspended and yields
 * to the scheduler. The OS thread is never blocked.
 */
class Mutex {
public:
    Mutex();
    ~Mutex();

    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    /// Block (yield) until the lock is acquired.
    void lock();

    /// Release the lock; if waiters exist, the first one (FIFO) is woken.
    void unlock();

    /// Non-blocking attempt. Returns false if already held; never yields.
    bool try_lock();

    co_mutex_t* native_handle() noexcept { return mutex_; }

private:
    co_mutex_t* mutex_;
};

// ============================================================================
// CondVar
// ============================================================================

/**
 * @brief Cooperative condition variable for coroutines.
 *
 * Mirrors the std::condition_variable interface.
 * wait() and wait_for() must be called from within a coroutine.
 *
 * Important: co_cond_wait() atomically releases and re-acquires the
 * underlying C mutex. The unique_lock's owns-flag remains true throughout -
 * do NOT call lock.unlock() around wait().
 */
class CondVar {
public:
    CondVar();
    ~CondVar();

    CondVar(const CondVar&) = delete;
    CondVar& operator=(const CondVar&) = delete;

    /// Suspend until notify_one/notify_all is called.
    void wait(std::unique_lock<Mutex>& lock);

    /// Suspend until pred() returns true.
    template<typename Predicate>
    void wait(std::unique_lock<Mutex>& lock, Predicate pred) {
        while (!pred()) wait(lock);
    }

    /**
     * @brief Suspend for at most timeout_ms milliseconds.
     * @return true if a notification was received; false if timed out.
     */
    bool wait_for(std::unique_lock<Mutex>& lock, uint32_t timeout_ms);

    /**
     * @brief Suspend for at most timeout_ms, re-checking pred after each wakeup.
     * @return true if pred() is satisfied; false if timed out before pred().
     * Note: total wait may exceed timeout_ms if pred() is repeatedly false.
     */
    template<typename Predicate>
    bool wait_for(std::unique_lock<Mutex>& lock, uint32_t timeout_ms, Predicate pred) {
        while (!pred()) {
            if (!wait_for(lock, timeout_ms)) {
                return pred();  // one final check after timeout
            }
        }
        return true;
    }

    /// Wake one waiting coroutine (FIFO order).
    void notify_one();

    /// Wake all waiting coroutines.
    void notify_all();

    co_cond_t* native_handle() noexcept { return cond_; }

private:
    co_cond_t* cond_;
};

// ============================================================================
// WaitGroup
// ============================================================================

/**
 * @brief Go-style WaitGroup: wait for a set of coroutines to finish.
 *
 * Implemented entirely in C++ on top of co::Mutex + co::CondVar.
 * No additional C-layer primitives are needed.
 *
 * Typical usage:
 *   co::WaitGroup wg;
 *   for (int i = 0; i < N; i++) {
 *       wg.add();
 *       sched.spawn([&]{ do_work(); wg.done(); });
 *   }
 *   wg.wait();
 */
class WaitGroup {
public:
    WaitGroup() = default;

    WaitGroup(const WaitGroup&) = delete;
    WaitGroup& operator=(const WaitGroup&) = delete;

    /**
     * @brief Increment the counter by delta (default 1).
     * Must be called before spawning the corresponding coroutine.
     */
    void add(int delta = 1) {
        std::lock_guard<Mutex> lg(mtx_);
        count_ += delta;
    }

    /**
     * @brief Decrement the counter by 1.
     * When the counter reaches zero, all wait() callers are unblocked.
     * Must be called from within a coroutine.
     */
    void done() {
        std::lock_guard<Mutex> lg(mtx_);
        if (--count_ == 0) {
            cv_.notify_all();
        }
    }

    /**
     * @brief Block until the counter reaches zero.
     * Must be called from within a coroutine.
     */
    void wait() {
        std::unique_lock<Mutex> ul(mtx_);
        cv_.wait(ul, [this] { return count_ == 0; });
    }

private:
    int     count_ = 0;
    Mutex   mtx_;
    CondVar cv_;
};

} // namespace co
