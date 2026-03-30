/**
 * @file co_mutex.c
 * @brief Coroutine mutex implementation
 * 
 * Coroutine-level mutex with key differences from pthread_mutex:
 * - On contention, the current coroutine is suspended with co_context_swap,
 *   allowing the scheduler thread to continue running other coroutines.
 * - Unlocking moves the first waiter back to the ready_queue for scheduling.
 * - The wait queue reuses each coroutine's existing queue_node because a
 *   WAITING coroutine is not present in ready_queue.
 */

#include "co_mutex.h"
#include "../co_scheduler.h"
#include <libco/co_sync.h>
#include <stdlib.h>
#include <stddef.h>
#include <assert.h>

// ============================================================================
// Creation and destruction
// ============================================================================

co_mutex_t *co_mutex_create(const void *attr) {
    (void)attr;  // Reserved for future extensions and currently ignored
    co_mutex_t *mutex = (co_mutex_t *)calloc(1, sizeof(co_mutex_t));
    if (!mutex) {
        return NULL;
    }
    mutex->locked = false;
    mutex->owner = NULL;
    co_queue_init(&mutex->wait_queue);
    return mutex;
}

co_error_t co_mutex_destroy(co_mutex_t *mutex) {
    if (!mutex) {
        return CO_ERROR_INVAL;
    }
    // The wait queue must be empty when destroying the mutex
    assert(co_queue_empty(&mutex->wait_queue) && "destroying a mutex with waiters");
    free(mutex);
    return CO_OK;
}

// ============================================================================
// Lock and unlock
// ============================================================================

co_error_t co_mutex_lock(co_mutex_t *mutex) {
    if (!mutex) {
        return CO_ERROR_INVAL;
    }

    co_scheduler_t *sched = co_current_scheduler();
    co_routine_t *current = co_current_routine();

    // Outside coroutine context, such as before the scheduler starts on the
    // main thread, take the lock directly. If already held, return BUSY
    // because blocking suspension is not possible.
    if (!sched || !current) {
        if (mutex->locked) {
            return CO_ERROR_BUSY;
        }
        mutex->locked = true;
        mutex->owner = NULL;
        return CO_OK;
    }

    if (!mutex->locked) {
        // Mutex is free, take ownership immediately
        mutex->locked = true;
        mutex->owner = current;
        return CO_OK;
    }

    // Mutex is busy, suspend the current coroutine and enqueue it as a waiter
    current->state = CO_STATE_WAITING;
    co_queue_push_back(&mutex->wait_queue, &current->queue_node);

    // Switch back to the scheduler and wait to be resumed on unlock
    co_context_swap(&current->context, &sched->main_ctx);

    // Resumed by co_mutex_unlock and now owns the mutex
    assert(mutex->locked && mutex->owner == current);
    return CO_OK;
}

co_error_t co_mutex_trylock(co_mutex_t *mutex) {
    if (!mutex) {
        return CO_ERROR_INVAL;
    }
    if (mutex->locked) {
        return CO_ERROR_BUSY;
    }
    co_routine_t *current = co_current_routine();
    mutex->locked = true;
    mutex->owner = current;
    return CO_OK;
}

co_error_t co_mutex_unlock(co_mutex_t *mutex) {
    if (!mutex) {
        return CO_ERROR_INVAL;
    }
    assert(mutex->locked && "unlocking a mutex that is not locked");

    co_queue_node_t *node = co_queue_pop_front(&mutex->wait_queue);
    if (!node) {
        // No waiters; release the mutex directly
        mutex->locked = false;
        mutex->owner = NULL;
        return CO_OK;
    }

    // Hand the mutex directly to the first waiter to avoid an unnecessary race
    co_routine_t *next = (co_routine_t *)((char *)node - offsetof(co_routine_t, queue_node));
    mutex->owner = next;
    // locked remains true because ownership has been transferred

    // Requeue the coroutine in the ready queue
    next->state = CO_STATE_READY;
    co_queue_push_back(&next->scheduler->ready_queue, &next->queue_node);

    return CO_OK;
}
