/**
 * @file test_stack_pool.c
 * @brief Stack pool unit tests
 * 
 * Tests stack pool allocation, release, and caching behavior.
 */

#include "unity.h"
#include "co_stack_pool.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// ============================================================================
// Test helpers
// ============================================================================

void setUp(void) {
    // Per-test initialization
}

void tearDown(void) {
    // Per-test cleanup
}

// ============================================================================
// Stack pool creation and destruction tests
// ============================================================================

/**
 * @brief Test stack pool creation and destruction
 */
void test_stack_pool_create_destroy(void) {
    co_stack_pool_t *pool = co_stack_pool_create(128 * 1024, 10);
    TEST_ASSERT_NOT_NULL(pool);
    
    TEST_ASSERT_EQUAL_UINT(128 * 1024, co_stack_pool_stack_size(pool));
    TEST_ASSERT_EQUAL_UINT(10, co_stack_pool_capacity(pool));
    TEST_ASSERT_EQUAL_UINT(0, co_stack_pool_available(pool));
    
    co_stack_pool_destroy(pool);
}

/**
 * @brief Test creating a zero-capacity stack pool
 */
void test_stack_pool_zero_capacity(void) {
    co_stack_pool_t *pool = co_stack_pool_create(64 * 1024, 0);
    TEST_ASSERT_NOT_NULL(pool);
    
    TEST_ASSERT_EQUAL_UINT(0, co_stack_pool_capacity(pool));
    TEST_ASSERT_EQUAL_UINT(0, co_stack_pool_available(pool));
    
    co_stack_pool_destroy(pool);
}

/**
 * @brief Test invalid creation parameters
 */
void test_stack_pool_invalid_params(void) {
    co_stack_pool_t *pool = co_stack_pool_create(0, 10);
    TEST_ASSERT_NULL(pool);
}

// ============================================================================
// Stack allocation and release tests
// ============================================================================

/**
 * @brief Test allocating and freeing a single stack
 */
void test_stack_pool_alloc_free(void) {
    co_stack_pool_t *pool = co_stack_pool_create(128 * 1024, 10);
    TEST_ASSERT_NOT_NULL(pool);
    
    // Allocate a stack
    void *stack = co_stack_pool_alloc(pool);
    TEST_ASSERT_NOT_NULL(stack);
    
    // Verify the stack is writable
    memset(stack, 0xAA, 1024);
    
    // Free the stack
    co_stack_pool_free(pool, stack);
    
    // Verify that the stack was cached
    TEST_ASSERT_EQUAL_UINT(1, co_stack_pool_available(pool));
    
    co_stack_pool_destroy(pool);
}

/**
 * @brief Test allocating and freeing multiple stacks
 */
void test_stack_pool_multiple_alloc_free(void) {
    const size_t count = 5;
    co_stack_pool_t *pool = co_stack_pool_create(64 * 1024, 10);
    TEST_ASSERT_NOT_NULL(pool);
    
    void **stacks = (void **)malloc(count * sizeof(void *));
    TEST_ASSERT_NOT_NULL(stacks);
    
    // Allocate multiple stacks
    for (size_t i = 0; i < count; i++) {
        stacks[i] = co_stack_pool_alloc(pool);
        TEST_ASSERT_NOT_NULL(stacks[i]);
    }
    
    // Free all stacks
    for (size_t i = 0; i < count; i++) {
        co_stack_pool_free(pool, stacks[i]);
    }
    
    // Verify that all stacks were cached
    TEST_ASSERT_EQUAL_UINT(count, co_stack_pool_available(pool));
    
    free(stacks);
    co_stack_pool_destroy(pool);
}

/**
 * @brief Test stack reuse
 */
void test_stack_pool_reuse(void) {
    co_stack_pool_t *pool = co_stack_pool_create(64 * 1024, 10);
    TEST_ASSERT_NOT_NULL(pool);
    
    // First allocation
    void *stack1 = co_stack_pool_alloc(pool);
    TEST_ASSERT_NOT_NULL(stack1);
    
    // Free it
    co_stack_pool_free(pool, stack1);
    TEST_ASSERT_EQUAL_UINT(1, co_stack_pool_available(pool));
    
    // The next allocation should reuse the same stack
    void *stack2 = co_stack_pool_alloc(pool);
    TEST_ASSERT_NOT_NULL(stack2);
    TEST_ASSERT_EQUAL_PTR(stack1, stack2);
    
    // Verify that the pool is empty again
    TEST_ASSERT_EQUAL_UINT(0, co_stack_pool_available(pool));
    
    co_stack_pool_free(pool, stack2);
    co_stack_pool_destroy(pool);
}

/**
 * @brief Test direct release after exceeding capacity
 */
void test_stack_pool_overflow(void) {
    const size_t capacity = 3;
    co_stack_pool_t *pool = co_stack_pool_create(32 * 1024, capacity);
    TEST_ASSERT_NOT_NULL(pool);
    
    void **stacks = (void **)malloc((capacity + 2) * sizeof(void *));
    TEST_ASSERT_NOT_NULL(stacks);
    
    // Allocate more stacks than the pool capacity
    for (size_t i = 0; i < capacity + 2; i++) {
        stacks[i] = co_stack_pool_alloc(pool);
        TEST_ASSERT_NOT_NULL(stacks[i]);
    }
    
    // Free all stacks
    for (size_t i = 0; i < capacity + 2; i++) {
        co_stack_pool_free(pool, stacks[i]);
    }
    
    // Verify that only capacity-limited stacks were cached
    TEST_ASSERT_EQUAL_UINT(capacity, co_stack_pool_available(pool));
    
    free(stacks);
    co_stack_pool_destroy(pool);
}

// ============================================================================
// Statistics tests
// ============================================================================

/**
 * @brief Test statistics reporting
 */
void test_stack_pool_stats(void) {
    co_stack_pool_t *pool = co_stack_pool_create(64 * 1024, 10);
    TEST_ASSERT_NOT_NULL(pool);
    
    co_stack_pool_stats_t stats;
    co_stack_pool_get_stats(pool, &stats);
    
    TEST_ASSERT_EQUAL_UINT(0, stats.total_allocs);
    TEST_ASSERT_EQUAL_UINT(0, stats.total_frees);
    TEST_ASSERT_EQUAL_UINT(0, stats.cache_hits);
    TEST_ASSERT_EQUAL_UINT(0, stats.cache_misses);
    TEST_ASSERT_EQUAL_UINT(0, stats.current_used);
    
    // Allocate three stacks
    void *s1 = co_stack_pool_alloc(pool);
    void *s2 = co_stack_pool_alloc(pool);
    void *s3 = co_stack_pool_alloc(pool);
    
    co_stack_pool_get_stats(pool, &stats);
    TEST_ASSERT_EQUAL_UINT(3, stats.total_allocs);
    TEST_ASSERT_EQUAL_UINT(3, stats.cache_misses);  // The pool is empty, so all accesses miss
    TEST_ASSERT_EQUAL_UINT(3, stats.current_used);
    
    // Free two stacks
    co_stack_pool_free(pool, s1);
    co_stack_pool_free(pool, s2);
    
    co_stack_pool_get_stats(pool, &stats);
    TEST_ASSERT_EQUAL_UINT(2, stats.total_frees);
    TEST_ASSERT_EQUAL_UINT(1, stats.current_used);
    
    // Allocate one more stack; this should hit the cache
    void *s4 = co_stack_pool_alloc(pool);
    
    co_stack_pool_get_stats(pool, &stats);
    TEST_ASSERT_EQUAL_UINT(4, stats.total_allocs);
    TEST_ASSERT_EQUAL_UINT(1, stats.cache_hits);    // One cache hit
    TEST_ASSERT_EQUAL_UINT(3, stats.cache_misses);
    TEST_ASSERT_EQUAL_UINT(2, stats.current_used);
    
    co_stack_pool_free(pool, s3);
    co_stack_pool_free(pool, s4);
    co_stack_pool_destroy(pool);
}

/**
 * @brief Test resetting statistics
 */
void test_stack_pool_reset_stats(void) {
    co_stack_pool_t *pool = co_stack_pool_create(64 * 1024, 10);
    TEST_ASSERT_NOT_NULL(pool);
    
    // Perform a few operations
    void *s1 = co_stack_pool_alloc(pool);
    void *s2 = co_stack_pool_alloc(pool);
    co_stack_pool_free(pool, s1);
    
    // Reset statistics
    co_stack_pool_reset_stats(pool);
    
    co_stack_pool_stats_t stats;
    co_stack_pool_get_stats(pool, &stats);
    
    // total_allocs and similar counters should reset, while current_used stays unchanged
    TEST_ASSERT_EQUAL_UINT(0, stats.total_allocs);
    TEST_ASSERT_EQUAL_UINT(0, stats.total_frees);
    TEST_ASSERT_EQUAL_UINT(0, stats.cache_hits);
    TEST_ASSERT_EQUAL_UINT(0, stats.cache_misses);
    TEST_ASSERT_EQUAL_UINT(1, stats.current_used);
    
    co_stack_pool_free(pool, s2);
    co_stack_pool_destroy(pool);
}

// ============================================================================
// Main function
// ============================================================================

int main(void) {
    UNITY_BEGIN();
    
    // Creation and destruction
    RUN_TEST(test_stack_pool_create_destroy);
    RUN_TEST(test_stack_pool_zero_capacity);
    RUN_TEST(test_stack_pool_invalid_params);
    
    // Allocation and release
    RUN_TEST(test_stack_pool_alloc_free);
    RUN_TEST(test_stack_pool_multiple_alloc_free);
    RUN_TEST(test_stack_pool_reuse);
    RUN_TEST(test_stack_pool_overflow);
    
    // Statistics
    RUN_TEST(test_stack_pool_stats);
    RUN_TEST(test_stack_pool_reset_stats);
    
    return UNITY_END();
}
