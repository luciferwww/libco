/**
 * @file co_scheduler.h
 * @brief Internal scheduler data structures
 * 
 * Defines the internal representation of the scheduler.
 */

#ifndef LIBCO_CO_SCHEDULER_H
#define LIBCO_CO_SCHEDULER_H

#include <libco/co.h>
#include "co_queue.h"
#include "co_routine.h"
#include "co_stack_pool.h"
#include "co_timer.h"
#include "co_iomux.h"
#include "platform/context.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Scheduler structure
// ============================================================================

/**
 * @brief Scheduler structure
 */
struct co_scheduler {
    // Ready queue (FIFO)
    co_queue_t ready_queue;
    
    // Week 6: Timer heap for sleeping coroutines
    co_timer_heap_t timer_heap;
    
    // Week 7: I/O multiplexer context
    co_iomux_t *iomux;             /**< I/O multiplexer (epoll/kqueue/IOCP) */
    
    // Currently running coroutine
    co_routine_t *current;
    
    // Main scheduler context
    co_context_t main_ctx;
    
    // Stack pool (Week 5)
    co_stack_pool_t *stack_pool;
    
    // Configuration
    size_t default_stack_size;  /**< Default stack size */
    
    // Runtime state
    bool running;               /**< Whether the scheduler is running */
    bool should_stop;           /**< Whether the scheduler should stop */
    
    // Statistics
    uint64_t total_routines;    /**< Total number of created coroutines */
    uint64_t active_routines;   /**< Number of currently active coroutines */
    uint64_t switch_count;      /**< Number of context switches */
    uint32_t waiting_io_count;  /**< Number of coroutines waiting for I/O */
    
    // ID generator
    uint64_t next_id;           /**< Next coroutine ID */
};

// ============================================================================
// Scheduler function declarations
// ============================================================================

/**
 * @brief Schedule the next coroutine
 * @param sched Scheduler pointer
 * @return CO_OK on success, other values indicate errors
 */
co_error_t co_scheduler_schedule(co_scheduler_t *sched);

/**
 * @brief Enqueue a coroutine in the ready queue
 * @param sched Scheduler pointer
 * @param routine Coroutine pointer
 */
void co_scheduler_enqueue(co_scheduler_t *sched, co_routine_t *routine);

/**
 * @brief Dequeue the next coroutine from the ready queue
 * @param sched Scheduler pointer
 * @return Coroutine pointer, or NULL if the queue is empty
 */
co_routine_t *co_scheduler_dequeue(co_scheduler_t *sched);

/**
 * @brief Get the scheduler for the current thread
 * @return Scheduler pointer, or NULL if not running
 */
co_scheduler_t *co_current_scheduler(void);

/**
 * @brief Get the currently running coroutine
 * @return Coroutine pointer, or NULL if none is running
 */
co_routine_t *co_current_routine(void);

#ifdef __cplusplus
}
#endif

#endif // LIBCO_CO_SCHEDULER_H
