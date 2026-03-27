/**
 * @file co_cond.c
 * @brief 协程条件变量实现
 * 
 * co_cond_wait 的语义与 pthread_cond_wait 相同：
 *   1. 原子地释放 mutex
 *   2. 将当前协程加入等待队列并挂起（切回调度器）
 *   3. 被 signal/broadcast 唤醒后，重新持有 mutex 再返回
 *
 * 步骤 1-2 是"原子"的：因为这是单线程调度器，
 * 释放 mutex 和挂起之间不会有其他协程插入（调度器未被调用）。
 */

#include "co_cond.h"
#include "co_mutex.h"
#include "../co_scheduler.h"
#include "../co_timer.h"
#include <libco/co_sync.h>
#include <stdlib.h>
#include <stddef.h>
#include <assert.h>

// ============================================================================
// 创建和销毁
// ============================================================================

co_cond_t *co_cond_create(const void *attr) {
    (void)attr;  // 预留扩展，当前忽略
    co_cond_t *cond = (co_cond_t *)calloc(1, sizeof(co_cond_t));
    if (!cond) {
        return NULL;
    }
    co_queue_init(&cond->wait_queue);
    return cond;
}

co_error_t co_cond_destroy(co_cond_t *cond) {
    if (!cond) {
        return CO_ERROR_INVAL;
    }
    assert(co_queue_empty(&cond->wait_queue) && "destroying a cond with waiters");
    free(cond);
    return CO_OK;
}

// ============================================================================
// 等待
// ============================================================================

co_error_t co_cond_wait(co_cond_t *cond, co_mutex_t *mutex) {
    if (!cond || !mutex) {
        return CO_ERROR_INVAL;
    }

    co_scheduler_t *sched = co_current_scheduler();
    co_routine_t *current = co_current_routine();
    if (!sched || !current) {
        return CO_ERROR_INVAL;
    }

    // 1. 将当前协程加入条件变量等待队列
    current->state = CO_STATE_WAITING;
    co_queue_push_back(&cond->wait_queue, &current->queue_node);

    // 2. 释放 mutex（可能唤醒一个正在等待该锁的协程）
    co_mutex_unlock(mutex);

    // 3. 切回调度器挂起
    co_context_swap(&current->context, &sched->main_ctx);

    // 4. 被 signal/broadcast 唤醒后，重新持有 mutex
    co_mutex_lock(mutex);

    return CO_OK;
}

co_error_t co_cond_timedwait(co_cond_t *cond, co_mutex_t *mutex,
                              uint32_t timeout_ms) {
    if (!cond || !mutex) {
        return CO_ERROR_INVAL;
    }

    // timeout_ms == 0：立即超时（非阻塞检查）
    if (timeout_ms == 0) {
        return CO_ERROR_TIMEOUT;
    }

    co_scheduler_t *sched = co_current_scheduler();
    co_routine_t *current = co_current_routine();
    if (!sched || !current) {
        return CO_ERROR_INVAL;
    }

    // 1. 加入 cond 等待队列，记录等待队列指针（供定时器超时时移除）
    current->state = CO_STATE_WAITING;
    current->cond_wait_queue = &cond->wait_queue;
    current->timed_out = false;
    co_queue_push_back(&cond->wait_queue, &current->queue_node);

    // 2. 注册定时器：超时后调度器将协程从 cond 等待队列移除并唤醒
    uint64_t now = co_get_monotonic_time_ms();
    current->wakeup_time = now + timeout_ms;
    if (!co_timer_heap_push(&sched->timer_heap, current)) {
        // 内存不足：撤销等待队列注册，恢复状态
        co_queue_remove(&cond->wait_queue, &current->queue_node);
        current->state = CO_STATE_READY;
        current->cond_wait_queue = NULL;
        return CO_ERROR_NOMEM;
    }

    // 3. 释放 mutex（与 co_cond_wait 相同语义）
    co_mutex_unlock(mutex);

    // 4. 切回调度器挂起，等待 signal 或定时器
    co_context_swap(&current->context, &sched->main_ctx);

    // 5. 若被 signal 提前唤醒，定时器仍留在堆中。必须在协程退出前将其移除，
    //    否则协程内存释放后 timer_heap 会持有悬空指针（Debug 模式下导致挂起）。
    if (!current->timed_out) {
        co_timer_heap_remove(&sched->timer_heap, current);
    }

    // 6. 恢复后重新持有 mutex
    co_mutex_lock(mutex);

    return current->timed_out ? CO_ERROR_TIMEOUT : CO_OK;
}

// ============================================================================
// 唤醒
// ============================================================================

co_error_t co_cond_signal(co_cond_t *cond) {
    if (!cond) {
        return CO_ERROR_INVAL;
    }

    co_queue_node_t *node = co_queue_pop_front(&cond->wait_queue);
    if (!node) {
        return CO_OK;  // 无等待者，忽略
    }

    co_routine_t *waiter = (co_routine_t *)((char *)node - offsetof(co_routine_t, queue_node));
    // 清除 timedwait 上下文：调度器定时器循环会检测 state != WAITING 来丢弃过期定时器
    waiter->cond_wait_queue = NULL;
    waiter->state = CO_STATE_READY;
    // 唤醒后 waiter 会去争抢 mutex（在 co_cond_wait / co_cond_timedwait 的最后一步）
    co_queue_push_back(&waiter->scheduler->ready_queue, &waiter->queue_node);

    return CO_OK;
}

co_error_t co_cond_broadcast(co_cond_t *cond) {
    if (!cond) {
        return CO_ERROR_INVAL;
    }

    co_queue_node_t *node;
    while ((node = co_queue_pop_front(&cond->wait_queue)) != NULL) {
        co_routine_t *waiter = (co_routine_t *)((char *)node - offsetof(co_routine_t, queue_node));
        waiter->cond_wait_queue = NULL;  // 清除 timedwait 上下文
        waiter->state = CO_STATE_READY;
        co_queue_push_back(&waiter->scheduler->ready_queue, &waiter->queue_node);
    }

    return CO_OK;
}
