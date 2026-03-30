/**
 * @file co_cond.h
 * @brief Internal coroutine condition variable structure
 */

#ifndef LIBCO_CO_COND_H
#define LIBCO_CO_COND_H

#include "../co_queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Coroutine condition variable
 * 
 * Coroutines in the wait queue remain in the WAITING state and do not occupy
 * the ready_queue. signal wakes one waiter at the front, while broadcast wakes
 * all waiters.
 */
struct co_cond {
    co_queue_t wait_queue;  /**< FIFO queue of coroutines waiting on the condition */
};

#ifdef __cplusplus
}
#endif

#endif // LIBCO_CO_COND_H
