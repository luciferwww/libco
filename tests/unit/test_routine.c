/**
 * @file test_routine.c
 * @brief Coroutine unit tests
 * 
 * Tests basic coroutine behavior and lifecycle handling.
 */

#include "unity.h"
#include <libco/co.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// ============================================================================
// Test helpers
// ============================================================================

// Global state
static int g_test_counter = 0;
static void *g_test_arg = NULL;
static co_routine_t *g_test_routine = NULL;

void setUp(void) {
    g_test_counter = 0;
    g_test_arg = NULL;
    g_test_routine = NULL;
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
    g_test_arg = arg;
    g_test_counter++;
}

/**
 * @brief Coroutine that validates co_current()
 */
static void test_coroutine_check_current(void *arg) {
     (void)arg;  // Unused parameter
    co_routine_t *current = co_current();
    TEST_ASSERT_NOT_NULL(current);
    TEST_ASSERT_EQUAL_PTR(g_test_routine, current);
    g_test_counter++;
}

/**
 * @brief Coroutine that validates co_current_scheduler()
 */
static void test_coroutine_check_scheduler(void *arg) {
    co_scheduler_t *sched = (co_scheduler_t *)arg;
    co_scheduler_t *current_sched = co_current_scheduler();
    TEST_ASSERT_NOT_NULL(current_sched);
    TEST_ASSERT_EQUAL_PTR(sched, current_sched);
    g_test_counter++;
}

/**
 * @brief Test coroutine argument passing
 */
static void test_coroutine_with_arg(void *arg) {
    int *value = (int *)arg;
    TEST_ASSERT_NOT_NULL(value);
    (*value) *= 2;
}

// ============================================================================
// Coroutine creation tests
// ============================================================================

/**
 * @brief Test coroutine creation
 */
void test_routine_spawn(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    TEST_ASSERT_NOT_NULL(sched);
    
    co_routine_t *co = co_spawn(sched, test_coroutine_simple, NULL, 0);
    TEST_ASSERT_NOT_NULL(co);
    
    co_scheduler_destroy(sched);
}

/**
 * @brief Test coroutine argument passing
 */
void test_routine_argument(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    TEST_ASSERT_NOT_NULL(sched);
    
    void *test_arg = (void *)0x12345678;
    co_routine_t *co = co_spawn(sched, test_coroutine_simple, test_arg, 0);
    TEST_ASSERT_NOT_NULL(co);
    
    co_error_t err = co_scheduler_run(sched);
    TEST_ASSERT_EQUAL_INT(CO_OK, err);
    
    // Verify that the argument was passed correctly
    TEST_ASSERT_EQUAL_PTR(test_arg, g_test_arg);
    
    co_scheduler_destroy(sched);
}

/**
 * @brief Test custom stack size
 */
void test_routine_custom_stack(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    TEST_ASSERT_NOT_NULL(sched);
    
    // Create a coroutine with a smaller 32 KB stack
    co_routine_t *co = co_spawn(sched, test_coroutine_simple, NULL, 32 * 1024);
    TEST_ASSERT_NOT_NULL(co);
    
    co_error_t err = co_scheduler_run(sched);
    TEST_ASSERT_EQUAL_INT(CO_OK, err);
    
    co_scheduler_destroy(sched);
}

// ============================================================================
// co_current() tests
// ============================================================================

/**
 * @brief Test co_current() on the main thread
 */
void test_current_in_main(void) {
    co_routine_t *current = co_current();
    TEST_ASSERT_NULL(current);  // Outside a coroutine this should return NULL
}

/**
 * @brief Test co_current() inside a coroutine
 */
void test_current_in_coroutine(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    TEST_ASSERT_NOT_NULL(sched);
    
    g_test_routine = co_spawn(sched, test_coroutine_check_current, NULL, 0);
    TEST_ASSERT_NOT_NULL(g_test_routine);
    
    co_error_t err = co_scheduler_run(sched);
    TEST_ASSERT_EQUAL_INT(CO_OK, err);
    
    // Verify that the coroutine executed
    TEST_ASSERT_EQUAL_INT(1, g_test_counter);
    
    co_scheduler_destroy(sched);
}

// ============================================================================
// co_current_scheduler() tests
// ============================================================================

/**
 * @brief Test co_current_scheduler() on the main thread
 */
void test_current_scheduler_in_main(void) {
    co_scheduler_t *sched = co_current_scheduler();
    TEST_ASSERT_NULL(sched);  // Outside a scheduler this should return NULL
}

/**
 * @brief Test co_current_scheduler() inside a coroutine
 */
void test_current_scheduler_in_coroutine(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    TEST_ASSERT_NOT_NULL(sched);
    
    co_routine_t *co = co_spawn(sched, test_coroutine_check_scheduler, sched, 0);
    TEST_ASSERT_NOT_NULL(co);
    
    co_error_t err = co_scheduler_run(sched);
    TEST_ASSERT_EQUAL_INT(CO_OK, err);
    
    // Verify that the coroutine executed
    TEST_ASSERT_EQUAL_INT(1, g_test_counter);
    
    co_scheduler_destroy(sched);
}

// ============================================================================
// Coroutine lifecycle tests
// ============================================================================

/**
 * @brief Test coroutine completion
 */
void test_routine_completion(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    TEST_ASSERT_NOT_NULL(sched);
    
    co_routine_t *co = co_spawn(sched, test_coroutine_simple, NULL, 0);
    TEST_ASSERT_NOT_NULL(co);
    
    TEST_ASSERT_EQUAL_INT(0, g_test_counter);
    
    co_error_t err = co_scheduler_run(sched);
    TEST_ASSERT_EQUAL_INT(CO_OK, err);
    
    // Verify that the coroutine ran
    TEST_ASSERT_EQUAL_INT(1, g_test_counter);
    
    co_scheduler_destroy(sched);
}

/**
 * @brief Test coroutine argument modification
 */
void test_routine_modify_argument(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    TEST_ASSERT_NOT_NULL(sched);
    
    int value = 10;
    co_routine_t *co = co_spawn(sched, test_coroutine_with_arg, &value, 0);
    TEST_ASSERT_NOT_NULL(co);
    
    co_error_t err = co_scheduler_run(sched);
    TEST_ASSERT_EQUAL_INT(CO_OK, err);
    
    // Verify that the argument was modified
    TEST_ASSERT_EQUAL_INT(20, value);
    
    co_scheduler_destroy(sched);
}

// ============================================================================
// Main function
// ============================================================================

int main(void) {
    UNITY_BEGIN();
    
    // Coroutine creation tests
    RUN_TEST(test_routine_spawn);
    RUN_TEST(test_routine_argument);
    RUN_TEST(test_routine_custom_stack);
    
    // co_current() tests
    RUN_TEST(test_current_in_main);
    RUN_TEST(test_current_in_coroutine);
    
    // co_current_scheduler() tests
    RUN_TEST(test_current_scheduler_in_main);
    RUN_TEST(test_current_scheduler_in_coroutine);
    
    // Lifecycle tests
    RUN_TEST(test_routine_completion);
    RUN_TEST(test_routine_modify_argument);
    
    return UNITY_END();
}
