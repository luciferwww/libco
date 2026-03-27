/**
 * @file co_channel.c
 * @brief 协程 Channel 实现
 *
 * 设计要点：
 * - 有缓冲（capacity > 0）：缓冲区未满时 send 立即返回；缓冲区空时 recv 挂起
 * - 无缓冲（capacity == 0）：send 必须等到有 recv 方，双方握手后同时继续
 * - 挂起用 co_context_swap() 而非 co_yield()，避免协程错误地重新入就绪队列
 * - 等待队列复用协程的 queue_node（WAITING 状态下节点可安全复用）
 * - chan_data 指针临时存放 send 的源数据指针 或 recv 的目标缓冲区指针
 *
 * send 优先级（有缓冲）：
 *   1. 有等待的 receiver → 直接交付，唤醒 receiver
 *   2. 缓冲区未满 → 写入缓冲区
 *   3. 否则挂起，等 receiver 来唤醒
 *
 * recv 优先级（有缓冲）：
 *   1. 缓冲区有数据 → 读出，并从 send_queue 取一个 sender 填充缓冲区
 *   2. 有等待的 sender（无缓冲 rendezvous）→ 直接从 sender 复制数据
 *   3. channel 已关闭 → 返回 CO_ERROR_CLOSED
 *   4. 否则挂起
 */

#include "co_channel.h"
#include "../co_scheduler.h"
#include "../co_routine.h"
#include <libco/co_sync.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <assert.h>

/* 从 queue_node 还原 co_routine_t 指针 */
static inline co_routine_t *node_to_routine(co_queue_node_t *node) {
    return (co_routine_t *)((char *)node - offsetof(co_routine_t, queue_node));
}

/* 将协程放回就绪队列 */
static inline void wake_routine(co_routine_t *co) {
    co->state = CO_STATE_READY;
    co_queue_push_back(&co->scheduler->ready_queue, &co->queue_node);
}

// ============================================================================
// 创建 / 销毁
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
// 发送
// ============================================================================

co_error_t co_channel_send(co_channel_t *ch, const void *data) {
    if (!ch || !data) {
        return CO_ERROR_INVAL;
    }
    if (ch->closed) {
        return CO_ERROR_CLOSED;
    }

    /* 1. 有等待的接收者：直接交付（无缓冲 rendezvous 也走这条路） */
    co_queue_node_t *node = co_queue_pop_front(&ch->recv_queue);
    if (node) {
        co_routine_t *receiver = node_to_routine(node);
        memcpy(receiver->chan_data, data, ch->elem_size);
        wake_routine(receiver);
        return CO_OK;
    }

    /* 2. 有缓冲且未满：写入缓冲区 */
    if (ch->count < ch->capacity) {
        void *dest = (char *)ch->buffer + ch->tail * ch->elem_size;
        memcpy(dest, data, ch->elem_size);
        ch->tail = (ch->tail + 1) % ch->capacity;
        ch->count++;
        return CO_OK;
    }

    /* 3. 阻塞：挂起当前协程，等待接收者来唤醒 */
    co_scheduler_t *sched   = co_current_scheduler();
    co_routine_t   *current = co_current_routine();
    if (!sched || !current) {
        return CO_ERROR_INVAL;
    }

    current->chan_data = (void *)data;   /* 指向调用者栈上的数据，send 期间有效 */
    current->state     = CO_STATE_WAITING;
    co_queue_push_back(&ch->send_queue, &current->queue_node);

    co_context_swap(&current->context, &sched->main_ctx);

    /* 被唤醒后检查 channel 是否在挂起期间被关闭 */
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

    return CO_ERROR_BUSY;   /* 缓冲区满且无等待 receiver */
}

// ============================================================================
// 接收
// ============================================================================

co_error_t co_channel_recv(co_channel_t *ch, void *data) {
    if (!ch || !data) {
        return CO_ERROR_INVAL;
    }

    /* 1. 缓冲区有数据 */
    if (ch->count > 0) {
        void *src = (char *)ch->buffer + ch->head * ch->elem_size;
        memcpy(data, src, ch->elem_size);
        ch->head = (ch->head + 1) % ch->capacity;
        ch->count--;

        /* 缓冲区腾出空间，唤醒一个等待的 sender 并将其数据写入缓冲区 */
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

    /* 2. 缓冲区空，但有等待的 sender（含无缓冲 rendezvous） */
    co_queue_node_t *node = co_queue_pop_front(&ch->send_queue);
    if (node) {
        co_routine_t *sender = node_to_routine(node);
        memcpy(data, sender->chan_data, ch->elem_size);
        wake_routine(sender);
        return CO_OK;
    }

    /* 3. channel 已关闭且缓冲区空 */
    if (ch->closed) {
        return CO_ERROR_CLOSED;
    }

    /* 4. 阻塞：挂起当前协程，等待发送者来唤醒 */
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
    return CO_ERROR_BUSY;   /* 空且无等待 sender */
}

// ============================================================================
// 关闭 / 状态查询
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
     * 唤醒所有等待的接收者，它们醒来后会读到 CO_ERROR_CLOSED。
     * 等待的发送者同理（向已关闭 channel 发送返回错误）。
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
