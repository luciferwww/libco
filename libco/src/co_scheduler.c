/**
 * @file co_scheduler.c
 * @brief 调度器实现
 * 
 * 实现协程调度器的核心功能。
 */

#include "co_scheduler.h"
#include "co_routine.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

// ============================================================================
// 默认配置
// ============================================================================

#define DEFAULT_STACK_SIZE (128 * 1024)  // 128KB
#define DEFAULT_STACK_POOL_CAPACITY 16   // 默认栈池容量

// ============================================================================
// 线程局部存储
// ============================================================================

// 当前线程的调度器
static _Thread_local co_scheduler_t *g_current_scheduler = NULL;

// ============================================================================
// 调度器创建和销毁
// ============================================================================

co_scheduler_t *co_scheduler_create(void *config) {
    // TODO(v2.1): 支持配置参数
    // config 应该是 co_scheduler_config_t* 类型，用于配置：
    // - 默认栈大小
    // - 最大协程数
    // - 调度策略
    // 当前忽略 config 参数，使用默认值
    (void)config;  // 未使用
    
    // 分配调度器
    co_scheduler_t *sched = (co_scheduler_t *)calloc(1, sizeof(co_scheduler_t));
    if (!sched) {
        return NULL;
    }
    
    // 初始化字段
    co_queue_init(&sched->ready_queue);
    sched->current = NULL;
    sched->default_stack_size = DEFAULT_STACK_SIZE;
    sched->running = false;
    sched->should_stop = false;
    sched->total_routines = 0;
    sched->active_routines = 0;
    sched->switch_count = 0;
    sched->waiting_io_count = 0;
    sched->next_id = 1;
    
    // 创建栈池（Week 5）
    sched->stack_pool = co_stack_pool_create(DEFAULT_STACK_SIZE, DEFAULT_STACK_POOL_CAPACITY);
    if (!sched->stack_pool) {
        free(sched);
        return NULL;
    }
    
    // 初始化定时器堆（Week 6）
    if (!co_timer_heap_init(&sched->timer_heap, 16)) {
        co_stack_pool_destroy(sched->stack_pool);
        free(sched);
        return NULL;
    }
    
    // 创建 I/O 多路复用器（Week 7）
    sched->iomux = co_iomux_create(1024);
    if (!sched->iomux) {
        co_timer_heap_destroy(&sched->timer_heap);
        co_stack_pool_destroy(sched->stack_pool);
        free(sched);
        return NULL;
    }
    
    // 初始化主上下文（用于保存调度器上下文）
    memset(&sched->main_ctx, 0, sizeof(sched->main_ctx));

    return sched;
}

void co_scheduler_destroy(co_scheduler_t *sched) {
    if (!sched) {
        return;
    }
    
    // 销毁所有未完成的协程
    while (!co_queue_empty(&sched->ready_queue)) {
        co_queue_node_t *node = co_queue_pop_front(&sched->ready_queue);
        co_routine_t *routine = (co_routine_t *)((char *)node - offsetof(co_routine_t, queue_node));
        co_routine_destroy(routine);
        free(routine);
    }
    
    // 销毁所有休眠的协程（Week 6）
    while (!co_timer_heap_empty(&sched->timer_heap)) {
        co_routine_t *routine = co_timer_heap_pop(&sched->timer_heap);
        co_routine_destroy(routine);
        free(routine);
    }
    
    // 销毁定时器堆（Week 6）
    co_timer_heap_destroy(&sched->timer_heap);
    
    // 销毁 I/O 多路复用器（Week 7）
    if (sched->iomux) {
        co_iomux_destroy(sched->iomux);
    }
    
    // 销毁栈池（Week 5）
    co_stack_pool_destroy(sched->stack_pool);

    // 释放调度器
    free(sched);
}

// ============================================================================
// 调度器运行
// ============================================================================

co_error_t co_scheduler_run(co_scheduler_t *sched) {
    if (!sched) {
        return CO_ERROR_INVAL;
    }
    
    if (sched->running) {
        return CO_ERROR;  // 已经在运行
    }
    
    // 设置为当前调度器
    g_current_scheduler = sched;
    sched->running = true;
    sched->should_stop = false;
    
    // 主循环：不断调度协程，直到所有协程结束或被停止
    while (!sched->should_stop) {
        // Week 6: 检查定时器，唤醒到期的休眠协程
        uint64_t now = co_get_monotonic_time_ms();
        while (!co_timer_heap_empty(&sched->timer_heap)) {
            co_routine_t *routine = co_timer_heap_peek(&sched->timer_heap);
            if (routine->wakeup_time > now) {
                break;  // 堆顶还未到期，后面的更不可能到期
            }
            
            // 弹出到期协程
            co_timer_heap_pop(&sched->timer_heap);

            // 检查协程是否已由 signal/broadcast 提前唤醒：
            // signal 会将 state 改为 READY 并入就绪队列，此时定时器已是过期事件，直接丢弃。
            if (routine->state != CO_STATE_WAITING) {
                continue;
            }

            // co_cond_timedwait 超时：从条件变量等待队列移除
            if (routine->cond_wait_queue != NULL) {
                co_queue_remove(routine->cond_wait_queue, &routine->queue_node);
                routine->timed_out = true;
                routine->cond_wait_queue = NULL;
            } else if (routine->io_waiting) {
                // co_wait_io 超时：更新 waiting_io_count，唤醒后由 co_wait_io 自身调用 co_iomux_del 清理
                routine->io_waiting = false;
                routine->timed_out = true;
                routine->scheduler->waiting_io_count--;
            }

            co_scheduler_enqueue(sched, routine);
        }
        
        // 如果没有就绪协程，检查是否需要等待
        if (co_queue_empty(&sched->ready_queue)) {
            // 如果还有休眠的协程，计算到下一个唤醒时间
            int io_timeout_ms = -1;  // 默认无限等待
            
            if (!co_timer_heap_empty(&sched->timer_heap)) {
                co_routine_t *next_wakeup = co_timer_heap_peek(&sched->timer_heap);
                int64_t wait_ms = (int64_t)(next_wakeup->wakeup_time - now);
                io_timeout_ms = (wait_ms > 0) ? (int)wait_ms : 0;
            }
            
            // Week 7: 轮询 I/O 事件（带超时）
            // 只有存在 I/O 等待者（waiting_io_count > 0）或定时器时才调用 epoll_wait，
            // 否则 ready_queue 和 timer_heap 均为空，意味着所有协程已完成，应退出。
            if (sched->waiting_io_count > 0 || !co_timer_heap_empty(&sched->timer_heap)) {
                int ready_count = 0;
                co_iomux_poll(sched->iomux, io_timeout_ms, &ready_count);
                // I/O 就绪的协程已被自动加入就绪队列
            }
            
            // 重新检查就绪队列
            if (co_queue_empty(&sched->ready_queue)) {
                // 仍然没有就绪协程，退出
                if (co_timer_heap_empty(&sched->timer_heap)) {
                    break;  // 没有任何协程在运行或等待
                }
            }
            
            // 继续循环，再次检查定时器和就绪队列
            continue;
        }
        
        // 调度一个协程
        co_error_t err = co_scheduler_schedule(sched);
        if (err != CO_OK) {
            sched->running = false;
            g_current_scheduler = NULL;
            return err;
        }
    }
    
    sched->running = false;
    g_current_scheduler = NULL;
    
    return CO_OK;
}

// ============================================================================
// 调度逻辑
// ============================================================================

co_error_t co_scheduler_schedule(co_scheduler_t *sched) {
    assert(sched != NULL);
    
    // 从就绪队列中取出下一个协程
    co_routine_t *next = co_scheduler_dequeue(sched);
    if (!next) {
        return CO_OK;  // 没有可运行的协程
    }
    
    // 调试输出
    #ifdef DEBUG_SCHED
    fprintf(stderr, "[SCHED] Scheduling routine %lu, state=%d\n", 
            next->id, next->state);
    #endif
    
    // 保存之前的协程（未使用，保留供未来协程间直接切换扩展）
    // co_routine_t *prev = sched->current;

    // 设置当前协程
    sched->current = next;
    next->state = CO_STATE_RUNNING;
    
    // 增加切换计数
    sched->switch_count++;

    // 从主上下文切换到协程
    // 调度器主循环始终通过 main_ctx 进入协程，不直接和协程互切
    co_error_t err = co_context_swap(&sched->main_ctx, &next->context);
    
    // 注意：这里在两个时刻会返回
    // 1. 其他协程 yield 回来时
    // 2. 当前协程结束时
    
    // 清除当前协程指针（协程yield或结束后）
    sched->current = NULL;
    
    // 检查协程是否已结束，如果是则清理
    if (next->state == CO_STATE_DEAD) {
        // 协程已完成，销毁并释放
        co_routine_destroy(next);
        free(next);
    }
    
    return err;
}

void co_scheduler_enqueue(co_scheduler_t *sched, co_routine_t *routine) {
    assert(sched != NULL);
    assert(routine != NULL);
    
    // 将协程加入就绪队列尾部
    co_queue_push_back(&sched->ready_queue, &routine->queue_node);
    routine->state = CO_STATE_READY;
}

co_routine_t *co_scheduler_dequeue(co_scheduler_t *sched) {
    assert(sched != NULL);
    
    // 从就绪队列头部取出协程
    co_queue_node_t *node = co_queue_pop_front(&sched->ready_queue);
    if (!node) {
        return NULL;
    }
    
    // 通过偏移计算协程指针（侵入式链表）
    co_routine_t *routine = (co_routine_t *)((char *)node - offsetof(co_routine_t, queue_node));
    return routine;
}

// ============================================================================
// 全局访问器
// ============================================================================

co_scheduler_t *co_current_scheduler(void) {
    return g_current_scheduler;
}

co_routine_t *co_current(void) {
    co_scheduler_t *sched = g_current_scheduler;
    return sched ? sched->current : NULL;
}

co_routine_t *co_current_routine(void) {
    return co_current();  // 别名
}
