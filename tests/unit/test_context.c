/**
 * @file test_context.c
 * @brief Context switching unit tests
 * 
 * Tests cross-platform context switching behavior.
 */

#include "unity.h"
#include "platform/context.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// ============================================================================
// Test helpers
// ============================================================================

// Default stack size
#define TEST_STACK_SIZE (64 * 1024)  // 64KB

// Global state used by the tests
static int g_test_counter = 0;
static int g_test_value = 0;

/**
 * @brief Test coroutine entry function for simple counting
 */
static void test_coroutine_simple(void *arg) {
    int *counter = (int *)arg;
    (*counter)++;
    printf("Coroutine executed: counter = %d\n", *counter);
}

/**
 * @brief Test coroutine entry function with multiple yield points
 */
static void test_coroutine_yield(void *arg) {
    int *value = (int *)arg;
    for (int i = 0; i < 3; i++) {
        (*value) += 10;
        printf("Coroutine: value = %d\n", *value);
        // Note: co_yield cannot be called directly here because the scheduler
        // has not been implemented yet. This test only validates context switching.
    }
}

// ============================================================================
    // Test cases
// ============================================================================

void setUp(void) {
    g_test_counter = 0;
    g_test_value = 0;
}

void tearDown(void) {
    // Cleanup work
}

/**
 * @brief Test context initialization
 */
void test_context_init_success(void) {
    co_context_t ctx;
    void *stack = malloc(TEST_STACK_SIZE);
    TEST_ASSERT_NOT_NULL(stack);
    
    co_error_t result = co_context_init(&ctx, stack, TEST_STACK_SIZE,
                                        test_coroutine_simple, &g_test_counter);
    
    TEST_ASSERT_EQUAL_INT(CO_OK, result);
    TEST_ASSERT_EQUAL_PTR(stack, ctx.stack_base);
    TEST_ASSERT_EQUAL_size_t(TEST_STACK_SIZE, ctx.stack_size);
    
    co_context_destroy(&ctx);
    free(stack);
}

/**
 * @brief Test invalid parameters
 */
void test_context_init_invalid_params(void) {
    co_context_t ctx;
    void *stack = malloc(TEST_STACK_SIZE);
    
    // NULL context
    TEST_ASSERT_EQUAL_INT(CO_ERROR_INVAL,
                         co_context_init(NULL, stack, TEST_STACK_SIZE,
                                        test_coroutine_simple, NULL));
    
    // NULL entry function
    TEST_ASSERT_EQUAL_INT(CO_ERROR_INVAL,
                         co_context_init(&ctx, stack, TEST_STACK_SIZE,
                                        NULL, NULL));
    
    // NULL stack, tested only on non-Windows platforms
    #if !defined(_WIN32)
    TEST_ASSERT_EQUAL_INT(CO_ERROR_INVAL,
                         co_context_init(&ctx, NULL, TEST_STACK_SIZE,
                                        test_coroutine_simple, NULL));
    #endif
    
    // Zero stack size, tested only on non-Windows platforms
    #if !defined(_WIN32)
    TEST_ASSERT_EQUAL_INT(CO_ERROR_INVAL,
                         co_context_init(&ctx, stack, 0,
                                        test_coroutine_simple, NULL));
    #endif
    
    free(stack);
}

/**
 * @brief Test basic context switching
 * 
 * Note: this test is more complex because it needs both a main context and a coroutine context.
 * 
 * TODO(Week 4): this test is currently skipped because coroutines do not yet
 * return automatically; scheduler support is required.
 */
void test_context_swap_basic(void) {
    // Skip this test for Week 3: the coroutine does not return after execution,
    // which would cause the test to hang. Re-enable it once the scheduler exists.
    TEST_IGNORE_MESSAGE("Requires scheduler (Week 4) to handle coroutine return");
    
#if 0  // Temporarily disabled; enable in Week 4
    // Allocate the stack
    void *stack = malloc(TEST_STACK_SIZE);
    TEST_ASSERT_NOT_NULL(stack);
    
    // Create the main context and the coroutine context
    co_context_t main_ctx;
    co_context_t co_ctx;
    
    // Initialize the coroutine context
    co_error_t result = co_context_init(&co_ctx, stack, TEST_STACK_SIZE,
                                        test_coroutine_simple, &g_test_counter);
    TEST_ASSERT_EQUAL_INT(CO_OK, result);
    
    // On Windows the main thread must first be converted to a Fiber, which is
    // handled automatically in the context switching code. On Unix, capture the current context.
    #if defined(LIBCO_PLATFORM_LINUX) || defined(LIBCO_PLATFORM_MACOS)
    result = co_context_get_current(&main_ctx);
    TEST_ASSERT_EQUAL_INT(CO_OK, result);
    #else
    // Windows: initialize an empty main context
    memset(&main_ctx, 0, sizeof(main_ctx));
    #endif
    
    // Verify the initial counter value
    TEST_ASSERT_EQUAL_INT(0, g_test_counter);
    
    // Switch to the coroutine
    printf("Switching to coroutine...\n");
    result = co_context_swap(&main_ctx, &co_ctx);
    
    // In the current simplified implementation the coroutine does not switch back
    // after completion, so this line may never run. A full scheduler handles the return path.
    printf("Returned from coroutine (if this prints, coroutine returned)\n");
    
    // Cleanup
    co_context_destroy(&co_ctx);
    free(stack);
    
    // Because the coroutine may not return, this assertion may never execute.
    // That is a known limitation which the full scheduler will resolve.
    // TEST_ASSERT_EQUAL_INT(1, g_test_counter);
#endif
}

/**
 * @brief Test context destruction
 */
void test_context_destroy(void) {
    co_context_t ctx;
    void *stack = malloc(TEST_STACK_SIZE);
    
    co_context_init(&ctx, stack, TEST_STACK_SIZE,
                   test_coroutine_simple, NULL);
    
    // Destroy the context
    co_context_destroy(&ctx);
    
    // Verify cleanup
    #if !defined(_WIN32)
    TEST_ASSERT_EQUAL_PTR(NULL, ctx.stack_base);
    TEST_ASSERT_EQUAL_size_t(0, ctx.stack_size);
    #endif
    
    free(stack);
}

/**
 * @brief Test destroying a NULL context without crashing
 */
void test_context_destroy_null(void) {
    co_context_destroy(NULL);  // Should return safely
    TEST_ASSERT_TRUE(true);    // Reaching this point means no crash occurred
}

// ============================================================================
// Main function
// ============================================================================

int main(void) {
    UNITY_BEGIN();
    
    // Initialization tests
    RUN_TEST(test_context_init_success);
    RUN_TEST(test_context_init_invalid_params);
    
    // Switching tests, skipped for Week 3 and enabled in Week 4
    RUN_TEST(test_context_swap_basic);
    
    // Destruction tests
    RUN_TEST(test_context_destroy);
    RUN_TEST(test_context_destroy_null);
    
    return UNITY_END();
}
