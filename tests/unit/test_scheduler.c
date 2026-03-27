/**
 * @file test_scheduler.c
 * @brief 调度器单元测试
 * 
 * 测试调度器的基本功能。
 */

#include "unity.h"
#include <libco/co.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// ============================================================================
// 测试辅助
// ============================================================================

// 全局计数器
static int g_test_counter = 0;
static int g_test_values[10] = {0};

void setUp(void) {
    g_test_counter = 0;
    memset(g_test_values, 0, sizeof(g_test_values));
}

void tearDown(void) {
    // 清理工作
}

// ============================================================================
// 测试协程函数
// ============================================================================

/**
 * @brief 简单的测试协程
 */
static void test_coroutine_simple(void *arg) {
    int *counter = (int *)arg;
    (*counter)++;
}

/**
 * @brief 带 yield 的测试协程
 */
static void test_coroutine_with_yield(void *arg) {
    int *counter = (int *)arg;
    (*counter)++;
    co_yield();
    (*counter)++;
}

/**
 * @brief 多次 yield 的测试协程
 */
static void test_coroutine_multi_yield(void *arg) {
    int index = *(int *)arg;
    for (int i = 0; i < 3; i++) {
        g_test_values[index]++;
        co_yield();
    }
}

// ============================================================================
// 调度器创建和销毁测试
// ============================================================================

/**
 * @brief 测试调度器创建和销毁
 */
void test_scheduler_create_destroy(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    TEST_ASSERT_NOT_NULL(sched);
    
    co_scheduler_destroy(sched);
    TEST_PASS();
}

/**
 * @brief 测试调度器运行（空队列）
 */
void test_scheduler_run_empty(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    TEST_ASSERT_NOT_NULL(sched);
    
    // 运行空调度器应该立即返回
    co_error_t err = co_scheduler_run(sched);
    TEST_ASSERT_EQUAL_INT(CO_OK, err);
    
    co_scheduler_destroy(sched);
}

// ============================================================================
// 协程创建和执行测试
// ============================================================================

/**
 * @brief 测试单个协程执行
 */
void test_single_coroutine(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    TEST_ASSERT_NOT_NULL(sched);
    
    // 创建协程
    co_routine_t *co = co_spawn(sched, test_coroutine_simple, &g_test_counter, 0);
    TEST_ASSERT_NOT_NULL(co);
    
    // 验证初始状态
    TEST_ASSERT_EQUAL_INT(0, g_test_counter);
    
    // 运行调度器
    co_error_t err = co_scheduler_run(sched);
    TEST_ASSERT_EQUAL_INT(CO_OK, err);
    
    // 验证协程已执行
    TEST_ASSERT_EQUAL_INT(1, g_test_counter);
    
    co_scheduler_destroy(sched);
}

/**
 * @brief 测试多个协程顺序执行
 */
void test_multiple_coroutines(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    TEST_ASSERT_NOT_NULL(sched);
    
    // 创建多个协程
    for (int i = 0; i < 5; i++) {
        co_routine_t *co = co_spawn(sched, test_coroutine_simple, &g_test_counter, 0);
        TEST_ASSERT_NOT_NULL(co);
    }
    
    // 验证初始状态
    TEST_ASSERT_EQUAL_INT(0, g_test_counter);
    
    // 运行调度器
    co_error_t err = co_scheduler_run(sched);
    TEST_ASSERT_EQUAL_INT(CO_OK, err);
    
    // 验证所有协程都执行了
    TEST_ASSERT_EQUAL_INT(5, g_test_counter);
    
    co_scheduler_destroy(sched);
}

/**
 * @brief 测试协程 yield
 */
void test_coroutine_yield(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    TEST_ASSERT_NOT_NULL(sched);
    
    // 创建带 yield 的协程
    co_routine_t *co = co_spawn(sched, test_coroutine_with_yield, &g_test_counter, 0);
    TEST_ASSERT_NOT_NULL(co);
    
    // 运行调度器
    co_error_t err = co_scheduler_run(sched);
    TEST_ASSERT_EQUAL_INT(CO_OK, err);
    
    // 验证协程完整执行（包括 yield 后的部分）
    TEST_ASSERT_EQUAL_INT(2, g_test_counter);
    
    co_scheduler_destroy(sched);
}

/**
 * @brief 测试多个协程交替执行
 */
void test_coroutines_interleaved(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    TEST_ASSERT_NOT_NULL(sched);
    
    // 创建3个协程，每个会 yield 3次
    int indices[3] = {0, 1, 2};
    for (int i = 0; i < 3; i++) {
        co_routine_t *co = co_spawn(sched, test_coroutine_multi_yield, &indices[i], 0);
        TEST_ASSERT_NOT_NULL(co);
    }
    
    // 运行调度器
    co_error_t err = co_scheduler_run(sched);
    TEST_ASSERT_EQUAL_INT(CO_OK, err);
    
    // 验证每个协程都执行了3次
    TEST_ASSERT_EQUAL_INT(3, g_test_values[0]);
    TEST_ASSERT_EQUAL_INT(3, g_test_values[1]);
    TEST_ASSERT_EQUAL_INT(3, g_test_values[2]);
    
    co_scheduler_destroy(sched);
}

// ============================================================================
// 嵌套协程测试
// ============================================================================

/**
 * @brief 在协程中创建协程
 */
static void test_coroutine_spawn_nested(void *arg) {
    int *counter = (int *)arg;
    (*counter)++;
    
    // 在协程中创建新协程
    co_scheduler_t *sched = co_current_scheduler();
    if (sched && *counter < 3) {
        co_spawn(sched, test_coroutine_spawn_nested, counter, 0);
    }
}

/**
 * @brief 测试嵌套协程创建
 */
void test_nested_spawn(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    TEST_ASSERT_NOT_NULL(sched);
    
    // 创建第一个协程，它会递归创建更多协程
    co_routine_t *co = co_spawn(sched, test_coroutine_spawn_nested, &g_test_counter, 0);
    TEST_ASSERT_NOT_NULL(co);
    
    // 运行调度器
    co_error_t err = co_scheduler_run(sched);
    TEST_ASSERT_EQUAL_INT(CO_OK, err);
    
    // 验证所有嵌套协程都执行了
    TEST_ASSERT_EQUAL_INT(3, g_test_counter);
    
    co_scheduler_destroy(sched);
}

// ============================================================================
// 主函数
// ============================================================================

int main(void) {
    UNITY_BEGIN();
    
    // 调度器基本测试
    RUN_TEST(test_scheduler_create_destroy);
    RUN_TEST(test_scheduler_run_empty);
    
    // 协程执行测试
    RUN_TEST(test_single_coroutine);
    RUN_TEST(test_multiple_coroutines);
    
    // Yield 测试
    RUN_TEST(test_coroutine_yield);
    RUN_TEST(test_coroutines_interleaved);
    
    // 嵌套测试
    RUN_TEST(test_nested_spawn);
    
    return UNITY_END();
}
