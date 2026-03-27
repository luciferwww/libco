/**
 * @file co_timer.c
 * @brief 定时器堆实现
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
// 内部辅助函数
// ============================================================================

/**
 * @brief 上浮操作（堆化）
 * 
 * 将索引 idx 处的元素向上调整，维护最小堆性质。
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
 * @brief 下沉操作（堆化）
 * 
 * 将索引 idx 处的元素向下调整，维护最小堆性质。
 */
static void heap_sift_down(co_timer_heap_t *heap, size_t idx) {
    assert(heap != NULL);
    assert(idx < heap->count);
    
    co_routine_t *item = heap->heap[idx];
    size_t half = heap->count / 2;
    
    while (idx < half) {
        size_t child = 2 * idx + 1;
        size_t right = child + 1;
        
        // 选择较小的子节点
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
// 定时器堆管理
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
    
    // 检查是否需要扩容
    if (heap->count >= heap->capacity) {
        size_t new_capacity = heap->capacity * 2;
        co_routine_t **new_heap = (co_routine_t **)realloc(
            heap->heap, 
            new_capacity * sizeof(co_routine_t *)
        );
        
        if (!new_heap) {
            return false;  // 内存不足
        }
        
        heap->heap = new_heap;
        heap->capacity = new_capacity;
    }
    
    // 插入到末尾并上浮：先递增 count 再 sift_up，确保 idx < heap->count 成立
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
        // 将最后一个元素移到堆顶并下沉
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
            // 替换元素可能比父节点更小（需上浮）或比子节点更大（需下沉）
            size_t parent = (i > 0) ? (i - 1) / 2 : 0;
            if (i > 0 && heap->heap[i]->wakeup_time < heap->heap[parent]->wakeup_time) {
                heap_sift_up(heap, i);
            } else {
                heap_sift_down(heap, i);
            }
        }
        return true;
    }
    return false;  // 未找到（已超时被弹出）
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
// 时间工具函数
// ============================================================================

uint64_t co_get_monotonic_time_ms(void) {
#ifdef _WIN32
    // Windows: 使用 QueryPerformanceCounter
    static LARGE_INTEGER frequency = {0};
    LARGE_INTEGER counter;
    
    if (frequency.QuadPart == 0) {
        QueryPerformanceFrequency(&frequency);
    }
    
    QueryPerformanceCounter(&counter);
    return (uint64_t)((counter.QuadPart * 1000) / frequency.QuadPart);
    
#else
    // Unix/Linux/macOS: 使用 clock_gettime(CLOCK_MONOTONIC)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
#endif
}
