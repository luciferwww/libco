/**
 * @file co_routine.c
 * @brief 协程实现
 * 
 * 实现协程的生命周期管理。
 */

#include "co_routine.h"
#include "co_scheduler.h"
#include "co_timer.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// ============================================================================
// 协程入口包装
// ============================================================================

/**
 * @brief 协程入口包装函数
 * 
 * 这个函数是所有协程的真正入口点。
 * 它调用用户的入口函数，并在函数返回后处理协程结束。
 * 
 * @param arg 协程指针
 */
static void co_routine_entry_wrapper(void *arg) {
    co_routine_t *routine = (co_routine_t *)arg;
    assert(routine != NULL);
    assert(routine->entry != NULL);
    
    // 调用用户的入口函数
    routine->entry(routine->arg);
    
    // 用户函数返回后，标记协程为已完成
    co_routine_finish(routine, NULL);
}

// ============================================================================
// 协程初始化和销毁
// ============================================================================

co_error_t co_routine_init(co_routine_t *routine,
                           struct co_scheduler *scheduler,
                           co_entry_func_t entry,
                           void *arg,
                           size_t stack_size) {
    if (!routine || !scheduler || !entry) {
        return CO_ERROR_INVAL;
    }
    
    // 使用默认栈大小
    if (stack_size == 0) {
        stack_size = scheduler->default_stack_size;
    }
    
    // 从栈池分配栈（Week 5）
    void *stack = co_stack_pool_alloc(scheduler->stack_pool);
    if (!stack) {
        return CO_ERROR_NOMEM;
    }
    
    // 初始化字段
    routine->id = scheduler->next_id++;
    routine->state = CO_STATE_READY;
    routine->stack_base = stack;
    routine->stack_size = stack_size;
    routine->entry = entry;
    routine->arg = arg;
    routine->scheduler = scheduler;
    routine->name = NULL;       // TODO(v2.1): 实现 co_routine_set_name()
    routine->detached = false;  // TODO(v2.1): 实现 co_routine_detach()
    routine->result = NULL;     // TODO(v2.1): 实现 co_await() 获取结果
    
    // 初始化队列节点
    co_queue_node_init(&routine->queue_node);
    
    // 初始化上下文
    // 注意：我们传递 routine 自身作为参数给包装函数
    co_error_t err = co_context_init(&routine->context,
                                     stack,
                                     stack_size,
                                     co_routine_entry_wrapper,
                                     routine);
    if (err != CO_OK) {
        co_stack_pool_free(scheduler->stack_pool, stack);
        return err;
    }
    
    return CO_OK;
}

void co_routine_destroy(co_routine_t *routine) {
    if (!routine) {
        return;
    }
    
    // 销毁上下文
    co_context_destroy(&routine->context);
    
    // 将栈归还给栈池（Week 5）
    if (routine->stack_base && routine->scheduler && routine->scheduler->stack_pool) {
        co_stack_pool_free(routine->scheduler->stack_pool, routine->stack_base);
        routine->stack_base = NULL;
    }
    
    // 清空字段
    routine->scheduler = NULL;
    routine->entry = NULL;
    routine->arg = NULL;
}

void co_routine_finish(co_routine_t *routine, void *result) {
    assert(routine != NULL);
    assert(routine->scheduler != NULL);
    
    // 标记为已完成
    routine->state = CO_STATE_DEAD;
    routine->result = result;
    
    // 更新调度器统计
    routine->scheduler->active_routines--;
    
    // 切换回调度器
    // 注意：这个调用不会返回，因为协程已经结束
    co_context_swap(&routine->context, &routine->scheduler->main_ctx);
    
    // 不应该到达这里
    assert(0 && "co_routine_finish: should not reach here");
}

// ============================================================================
// 协程创建 API
// ============================================================================

co_routine_t *co_spawn(co_scheduler_t *sched,
                       co_entry_func_t entry,
                       void *arg,
                       size_t stack_size) {
    // 如果没有指定调度器，使用当前线程的调度器
    if (!sched) {
        sched = co_current_scheduler();
    }
    
    if (!sched || !entry) {
        return NULL;
    }
    
    // 分配协程
    co_routine_t *routine = (co_routine_t *)calloc(1, sizeof(co_routine_t));
    if (!routine) {
        return NULL;
    }
    
    // 初始化协程
    co_error_t err = co_routine_init(routine, sched, entry, arg, stack_size);
    if (err != CO_OK) {
        free(routine);
        return NULL;
    }
    
    // 更新调度器统计
    sched->total_routines++;
    sched->active_routines++;
    
    // 将协程加入就绪队列
    co_scheduler_enqueue(sched, routine);
    
    return routine;
}

// ============================================================================
// 协程控制 API
// ============================================================================

co_error_t co_yield_now(void) {
    // TODO(v2.1): 支持 co_yield_value() 返回值给调用者
    // TODO(v2.1): 支持 co_yield_to(routine) 显式切换到指定协程
    
    co_routine_t *current = co_current();
    if (!current) {
        return CO_ERROR;  // 不在协程中
    }
    
    co_scheduler_t *sched = current->scheduler;
    assert(sched != NULL);
    
    // 将当前协程重新加入就绪队列（放到队尾）
    co_scheduler_enqueue(sched, current);
    
    // 切换回调度器
    return co_context_swap(&current->context, &sched->main_ctx);
}

co_error_t co_sleep(uint32_t msec) {
    // msec 为 0 时等价于 yield
    if (msec == 0) {
        return co_yield_now();
    }
    
    co_routine_t *current = co_current();
    if (!current) {
        return CO_ERROR;  // 不在协程中
    }
    
    co_scheduler_t *sched = current->scheduler;
    assert(sched != NULL);
    
    // 计算唤醒时间
    uint64_t now = co_get_monotonic_time_ms();
    current->wakeup_time = now + msec;
    
    // 将状态设置为 WAITING（等待休眠结束）
    current->state = CO_STATE_WAITING;
    
    // 加入定时器堆
    if (!co_timer_heap_push(&sched->timer_heap, current)) {
        // 插入失败（内存不足），恢复状态
        current->state = CO_STATE_READY;
        return CO_ERROR_NOMEM;
    }
    
    // 切换回调度器
    return co_context_swap(&current->context, &sched->main_ctx);
}
