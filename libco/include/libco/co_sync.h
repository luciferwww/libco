/**
 * @file co_sync.h
 * @brief libco synchronization primitives API
 * 
 * Provides coroutine-level mutexes and condition variables.
 * These primitives synchronize between coroutines without blocking the entire
 * thread. When lock contention occurs, the current coroutine is suspended and
 * control returns to the scheduler so other coroutines can continue running.
 */

#ifndef LIBCO_CO_SYNC_H
#define LIBCO_CO_SYNC_H

#include <libco/co.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Opaque type declarations
// ============================================================================

/**
 * @brief Coroutine mutex (opaque handle)
 */
typedef struct co_mutex co_mutex_t;

/**
 * @brief Coroutine condition variable (opaque handle)
 */
typedef struct co_cond co_cond_t;

// ============================================================================
// Mutex API
// ============================================================================

/**
 * @brief Create a mutex
 * @param attr Reserved for future extensions; pass NULL for now
 * @return Mutex handle, or NULL on failure
 */
co_mutex_t *co_mutex_create(const void *attr);

/**
 * @brief Destroy a mutex
 * @param mutex Mutex handle
 * @return CO_OK on success, CO_ERROR_INVAL for an invalid argument
 * @note Ensure that no coroutine is waiting on this mutex before destroying it
 */
co_error_t co_mutex_destroy(co_mutex_t *mutex);

/**
 * @brief Lock a mutex, blocking until successful
 * 
 * If the mutex is held by another coroutine, the current coroutine is
 * suspended and control returns to the scheduler. It is resumed after the
 * mutex is released. This does not block the scheduler thread.
 * 
 * @param mutex Mutex handle
 * @return CO_OK on success, CO_ERROR_INVAL for an invalid argument
 */
co_error_t co_mutex_lock(co_mutex_t *mutex);

/**
 * @brief Try to lock a mutex without blocking
 * 
 * @param mutex Mutex handle
 * @return CO_OK if the lock was acquired, CO_ERROR_BUSY if it is already held
 */
co_error_t co_mutex_trylock(co_mutex_t *mutex);

/**
 * @brief Unlock a mutex
 * 
 * Release the lock. If coroutines are waiting, wake one of them in FIFO order.
 * 
 * @param mutex Mutex handle
 * @return CO_OK on success, CO_ERROR_INVAL for an invalid argument
 */
co_error_t co_mutex_unlock(co_mutex_t *mutex);

// ============================================================================
// Condition variable API
// ============================================================================

/**
 * @brief Create a condition variable
 * @param attr Reserved for future extensions; pass NULL for now
 * @return Condition variable handle, or NULL on failure
 */
co_cond_t *co_cond_create(const void *attr);

/**
 * @brief Destroy a condition variable
 * @param cond Condition variable handle
 * @return CO_OK on success, CO_ERROR_INVAL for an invalid argument
 */
co_error_t co_cond_destroy(co_cond_t *cond);

/**
 * @brief Wait on a condition variable
 * 
 * Atomically release mutex and suspend the current coroutine. After being
 * awakened by co_cond_signal or co_cond_broadcast, the mutex is reacquired
 * before returning.
 * 
 * @param cond Condition variable handle
 * @param mutex Locked mutex
 * @return CO_OK on success
 */
co_error_t co_cond_wait(co_cond_t *cond, co_mutex_t *mutex);

/**
 * @brief Wait with a timeout
 * 
 * @param cond Condition variable handle
 * @param mutex Locked mutex
 * @param timeout_ms Relative wait time in milliseconds from the call time
 * @return CO_OK on success, CO_ERROR_TIMEOUT on timeout
 */
co_error_t co_cond_timedwait(co_cond_t *cond, co_mutex_t *mutex,
                              uint32_t timeout_ms);

/**
 * @brief Wake one waiting coroutine in FIFO order
 * @param cond Condition variable handle
 * @return CO_OK on success
 */
co_error_t co_cond_signal(co_cond_t *cond);

/**
 * @brief Wake all waiting coroutines
 * @param cond Condition variable handle
 * @return CO_OK on success
 */
co_error_t co_cond_broadcast(co_cond_t *cond);

// ============================================================================
// Channel API (Go-style, Week 10)
// ============================================================================

/**
 * @brief Opaque channel handle
 */
typedef struct co_channel co_channel_t;

/**
 * @brief Create a channel
 * @param elem_size Size in bytes of each element, must be greater than 0
 * @param capacity Buffer capacity; 0 creates an unbuffered channel with
 *        rendezvous semantics
 * @return Channel handle, or NULL on failure
 */
co_channel_t *co_channel_create(size_t elem_size, size_t capacity);

/**
 * @brief Destroy a channel after all coroutines have stopped using it
 * @param ch Channel handle
 * @return CO_OK on success, CO_ERROR_INVAL for an invalid argument
 */
co_error_t co_channel_destroy(co_channel_t *ch);

/**
 * @brief Send data to a channel, blocking until successful
 *
 * If the buffer is not full, the data is written immediately and the call
 * returns. If the buffer is full, the current coroutine is suspended until a
 * receiver consumes data.
 *
 * @param ch Channel handle
 * @param data Pointer to the data to send; elem_size bytes are copied
 * @return CO_OK on success, CO_ERROR_CLOSED if the channel is closed,
 *         CO_ERROR_INVAL for an invalid argument
 */
co_error_t co_channel_send(co_channel_t *ch, const void *data);

/**
 * @brief Receive data from a channel, blocking until successful
 *
 * If buffered data is available, it is read immediately and returned. If the
 * channel is empty, the current coroutine is suspended until a sender writes
 * data. If the channel is closed and the buffer is drained, CO_ERROR_CLOSED is
 * returned.
 *
 * @param ch Channel handle
 * @param data Receive buffer; elem_size bytes are copied into it
 * @return CO_OK on success, CO_ERROR_CLOSED if the channel is closed and no
 *         data remains
 */
co_error_t co_channel_recv(co_channel_t *ch, void *data);

/**
 * @brief Try to send without blocking
 * @return CO_OK on success, CO_ERROR_BUSY if the buffer is full,
 *         CO_ERROR_CLOSED if the channel is closed
 */
co_error_t co_channel_trysend(co_channel_t *ch, const void *data);

/**
 * @brief Try to receive without blocking
 * @return CO_OK on success, CO_ERROR_BUSY if the buffer is empty,
 *         CO_ERROR_CLOSED if the channel is closed and no data remains
 */
co_error_t co_channel_tryrecv(co_channel_t *ch, void *data);

/**
 * @brief Close a channel
 *
 * Wake all waiting receivers and senders, both of which return
 * CO_ERROR_CLOSED. Calling this function on an already closed channel also
 * returns CO_ERROR_CLOSED.
 *
 * @param ch Channel handle
 * @return CO_OK on success, CO_ERROR_CLOSED if it is already closed
 */
co_error_t co_channel_close(co_channel_t *ch);

/**
 * @brief Get the current number of buffered elements in a channel
 */
size_t co_channel_len(const co_channel_t *ch);

/**
 * @brief Get the channel buffer capacity
 */
size_t co_channel_cap(const co_channel_t *ch);

/**
 * @brief Check whether a channel has been closed
 */
bool co_channel_is_closed(const co_channel_t *ch);

#ifdef __cplusplus
}
#endif

#endif // LIBCO_CO_SYNC_H
