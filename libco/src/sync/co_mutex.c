/**
 * @file co_mutex.c
 * @brief 协程互斥锁实现
 * 
 * 协程级互斥锁，与 pthread_mutex 的核心区别：
 * - 锁冲突时当前协程挂起（co_context_swap 切回调度器），
 *   调度器线程继续运行其他协程，不会阻塞整个线程。
 * - 解锁时将等待队列中第一个协程放回 ready_queue，由调度器调度执行。
 * - 等待队列使用协程已有的 queue_node（WAITING 状态的协程不在 ready_queue 中，
 *   节点空闲可复用）。
 */

#include "co_mutex.h"
#include "../co_scheduler.h"
#include <libco/co_sync.h>
#include <stdlib.h>
#include <stddef.h>
#include <assert.h>

// ============================================================================
// 创建和销毁
// ============================================================================

co_mutex_t *co_mutex_create(const void *attr) {
    (void)attr;  // 预留扩展，当前忽略
    co_mutex_t *mutex = (co_mutex_t *)calloc(1, sizeof(co_mutex_t));
    if (!mutex) {
        return NULL;
    }
    mutex->locked = false;
    mutex->owner = NULL;
    co_queue_init(&mutex->wait_queue);
    return mutex;
}

co_error_t co_mutex_destroy(co_mutex_t *mutex) {
    if (!mutex) {
        return CO_ERROR_INVAL;
    }
    // 销毁时等待队列应为空
    assert(co_queue_empty(&mutex->wait_queue) && "destroying a mutex with waiters");
    free(mutex);
    return CO_OK;
}

// ============================================================================
// 加锁 / 解锁
// ============================================================================

co_error_t co_mutex_lock(co_mutex_t *mutex) {
    if (!mutex) {
        return CO_ERROR_INVAL;
    }

    co_scheduler_t *sched = co_current_scheduler();
    co_routine_t *current = co_current_routine();

    // 不在协程上下文中（如调度器启动前从主线程调用）：
    // 直接获取锁（无并发，不需要挂起），若已被占用则返回 BUSY（无法阻塞等待）。
    if (!sched || !current) {
        if (mutex->locked) {
            return CO_ERROR_BUSY;
        }
        mutex->locked = true;
        mutex->owner = NULL;
        return CO_OK;
    }

    if (!mutex->locked) {
        // 锁空闲，直接持有
        mutex->locked = true;
        mutex->owner = current;
        return CO_OK;
    }

    // 锁已被占用，将当前协程挂起放入等待队列
    current->state = CO_STATE_WAITING;
    co_queue_push_back(&mutex->wait_queue, &current->queue_node);

    // 切回调度器，等待解锁后被唤醒
    co_context_swap(&current->context, &sched->main_ctx);

    // 被 co_mutex_unlock 唤醒，此时已持有锁
    assert(mutex->locked && mutex->owner == current);
    return CO_OK;
}

co_error_t co_mutex_trylock(co_mutex_t *mutex) {
    if (!mutex) {
        return CO_ERROR_INVAL;
    }
    if (mutex->locked) {
        return CO_ERROR_BUSY;
    }
    co_routine_t *current = co_current_routine();
    mutex->locked = true;
    mutex->owner = current;
    return CO_OK;
}

co_error_t co_mutex_unlock(co_mutex_t *mutex) {
    if (!mutex) {
        return CO_ERROR_INVAL;
    }
    assert(mutex->locked && "unlocking a mutex that is not locked");

    co_queue_node_t *node = co_queue_pop_front(&mutex->wait_queue);
    if (!node) {
        // 无等待者，直接释放
        mutex->locked = false;
        mutex->owner = NULL;
        return CO_OK;
    }

    // 有等待者：将锁直接转交给队首协程（减少一次无谓的竞争）
    co_routine_t *next = (co_routine_t *)((char *)node - offsetof(co_routine_t, queue_node));
    mutex->owner = next;
    // locked 保持 true，锁已转交

    // 将该协程放回就绪队列
    next->state = CO_STATE_READY;
    co_queue_push_back(&next->scheduler->ready_queue, &next->queue_node);

    return CO_OK;
}
