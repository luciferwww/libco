/**
 * @file co_cond.c
 * @brief Coroutine condition variable implementation
 * 
 * co_cond_wait follows the same semantics as pthread_cond_wait:
 *   1. Atomically release the mutex
 *   2. Add the current coroutine to the wait queue and suspend it
 *   3. Reacquire the mutex after signal/broadcast resumes it
 *
 * Steps 1 and 2 are effectively atomic because the scheduler is single-
 * threaded, so no other coroutine can run between unlocking the mutex and
 * suspending the current coroutine.
 */

#include "co_cond.h"
#include "co_mutex.h"
#include "../co_scheduler.h"
#include "../co_timer.h"
#include <libco/co_sync.h>
#include <stdlib.h>
#include <stddef.h>
#include <assert.h>

// ============================================================================
// Creation and destruction
// ============================================================================

co_cond_t *co_cond_create(const void *attr) {
    (void)attr;  // Reserved for future extensions and currently ignored
    co_cond_t *cond = (co_cond_t *)calloc(1, sizeof(co_cond_t));
    if (!cond) {
        return NULL;
    }
    co_queue_init(&cond->wait_queue);
    return cond;
}

co_error_t co_cond_destroy(co_cond_t *cond) {
    if (!cond) {
        return CO_ERROR_INVAL;
    }
    assert(co_queue_empty(&cond->wait_queue) && "destroying a cond with waiters");
    free(cond);
    return CO_OK;
}

// ============================================================================
// Waiting
// ============================================================================

co_error_t co_cond_wait(co_cond_t *cond, co_mutex_t *mutex) {
    if (!cond || !mutex) {
        return CO_ERROR_INVAL;
    }

    co_scheduler_t *sched = co_current_scheduler();
    co_routine_t *current = co_current_routine();
    if (!sched || !current) {
        return CO_ERROR_INVAL;
    }

    // 1. Add the current coroutine to the condition wait queue
    current->state = CO_STATE_WAITING;
    co_queue_push_back(&cond->wait_queue, &current->queue_node);

    // 2. Release the mutex, possibly waking a coroutine waiting on it
    co_mutex_unlock(mutex);

    // 3. Switch back to the scheduler and suspend
    co_context_swap(&current->context, &sched->main_ctx);

    // 4. Reacquire the mutex after being resumed by signal or broadcast
    co_mutex_lock(mutex);

    return CO_OK;
}

co_error_t co_cond_timedwait(co_cond_t *cond, co_mutex_t *mutex,
                              uint32_t timeout_ms) {
    if (!cond || !mutex) {
        return CO_ERROR_INVAL;
    }

    // timeout_ms == 0 means an immediate timeout for non-blocking checks
    if (timeout_ms == 0) {
        return CO_ERROR_TIMEOUT;
    }

    co_scheduler_t *sched = co_current_scheduler();
    co_routine_t *current = co_current_routine();
    if (!sched || !current) {
        return CO_ERROR_INVAL;
    }

    // 1. Add to the condition wait queue and record the queue pointer so a timeout can remove it
    current->state = CO_STATE_WAITING;
    current->cond_wait_queue = &cond->wait_queue;
    current->timed_out = false;
    co_queue_push_back(&cond->wait_queue, &current->queue_node);

    // 2. Register a timer so the scheduler can remove and resume the coroutine on timeout
    uint64_t now = co_get_monotonic_time_ms();
    current->wakeup_time = now + timeout_ms;
    if (!co_timer_heap_push(&sched->timer_heap, current)) {
        // Out of memory: roll back queue registration and restore state
        co_queue_remove(&cond->wait_queue, &current->queue_node);
        current->state = CO_STATE_READY;
        current->cond_wait_queue = NULL;
        return CO_ERROR_NOMEM;
    }

    // 3. Release the mutex with the same semantics as co_cond_wait
    co_mutex_unlock(mutex);

    // 4. Switch back to the scheduler and wait for signal or timer expiration
    co_context_swap(&current->context, &sched->main_ctx);

    // 5. If signal resumes the coroutine before timeout, remove the timer
    //    before the coroutine exits to avoid leaving a dangling pointer in
    //    timer_heap after coroutine memory is released.
    if (!current->timed_out) {
        co_timer_heap_remove(&sched->timer_heap, current);
    }

    // 6. Reacquire the mutex after resuming
    co_mutex_lock(mutex);

    return current->timed_out ? CO_ERROR_TIMEOUT : CO_OK;
}

// ============================================================================
// Wakeup
// ============================================================================

co_error_t co_cond_signal(co_cond_t *cond) {
    if (!cond) {
        return CO_ERROR_INVAL;
    }

    co_queue_node_t *node = co_queue_pop_front(&cond->wait_queue);
    if (!node) {
        return CO_OK;  // No waiters, nothing to do
    }

    co_routine_t *waiter = (co_routine_t *)((char *)node - offsetof(co_routine_t, queue_node));
    // Clear timedwait state so stale timer entries are ignored later
    waiter->cond_wait_queue = NULL;
    waiter->state = CO_STATE_READY;
    // Once resumed, the waiter will contend for the mutex in the final wait step
    co_queue_push_back(&waiter->scheduler->ready_queue, &waiter->queue_node);

    return CO_OK;
}

co_error_t co_cond_broadcast(co_cond_t *cond) {
    if (!cond) {
        return CO_ERROR_INVAL;
    }

    co_queue_node_t *node;
    while ((node = co_queue_pop_front(&cond->wait_queue)) != NULL) {
        co_routine_t *waiter = (co_routine_t *)((char *)node - offsetof(co_routine_t, queue_node));
        waiter->cond_wait_queue = NULL;  // Clear timedwait state
        waiter->state = CO_STATE_READY;
        co_queue_push_back(&waiter->scheduler->ready_queue, &waiter->queue_node);
    }

    return CO_OK;
}
