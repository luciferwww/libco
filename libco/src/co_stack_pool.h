/**
 * @file co_stack_pool.h
 * @brief Coroutine stack memory pool
 * 
 * The stack pool reuses coroutine stack memory to avoid frequent allocation
 * and free overhead. When a coroutine finishes, its stack is returned to the
 * pool instead of being released immediately.
 */

#ifndef LIBCO_STACK_POOL_H
#define LIBCO_STACK_POOL_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Type definitions
// ============================================================================

/**
 * @brief Opaque stack pool type
 */
typedef struct co_stack_pool co_stack_pool_t;

// ============================================================================
// Stack pool management
// ============================================================================

/**
 * @brief Create a stack pool
 * 
 * @param stack_size Size of each stack in bytes
 * @param initial_capacity Initial capacity in number of stacks
 * @return Stack pool pointer, or NULL on failure
 */
co_stack_pool_t *co_stack_pool_create(size_t stack_size, size_t initial_capacity);

/**
 * @brief Destroy a stack pool
 * 
 * Frees all stack memory currently cached in the pool.
 * 
 * @param pool Stack pool pointer
 */
void co_stack_pool_destroy(co_stack_pool_t *pool);

/**
 * @brief Allocate a stack from the pool
 * 
 * Returns a cached stack if available, otherwise allocates a new one.
 * 
 * @param pool Stack pool pointer
 * @return Stack pointer, or NULL on failure
 */
void *co_stack_pool_alloc(co_stack_pool_t *pool);

/**
 * @brief Return a stack to the pool
 * 
 * The stack is cached if the pool is not full; otherwise it is freed.
 * 
 * @param pool Stack pool pointer
 * @param stack Stack pointer
 */
void co_stack_pool_free(co_stack_pool_t *pool, void *stack);

// ============================================================================
// Stack pool queries
// ============================================================================

/**
 * @brief Get the stack size
 * 
 * @param pool Stack pool pointer
 * @return Stack size in bytes
 */
size_t co_stack_pool_stack_size(const co_stack_pool_t *pool);

/**
 * @brief Get the number of available cached stacks
 * 
 * @param pool Stack pool pointer
 * @return Number of cached stacks currently available
 */
size_t co_stack_pool_available(const co_stack_pool_t *pool);

/**
 * @brief Get the pool capacity
 * 
 * @param pool Stack pool pointer
 * @return Maximum pool capacity
 */
size_t co_stack_pool_capacity(const co_stack_pool_t *pool);

// ============================================================================
// Statistics
// ============================================================================

/**
 * @brief Stack pool statistics
 */
typedef struct co_stack_pool_stats {
    size_t total_allocs;    /**< Total allocation count */
    size_t total_frees;     /**< Total free count */
    size_t cache_hits;      /**< Cache hits from the pool */
    size_t cache_misses;    /**< Cache misses that required new allocation */
    size_t current_used;    /**< Number of stacks currently in use */
} co_stack_pool_stats_t;

/**
 * @brief Get stack pool statistics
 * 
 * @param pool Stack pool pointer
 * @param stats Output statistics structure
 */
void co_stack_pool_get_stats(const co_stack_pool_t *pool, co_stack_pool_stats_t *stats);

/**
 * @brief Reset stack pool statistics
 * 
 * @param pool Stack pool pointer
 */
void co_stack_pool_reset_stats(co_stack_pool_t *pool);

#ifdef __cplusplus
}
#endif

#endif // LIBCO_STACK_POOL_H
