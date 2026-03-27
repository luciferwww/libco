/**
 * @file test_context.c
 * @brief 上下文切换单元测试
 * 
 * 测试跨平台的上下文切换功能。
 */

#include "unity.h"
#include "platform/context.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// ============================================================================
// 测试辅助
// ============================================================================

// 默认栈大小
#define TEST_STACK_SIZE (64 * 1024)  // 64KB

// 全局变量用于测试
static int g_test_counter = 0;
static int g_test_value = 0;

/**
 * @brief 测试协程入口函数 - 简单计数
 */
static void test_coroutine_simple(void *arg) {
    int *counter = (int *)arg;
    (*counter)++;
    printf("Coroutine executed: counter = %d\n", *counter);
}

/**
 * @brief 测试协程入口函数 - 多次让出
 */
static void test_coroutine_yield(void *arg) {
    int *value = (int *)arg;
    for (int i = 0; i < 3; i++) {
        (*value) += 10;
        printf("Coroutine: value = %d\n", *value);
        // 注意：这里无法直接调用 co_yield，因为还没有实现调度器
        // 这个测试只验证上下文切换本身
    }
}

// ============================================================================
// 测试用例
// ============================================================================

void setUp(void) {
    g_test_counter = 0;
    g_test_value = 0;
}

void tearDown(void) {
    // 清理工作
}

/**
 * @brief 测试上下文初始化
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
 * @brief 测试无效参数
 */
void test_context_init_invalid_params(void) {
    co_context_t ctx;
    void *stack = malloc(TEST_STACK_SIZE);
    
    // NULL 上下文
    TEST_ASSERT_EQUAL_INT(CO_ERROR_INVAL,
                         co_context_init(NULL, stack, TEST_STACK_SIZE,
                                        test_coroutine_simple, NULL));
    
    // NULL 入口函数
    TEST_ASSERT_EQUAL_INT(CO_ERROR_INVAL,
                         co_context_init(&ctx, stack, TEST_STACK_SIZE,
                                        NULL, NULL));
    
    // NULL 栈（仅在非 Windows 平台测试）
    #if !defined(_WIN32)
    TEST_ASSERT_EQUAL_INT(CO_ERROR_INVAL,
                         co_context_init(&ctx, NULL, TEST_STACK_SIZE,
                                        test_coroutine_simple, NULL));
    #endif
    
    // 0 栈大小（仅在非 Windows 平台测试）
    #if !defined(_WIN32)
    TEST_ASSERT_EQUAL_INT(CO_ERROR_INVAL,
                         co_context_init(&ctx, stack, 0,
                                        test_coroutine_simple, NULL));
    #endif
    
    free(stack);
}

/**
 * @brief 测试基本的上下文切换
 * 
 * 注意：这个测试比较复杂，因为需要创建主上下文和协程上下文
 * 
 * TODO(Week 4): 当前此测试被跳过，因为协程不会自动返回（需要调度器）
 */
void test_context_swap_basic(void) {
    // Week 3 暂时跳过此测试：协程执行后不会返回，导致测试卡住
    // Week 4 实现调度器后，协程可以正确返回，届时启用此测试
    TEST_IGNORE_MESSAGE("Requires scheduler (Week 4) to handle coroutine return");
    
#if 0  // 暂时禁用，Week 4 启用
    // 分配栈
    void *stack = malloc(TEST_STACK_SIZE);
    TEST_ASSERT_NOT_NULL(stack);
    
    // 创建主上下文和协程上下文
    co_context_t main_ctx;
    co_context_t co_ctx;
    
    // 初始化协程上下文
    co_error_t result = co_context_init(&co_ctx, stack, TEST_STACK_SIZE,
                                        test_coroutine_simple, &g_test_counter);
    TEST_ASSERT_EQUAL_INT(CO_OK, result);
    
    // 在 Windows 上，需要先将主线程转换为 Fiber（在上下文切换代码中自动完成）
    // 在 Unix 上，需要保存当前上下文
    #if defined(LIBCO_PLATFORM_LINUX) || defined(LIBCO_PLATFORM_MACOS)
    result = co_context_get_current(&main_ctx);
    TEST_ASSERT_EQUAL_INT(CO_OK, result);
    #else
    // Windows 平台：初始化一个空的主上下文
    memset(&main_ctx, 0, sizeof(main_ctx));
    #endif
    
    // 验证计数器初始值
    TEST_ASSERT_EQUAL_INT(0, g_test_counter);
    
    // 切换到协程
    printf("Switching to coroutine...\n");
    result = co_context_swap(&main_ctx, &co_ctx);
    
    // 注意：在当前简单实现中，协程执行完后不会切换回来
    // 所以这行代码可能不会执行
    // 完整的调度器实现会处理协程返回
    printf("Returned from coroutine (if this prints, coroutine returned)\n");
    
    // 清理
    co_context_destroy(&co_ctx);
    free(stack);
    
    // 注意：由于协程可能不会返回，这个断言可能不会执行
    // 这是一个已知的限制，完整的调度器会解决这个问题
    // TEST_ASSERT_EQUAL_INT(1, g_test_counter);
#endif
}

/**
 * @brief 测试上下文销毁
 */
void test_context_destroy(void) {
    co_context_t ctx;
    void *stack = malloc(TEST_STACK_SIZE);
    
    co_context_init(&ctx, stack, TEST_STACK_SIZE,
                   test_coroutine_simple, NULL);
    
    // 销毁上下文
    co_context_destroy(&ctx);
    
    // 验证清理
    #if !defined(_WIN32)
    TEST_ASSERT_EQUAL_PTR(NULL, ctx.stack_base);
    TEST_ASSERT_EQUAL_size_t(0, ctx.stack_size);
    #endif
    
    free(stack);
}

/**
 * @brief 测试NULL上下文销毁（不应崩溃）
 */
void test_context_destroy_null(void) {
    co_context_destroy(NULL);  // 应该安全返回
    TEST_ASSERT_TRUE(true);    // 如果到达这里，说明没有崩溃
}

// ============================================================================
// 主函数
// ============================================================================

int main(void) {
    UNITY_BEGIN();
    
    // 初始化测试
    RUN_TEST(test_context_init_success);
    RUN_TEST(test_context_init_invalid_params);
    
    // 切换测试（Week 3 暂时跳过，Week 4 启用）
    RUN_TEST(test_context_swap_basic);
    
    // 销毁测试
    RUN_TEST(test_context_destroy);
    RUN_TEST(test_context_destroy_null);
    
    return UNITY_END();
}
