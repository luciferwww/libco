/**
 * @file co_scheduler.c
 * @brief Scheduler implementation
 * 
 * Implements the core coroutine scheduler logic.
 */

#include "co_scheduler.h"
#include "co_routine.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

// ============================================================================
// Default configuration
// ============================================================================

#define DEFAULT_STACK_SIZE (128 * 1024)  // 128KB
#define DEFAULT_STACK_POOL_CAPACITY 16   // Default stack pool capacity

// ============================================================================
// Thread-local storage
// ============================================================================

// Thread-local scheduler for the current thread.
// Each thread can have its own independent scheduler instance.
// This enables multi-threaded coroutine execution without race conditions.
#if defined(_MSC_VER)
    // MSVC uses __declspec(thread)
    static __declspec(thread) co_scheduler_t *tls_current_scheduler = NULL;
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
    // C11 standard thread-local storage
    static _Thread_local co_scheduler_t *tls_current_scheduler = NULL;
#elif defined(__GNUC__) || defined(__clang__)
    // GCC/Clang extension
    static __thread co_scheduler_t *tls_current_scheduler = NULL;
#else
    #error "Thread-local storage not supported on this platform"
#endif

// ============================================================================
// Scheduler creation and destruction
// ============================================================================

co_scheduler_t *co_scheduler_create(void *config) {
    // TODO(v2.1): Support configuration parameters
    // config should be of type co_scheduler_config_t* and configure:
    // - default stack size
    // - maximum coroutine count
    // - scheduling policy
    // For now, config is ignored and defaults are used.
    (void)config;  // Unused
    
    // Allocate the scheduler
    co_scheduler_t *sched = (co_scheduler_t *)calloc(1, sizeof(co_scheduler_t));
    if (!sched) {
        return NULL;
    }
    
    // Initialize fields
    co_queue_init(&sched->ready_queue);
    sched->current = NULL;
    sched->default_stack_size = DEFAULT_STACK_SIZE;
    sched->running = false;
    sched->should_stop = false;
    sched->total_routines = 0;
    sched->active_routines = 0;
    sched->switch_count = 0;
    sched->waiting_io_count = 0;
    sched->next_id = 1;
    
    // Create the stack pool (Week 5)
    sched->stack_pool = co_stack_pool_create(DEFAULT_STACK_SIZE, DEFAULT_STACK_POOL_CAPACITY);
    if (!sched->stack_pool) {
        free(sched);
        return NULL;
    }
    
    // Initialize the timer heap (Week 6)
    if (!co_timer_heap_init(&sched->timer_heap, 16)) {
        co_stack_pool_destroy(sched->stack_pool);
        free(sched);
        return NULL;
    }
    
    // Create the I/O multiplexer (Week 7)
    sched->iomux = co_iomux_create(1024);
    if (!sched->iomux) {
        co_timer_heap_destroy(&sched->timer_heap);
        co_stack_pool_destroy(sched->stack_pool);
        free(sched);
        return NULL;
    }
    
    // Initialize the main context used by the scheduler
    memset(&sched->main_ctx, 0, sizeof(sched->main_ctx));

    return sched;
}

void co_scheduler_destroy(co_scheduler_t *sched) {
    if (!sched) {
        return;
    }
    
    // Destroy all unfinished coroutines
    while (!co_queue_empty(&sched->ready_queue)) {
        co_queue_node_t *node = co_queue_pop_front(&sched->ready_queue);
        co_routine_t *routine = (co_routine_t *)((char *)node - offsetof(co_routine_t, queue_node));
        co_routine_destroy(routine);
        free(routine);
    }
    
    // Destroy all sleeping coroutines (Week 6)
    while (!co_timer_heap_empty(&sched->timer_heap)) {
        co_routine_t *routine = co_timer_heap_pop(&sched->timer_heap);
        co_routine_destroy(routine);
        free(routine);
    }
    
    // Destroy the timer heap (Week 6)
    co_timer_heap_destroy(&sched->timer_heap);
    
    // Destroy the I/O multiplexer (Week 7)
    if (sched->iomux) {
        co_iomux_destroy(sched->iomux);
    }
    
    // Destroy the stack pool (Week 5)
    co_stack_pool_destroy(sched->stack_pool);

    // Release the scheduler
    free(sched);
}

// ============================================================================
// Scheduler execution
// ============================================================================

co_error_t co_scheduler_run(co_scheduler_t *sched) {
    if (!sched) {
        return CO_ERROR_INVAL;
    }
    
    if (sched->running) {
        return CO_ERROR;  // Already running
    }
    
    // Set as the current thread-local scheduler
    tls_current_scheduler = sched;
    sched->running = true;
    sched->should_stop = false;
    
    // Main loop: keep scheduling until all coroutines finish or stop is requested
    while (!sched->should_stop) {
        // Week 6: check timers and wake expired sleeping coroutines
        uint64_t now = co_get_monotonic_time_ms();
        while (!co_timer_heap_empty(&sched->timer_heap)) {
            co_routine_t *routine = co_timer_heap_peek(&sched->timer_heap);
            if (routine->wakeup_time > now) {
                break;  // The heap top has not expired yet; later entries cannot either
            }
            
            // Pop the expired coroutine
            co_timer_heap_pop(&sched->timer_heap);

            // Drop stale timer events for coroutines already resumed by signal or broadcast.
            if (routine->state != CO_STATE_WAITING) {
                continue;
            }

            // co_cond_timedwait timeout: remove it from the condition wait queue
            if (routine->cond_wait_queue != NULL) {
                co_queue_remove(routine->cond_wait_queue, &routine->queue_node);
                routine->timed_out = true;
                routine->cond_wait_queue = NULL;
            } else if (routine->io_waiting) {
                // co_wait_io timeout: update waiting_io_count; co_wait_io cleans up via co_iomux_del
                routine->io_waiting = false;
                routine->timed_out = true;
                routine->scheduler->waiting_io_count--;
            }

            co_scheduler_enqueue(sched, routine);
        }
        
        // If no coroutine is ready, determine whether to wait
        if (co_queue_empty(&sched->ready_queue)) {
            // If sleeping coroutines remain, compute the next wakeup deadline
            int io_timeout_ms = -1;  // Default: wait indefinitely
            
            if (!co_timer_heap_empty(&sched->timer_heap)) {
                co_routine_t *next_wakeup = co_timer_heap_peek(&sched->timer_heap);
                int64_t wait_ms = (int64_t)(next_wakeup->wakeup_time - now);
                io_timeout_ms = (wait_ms > 0) ? (int)wait_ms : 0;
            }
            
            // Week 7: poll I/O events with timeout.
            // Only poll when there are I/O waiters or timers. Otherwise all work is done.
            if (sched->waiting_io_count > 0 || !co_timer_heap_empty(&sched->timer_heap)) {
                int ready_count = 0;
                co_iomux_poll(sched->iomux, io_timeout_ms, &ready_count);
                // I/O-ready coroutines have already been requeued automatically
            }
            
            // Recheck the ready queue
            if (co_queue_empty(&sched->ready_queue)) {
                // Still no ready coroutines; exit if no timers remain
                if (co_timer_heap_empty(&sched->timer_heap)) {
                    break;  // No coroutine is running or waiting anymore
                }
            }
            
            // Continue the loop and recheck timers and ready coroutines
            continue;
        }
        
        // Schedule one coroutine
        co_error_t err = co_scheduler_schedule(sched);
        if (err != CO_OK) {
            sched->running = false;
            tls_current_scheduler = NULL;
            return err;
        }
    }
    
    sched->running = false;
    tls_current_scheduler = NULL;
    
    return CO_OK;
}

// ============================================================================
// Scheduling logic
// ============================================================================

co_error_t co_scheduler_schedule(co_scheduler_t *sched) {
    assert(sched != NULL);
    
    // Pop the next coroutine from the ready queue
    co_routine_t *next = co_scheduler_dequeue(sched);
    if (!next) {
        return CO_OK;  // No runnable coroutine
    }
    
    // Debug output
    #ifdef DEBUG_SCHED
    fprintf(stderr, "[SCHED] Scheduling routine %lu, state=%d\n", 
            next->id, next->state);
    #endif
    
    // Save the previous coroutine for possible future direct coroutine switching support
    // co_routine_t *prev = sched->current;

    // Set the current coroutine
    sched->current = next;
    next->state = CO_STATE_RUNNING;
    
    // Increment the context switch counter
    sched->switch_count++;

    // Switch from the main scheduler context into the coroutine.
    // The scheduler always enters through main_ctx instead of coroutine-to-coroutine switching.
    co_error_t err = co_context_swap(&sched->main_ctx, &next->context);
    
    // Note: execution returns here in two cases:
    // 1. another coroutine yields back
    // 2. the current coroutine finishes
    
    // Clear the current coroutine pointer after yield or completion
    sched->current = NULL;
    
    // Clean up finished coroutines
    if (next->state == CO_STATE_DEAD) {
        // Coroutine is done; destroy and free it
        co_routine_destroy(next);
        free(next);
    }
    
    return err;
}

void co_scheduler_enqueue(co_scheduler_t *sched, co_routine_t *routine) {
    assert(sched != NULL);
    assert(routine != NULL);
    
    // Append the coroutine to the end of the ready queue
    co_queue_push_back(&sched->ready_queue, &routine->queue_node);
    routine->state = CO_STATE_READY;
}

co_routine_t *co_scheduler_dequeue(co_scheduler_t *sched) {
    assert(sched != NULL);
    
    // Pop a coroutine from the front of the ready queue
    co_queue_node_t *node = co_queue_pop_front(&sched->ready_queue);
    if (!node) {
        return NULL;
    }
    
    // Compute the coroutine pointer via offset arithmetic (intrusive list)
    co_routine_t *routine = (co_routine_t *)((char *)node - offsetof(co_routine_t, queue_node));
    return routine;
}

// ============================================================================
// Global accessors
// ============================================================================

co_scheduler_t *co_current_scheduler(void) {
    return tls_current_scheduler;
}

co_routine_t *co_current(void) {
    co_scheduler_t *sched = tls_current_scheduler;
    return sched ? sched->current : NULL;
}

co_routine_t *co_current_routine(void) {
    return co_current();  // Alias
}
