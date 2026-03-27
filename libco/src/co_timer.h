/**
 * @file co_timer.h
 * @brief 定时器堆（最小堆）
 * 
 * 用于管理休眠协程，支持高效的定时唤醒。
 * 按唤醒时间排序，堆顶是最早到期的协程。
 */

#ifndef LIBCO_TIMER_H
#define LIBCO_TIMER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 前向声明
typedef struct co_routine co_routine_t;

// ============================================================================
// 定时器堆类型
// ============================================================================

/**
 * @brief 定时器堆结构（最小堆）
 * 
 * 协程按 wakeup_time 排序，堆顶是最早到期的协程。
 */
typedef struct co_timer_heap {
    co_routine_t **heap;    /**< 协程指针数组 */
    size_t count;           /**< 当前元素数量 */
    size_t capacity;        /**< 容量 */
} co_timer_heap_t;

// ============================================================================
// 定时器堆管理
// ============================================================================

/**
 * @brief 初始化定时器堆
 * 
 * @param heap 定时器堆指针
 * @param initial_capacity 初始容量
 * @return true 成功，false 失败
 */
bool co_timer_heap_init(co_timer_heap_t *heap, size_t initial_capacity);

/**
 * @brief 销毁定时器堆
 * 
 * 不会释放协程本身，只释放堆结构。
 * 
 * @param heap 定时器堆指针
 */
void co_timer_heap_destroy(co_timer_heap_t *heap);

/**
 * @brief 将协程插入定时器堆
 * 
 * @param heap 定时器堆指针
 * @param routine 协程指针（必须设置了 wakeup_time）
 * @return true 成功，false 失败（内存不足）
 */
bool co_timer_heap_push(co_timer_heap_t *heap, co_routine_t *routine);

/**
 * @brief 弹出堆顶协程（最早到期）
 * 
 * @param heap 定时器堆指针
 * @return 堆顶协程，堆为空返回 NULL
 */
co_routine_t *co_timer_heap_pop(co_timer_heap_t *heap);

/**
 * @brief 查看堆顶协程（不弹出）
 * 
 * @param heap 定时器堆指针
 * @return 堆顶协程，堆为空返回 NULL
 */
co_routine_t *co_timer_heap_peek(const co_timer_heap_t *heap);

/**
 * @brief 获取堆中元素数量
 * 
 * @param heap 定时器堆指针
 * @return 元素数量
 */
size_t co_timer_heap_size(const co_timer_heap_t *heap);

/**
 * @brief 检查堆是否为空
 * 
 * @param heap 定时器堆指针
 * @return true 为空，false 非空
 */
bool co_timer_heap_empty(const co_timer_heap_t *heap);

/**
 * @brief 从定时器堆中移除指定协程（如存在）
 * 
 * 用于 co_cond_timedwait 被 signal 提前唤醒后，撤销已注册但尚未触发的定时器。
 * 避免协程退出（内存释放）后 timer_heap 持有悬空指针。
 * 
 * 时间复杂度：O(n)（遍历查找） + O(log n)（re-heapify）
 * 
 * @param heap    定时器堆指针
 * @param routine 要移除的协程指针
 * @return true 找到并移除，false 未找到
 */
bool co_timer_heap_remove(co_timer_heap_t *heap, co_routine_t *routine);

// ============================================================================
// 时间工具函数
// ============================================================================

/**
 * @brief 获取单调递增的时间（毫秒）
 * 
 * 用于定时器，不受系统时间调整影响。
 * 
 * @return 当前时间戳（毫秒）
 */
uint64_t co_get_monotonic_time_ms(void);

#ifdef __cplusplus
}
#endif

#endif // LIBCO_TIMER_H
