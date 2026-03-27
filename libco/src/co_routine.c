/**
 * @file co_routine.c
 * @brief Coroutine implementation
 * 
 * Implements coroutine lifecycle management.
 */

#include "co_routine.h"
#include "co_scheduler.h"
#include "co_timer.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// ============================================================================
// Coroutine entry wrapper
// ============================================================================

/**
 * @brief Coroutine entry wrapper function
 * 
 * This function is the actual entry point for every coroutine.
 * It invokes the user entry function and handles coroutine termination when
 * that function returns.
 * 
 * @param arg Coroutine pointer
 */
static void co_routine_entry_wrapper(void *arg) {
    co_routine_t *routine = (co_routine_t *)arg;
    assert(routine != NULL);
    assert(routine->entry != NULL);
    
    // Call the user entry function
    routine->entry(routine->arg);
    
    // Mark the coroutine as finished after the user function returns
    co_routine_finish(routine, NULL);
}

// ============================================================================
// Coroutine initialization and destruction
// ============================================================================

co_error_t co_routine_init(co_routine_t *routine,
                           struct co_scheduler *scheduler,
                           co_entry_func_t entry,
                           void *arg,
                           size_t stack_size) {
    if (!routine || !scheduler || !entry) {
        return CO_ERROR_INVAL;
    }
    
    // Use the default stack size
    if (stack_size == 0) {
        stack_size = scheduler->default_stack_size;
    }
    
    // Allocate a stack from the stack pool (Week 5)
    void *stack = co_stack_pool_alloc(scheduler->stack_pool);
    if (!stack) {
        return CO_ERROR_NOMEM;
    }
    
    // Initialize fields
    routine->id = scheduler->next_id++;
    routine->state = CO_STATE_READY;
    routine->stack_base = stack;
    routine->stack_size = stack_size;
    routine->entry = entry;
    routine->arg = arg;
    routine->scheduler = scheduler;
    routine->name = NULL;       // TODO(v2.1): Implement co_routine_set_name()
    routine->detached = false;  // TODO(v2.1): Implement co_routine_detach()
    routine->result = NULL;     // TODO(v2.1): Implement co_await() result retrieval
    
    // Initialize the queue node
    co_queue_node_init(&routine->queue_node);
    
    // Initialize the context
    // Note: pass the routine itself as the wrapper argument
    co_error_t err = co_context_init(&routine->context,
                                     stack,
                                     stack_size,
                                     co_routine_entry_wrapper,
                                     routine);
    if (err != CO_OK) {
        co_stack_pool_free(scheduler->stack_pool, stack);
        return err;
    }
    
    return CO_OK;
}

void co_routine_destroy(co_routine_t *routine) {
    if (!routine) {
        return;
    }
    
    // Destroy the context
    co_context_destroy(&routine->context);
    
    // Return the stack to the stack pool (Week 5)
    if (routine->stack_base && routine->scheduler && routine->scheduler->stack_pool) {
        co_stack_pool_free(routine->scheduler->stack_pool, routine->stack_base);
        routine->stack_base = NULL;
    }
    
    // Clear fields
    routine->scheduler = NULL;
    routine->entry = NULL;
    routine->arg = NULL;
}

void co_routine_finish(co_routine_t *routine, void *result) {
    assert(routine != NULL);
    assert(routine->scheduler != NULL);
    
    // Mark as finished
    routine->state = CO_STATE_DEAD;
    routine->result = result;
    
    // Update scheduler statistics
    routine->scheduler->active_routines--;
    
    // Switch back to the scheduler
    // Note: this call never returns because the coroutine has ended
    co_context_swap(&routine->context, &routine->scheduler->main_ctx);
    
    // This point should never be reached
    assert(0 && "co_routine_finish: should not reach here");
}

// ============================================================================
// Coroutine creation API
// ============================================================================

co_routine_t *co_spawn(co_scheduler_t *sched,
                       co_entry_func_t entry,
                       void *arg,
                       size_t stack_size) {
    // Use the current thread scheduler if none was provided
    if (!sched) {
        sched = co_current_scheduler();
    }
    
    if (!sched || !entry) {
        return NULL;
    }
    
    // Allocate the coroutine
    co_routine_t *routine = (co_routine_t *)calloc(1, sizeof(co_routine_t));
    if (!routine) {
        return NULL;
    }
    
    // Initialize the coroutine
    co_error_t err = co_routine_init(routine, sched, entry, arg, stack_size);
    if (err != CO_OK) {
        free(routine);
        return NULL;
    }
    
    // Update scheduler statistics
    sched->total_routines++;
    sched->active_routines++;
    
    // Enqueue the coroutine in the ready queue
    co_scheduler_enqueue(sched, routine);
    
    return routine;
}

// ============================================================================
// Coroutine control API
// ============================================================================

co_error_t co_yield_now(void) {
    // TODO(v2.1): Support co_yield_value() return values to the caller
    // TODO(v2.1): Support co_yield_to(routine) for explicit coroutine switches
    
    co_routine_t *current = co_current();
    if (!current) {
        return CO_ERROR;  // Not running inside a coroutine
    }
    
    co_scheduler_t *sched = current->scheduler;
    assert(sched != NULL);
    
    // Requeue the current coroutine at the back of the ready queue
    co_scheduler_enqueue(sched, current);
    
    // Switch back to the scheduler
    return co_context_swap(&current->context, &sched->main_ctx);
}

co_error_t co_sleep(uint32_t msec) {
    // A zero-duration sleep is equivalent to yield
    if (msec == 0) {
        return co_yield_now();
    }
    
    co_routine_t *current = co_current();
    if (!current) {
        return CO_ERROR;  // Not running inside a coroutine
    }
    
    co_scheduler_t *sched = current->scheduler;
    assert(sched != NULL);
    
    // Compute the wakeup time
    uint64_t now = co_get_monotonic_time_ms();
    current->wakeup_time = now + msec;
    
    // Mark the coroutine as waiting for the sleep to finish
    current->state = CO_STATE_WAITING;
    
    // Insert into the timer heap
    if (!co_timer_heap_push(&sched->timer_heap, current)) {
        // Insertion failed due to memory pressure, restore the state
        current->state = CO_STATE_READY;
        return CO_ERROR_NOMEM;
    }
    
    // Switch back to the scheduler
    return co_context_swap(&current->context, &sched->main_ctx);
}
