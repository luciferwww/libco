/**
 * @file scheduler.hpp
 * @brief co::Scheduler and co::Task — C++17 RAII wrappers for the libco scheduler.
 *
 * All co:: objects must be used within the same scheduler thread.
 * They do NOT protect against multi-threaded access; they synchronize
 * coroutines within a single scheduler context.
 */
#pragma once

#include <exception>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <libco/co.h>
#include <libco/co_sync.h>

namespace co {

// ============================================================================
// TaskSharedState — internal completion token shared between Task and trampoline
// ============================================================================

/**
 * @brief Internal state shared between a Task handle and its running coroutine.
 *
 * Kept alive by shared_ptr until BOTH the Task handle and the coroutine's
 * trampoline release their references, whichever happens last (detach-safe).
 */
struct TaskSharedState {
    co_mutex_t*        mutex;
    co_cond_t*         cond;
    bool               done      = false;
    std::exception_ptr exception = nullptr;

    TaskSharedState()
        : mutex(co_mutex_create(nullptr))
        , cond(co_cond_create(nullptr))
    {
        if (!mutex || !cond) {
            if (mutex) co_mutex_destroy(mutex);
            if (cond)  co_cond_destroy(cond);
            throw std::runtime_error("co::Task: failed to allocate sync primitives");
        }
    }

    ~TaskSharedState() {
        (void)co_mutex_destroy(mutex);
        (void)co_cond_destroy(cond);
    }

    // Non-copyable, non-movable
    TaskSharedState(const TaskSharedState&) = delete;
    TaskSharedState& operator=(const TaskSharedState&) = delete;
};

// ============================================================================
// Task
// ============================================================================

/**
 * @brief Handle to a spawned coroutine.
 *
 * Destruction semantics: **detach** (does not block, like a goroutine).
 * To wait for completion, call join() explicitly.
 *
 * join() must be called from inside a coroutine (it suspends the caller
 * until the target coroutine completes).
 */
class Task {
public:
    Task() noexcept = default;
    Task(Task&&) noexcept;
    Task& operator=(Task&&) noexcept;

    /// Destructor = detach. The underlying coroutine continues running.
    ~Task();

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    /// Returns true if this handle is associated with a coroutine.
    bool valid() const noexcept { return state_ != nullptr; }

    /**
     * @brief Wait until the coroutine completes.
     *
     * Must be called from within a coroutine (suspends the caller).
     * Re-throws any exception the coroutine threw.
     * After returning, valid() == false.
     */
    void join();

    /**
     * @brief Release the handle without waiting.
     * After this, valid() == false.
     */
    void detach() noexcept { state_.reset(); }

private:
    friend class Scheduler;
    explicit Task(std::shared_ptr<TaskSharedState> state) noexcept;

    std::shared_ptr<TaskSharedState> state_;
};

// ============================================================================
// Internal detail: type-erased callable wrapper + trampoline
// ============================================================================

namespace detail {

struct WrapperBase {
    std::shared_ptr<TaskSharedState> state;

    explicit WrapperBase(std::shared_ptr<TaskSharedState> s) : state(std::move(s)) {}
    virtual void run() = 0;
    virtual ~WrapperBase() = default;
};

template<typename F>
struct Wrapper : WrapperBase {
    std::decay_t<F> fn;

    Wrapper(std::shared_ptr<TaskSharedState> s, F&& f)
        : WrapperBase(std::move(s)), fn(std::forward<F>(f)) {}

    void run() override { fn(); }
};

/// Non-template trampoline — defined in scheduler.cpp.
/// Invokes WrapperBase::run(), captures exceptions, signals completion.
void trampoline(void* arg) noexcept;

} // namespace detail

// ============================================================================
// Scheduler
// ============================================================================

/**
 * @brief RAII wrapper for co_scheduler_t.
 *
 * Usage:
 *   co::Scheduler sched;
 *   auto t = sched.spawn([&]{ do_work(); });
 *   sched.run();            // blocks until all coroutines complete
 */
class Scheduler {
public:
    explicit Scheduler(void* config = nullptr);
    ~Scheduler();

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    /**
     * @brief Spawn a coroutine.
     *
     * @tparam F  Callable with signature void(). Captured by value into the coroutine.
     * @param  fn Coroutine body.
     * @param  stack_size Stack size in bytes (0 = library default).
     * @return Task handle that can be join()ed or detach()ed.
     * @throws std::runtime_error if the underlying co_spawn fails.
     */
    template<typename F>
    Task spawn(F&& fn, size_t stack_size = 0);

    /**
     * @brief Run the scheduler until all coroutines complete.
     * @throws std::runtime_error if co_scheduler_run returns an error.
     */
    void run();

    co_scheduler_t* native_handle() noexcept { return sched_; }

private:
    co_scheduler_t* sched_;
};

// ── Scheduler::spawn (template, must stay in header) ─────────────────────────

template<typename F>
Task Scheduler::spawn(F&& fn, size_t stack_size) {
    auto state = std::make_shared<TaskSharedState>();
    auto* w    = new detail::Wrapper<F>(state, std::forward<F>(fn));

    co_routine_t* r = co_spawn(sched_, detail::trampoline, w, stack_size);
    if (!r) {
        delete w;
        throw std::runtime_error("co::Scheduler::spawn: co_spawn failed");
    }
    return Task(std::move(state));
}

} // namespace co
