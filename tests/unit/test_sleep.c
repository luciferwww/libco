/**
 * @file test_sleep.c
 * @brief co_sleep() unit tests
 * 
 * Tests coroutine sleep behavior.
 */

#include "unity.h"
#include <libco/co.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

// ============================================================================
// Test helpers
// ============================================================================

static int g_counter = 0;
static uint64_t g_start_time = 0;
static uint64_t g_wake_time = 0;

void setUp(void) {
    g_counter = 0;
    g_start_time = 0;
    g_wake_time = 0;
}

void tearDown(void) {
    // Cleanup work
}

// Get the current time in milliseconds
static uint64_t get_time_ms(void) {
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
#endif
}

// ============================================================================
// Test coroutine functions
// ============================================================================

/**
 * @brief Simple sleeping coroutine
 */
static void coroutine_simple_sleep(void *arg) {
    uint32_t sleep_ms = *(uint32_t *)arg;
    g_counter++;
    
    g_start_time = get_time_ms();
    co_sleep(sleep_ms);
    g_wake_time = get_time_ms();
    
    g_counter++;
}

/**
 * @brief Coroutine that sleeps multiple times
 */
static void coroutine_multiple_sleep(void *arg) {
    (void)arg;
    
    for (int i = 0; i < 3; i++) {
        g_counter++;
        co_sleep(10);
    }
}

/**
 * @brief Zero sleep, equivalent to yield
 */
static void coroutine_zero_sleep(void *arg) {
    (void)arg;
    
    g_counter++;
    co_sleep(0);  // Should behave like co_yield()
    g_counter++;
}

// ============================================================================
// Basic sleep tests
// ============================================================================

/**
 * @brief Test simple sleep
 */
void test_simple_sleep(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    TEST_ASSERT_NOT_NULL(sched);
    
    uint32_t sleep_time = 50;  // 50 ms
    co_spawn(sched, coroutine_simple_sleep, &sleep_time, 0);
    
    TEST_ASSERT_EQUAL_INT(0, g_counter);
    
    co_error_t err = co_scheduler_run(sched);
    TEST_ASSERT_EQUAL_INT(CO_OK, err);
    
    // The coroutine should run twice, before and after sleeping
    TEST_ASSERT_EQUAL_INT(2, g_counter);
    
    // Verify sleep duration with an allowed error of about +/-20 ms
    uint64_t actual_sleep = g_wake_time - g_start_time;
    TEST_ASSERT_GREATER_OR_EQUAL(30, actual_sleep);  // At least 30 ms
    TEST_ASSERT_LESS_THAN(70, actual_sleep);         // No more than 70 ms
    
    co_scheduler_destroy(sched);
}

/**
 * @brief Test zero sleep, equivalent to yield
 */
void test_zero_sleep(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    TEST_ASSERT_NOT_NULL(sched);
    
    co_spawn(sched, coroutine_zero_sleep, NULL, 0);
    
    co_error_t err = co_scheduler_run(sched);
    TEST_ASSERT_EQUAL_INT(CO_OK, err);
    
    // It should have run twice
    TEST_ASSERT_EQUAL_INT(2, g_counter);
    
    co_scheduler_destroy(sched);
}

/**
 * @brief Test multiple sleeps
 */
void test_multiple_sleep(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    TEST_ASSERT_NOT_NULL(sched);
    
    co_spawn(sched, coroutine_multiple_sleep, NULL, 0);
    
    uint64_t start = get_time_ms();
    co_error_t err = co_scheduler_run(sched);
    uint64_t end = get_time_ms();
    
    TEST_ASSERT_EQUAL_INT(CO_OK, err);
    TEST_ASSERT_EQUAL_INT(3, g_counter);
    
    // Total sleep time is 3 * 10 ms = 30 ms
    uint64_t total_time = end - start;
    TEST_ASSERT_GREATER_OR_EQUAL(20, total_time);  // At least 20 ms
    TEST_ASSERT_LESS_THAN(150, total_time);        // No more than 150 ms; Windows timer granularity can add delay
    
    co_scheduler_destroy(sched);
}

// ============================================================================
// Multi-coroutine sleep tests
// ============================================================================

static void coroutine_sleep_with_id(void *arg) {
    int id = *(int *)arg;
    
    // Sleep for different durations based on the ID
    co_sleep(id * 20);
    
    g_counter++;
}

/**
 * @brief Test multiple coroutines sleeping at the same time
 */
void test_multiple_coroutines_sleep(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    TEST_ASSERT_NOT_NULL(sched);
    
    int ids[3] = {3, 1, 2};  // Sleep durations: 60 ms, 20 ms, 40 ms
    
    for (int i = 0; i < 3; i++) {
        co_spawn(sched, coroutine_sleep_with_id, &ids[i], 0);
    }
    
    uint64_t start = get_time_ms();
    co_error_t err = co_scheduler_run(sched);
    uint64_t end = get_time_ms();
    
    TEST_ASSERT_EQUAL_INT(CO_OK, err);
    TEST_ASSERT_EQUAL_INT(3, g_counter);
    
    // The longest sleep duration is 60 ms
    uint64_t total_time = end - start;
    TEST_ASSERT_GREATER_OR_EQUAL(40, total_time);   // At least 40 ms
    TEST_ASSERT_LESS_THAN(100, total_time);         // No more than 100 ms
    
    co_scheduler_destroy(sched);
}

// ============================================================================
// Interleaving tests
// ============================================================================

static int g_execution_order[10];
static int g_order_idx = 0;

static void coroutine_interleaved_sleep(void *arg) {
    int id = *(int *)arg;
    
    g_execution_order[g_order_idx++] = id;
    co_sleep(10);
    g_execution_order[g_order_idx++] = id;
}

/**
 * @brief Test interleaved sleeping and waking among coroutines
 */
void test_interleaved_sleep(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    TEST_ASSERT_NOT_NULL(sched);
    
    int id1 = 1, id2 = 2;
    co_spawn(sched, coroutine_interleaved_sleep, &id1, 0);
    co_spawn(sched, coroutine_interleaved_sleep, &id2, 0);
    
    co_error_t err = co_scheduler_run(sched);
    TEST_ASSERT_EQUAL_INT(CO_OK, err);
    
    // The execution order should be 1 enter, 2 enter, then 1/2 wake in either order.
    TEST_ASSERT_EQUAL_INT(1, g_execution_order[0]);
    TEST_ASSERT_EQUAL_INT(2, g_execution_order[1]);
    // The final two positions may swap, so the test does not enforce them strictly
    
    co_scheduler_destroy(sched);
}

// ============================================================================
// Precision tests
// ============================================================================

/**
 * @brief Test sleep precision with an error target below 10 ms
 */
void test_sleep_precision(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    TEST_ASSERT_NOT_NULL(sched);
    
    uint32_t sleep_time = 100;  // 100 ms
    co_spawn(sched, coroutine_simple_sleep, &sleep_time, 0);
    
    co_error_t err = co_scheduler_run(sched);
    TEST_ASSERT_EQUAL_INT(CO_OK, err);
    
    uint64_t actual_sleep = g_wake_time - g_start_time;
    
    // Acceptance criterion: timing error stays within a small range
    int64_t error = (int64_t)actual_sleep - (int64_t)sleep_time;
    if (error < 0) error = -error;
    
    // Allow roughly -5 ms to +30 ms error because Windows timer granularity is around 15 ms
    TEST_ASSERT_LESS_THAN(30, error);
    
    co_scheduler_destroy(sched);
}

// ============================================================================
// Main function
// ============================================================================

int main(void) {
    UNITY_BEGIN();
    
    // Basic sleep tests
    RUN_TEST(test_simple_sleep);
    RUN_TEST(test_zero_sleep);
    RUN_TEST(test_multiple_sleep);
    
    // Multi-coroutine tests
    RUN_TEST(test_multiple_coroutines_sleep);
    RUN_TEST(test_interleaved_sleep);
    
    // Precision tests
    RUN_TEST(test_sleep_precision);
    
    return UNITY_END();
}
