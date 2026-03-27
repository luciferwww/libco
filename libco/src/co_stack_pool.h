/**
 * @file co_stack_pool.h
 * @brief 协程栈内存池
 * 
 * 栈池用于复用协程栈内存，避免频繁的分配和释放开销。
 * 当协程结束时，其栈内存不会立即释放，而是放回池中供新协程使用。
 */

#ifndef LIBCO_STACK_POOL_H
#define LIBCO_STACK_POOL_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// 类型定义
// ============================================================================

/**
 * @brief 栈池不透明类型
 */
typedef struct co_stack_pool co_stack_pool_t;

// ============================================================================
// 栈池管理
// ============================================================================

/**
 * @brief 创建栈池
 * 
 * @param stack_size 每个栈的大小（字节）
 * @param initial_capacity 初始容量（预分配的栈数量）
 * @return 栈池指针，失败返回 NULL
 */
co_stack_pool_t *co_stack_pool_create(size_t stack_size, size_t initial_capacity);

/**
 * @brief 销毁栈池
 * 
 * 会释放池中所有栈内存。
 * 
 * @param pool 栈池指针
 */
void co_stack_pool_destroy(co_stack_pool_t *pool);

/**
 * @brief 从栈池分配栈
 * 
 * 如果池中有可用栈，直接返回；否则分配新栈。
 * 
 * @param pool 栈池指针
 * @return 栈指针，失败返回 NULL
 */
void *co_stack_pool_alloc(co_stack_pool_t *pool);

/**
 * @brief 将栈归还给栈池
 * 
 * 如果池未满，栈会被缓存；否则直接释放。
 * 
 * @param pool 栈池指针
 * @param stack 栈指针
 */
void co_stack_pool_free(co_stack_pool_t *pool, void *stack);

// ============================================================================
// 栈池查询
// ============================================================================

/**
 * @brief 获取栈大小
 * 
 * @param pool 栈池指针
 * @return 栈大小（字节）
 */
size_t co_stack_pool_stack_size(const co_stack_pool_t *pool);

/**
 * @brief 获取池中可用栈数量
 * 
 * @param pool 栈池指针
 * @return 当前缓存的栈数量
 */
size_t co_stack_pool_available(const co_stack_pool_t *pool);

/**
 * @brief 获取池容量
 * 
 * @param pool 栈池指针
 * @return 池的最大容量
 */
size_t co_stack_pool_capacity(const co_stack_pool_t *pool);

// ============================================================================
// 统计信息
// ============================================================================

/**
 * @brief 栈池统计信息
 */
typedef struct co_stack_pool_stats {
    size_t total_allocs;    /**< 总分配次数 */
    size_t total_frees;     /**< 总释放次数 */
    size_t cache_hits;      /**< 缓存命中次数（从池中获取） */
    size_t cache_misses;    /**< 缓存未命中次数（需要新分配） */
    size_t current_used;    /**< 当前使用中的栈数量 */
} co_stack_pool_stats_t;

/**
 * @brief 获取栈池统计信息
 * 
 * @param pool 栈池指针
 * @param stats 输出统计信息
 */
void co_stack_pool_get_stats(const co_stack_pool_t *pool, co_stack_pool_stats_t *stats);

/**
 * @brief 重置栈池统计信息
 * 
 * @param pool 栈池指针
 */
void co_stack_pool_reset_stats(co_stack_pool_t *pool);

#ifdef __cplusplus
}
#endif

#endif // LIBCO_STACK_POOL_H
