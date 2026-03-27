/**
 * @file co_channel.c
 * @brief Coroutine channel implementation
 *
 * Design notes:
 * - Buffered channels (capacity > 0): send returns immediately if space exists;
 *   recv suspends when the buffer is empty
 * - Unbuffered channels (capacity == 0): send waits for a receiver so both
 *   sides continue after a rendezvous
 * - Suspension uses co_context_swap() instead of co_yield() to avoid placing a
 *   waiting coroutine back into the ready queue by mistake
 * - Wait queues reuse each coroutine's queue_node while it is WAITING
 * - chan_data temporarily stores the send source pointer or recv destination
 *   buffer pointer
 *
 * Send priority for buffered channels:
 *   1. Waiting receiver -> hand off data directly and wake the receiver
 *   2. Buffer has space -> write into the buffer
 *   3. Otherwise suspend until a receiver resumes the sender
 *
 * Receive priority for buffered channels:
 *   1. Buffer has data -> read it, then refill from send_queue if possible
 *   2. Waiting sender in unbuffered rendezvous -> copy directly from sender
 *   3. Closed channel -> return CO_ERROR_CLOSED
 *   4. Otherwise suspend
 */

#include "co_channel.h"
#include "../co_scheduler.h"
#include "../co_routine.h"
#include <libco/co_sync.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <assert.h>

/* Recover the co_routine_t pointer from a queue_node */
static inline co_routine_t *node_to_routine(co_queue_node_t *node) {
    return (co_routine_t *)((char *)node - offsetof(co_routine_t, queue_node));
}

/* Put a coroutine back into the ready queue */
static inline void wake_routine(co_routine_t *co) {
    co->state = CO_STATE_READY;
    co_queue_push_back(&co->scheduler->ready_queue, &co->queue_node);
}

// ============================================================================
// Creation and destruction
// ============================================================================

co_channel_t *co_channel_create(size_t elem_size, size_t capacity) {
    if (elem_size == 0) {
        return NULL;
    }
    co_channel_t *ch = (co_channel_t *)calloc(1, sizeof(co_channel_t));
    if (!ch) {
        return NULL;
    }
    ch->elem_size = elem_size;
    ch->capacity  = capacity;
    ch->buffer    = (capacity > 0) ? malloc(elem_size * capacity) : NULL;
    if (capacity > 0 && !ch->buffer) {
        free(ch);
        return NULL;
    }
    co_queue_init(&ch->send_queue);
    co_queue_init(&ch->recv_queue);
    return ch;
}

co_error_t co_channel_destroy(co_channel_t *ch) {
    if (!ch) {
        return CO_ERROR_INVAL;
    }
    assert(co_queue_empty(&ch->send_queue) && "destroying channel with pending senders");
    assert(co_queue_empty(&ch->recv_queue) && "destroying channel with pending receivers");
    free(ch->buffer);
    free(ch);
    return CO_OK;
}

// ============================================================================
// Send
// ============================================================================

co_error_t co_channel_send(co_channel_t *ch, const void *data) {
    if (!ch || !data) {
        return CO_ERROR_INVAL;
    }
    if (ch->closed) {
        return CO_ERROR_CLOSED;
    }

    /* 1. Waiting receiver: hand off directly, including unbuffered rendezvous */
    co_queue_node_t *node = co_queue_pop_front(&ch->recv_queue);
    if (node) {
        co_routine_t *receiver = node_to_routine(node);
        memcpy(receiver->chan_data, data, ch->elem_size);
        wake_routine(receiver);
        return CO_OK;
    }

    /* 2. Buffered and not full: write into the buffer */
    if (ch->count < ch->capacity) {
        void *dest = (char *)ch->buffer + ch->tail * ch->elem_size;
        memcpy(dest, data, ch->elem_size);
        ch->tail = (ch->tail + 1) % ch->capacity;
        ch->count++;
        return CO_OK;
    }

    /* 3. Block by suspending until a receiver wakes the current coroutine */
    co_scheduler_t *sched   = co_current_scheduler();
    co_routine_t   *current = co_current_routine();
    if (!sched || !current) {
        return CO_ERROR_INVAL;
    }

    current->chan_data = (void *)data;   /* Points to caller stack data, valid for the send duration */
    current->state     = CO_STATE_WAITING;
    co_queue_push_back(&ch->send_queue, &current->queue_node);

    co_context_swap(&current->context, &sched->main_ctx);

    /* After resuming, check whether the channel was closed while suspended */
    return ch->closed ? CO_ERROR_CLOSED : CO_OK;
}

co_error_t co_channel_trysend(co_channel_t *ch, const void *data) {
    if (!ch || !data) {
        return CO_ERROR_INVAL;
    }
    if (ch->closed) {
        return CO_ERROR_CLOSED;
    }

    co_queue_node_t *node = co_queue_pop_front(&ch->recv_queue);
    if (node) {
        co_routine_t *receiver = node_to_routine(node);
        memcpy(receiver->chan_data, data, ch->elem_size);
        wake_routine(receiver);
        return CO_OK;
    }

    if (ch->count < ch->capacity) {
        void *dest = (char *)ch->buffer + ch->tail * ch->elem_size;
        memcpy(dest, data, ch->elem_size);
        ch->tail = (ch->tail + 1) % ch->capacity;
        ch->count++;
        return CO_OK;
    }

    return CO_ERROR_BUSY;   /* Buffer is full and no receiver is waiting */
}

// ============================================================================
// Receive
// ============================================================================

co_error_t co_channel_recv(co_channel_t *ch, void *data) {
    if (!ch || !data) {
        return CO_ERROR_INVAL;
    }

    /* 1. Buffered data is available */
    if (ch->count > 0) {
        void *src = (char *)ch->buffer + ch->head * ch->elem_size;
        memcpy(data, src, ch->elem_size);
        ch->head = (ch->head + 1) % ch->capacity;
        ch->count--;

        /* Space was freed, so wake one waiting sender and move its data into the buffer */
        co_queue_node_t *node = co_queue_pop_front(&ch->send_queue);
        if (node) {
            co_routine_t *sender = node_to_routine(node);
            void *dest = (char *)ch->buffer + ch->tail * ch->elem_size;
            memcpy(dest, sender->chan_data, ch->elem_size);
            ch->tail = (ch->tail + 1) % ch->capacity;
            ch->count++;
            wake_routine(sender);
        }
        return CO_OK;
    }

    /* 2. Buffer is empty but a sender is waiting, including unbuffered rendezvous */
    co_queue_node_t *node = co_queue_pop_front(&ch->send_queue);
    if (node) {
        co_routine_t *sender = node_to_routine(node);
        memcpy(data, sender->chan_data, ch->elem_size);
        wake_routine(sender);
        return CO_OK;
    }

    /* 3. Channel is closed and the buffer is empty */
    if (ch->closed) {
        return CO_ERROR_CLOSED;
    }

    /* 4. Suspend until a sender wakes the current coroutine */
    co_scheduler_t *sched   = co_current_scheduler();
    co_routine_t   *current = co_current_routine();
    if (!sched || !current) {
        return CO_ERROR_INVAL;
    }

    current->chan_data = data;
    current->state     = CO_STATE_WAITING;
    co_queue_push_back(&ch->recv_queue, &current->queue_node);

    co_context_swap(&current->context, &sched->main_ctx);

    return ch->closed ? CO_ERROR_CLOSED : CO_OK;
}

co_error_t co_channel_tryrecv(co_channel_t *ch, void *data) {
    if (!ch || !data) {
        return CO_ERROR_INVAL;
    }

    if (ch->count > 0) {
        void *src = (char *)ch->buffer + ch->head * ch->elem_size;
        memcpy(data, src, ch->elem_size);
        ch->head = (ch->head + 1) % ch->capacity;
        ch->count--;

        co_queue_node_t *node = co_queue_pop_front(&ch->send_queue);
        if (node) {
            co_routine_t *sender = node_to_routine(node);
            void *dest = (char *)ch->buffer + ch->tail * ch->elem_size;
            memcpy(dest, sender->chan_data, ch->elem_size);
            ch->tail = (ch->tail + 1) % ch->capacity;
            ch->count++;
            wake_routine(sender);
        }
        return CO_OK;
    }

    co_queue_node_t *node = co_queue_pop_front(&ch->send_queue);
    if (node) {
        co_routine_t *sender = node_to_routine(node);
        memcpy(data, sender->chan_data, ch->elem_size);
        wake_routine(sender);
        return CO_OK;
    }

    if (ch->closed) {
        return CO_ERROR_CLOSED;
    }
    return CO_ERROR_BUSY;   /* Empty and no sender is waiting */
}

// ============================================================================
// Close and state queries
// ============================================================================

co_error_t co_channel_close(co_channel_t *ch) {
    if (!ch) {
        return CO_ERROR_INVAL;
    }
    if (ch->closed) {
        return CO_ERROR_CLOSED;
    }
    ch->closed = true;

    /*
     * Wake all waiting receivers so they observe CO_ERROR_CLOSED.
     * Waiting senders behave similarly and fail when sending to a closed channel.
     */
    co_queue_node_t *node;
    while ((node = co_queue_pop_front(&ch->recv_queue)) != NULL) {
        wake_routine(node_to_routine(node));
    }
    while ((node = co_queue_pop_front(&ch->send_queue)) != NULL) {
        wake_routine(node_to_routine(node));
    }
    return CO_OK;
}

size_t co_channel_len(const co_channel_t *ch) {
    return ch ? ch->count : 0;
}

size_t co_channel_cap(const co_channel_t *ch) {
    return ch ? ch->capacity : 0;
}

bool co_channel_is_closed(const co_channel_t *ch) {
    return ch ? ch->closed : true;
}
