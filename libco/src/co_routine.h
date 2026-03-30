/**
 * @file co_routine.h
 * @brief Internal coroutine data structures
 * 
 * Defines the internal representation of a coroutine.
 */

#ifndef LIBCO_CO_ROUTINE_H
#define LIBCO_CO_ROUTINE_H

#include <libco/co.h>
#include "co_queue.h"
#include "platform/context.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration
struct co_scheduler;

// ============================================================================
// Coroutine structure
// ============================================================================

/**
 * @brief Coroutine structure
 */
struct co_routine {
    // Unique ID
    uint64_t id;
    
    // State
    co_state_t state;
    
    // Context
    co_context_t context;
    
    // Stack information
    void *stack_base;
    size_t stack_size;
    
    // Entry function
    co_entry_func_t entry;
    void *arg;
    
    // Owning scheduler
    struct co_scheduler *scheduler;
    
    // Queue node used by the ready queue
    co_queue_node_t queue_node;
    
    // Week 6: Timer support
    uint64_t wakeup_time;          /**< Wakeup time in milliseconds */

    // Week 10: Channel support
    void *chan_data;               /**< Data pointer used during channel send/recv */

    // CondVar timedwait support
    co_queue_t *cond_wait_queue;   /**< Condition wait queue for timedwait, or NULL */
    bool timed_out;                /**< Whether the coroutine was resumed by timeout */

    // I/O timeout support
    bool io_waiting;               /**< Whether the coroutine is waiting for I/O */

    // TODO(Week 7-8): Add I/O wait support
    // int wait_fd;                   // File descriptor being waited on
    // uint32_t wait_events;          // Events being waited on (read/write)
    
    // TODO(v2.1): Add lifecycle management support
    // co_queue_t join_waiters;       // Coroutines waiting for this coroutine to finish
    // co_routine_t *cancel_target;   // Target coroutine to cancel
    
    // Debug information
    const char *name;  /**< Optional coroutine name */
    
    // Lifecycle management
    bool detached;            /**< Whether the coroutine is detached */
    void *result;             /**< Return value */
};

// ============================================================================
// Coroutine function declarations
// ============================================================================

/**
 * @brief Initialize a coroutine
 * @param routine Coroutine pointer
 * @param scheduler Owning scheduler
 * @param entry Entry function
 * @param arg User argument
 * @param stack_size Stack size
 * @return CO_OK on success, other values indicate errors
 */
co_error_t co_routine_init(co_routine_t *routine,
                           struct co_scheduler *scheduler,
                           co_entry_func_t entry,
                           void *arg,
                           size_t stack_size);

/**
 * @brief Destroy a coroutine
 * @param routine Coroutine pointer
 */
void co_routine_destroy(co_routine_t *routine);

/**
 * @brief Mark a coroutine as finished
 * @param routine Coroutine pointer
 * @param result Return value
 */
void co_routine_finish(co_routine_t *routine, void *result);

#ifdef __cplusplus
}
#endif

#endif // LIBCO_CO_ROUTINE_H
