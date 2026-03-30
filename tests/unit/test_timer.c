/**
 * @file test_timer.c
 * @brief Timer heap unit tests
 * 
 * Tests basic timer heap behavior.
 */

#include "unity.h"
#include "co_timer.h"
#include "co_routine.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

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
// Basic timer heap tests
// ============================================================================

/**
 * @brief Test timer heap creation and destruction
 */
void test_timer_heap_create_destroy(void) {
    co_timer_heap_t heap;
    
    bool result = co_timer_heap_init(&heap, 16);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_UINT(0, co_timer_heap_size(&heap));
    TEST_ASSERT_TRUE(co_timer_heap_empty(&heap));
    
    co_timer_heap_destroy(&heap);
}

/**
 * @brief Test timer heap push and pop
 */
void test_timer_heap_push_pop(void) {
    co_timer_heap_t heap;
    co_timer_heap_init(&heap, 4);
    
    // Create a few mock coroutines that only need wakeup_time
    co_routine_t r1 = { .wakeup_time = 100 };
    co_routine_t r2 = { .wakeup_time = 50 };
    co_routine_t r3 = { .wakeup_time = 150 };
    
    // Push elements
    TEST_ASSERT_TRUE(co_timer_heap_push(&heap, &r1));
    TEST_ASSERT_TRUE(co_timer_heap_push(&heap, &r2));
    TEST_ASSERT_TRUE(co_timer_heap_push(&heap, &r3));
    
    TEST_ASSERT_EQUAL_UINT(3, co_timer_heap_size(&heap));
    TEST_ASSERT_FALSE(co_timer_heap_empty(&heap));
    
    // Pop them back in time order
    co_routine_t *popped = co_timer_heap_pop(&heap);
    TEST_ASSERT_NOT_NULL(popped);
    TEST_ASSERT_EQUAL_UINT(50, (unsigned int)popped->wakeup_time);
    
    popped = co_timer_heap_pop(&heap);
    TEST_ASSERT_NOT_NULL(popped);
    TEST_ASSERT_EQUAL_UINT(100, (unsigned int)popped->wakeup_time);
    
    popped = co_timer_heap_pop(&heap);
    TEST_ASSERT_NOT_NULL(popped);
    TEST_ASSERT_EQUAL_UINT(150, (unsigned int)popped->wakeup_time);
    
    TEST_ASSERT_TRUE(co_timer_heap_empty(&heap));
    
    co_timer_heap_destroy(&heap);
}

/**
 * @brief Test peek without popping
 */
void test_timer_heap_peek(void) {
    co_timer_heap_t heap;
    co_timer_heap_init(&heap, 4);
    
    co_routine_t r1 = { .wakeup_time = 100 };
    co_routine_t r2 = { .wakeup_time = 50 };
    
    co_timer_heap_push(&heap, &r1);
    co_timer_heap_push(&heap, &r2);
    
    // peek should not change the heap
    co_routine_t *peeked = co_timer_heap_peek(&heap);
    TEST_ASSERT_NOT_NULL(peeked);
    TEST_ASSERT_EQUAL_UINT(50, (unsigned int)peeked->wakeup_time);
    TEST_ASSERT_EQUAL_UINT(2, co_timer_heap_size(&heap));
    
    // Repeated peek should return the same element
    peeked = co_timer_heap_peek(&heap);
    TEST_ASSERT_EQUAL_UINT(50, (unsigned int)peeked->wakeup_time);
    
    co_timer_heap_destroy(&heap);
}

/**
 * @brief Test automatic heap growth
 */
void test_timer_heap_resize(void) {
    co_timer_heap_t heap;
    co_timer_heap_init(&heap, 2);  // Start with a small capacity
    
    co_routine_t routines[10];
    for (int i = 0; i < 10; i++) {
        routines[i].wakeup_time = i * 10;
        TEST_ASSERT_TRUE(co_timer_heap_push(&heap, &routines[i]));
    }
    
    TEST_ASSERT_EQUAL_UINT(10, co_timer_heap_size(&heap));
    
    // Verify ordering from smallest to largest
    for (int i = 0; i < 10; i++) {
        co_routine_t *popped = co_timer_heap_pop(&heap);
        TEST_ASSERT_NOT_NULL(popped);
        TEST_ASSERT_EQUAL_UINT((unsigned int)(i * 10), (unsigned int)popped->wakeup_time);
    }
    
    co_timer_heap_destroy(&heap);
}

/**
 * @brief Test out-of-order insertion
 */
void test_timer_heap_random_order(void) {
    co_timer_heap_t heap;
    co_timer_heap_init(&heap, 8);
    
    // Insert in arbitrary order
    co_routine_t r1 = { .wakeup_time = 300 };
    co_routine_t r2 = { .wakeup_time = 100 };
    co_routine_t r3 = { .wakeup_time = 500 };
    co_routine_t r4 = { .wakeup_time = 200 };
    co_routine_t r5 = { .wakeup_time = 400 };
    
    co_timer_heap_push(&heap, &r1);
    co_timer_heap_push(&heap, &r2);
    co_timer_heap_push(&heap, &r3);
    co_timer_heap_push(&heap, &r4);
    co_timer_heap_push(&heap, &r5);
    
    // Elements should pop in ascending order
    TEST_ASSERT_EQUAL_UINT(100, (unsigned int)co_timer_heap_pop(&heap)->wakeup_time);
    TEST_ASSERT_EQUAL_UINT(200, (unsigned int)co_timer_heap_pop(&heap)->wakeup_time);
    TEST_ASSERT_EQUAL_UINT(300, (unsigned int)co_timer_heap_pop(&heap)->wakeup_time);
    TEST_ASSERT_EQUAL_UINT(400, (unsigned int)co_timer_heap_pop(&heap)->wakeup_time);
    TEST_ASSERT_EQUAL_UINT(500, (unsigned int)co_timer_heap_pop(&heap)->wakeup_time);
    
    co_timer_heap_destroy(&heap);
}

// ============================================================================
// Time function tests
// ============================================================================

/**
 * @brief Test the monotonic time function
 */
void test_get_monotonic_time(void) {
    uint64_t t1 = co_get_monotonic_time_ms();
    
    // Short delay
    #ifdef _WIN32
    Sleep(10);  // 10 ms
    #else
    struct timespec ts = {0, 10000000};  // 10 ms
    nanosleep(&ts, NULL);
    #endif
    
    uint64_t t2 = co_get_monotonic_time_ms();
    
    // Time should increase monotonically
    TEST_ASSERT_GREATER_OR_EQUAL(t1, t2);
    
    // At least 8 ms should have elapsed, allowing for scheduling noise
    TEST_ASSERT_GREATER_OR_EQUAL(8, t2 - t1);
}

/**
 * @brief Test time precision
 */
void test_time_precision(void) {
    uint64_t t1 = co_get_monotonic_time_ms();
    uint64_t t2 = co_get_monotonic_time_ms();
    
    // The difference between two consecutive calls should be small (< 5 ms)
    TEST_ASSERT_LESS_THAN(5, t2 - t1);
}

// ============================================================================
// Main function
// ============================================================================

int main(void) {
    UNITY_BEGIN();
    
    // Basic tests
    RUN_TEST(test_timer_heap_create_destroy);
    RUN_TEST(test_timer_heap_push_pop);
    RUN_TEST(test_timer_heap_peek);
    RUN_TEST(test_timer_heap_resize);
    RUN_TEST(test_timer_heap_random_order);
    
    // Time function tests
    RUN_TEST(test_get_monotonic_time);
    RUN_TEST(test_time_precision);
    
    return UNITY_END();
}
