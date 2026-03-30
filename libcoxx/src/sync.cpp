/**
 * @file sync.cpp
 * @brief co::Mutex and co::CondVar implementation.
 *
 * co::WaitGroup is header-only (implemented in sync.hpp using Mutex + CondVar).
 */

#include <coxx/sync.hpp>
#include <stdexcept>

namespace co {

// ============================================================================
// Mutex
// ============================================================================

Mutex::Mutex() : mutex_(co_mutex_create(nullptr)) {
    if (!mutex_) throw std::runtime_error("co::Mutex: co_mutex_create failed");
}

Mutex::~Mutex() {
    (void)co_mutex_destroy(mutex_);
}

void Mutex::lock() {
    co_mutex_lock(mutex_);
}

void Mutex::unlock() {
    co_mutex_unlock(mutex_);
}

bool Mutex::try_lock() {
    return co_mutex_trylock(mutex_) == CO_OK;
}

// ============================================================================
// CondVar
// ============================================================================

CondVar::CondVar() : cond_(co_cond_create(nullptr)) {
    if (!cond_) throw std::runtime_error("co::CondVar: co_cond_create failed");
}

CondVar::~CondVar() {
    (void)co_cond_destroy(cond_);
}

void CondVar::wait(std::unique_lock<Mutex>& lock) {
    // co_cond_wait() atomically releases the C-level mutex and suspends this
    // coroutine, then re-acquires the mutex before returning.
    // The unique_lock's owns-flag stays true throughout - no unlock() needed.
    co_cond_wait(cond_, lock.mutex()->native_handle());
}

bool CondVar::wait_for(std::unique_lock<Mutex>& lock, uint32_t timeout_ms) {
    co_error_t e = co_cond_timedwait(cond_, lock.mutex()->native_handle(), timeout_ms);
    return e == CO_OK;  // false means timed out
}

void CondVar::notify_one() {
    co_cond_signal(cond_);
}

void CondVar::notify_all() {
    co_cond_broadcast(cond_);
}

} // namespace co
