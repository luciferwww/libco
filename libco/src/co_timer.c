/**
 * @file co_timer.c
 * @brief Timer heap implementation
 */

#include "co_timer.h"
#include "co_routine.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

// ============================================================================
// Internal helper functions
// ============================================================================

/**
 * @brief Sift up operation for heap maintenance
 * 
 * Move the element at idx upward to preserve the min-heap property.
 */
static void heap_sift_up(co_timer_heap_t *heap, size_t idx) {
    assert(heap != NULL);
    assert(idx < heap->count);
    
    co_routine_t *item = heap->heap[idx];
    
    while (idx > 0) {
        size_t parent = (idx - 1) / 2;
        if (heap->heap[parent]->wakeup_time <= item->wakeup_time) {
            break;
        }
        heap->heap[idx] = heap->heap[parent];
        idx = parent;
    }
    
    heap->heap[idx] = item;
}

/**
 * @brief Sift down operation for heap maintenance
 * 
 * Move the element at idx downward to preserve the min-heap property.
 */
static void heap_sift_down(co_timer_heap_t *heap, size_t idx) {
    assert(heap != NULL);
    assert(idx < heap->count);
    
    co_routine_t *item = heap->heap[idx];
    size_t half = heap->count / 2;
    
    while (idx < half) {
        size_t child = 2 * idx + 1;
        size_t right = child + 1;
        
        // Choose the smaller child node
        if (right < heap->count && 
            heap->heap[right]->wakeup_time < heap->heap[child]->wakeup_time) {
            child = right;
        }
        
        if (item->wakeup_time <= heap->heap[child]->wakeup_time) {
            break;
        }
        
        heap->heap[idx] = heap->heap[child];
        idx = child;
    }
    
    heap->heap[idx] = item;
}

// ============================================================================
// Timer heap management
// ============================================================================

bool co_timer_heap_init(co_timer_heap_t *heap, size_t initial_capacity) {
    if (!heap || initial_capacity == 0) {
        return false;
    }
    
    heap->heap = (co_routine_t **)calloc(initial_capacity, sizeof(co_routine_t *));
    if (!heap->heap) {
        return false;
    }
    
    heap->count = 0;
    heap->capacity = initial_capacity;
    
    return true;
}

void co_timer_heap_destroy(co_timer_heap_t *heap) {
    if (!heap) {
        return;
    }
    
    free(heap->heap);
    heap->heap = NULL;
    heap->count = 0;
    heap->capacity = 0;
}

bool co_timer_heap_push(co_timer_heap_t *heap, co_routine_t *routine) {
    assert(heap != NULL);
    assert(routine != NULL);
    
    // Check whether the heap needs to grow
    if (heap->count >= heap->capacity) {
        size_t new_capacity = heap->capacity * 2;
        co_routine_t **new_heap = (co_routine_t **)realloc(
            heap->heap, 
            new_capacity * sizeof(co_routine_t *)
        );
        
        if (!new_heap) {
            return false;  // Out of memory
        }
        
        heap->heap = new_heap;
        heap->capacity = new_capacity;
    }
    
    // Insert at the end and sift up. Increment count first so idx < heap->count holds.
    size_t insert_idx = heap->count;
    heap->heap[insert_idx] = routine;
    heap->count++;
    heap_sift_up(heap, insert_idx);
    
    return true;
}

co_routine_t *co_timer_heap_pop(co_timer_heap_t *heap) {
    assert(heap != NULL);
    
    if (heap->count == 0) {
        return NULL;
    }
    
    co_routine_t *result = heap->heap[0];
    heap->count--;
    
    if (heap->count > 0) {
        // Move the last element to the root and sift down
        heap->heap[0] = heap->heap[heap->count];
        heap_sift_down(heap, 0);
    }
    
    return result;
}

bool co_timer_heap_remove(co_timer_heap_t *heap, co_routine_t *routine) {
    assert(heap != NULL);
    assert(routine != NULL);

    for (size_t i = 0; i < heap->count; i++) {
        if (heap->heap[i] != routine) continue;

        heap->count--;
        if (i < heap->count) {
            heap->heap[i] = heap->heap[heap->count];
            // The replacement may need to sift up or down depending on its new neighbors
            size_t parent = (i > 0) ? (i - 1) / 2 : 0;
            if (i > 0 && heap->heap[i]->wakeup_time < heap->heap[parent]->wakeup_time) {
                heap_sift_up(heap, i);
            } else {
                heap_sift_down(heap, i);
            }
        }
        return true;
    }
    return false;  // Not found, possibly already popped on timeout
}

co_routine_t *co_timer_heap_peek(const co_timer_heap_t *heap) {
    assert(heap != NULL);
    
    if (heap->count == 0) {
        return NULL;
    }
    
    return heap->heap[0];
}

size_t co_timer_heap_size(const co_timer_heap_t *heap) {
    assert(heap != NULL);
    return heap->count;
}

bool co_timer_heap_empty(const co_timer_heap_t *heap) {
    assert(heap != NULL);
    return heap->count == 0;
}

// ============================================================================
// Time helper functions
// ============================================================================

uint64_t co_get_monotonic_time_ms(void) {
#ifdef _WIN32
    // Windows: use QueryPerformanceCounter
    static LARGE_INTEGER frequency = {0};
    LARGE_INTEGER counter;
    
    if (frequency.QuadPart == 0) {
        QueryPerformanceFrequency(&frequency);
    }
    
    QueryPerformanceCounter(&counter);
    return (uint64_t)((counter.QuadPart * 1000) / frequency.QuadPart);
    
#else
    // Unix/Linux/macOS: use clock_gettime(CLOCK_MONOTONIC)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
#endif
}
