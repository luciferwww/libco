/**
 * @file co_stack_pool.c
 * @brief Coroutine stack pool implementation
 */

#include "co_stack_pool.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// ============================================================================
// Internal structure
// ============================================================================

/**
 * @brief Stack pool structure
 */
struct co_stack_pool {
    size_t stack_size;           /**< Size of each stack */
    void **stacks;               /**< Array of stack pointers */
    size_t capacity;             /**< Maximum pool capacity */
    size_t count;                /**< Number of cached stacks */
    
    // Statistics
    co_stack_pool_stats_t stats;
};

// ============================================================================
// Stack pool management
// ============================================================================

co_stack_pool_t *co_stack_pool_create(size_t stack_size, size_t initial_capacity) {
    if (stack_size == 0) {
        return NULL;
    }
    
    // Allocate the pool structure
    co_stack_pool_t *pool = (co_stack_pool_t *)calloc(1, sizeof(co_stack_pool_t));
    if (!pool) {
        return NULL;
    }
    
    pool->stack_size = stack_size;
    pool->capacity = initial_capacity;
    pool->count = 0;
    
    // Allocate the stack pointer array
    if (initial_capacity > 0) {
        pool->stacks = (void **)calloc(initial_capacity, sizeof(void *));
        if (!pool->stacks) {
            free(pool);
            return NULL;
        }
        
        // Preallocating stacks is optional; for now allocation is on demand.
        // This avoids large upfront memory usage during startup.
    }
    
    memset(&pool->stats, 0, sizeof(co_stack_pool_stats_t));
    
    return pool;
}

void co_stack_pool_destroy(co_stack_pool_t *pool) {
    if (!pool) {
        return;
    }
    
    // Free all cached stacks
    for (size_t i = 0; i < pool->count; i++) {
        free(pool->stacks[i]);
    }
    
    free(pool->stacks);
    free(pool);
}

void *co_stack_pool_alloc(co_stack_pool_t *pool) {
    assert(pool != NULL);
    
    pool->stats.total_allocs++;
    
    // Try to reuse a stack from the pool
    if (pool->count > 0) {
        pool->stats.cache_hits++;
        pool->stats.current_used++;
        return pool->stacks[--pool->count];
    }
    
    // Pool is empty; allocate a new stack
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
    
    // Cache the stack if the pool is not full
    if (pool->count < pool->capacity) {
        pool->stacks[pool->count++] = stack;
    } else {
        // Pool is full; free it directly
        free(stack);
    }
}

// ============================================================================
// Stack pool queries
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
// Statistics
// ============================================================================

void co_stack_pool_get_stats(const co_stack_pool_t *pool, co_stack_pool_stats_t *stats) {
    assert(pool != NULL);
    assert(stats != NULL);
    
    *stats = pool->stats;
}

void co_stack_pool_reset_stats(co_stack_pool_t *pool) {
    assert(pool != NULL);
    
    // Preserve current_used while resetting the other counters
    size_t current_used = pool->stats.current_used;
    memset(&pool->stats, 0, sizeof(co_stack_pool_stats_t));
    pool->stats.current_used = current_used;
}
