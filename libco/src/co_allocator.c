/**
 * @file co_allocator.c
 * @brief Custom memory allocator implementation
 */

#include "co_allocator.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// ============================================================================
// Global allocator
// ============================================================================

static co_allocator_t g_allocator = {
    .malloc_fn = NULL,
    .realloc_fn = NULL,
    .free_fn = NULL,
    .userdata = NULL
};

static bool g_has_custom_allocator = false;

// ============================================================================
// Allocator management
// ============================================================================

void co_set_allocator(const co_allocator_t *allocator) {
    if (allocator) {
        g_allocator = *allocator;
        g_has_custom_allocator = true;
    } else {
        // Restore the default allocator
        g_allocator.malloc_fn = NULL;
        g_allocator.realloc_fn = NULL;
        g_allocator.free_fn = NULL;
        g_allocator.userdata = NULL;
        g_has_custom_allocator = false;
    }
}

const co_allocator_t *co_get_allocator(void) {
    return g_has_custom_allocator ? &g_allocator : NULL;
}

// ============================================================================
// Internal allocation helpers
// ============================================================================

void *co_malloc(size_t size) {
    if (g_has_custom_allocator && g_allocator.malloc_fn) {
        return g_allocator.malloc_fn(size, g_allocator.userdata);
    }
    return malloc(size);
}

void *co_calloc(size_t count, size_t size) {
    size_t total = count * size;
    void *ptr = co_malloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void *co_realloc(void *ptr, size_t size) {
    if (g_has_custom_allocator && g_allocator.realloc_fn) {
        return g_allocator.realloc_fn(ptr, size, g_allocator.userdata);
    }
    return realloc(ptr, size);
}

void co_free(void *ptr) {
    if (!ptr) {
        return;
    }
    
    if (g_has_custom_allocator && g_allocator.free_fn) {
        g_allocator.free_fn(ptr, g_allocator.userdata);
    } else {
        free(ptr);
    }
}
