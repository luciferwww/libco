/**
 * @file co_cond.h
 * @brief 协程条件变量内部结构
 */

#ifndef LIBCO_CO_COND_H
#define LIBCO_CO_COND_H

#include "../co_queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 协程条件变量
 * 
 * 等待队列中的协程均处于 WAITING 状态，不占用 ready_queue。
 * signal 唤醒队首一个，broadcast 唤醒全部。
 */
struct co_cond {
    co_queue_t wait_queue;  /**< 等待此条件的协程队列（FIFO） */
};

#ifdef __cplusplus
}
#endif

#endif // LIBCO_CO_COND_H
