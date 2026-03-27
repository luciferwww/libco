/**
 * @file scheduler.cpp
 * @brief co::Scheduler and co::Task implementation.
 */

#include <coxx/scheduler.hpp>
#include <stdexcept>

namespace co {

// ============================================================================
// detail::trampoline
// ============================================================================

namespace detail {

/**
 * Entry point for every spawned coroutine.
 *
 * Sequence:
 *   1. Move state and callable out of the heap wrapper (then delete it).
 *   2. Invoke the user function; catch any exception into state->exception.
 *   3. Signal completion: lock -> done=true -> broadcast -> unlock.
 *      (co_cond_broadcast wakes any coroutine blocked in Task::join().)
 *
 * Marked noexcept so that exceptions can never propagate into the C scheduler.
 */
void trampoline(void* arg) noexcept {
    auto* w = static_cast<WrapperBase*>(arg);

    // Move state out of the wrapper before deleting it, so the shared_ptr
    // lifetime extends beyond `delete w` even when the Task was detached.
    auto state = std::move(w->state);

    try {
        w->run();
    } catch (...) {
        state->exception = std::current_exception();
    }

    delete w;

    // Signal completion to any join() waiters.
    co_mutex_lock(state->mutex);
    state->done = true;
    co_cond_broadcast(state->cond);
    co_mutex_unlock(state->mutex);

    // `state` goes out of scope here; shared_ptr decrements refcount.
}

} // namespace detail

// ============================================================================
// Task
// ============================================================================

Task::Task(std::shared_ptr<TaskSharedState> state) noexcept
    : state_(std::move(state)) {}

Task::Task(Task&& other) noexcept
    : state_(std::move(other.state_)) {}

Task& Task::operator=(Task&& other) noexcept {
    if (this != &other) {
        state_ = std::move(other.state_);
    }
    return *this;
}

Task::~Task() {
    // detach: release the shared_ptr without waiting.
    // The trampoline holds its own copy of the shared_ptr and will signal
    // completion regardless of whether the Task handle still exists.
}

void Task::join() {
    if (!state_) {
        throw std::logic_error(
            "co::Task::join() called on an invalid (moved-from or already joined) Task");
    }

    // Suspend this coroutine until the target coroutine signals completion.
    co_mutex_lock(state_->mutex);
    while (!state_->done) {
        co_cond_wait(state_->cond, state_->mutex);
    }
    co_mutex_unlock(state_->mutex);

    // Consume the handle and re-throw any stored exception.
    auto ex = state_->exception;
    state_.reset();
    if (ex) {
        std::rethrow_exception(ex);
    }
}

// ============================================================================
// Scheduler
// ============================================================================

Scheduler::Scheduler(void* config)
    : sched_(co_scheduler_create(config))
{
    if (!sched_) {
        throw std::runtime_error("co::Scheduler: co_scheduler_create failed");
    }
}

Scheduler::~Scheduler() {
    co_scheduler_destroy(sched_);
}

void Scheduler::run() {
    co_error_t err = co_scheduler_run(sched_);
    if (err != CO_OK) {
        throw std::runtime_error("co::Scheduler::run() returned an error");
    }
}

} // namespace co
