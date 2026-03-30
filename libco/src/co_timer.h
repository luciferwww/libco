/**
 * @file co_timer.h
 * @brief Timer heap (min-heap)
 * 
 * Manages sleeping coroutines and supports efficient timed wakeups.
 * The heap is ordered by wakeup time, with the earliest deadline at the top.
 */

#ifndef LIBCO_TIMER_H
#define LIBCO_TIMER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration
typedef struct co_routine co_routine_t;

// ============================================================================
// Timer heap type
// ============================================================================

/**
 * @brief Timer heap structure (min-heap)
 * 
 * Coroutines are ordered by wakeup_time, with the earliest one at the top.
 */
typedef struct co_timer_heap {
    co_routine_t **heap;    /**< Array of coroutine pointers */
    size_t count;           /**< Number of elements */
    size_t capacity;        /**< Capacity */
} co_timer_heap_t;

// ============================================================================
// Timer heap management
// ============================================================================

/**
 * @brief Initialize a timer heap
 * 
 * @param heap Timer heap pointer
 * @param initial_capacity Initial capacity
 * @return true on success, false on failure
 */
bool co_timer_heap_init(co_timer_heap_t *heap, size_t initial_capacity);

/**
 * @brief Destroy a timer heap
 * 
 * Does not free the coroutines themselves, only the heap storage.
 * 
 * @param heap Timer heap pointer
 */
void co_timer_heap_destroy(co_timer_heap_t *heap);

/**
 * @brief Insert a coroutine into the timer heap
 * 
 * @param heap Timer heap pointer
 * @param routine Coroutine pointer with wakeup_time already set
 * @return true on success, false on failure due to memory pressure
 */
bool co_timer_heap_push(co_timer_heap_t *heap, co_routine_t *routine);

/**
 * @brief Pop the heap top coroutine (earliest deadline)
 * 
 * @param heap Timer heap pointer
 * @return Heap top coroutine, or NULL if empty
 */
co_routine_t *co_timer_heap_pop(co_timer_heap_t *heap);

/**
 * @brief Peek at the heap top coroutine without popping it
 * 
 * @param heap Timer heap pointer
 * @return Heap top coroutine, or NULL if empty
 */
co_routine_t *co_timer_heap_peek(const co_timer_heap_t *heap);

/**
 * @brief Get the number of elements in the heap
 * 
 * @param heap Timer heap pointer
 * @return Element count
 */
size_t co_timer_heap_size(const co_timer_heap_t *heap);

/**
 * @brief Check whether the heap is empty
 * 
 * @param heap Timer heap pointer
 * @return true if empty, false otherwise
 */
bool co_timer_heap_empty(const co_timer_heap_t *heap);

/**
 * @brief Remove a specific coroutine from the timer heap if present
 * 
 * Used when co_cond_timedwait is resumed by signal before timeout so the
 * registered timer can be cancelled. This prevents timer_heap from holding a
 * dangling pointer after the coroutine memory is released.
 * 
 * Time complexity: O(n) search plus O(log n) re-heapify.
 * 
 * @param heap Timer heap pointer
 * @param routine Coroutine pointer to remove
 * @return true if found and removed, false otherwise
 */
bool co_timer_heap_remove(co_timer_heap_t *heap, co_routine_t *routine);

// ============================================================================
// Time helper functions
// ============================================================================

/**
 * @brief Get monotonic time in milliseconds
 * 
 * Used for timers and unaffected by system time adjustments.
 * 
 * @return Current timestamp in milliseconds
 */
uint64_t co_get_monotonic_time_ms(void);

#ifdef __cplusplus
}
#endif

#endif // LIBCO_TIMER_H
