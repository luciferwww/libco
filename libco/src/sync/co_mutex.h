/**
 * @file co_mutex.h
 * @brief Internal coroutine mutex structure
 */

#ifndef LIBCO_CO_MUTEX_H
#define LIBCO_CO_MUTEX_H

#include "../co_queue.h"
#include "../co_routine.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Coroutine mutex
 * 
 * Not thread-safe and intended for use within a single-threaded scheduler.
 * On contention, the coroutine is suspended without blocking the scheduler
 * thread. Waiters are resumed in FIFO order to avoid starvation.
 */
struct co_mutex {
    bool locked;                /**< Whether the mutex is currently locked */
    co_routine_t *owner;        /**< Current owner, used only for assertions */
    co_queue_t wait_queue;      /**< FIFO queue of coroutines waiting for the mutex */
};

#ifdef __cplusplus
}
#endif

#endif // LIBCO_CO_MUTEX_H
