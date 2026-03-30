/**
 * @file test_allocator.c
 * @brief Custom allocator unit tests
 * 
 * Tests custom memory allocator behavior.
 */

#include "unity.h"
#include "co_allocator.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// ============================================================================
// Test helpers
// ============================================================================

// Statistics for the custom allocator
static struct {
    size_t malloc_count;
    size_t realloc_count;
    size_t free_count;
    size_t bytes_allocated;
} g_alloc_stats;

/**
 * @brief Reset the statistics
 */
static void reset_stats(void) {
    memset(&g_alloc_stats, 0, sizeof(g_alloc_stats));
}

/**
 * @brief Custom malloc function
 */
static void *custom_malloc(size_t size, void *userdata) {
    (void)userdata;
    g_alloc_stats.malloc_count++;
    g_alloc_stats.bytes_allocated += size;
    return malloc(size);
}

/**
 * @brief Custom realloc function
 */
static void *custom_realloc(void *ptr, size_t size, void *userdata) {
    (void)userdata;
    g_alloc_stats.realloc_count++;
    return realloc(ptr, size);
}

/**
 * @brief Custom free function
 */
static void custom_free(void *ptr, void *userdata) {
    (void)userdata;
    g_alloc_stats.free_count++;
    free(ptr);
}

void setUp(void) {
    reset_stats();
    // Restore the default allocator
    co_set_allocator(NULL);
}

void tearDown(void) {
    // Restore the default allocator
    co_set_allocator(NULL);
}

// ============================================================================
// Allocator set/get tests
// ============================================================================

/**
 * @brief Test the default allocator
 */
void test_default_allocator(void) {
    const co_allocator_t *allocator = co_get_allocator();
    TEST_ASSERT_NULL(allocator);  // The default allocator should be NULL
}

/**
 * @brief Test setting a custom allocator
 */
void test_set_custom_allocator(void) {
    co_allocator_t allocator = {
        .malloc_fn = custom_malloc,
        .realloc_fn = custom_realloc,
        .free_fn = custom_free,
        .userdata = NULL
    };
    
    co_set_allocator(&allocator);
    
    const co_allocator_t *current = co_get_allocator();
    TEST_ASSERT_NOT_NULL(current);
    TEST_ASSERT_EQUAL_PTR(custom_malloc, current->malloc_fn);
    TEST_ASSERT_EQUAL_PTR(custom_realloc, current->realloc_fn);
    TEST_ASSERT_EQUAL_PTR(custom_free, current->free_fn);
}

/**
 * @brief Test restoring the default allocator
 */
void test_restore_default_allocator(void) {
    co_allocator_t allocator = {
        .malloc_fn = custom_malloc,
        .realloc_fn = custom_realloc,
        .free_fn = custom_free,
        .userdata = NULL
    };
    
    // Set a custom allocator
    co_set_allocator(&allocator);
    TEST_ASSERT_NOT_NULL(co_get_allocator());
    
    // Restore the default allocator
    co_set_allocator(NULL);
    TEST_ASSERT_NULL(co_get_allocator());
}

// ============================================================================
// Allocator functionality tests
// ============================================================================

/**
 * @brief Test using the default allocator
 */
void test_use_default_allocator(void) {
     // Allocate memory with the default allocator
    void *ptr = co_malloc(1024);
    TEST_ASSERT_NOT_NULL(ptr);
    
     // Verify statistics; the default allocator is not counted
    TEST_ASSERT_EQUAL_UINT(0, g_alloc_stats.malloc_count);
    
    co_free(ptr);
}

/**
 * @brief Test using a custom allocator
 */
void test_use_custom_allocator(void) {
    co_allocator_t allocator = {
        .malloc_fn = custom_malloc,
        .realloc_fn = custom_realloc,
        .free_fn = custom_free,
        .userdata = NULL
    };
    co_set_allocator(&allocator);
    
    // Allocate memory
    void *ptr = co_malloc(1024);
    TEST_ASSERT_NOT_NULL(ptr);
    
    // Verify statistics
    TEST_ASSERT_EQUAL_UINT(1, g_alloc_stats.malloc_count);
    TEST_ASSERT_EQUAL_UINT(1024, g_alloc_stats.bytes_allocated);
    
    // Free memory
    co_free(ptr);
    TEST_ASSERT_EQUAL_UINT(1, g_alloc_stats.free_count);
}

/**
 * @brief Test co_calloc
 */
void test_co_calloc(void) {
    co_allocator_t allocator = {
        .malloc_fn = custom_malloc,
        .realloc_fn = custom_realloc,
        .free_fn = custom_free,
        .userdata = NULL
    };
    co_set_allocator(&allocator);
    
    // Allocate and zero-initialize
    size_t count = 10;
    size_t size = sizeof(int);
    int *arr = (int *)co_calloc(count, size);
    TEST_ASSERT_NOT_NULL(arr);
    
    // Verify the memory was zero-initialized
    for (size_t i = 0; i < count; i++) {
        TEST_ASSERT_EQUAL_INT(0, arr[i]);
    }
    
    // Verify statistics
    TEST_ASSERT_EQUAL_UINT(1, g_alloc_stats.malloc_count);
    TEST_ASSERT_EQUAL_UINT(count * size, g_alloc_stats.bytes_allocated);
    
    co_free(arr);
}

/**
 * @brief Test co_realloc
 */
void test_co_realloc(void) {
    co_allocator_t allocator = {
        .malloc_fn = custom_malloc,
        .realloc_fn = custom_realloc,
        .free_fn = custom_free,
        .userdata = NULL
    };
    co_set_allocator(&allocator);
    
    // Initial allocation
    void *ptr = co_malloc(512);
    TEST_ASSERT_NOT_NULL(ptr);
    TEST_ASSERT_EQUAL_UINT(1, g_alloc_stats.malloc_count);
    
    // Reallocate
    void *new_ptr = co_realloc(ptr, 1024);
    TEST_ASSERT_NOT_NULL(new_ptr);
    TEST_ASSERT_EQUAL_UINT(1, g_alloc_stats.realloc_count);
    
    co_free(new_ptr);
}

/**
 * @brief Test freeing a NULL pointer
 */
void test_free_null(void) {
    co_allocator_t allocator = {
        .malloc_fn = custom_malloc,
        .realloc_fn = custom_realloc,
        .free_fn = custom_free,
        .userdata = NULL
    };
    co_set_allocator(&allocator);
    
    // Freeing NULL should be safe
    co_free(NULL);
    
    // The custom free function should not be called
    TEST_ASSERT_EQUAL_UINT(0, g_alloc_stats.free_count);
}

// ============================================================================
// User data tests
// ============================================================================

/**
 * @brief Test allocator user data
 */
void test_allocator_userdata(void) {
    int userdata_value = 12345;
    
    co_allocator_t allocator = {
        .malloc_fn = custom_malloc,
        .realloc_fn = custom_realloc,
        .free_fn = custom_free,
        .userdata = &userdata_value
    };
    co_set_allocator(&allocator);
    
    const co_allocator_t *current = co_get_allocator();
    TEST_ASSERT_NOT_NULL(current);
    TEST_ASSERT_EQUAL_PTR(&userdata_value, current->userdata);
}

// ============================================================================
// Repeated allocation tests
// ============================================================================

/**
 * @brief Test repeated allocation and release
 */
void test_multiple_alloc_free(void) {
    co_allocator_t allocator = {
        .malloc_fn = custom_malloc,
        .realloc_fn = custom_realloc,
        .free_fn = custom_free,
        .userdata = NULL
    };
    co_set_allocator(&allocator);
    
    const int count = 10;
    void **ptrs = (void **)malloc(count * sizeof(void *));
    TEST_ASSERT_NOT_NULL(ptrs);
    
    // Allocate multiple times
    for (int i = 0; i < count; i++) {
        ptrs[i] = co_malloc(1024 * (i + 1));
        TEST_ASSERT_NOT_NULL(ptrs[i]);
    }
    
    TEST_ASSERT_EQUAL_UINT(count, g_alloc_stats.malloc_count);
    
    // Free multiple times
    for (int i = 0; i < count; i++) {
        co_free(ptrs[i]);
    }
    
    TEST_ASSERT_EQUAL_UINT(count, g_alloc_stats.free_count);
    
    free(ptrs);
}

// ============================================================================
// Main function
// ============================================================================

int main(void) {
    UNITY_BEGIN();
    
    // Set/get tests
    RUN_TEST(test_default_allocator);
    RUN_TEST(test_set_custom_allocator);
    RUN_TEST(test_restore_default_allocator);
    
    // Functionality tests
    RUN_TEST(test_use_default_allocator);
    RUN_TEST(test_use_custom_allocator);
    RUN_TEST(test_co_calloc);
    RUN_TEST(test_co_realloc);
    RUN_TEST(test_free_null);
    
    // User data tests
    RUN_TEST(test_allocator_userdata);
    
    // Repeated allocations
    RUN_TEST(test_multiple_alloc_free);
    
    return UNITY_END();
}
