/**
 * @file co_mutex.h
 * @brief 协程互斥锁内部结构
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
 * @brief 协程互斥锁
 * 
 * 非线程安全（单线程调度器内使用）。
 * 锁冲突时协程挂起，不阻塞调度器线程。
 * 等待队列按 FIFO 顺序唤醒，防止饥饿。
 */
struct co_mutex {
    bool locked;                /**< 当前是否已加锁 */
    co_routine_t *owner;        /**< 当前持有者（仅用于断言检查） */
    co_queue_t wait_queue;      /**< 等待此锁的协程队列（FIFO） */
};

#ifdef __cplusplus
}
#endif

#endif // LIBCO_CO_MUTEX_H
