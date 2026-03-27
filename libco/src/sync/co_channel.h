/**
 * @file co_channel.h
 * @brief 协程 Channel 内部结构定义
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
 * @brief Channel 内部结构
 *
 * 有界环形缓冲区 + 两个等待队列（send_queue / recv_queue）。
 * capacity == 0 表示无缓冲 channel（rendezvous 语义）。
 *
 * 等待队列复用协程已有的 queue_node（WAITING 状态下节点空闲），
 * 协程的 chan_data 指针临时存储 send 时的源指针或 recv 时的目标指针。
 */
struct co_channel {
    size_t elem_size;   /**< 每个元素的字节数 */
    size_t capacity;    /**< 缓冲区容量（0 = 无缓冲） */

    /* 环形缓冲区 */
    void   *buffer;
    size_t  head;       /**< 下一个出队位置 */
    size_t  tail;       /**< 下一个入队位置 */
    size_t  count;      /**< 当前缓冲元素数 */

    /* 等待队列 */
    co_queue_t send_queue;  /**< 等待发送的协程（缓冲区满） */
    co_queue_t recv_queue;  /**< 等待接收的协程（缓冲区空） */

    bool closed;
};

#ifdef __cplusplus
}
#endif

#endif /* LIBCO_SYNC_CO_CHANNEL_H */
