/**
 * @file test_scheduler.c
 * @brief Scheduler unit tests
 * 
 * Tests basic scheduler behavior.
 */

#include "unity.h"
#include <libco/co.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// ============================================================================
// Test helpers
// ============================================================================

// Global counters
static int g_test_counter = 0;
static int g_test_values[10] = {0};

void setUp(void) {
    g_test_counter = 0;
    memset(g_test_values, 0, sizeof(g_test_values));
}

void tearDown(void) {
    // Cleanup work
}

// ============================================================================
// Test coroutine functions
// ============================================================================

/**
 * @brief Simple test coroutine
 */
static void test_coroutine_simple(void *arg) {
    int *counter = (int *)arg;
    (*counter)++;
}

/**
 * @brief Test coroutine with a yield
 */
static void test_coroutine_with_yield(void *arg) {
    int *counter = (int *)arg;
    (*counter)++;
    co_yield();
    (*counter)++;
}

/**
 * @brief Test coroutine with multiple yields
 */
static void test_coroutine_multi_yield(void *arg) {
    int index = *(int *)arg;
    for (int i = 0; i < 3; i++) {
        g_test_values[index]++;
        co_yield();
    }
}

// ============================================================================
// Scheduler creation and destruction tests
// ============================================================================

/**
 * @brief Test scheduler creation and destruction
 */
void test_scheduler_create_destroy(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    TEST_ASSERT_NOT_NULL(sched);
    
    co_scheduler_destroy(sched);
    TEST_PASS();
}

/**
 * @brief Test running a scheduler with an empty queue
 */
void test_scheduler_run_empty(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    TEST_ASSERT_NOT_NULL(sched);
    
    // Running an empty scheduler should return immediately
    co_error_t err = co_scheduler_run(sched);
    TEST_ASSERT_EQUAL_INT(CO_OK, err);
    
    co_scheduler_destroy(sched);
}

// ============================================================================
// Coroutine creation and execution tests
// ============================================================================

/**
 * @brief Test single coroutine execution
 */
void test_single_coroutine(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    TEST_ASSERT_NOT_NULL(sched);
    
    // Create the coroutine
    co_routine_t *co = co_spawn(sched, test_coroutine_simple, &g_test_counter, 0);
    TEST_ASSERT_NOT_NULL(co);
    
    // Verify initial state
    TEST_ASSERT_EQUAL_INT(0, g_test_counter);
    
    // Run the scheduler
    co_error_t err = co_scheduler_run(sched);
    TEST_ASSERT_EQUAL_INT(CO_OK, err);
    
    // Verify that the coroutine executed
    TEST_ASSERT_EQUAL_INT(1, g_test_counter);
    
    co_scheduler_destroy(sched);
}

/**
 * @brief Test sequential execution of multiple coroutines
 */
void test_multiple_coroutines(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    TEST_ASSERT_NOT_NULL(sched);
    
    // Create multiple coroutines
    for (int i = 0; i < 5; i++) {
        co_routine_t *co = co_spawn(sched, test_coroutine_simple, &g_test_counter, 0);
        TEST_ASSERT_NOT_NULL(co);
    }
    
    // Verify initial state
    TEST_ASSERT_EQUAL_INT(0, g_test_counter);
    
    // Run the scheduler
    co_error_t err = co_scheduler_run(sched);
    TEST_ASSERT_EQUAL_INT(CO_OK, err);
    
    // Verify that all coroutines executed
    TEST_ASSERT_EQUAL_INT(5, g_test_counter);
    
    co_scheduler_destroy(sched);
}

/**
 * @brief Test coroutine yield
 */
void test_coroutine_yield(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    TEST_ASSERT_NOT_NULL(sched);
    
    // Create a coroutine that yields
    co_routine_t *co = co_spawn(sched, test_coroutine_with_yield, &g_test_counter, 0);
    TEST_ASSERT_NOT_NULL(co);
    
    // Run the scheduler
    co_error_t err = co_scheduler_run(sched);
    TEST_ASSERT_EQUAL_INT(CO_OK, err);
    
    // Verify full execution, including the part after yielding
    TEST_ASSERT_EQUAL_INT(2, g_test_counter);
    
    co_scheduler_destroy(sched);
}

/**
 * @brief Test interleaved execution of multiple coroutines
 */
void test_coroutines_interleaved(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    TEST_ASSERT_NOT_NULL(sched);
    
    // Create three coroutines, each yielding three times
    int indices[3] = {0, 1, 2};
    for (int i = 0; i < 3; i++) {
        co_routine_t *co = co_spawn(sched, test_coroutine_multi_yield, &indices[i], 0);
        TEST_ASSERT_NOT_NULL(co);
    }
    
    // Run the scheduler
    co_error_t err = co_scheduler_run(sched);
    TEST_ASSERT_EQUAL_INT(CO_OK, err);
    
    // Verify that each coroutine ran three times
    TEST_ASSERT_EQUAL_INT(3, g_test_values[0]);
    TEST_ASSERT_EQUAL_INT(3, g_test_values[1]);
    TEST_ASSERT_EQUAL_INT(3, g_test_values[2]);
    
    co_scheduler_destroy(sched);
}

// ============================================================================
// Nested coroutine tests
// ============================================================================

/**
 * @brief Create a coroutine from inside another coroutine
 */
static void test_coroutine_spawn_nested(void *arg) {
    int *counter = (int *)arg;
    (*counter)++;
    
    // Create a new coroutine from inside a coroutine
    co_scheduler_t *sched = co_current_scheduler();
    if (sched && *counter < 3) {
        co_spawn(sched, test_coroutine_spawn_nested, counter, 0);
    }
}

/**
 * @brief Test nested coroutine creation
 */
void test_nested_spawn(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    TEST_ASSERT_NOT_NULL(sched);
    
    // Create the first coroutine; it will recursively spawn more coroutines
    co_routine_t *co = co_spawn(sched, test_coroutine_spawn_nested, &g_test_counter, 0);
    TEST_ASSERT_NOT_NULL(co);
    
    // Run the scheduler
    co_error_t err = co_scheduler_run(sched);
    TEST_ASSERT_EQUAL_INT(CO_OK, err);
    
    // Verify that all nested coroutines executed
    TEST_ASSERT_EQUAL_INT(3, g_test_counter);
    
    co_scheduler_destroy(sched);
}

// ============================================================================
// Main function
// ============================================================================

int main(void) {
    UNITY_BEGIN();
    
    // Basic scheduler tests
    RUN_TEST(test_scheduler_create_destroy);
    RUN_TEST(test_scheduler_run_empty);
    
    // Coroutine execution tests
    RUN_TEST(test_single_coroutine);
    RUN_TEST(test_multiple_coroutines);
    
    // Yield tests
    RUN_TEST(test_coroutine_yield);
    RUN_TEST(test_coroutines_interleaved);
    
    // Nested tests
    RUN_TEST(test_nested_spawn);
    
    return UNITY_END();
}
