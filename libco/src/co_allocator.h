/**
 * @file co_allocator.h
 * @brief Custom memory allocator interface
 * 
 * Allows users to provide custom allocation functions for all libco memory
 * operations. This is useful for memory tracking, custom allocation
 * strategies, or memory pools.
 */

#ifndef LIBCO_ALLOCATOR_H
#define LIBCO_ALLOCATOR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Type definitions
// ============================================================================

/**
 * @brief Memory allocation function type
 * 
 * @param size Number of bytes to allocate
 * @param userdata User data pointer
 * @return Allocated memory pointer, or NULL on failure
 */
typedef void *(*co_malloc_fn)(size_t size, void *userdata);

/**
 * @brief Memory reallocation function type
 * 
 * @param ptr Original memory pointer, or NULL
 * @param size New size
 * @param userdata User data pointer
 * @return Reallocated memory pointer, or NULL on failure
 */
typedef void *(*co_realloc_fn)(void *ptr, size_t size, void *userdata);

/**
 * @brief Memory free function type
 * 
 * @param ptr Memory pointer to free
 * @param userdata User data pointer
 */
typedef void (*co_free_fn)(void *ptr, void *userdata);

/**
 * @brief Custom allocator structure
 */
typedef struct co_allocator {
    co_malloc_fn malloc_fn;     /**< Allocation function */
    co_realloc_fn realloc_fn;   /**< Reallocation function */
    co_free_fn free_fn;         /**< Free function */
    void *userdata;             /**< User data pointer */
} co_allocator_t;

// ============================================================================
// Allocator management
// ============================================================================

/**
 * @brief Set the global allocator
 * 
 * This must be called before creating any libco objects.
 * If not called, libco uses the standard malloc/realloc/free functions.
 * 
 * @param allocator Custom allocator pointer, or NULL to restore the default
 *        allocator
 */
void co_set_allocator(const co_allocator_t *allocator);

/**
 * @brief Get the current global allocator
 * 
 * @return Current allocator pointer, or NULL when using the default allocator
 */
const co_allocator_t *co_get_allocator(void);

// ============================================================================
// Internal allocation helpers for libco
// ============================================================================

/**
 * @brief Internal allocation function
 * 
 * Allocate memory using the currently configured allocator.
 * 
 * @param size Number of bytes to allocate
 * @return Allocated memory pointer, or NULL on failure
 */
void *co_malloc(size_t size);

/**
 * @brief Internal allocate-and-zero function
 * 
 * Allocate memory using the current allocator and zero-initialize it.
 * 
 * @param count Number of elements
 * @param size Size of each element
 * @return Allocated memory pointer, or NULL on failure
 */
void *co_calloc(size_t count, size_t size);

/**
 * @brief Internal reallocation function
 * 
 * Reallocate memory using the current allocator.
 * 
 * @param ptr Original memory pointer
 * @param size New size
 * @return Reallocated memory pointer, or NULL on failure
 */
void *co_realloc(void *ptr, size_t size);

/**
 * @brief Internal free function
 * 
 * Free memory using the current allocator.
 * 
 * @param ptr Memory pointer to free
 */
void co_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif // LIBCO_ALLOCATOR_H
