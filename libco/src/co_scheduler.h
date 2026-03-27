/**
 * @file co_scheduler.h
 * @brief 调度器内部数据结构
 * 
 * 定义调度器的内部表示。
 */

#ifndef LIBCO_CO_SCHEDULER_H
#define LIBCO_CO_SCHEDULER_H

#include <libco/co.h>
#include "co_queue.h"
#include "co_routine.h"
#include "co_stack_pool.h"
#include "co_timer.h"
#include "co_iomux.h"
#include "platform/context.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// 调度器结构
// ============================================================================

/**
 * @brief 调度器结构
 */
struct co_scheduler {
    // 就绪队列（FIFO）
    co_queue_t ready_queue;
    
    // Week 6: 定时器堆（休眠协程管理）
    co_timer_heap_t timer_heap;
    
    // Week 7: I/O 多路复用上下文
    co_iomux_t *iomux;             /**< I/O 多路复用器 (epoll/kqueue/IOCP) */
    
    // 当前运行的协程
    co_routine_t *current;
    
    // 主上下文（调度器上下文）
    co_context_t main_ctx;
    
    // 栈池（Week 5）
    co_stack_pool_t *stack_pool;
    
    // 配置
    size_t default_stack_size;  /**< 默认栈大小 */
    
    // 运行状态
    bool running;               /**< 是否正在运行 */
    bool should_stop;           /**< 是否应该停止 */
    
    // 统计信息
    uint64_t total_routines;    /**< 已创建的协程总数 */
    uint64_t active_routines;   /**< 当前活跃的协程数 */
    uint64_t switch_count;      /**< 上下文切换次数 */
    uint32_t waiting_io_count;  /**< 当前处于 CO_STATE_WAITING（等待 I/O）的协程数 */
    
    // ID 生成器
    uint64_t next_id;           /**< 下一个协程 ID */
};

// ============================================================================
// 调度器函数声明
// ============================================================================

/**
 * @brief 调度下一个协程
 * @param sched 调度器指针
 * @return CO_OK 成功，其他值表示错误
 */
co_error_t co_scheduler_schedule(co_scheduler_t *sched);

/**
 * @brief 将协程加入就绪队列
 * @param sched 调度器指针
 * @param routine 协程指针
 */
void co_scheduler_enqueue(co_scheduler_t *sched, co_routine_t *routine);

/**
 * @brief 从就绪队列中取出下一个协程
 * @param sched 调度器指针
 * @return 协程指针，队列为空返回 NULL
 */
co_routine_t *co_scheduler_dequeue(co_scheduler_t *sched);

/**
 * @brief 获取当前线程的调度器
 * @return 调度器指针，未运行返回 NULL
 */
co_scheduler_t *co_current_scheduler(void);

/**
 * @brief 获取当前运行的协程
 * @return 协程指针，未运行返回 NULL
 */
co_routine_t *co_current_routine(void);

#ifdef __cplusplus
}
#endif

#endif // LIBCO_CO_SCHEDULER_H
