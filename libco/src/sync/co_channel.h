/**
 * @file co_channel.h
 * @brief Internal coroutine channel structure definition
 */

#ifndef LIBCO_SYNC_CO_CHANNEL_H
#define LIBCO_SYNC_CO_CHANNEL_H

#include "../co_queue.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Internal channel structure
 *
 * Bounded ring buffer plus two wait queues (send_queue and recv_queue).
 * capacity == 0 creates an unbuffered channel with rendezvous semantics.
 *
 * Wait queues reuse each coroutine's queue_node because it is free while the
 * coroutine is in the WAITING state. chan_data temporarily stores the source
 * pointer for send or the destination pointer for recv.
 */
struct co_channel {
    size_t elem_size;   /**< Size of each element in bytes */
    size_t capacity;    /**< Buffer capacity, or 0 for unbuffered */

    /* Ring buffer */
    void   *buffer;
    size_t  head;       /**< Next dequeue position */
    size_t  tail;       /**< Next enqueue position */
    size_t  count;      /**< Number of buffered elements */

    /* Wait queues */
    co_queue_t send_queue;  /**< Coroutines waiting to send when the buffer is full */
    co_queue_t recv_queue;  /**< Coroutines waiting to receive when the buffer is empty */

    bool closed;
};

#ifdef __cplusplus
}
#endif

#endif /* LIBCO_SYNC_CO_CHANNEL_H */
