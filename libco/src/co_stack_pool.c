/**
 * @file co_stack_pool.c
 * @brief 协程栈内存池实现
 */

#include "co_stack_pool.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// ============================================================================
// 内部结构
// ============================================================================

/**
 * @brief 栈池结构
 */
struct co_stack_pool {
    size_t stack_size;           /**< 每个栈的大小 */
    void **stacks;               /**< 栈指针数组 */
    size_t capacity;             /**< 池的最大容量 */
    size_t count;                /**< 当前缓存的栈数量 */
    
    // 统计信息
    co_stack_pool_stats_t stats;
};

// ============================================================================
// 栈池管理
// ============================================================================

co_stack_pool_t *co_stack_pool_create(size_t stack_size, size_t initial_capacity) {
    if (stack_size == 0) {
        return NULL;
    }
    
    // 分配池结构
    co_stack_pool_t *pool = (co_stack_pool_t *)calloc(1, sizeof(co_stack_pool_t));
    if (!pool) {
        return NULL;
    }
    
    pool->stack_size = stack_size;
    pool->capacity = initial_capacity;
    pool->count = 0;
    
    // 分配栈指针数组
    if (initial_capacity > 0) {
        pool->stacks = (void **)calloc(initial_capacity, sizeof(void *));
        if (!pool->stacks) {
            free(pool);
            return NULL;
        }
        
        // 预分配栈（可选：暂时不预分配，按需分配）
        // 这样可以避免启动时分配大量内存
    }
    
    memset(&pool->stats, 0, sizeof(co_stack_pool_stats_t));
    
    return pool;
}

void co_stack_pool_destroy(co_stack_pool_t *pool) {
    if (!pool) {
        return;
    }
    
    // 释放所有缓存的栈
    for (size_t i = 0; i < pool->count; i++) {
        free(pool->stacks[i]);
    }
    
    free(pool->stacks);
    free(pool);
}

void *co_stack_pool_alloc(co_stack_pool_t *pool) {
    assert(pool != NULL);
    
    pool->stats.total_allocs++;
    
    // 尝试从池中获取
    if (pool->count > 0) {
        pool->stats.cache_hits++;
        pool->stats.current_used++;
        return pool->stacks[--pool->count];
    }
    
    // 池为空，分配新栈
    pool->stats.cache_misses++;
    pool->stats.current_used++;
    
    void *stack = malloc(pool->stack_size);
    return stack;
}

void co_stack_pool_free(co_stack_pool_t *pool, void *stack) {
    assert(pool != NULL);
    assert(stack != NULL);
    
    pool->stats.total_frees++;
    pool->stats.current_used--;
    
    // 如果池未满，缓存该栈
    if (pool->count < pool->capacity) {
        pool->stacks[pool->count++] = stack;
    } else {
        // 池已满，直接释放
        free(stack);
    }
}

// ============================================================================
// 栈池查询
// ============================================================================

size_t co_stack_pool_stack_size(const co_stack_pool_t *pool) {
    assert(pool != NULL);
    return pool->stack_size;
}

size_t co_stack_pool_available(const co_stack_pool_t *pool) {
    assert(pool != NULL);
    return pool->count;
}

size_t co_stack_pool_capacity(const co_stack_pool_t *pool) {
    assert(pool != NULL);
    return pool->capacity;
}

// ============================================================================
// 统计信息
// ============================================================================

void co_stack_pool_get_stats(const co_stack_pool_t *pool, co_stack_pool_stats_t *stats) {
    assert(pool != NULL);
    assert(stats != NULL);
    
    *stats = pool->stats;
}

void co_stack_pool_reset_stats(co_stack_pool_t *pool) {
    assert(pool != NULL);
    
    // 保留 current_used，重置其他统计
    size_t current_used = pool->stats.current_used;
    memset(&pool->stats, 0, sizeof(co_stack_pool_stats_t));
    pool->stats.current_used = current_used;
}
