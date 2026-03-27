/**
 * @file co_routine.h
 * @brief 协程内部数据结构
 * 
 * 定义协程的内部表示。
 */

#ifndef LIBCO_CO_ROUTINE_H
#define LIBCO_CO_ROUTINE_H

#include <libco/co.h>
#include "co_queue.h"
#include "platform/context.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 前向声明
struct co_scheduler;

// ============================================================================
// 协程结构
// ============================================================================

/**
 * @brief 协程结构
 */
struct co_routine {
    // 唯一 ID
    uint64_t id;
    
    // 状态
    co_state_t state;
    
    // 上下文
    co_context_t context;
    
    // 栈信息
    void *stack_base;
    size_t stack_size;
    
    // 入口函数
    co_entry_func_t entry;
    void *arg;
    
    // 所属调度器
    struct co_scheduler *scheduler;
    
    // 队列节点（用于就绪队列）
    co_queue_node_t queue_node;
    
    // Week 6: 定时器支持
    uint64_t wakeup_time;          /**< 唤醒时间（毫秒时间戳）*/

    // Week 10: Channel 支持
    void *chan_data;               /**< channel send/recv 时的数据指针 */

    // CondVar timedwait 支持
    co_queue_t *cond_wait_queue;   /**< timedwait 时所在的 cond 等待队列（NULL 表示非 timedwait）*/
    bool timed_out;                /**< 是否因超时被唤醒（由 co_cond_timedwait / co_wait_io 读取）*/

    // I/O 超时支持
    bool io_waiting;               /**< 是否正在等待 I/O（定时器超时时需要减少 waiting_io_count）*/

    // TODO(Week 7-8): 添加 I/O 等待支持
    // int wait_fd;                   // 等待的文件描述符
    // uint32_t wait_events;          // 等待的事件（读/写）
    
    // TODO(v2.1): 添加生命周期管理支持
    // co_queue_t join_waiters;       // 等待此协程结束的协程列表
    // co_routine_t *cancel_target;   // 取消目标协程
    
    // 调试信息
    const char *name;  /**< 协程名称（可选） */
    
    // 生命周期管理
    bool detached;            /**< 是否已分离 */
    void *result;             /**< 返回值 */
};

// ============================================================================
// 协程函数声明
// ============================================================================

/**
 * @brief 初始化协程
 * @param routine 协程指针
 * @param scheduler 所属调度器
 * @param entry 入口函数
 * @param arg 用户参数
 * @param stack_size 栈大小
 * @return CO_OK 成功，其他值表示错误
 */
co_error_t co_routine_init(co_routine_t *routine,
                           struct co_scheduler *scheduler,
                           co_entry_func_t entry,
                           void *arg,
                           size_t stack_size);

/**
 * @brief 销毁协程
 * @param routine 协程指针
 */
void co_routine_destroy(co_routine_t *routine);

/**
 * @brief 将协程标记为已完成
 * @param routine 协程指针
 * @param result 返回值
 */
void co_routine_finish(co_routine_t *routine, void *result);

#ifdef __cplusplus
}
#endif

#endif // LIBCO_CO_ROUTINE_H
