/**
 * @file test_routine.c
 * @brief 协程单元测试
 * 
 * 测试协程的基本功能和生命周期。
 */

#include "unity.h"
#include <libco/co.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// ============================================================================
// 测试辅助
// ============================================================================

// 全局变量
static int g_test_counter = 0;
static void *g_test_arg = NULL;
static co_routine_t *g_test_routine = NULL;

void setUp(void) {
    g_test_counter = 0;
    g_test_arg = NULL;
    g_test_routine = NULL;
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
    g_test_arg = arg;
    g_test_counter++;
}

/**
 * @brief 验证 co_current() 的协程
 */
static void test_coroutine_check_current(void *arg) {
    (void)arg;  // 未使用参数
    co_routine_t *current = co_current();
    TEST_ASSERT_NOT_NULL(current);
    TEST_ASSERT_EQUAL_PTR(g_test_routine, current);
    g_test_counter++;
}

/**
 * @brief 验证 co_current_scheduler() 的协程
 */
static void test_coroutine_check_scheduler(void *arg) {
    co_scheduler_t *sched = (co_scheduler_t *)arg;
    co_scheduler_t *current_sched = co_current_scheduler();
    TEST_ASSERT_NOT_NULL(current_sched);
    TEST_ASSERT_EQUAL_PTR(sched, current_sched);
    g_test_counter++;
}

/**
 * @brief 测试协程参数传递
 */
static void test_coroutine_with_arg(void *arg) {
    int *value = (int *)arg;
    TEST_ASSERT_NOT_NULL(value);
    (*value) *= 2;
}

// ============================================================================
// 协程创建测试
// ============================================================================

/**
 * @brief 测试协程创建
 */
void test_routine_spawn(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    TEST_ASSERT_NOT_NULL(sched);
    
    co_routine_t *co = co_spawn(sched, test_coroutine_simple, NULL, 0);
    TEST_ASSERT_NOT_NULL(co);
    
    co_scheduler_destroy(sched);
}

/**
 * @brief 测试协程参数传递
 */
void test_routine_argument(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    TEST_ASSERT_NOT_NULL(sched);
    
    void *test_arg = (void *)0x12345678;
    co_routine_t *co = co_spawn(sched, test_coroutine_simple, test_arg, 0);
    TEST_ASSERT_NOT_NULL(co);
    
    co_error_t err = co_scheduler_run(sched);
    TEST_ASSERT_EQUAL_INT(CO_OK, err);
    
    // 验证参数正确传递
    TEST_ASSERT_EQUAL_PTR(test_arg, g_test_arg);
    
    co_scheduler_destroy(sched);
}

/**
 * @brief 测试自定义栈大小
 */
void test_routine_custom_stack(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    TEST_ASSERT_NOT_NULL(sched);
    
    // 创建较小栈的协程（32KB）
    co_routine_t *co = co_spawn(sched, test_coroutine_simple, NULL, 32 * 1024);
    TEST_ASSERT_NOT_NULL(co);
    
    co_error_t err = co_scheduler_run(sched);
    TEST_ASSERT_EQUAL_INT(CO_OK, err);
    
    co_scheduler_destroy(sched);
}

// ============================================================================
// co_current() 测试
// ============================================================================

/**
 * @brief 测试 co_current() 在主线程中
 */
void test_current_in_main(void) {
    co_routine_t *current = co_current();
    TEST_ASSERT_NULL(current);  // 不在协程中应该返回 NULL
}

/**
 * @brief 测试 co_current() 在协程中
 */
void test_current_in_coroutine(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    TEST_ASSERT_NOT_NULL(sched);
    
    g_test_routine = co_spawn(sched, test_coroutine_check_current, NULL, 0);
    TEST_ASSERT_NOT_NULL(g_test_routine);
    
    co_error_t err = co_scheduler_run(sched);
    TEST_ASSERT_EQUAL_INT(CO_OK, err);
    
    // 验证协程已执行
    TEST_ASSERT_EQUAL_INT(1, g_test_counter);
    
    co_scheduler_destroy(sched);
}

// ============================================================================
// co_current_scheduler() 测试
// ============================================================================

/**
 * @brief 测试 co_current_scheduler() 在主线程中
 */
void test_current_scheduler_in_main(void) {
    co_scheduler_t *sched = co_current_scheduler();
    TEST_ASSERT_NULL(sched);  // 不在调度器中应该返回 NULL
}

/**
 * @brief 测试 co_current_scheduler() 在协程中
 */
void test_current_scheduler_in_coroutine(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    TEST_ASSERT_NOT_NULL(sched);
    
    co_routine_t *co = co_spawn(sched, test_coroutine_check_scheduler, sched, 0);
    TEST_ASSERT_NOT_NULL(co);
    
    co_error_t err = co_scheduler_run(sched);
    TEST_ASSERT_EQUAL_INT(CO_OK, err);
    
    // 验证协程已执行
    TEST_ASSERT_EQUAL_INT(1, g_test_counter);
    
    co_scheduler_destroy(sched);
}

// ============================================================================
// 协程生命周期测试
// ============================================================================

/**
 * @brief 测试协程执行完成
 */
void test_routine_completion(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    TEST_ASSERT_NOT_NULL(sched);
    
    co_routine_t *co = co_spawn(sched, test_coroutine_simple, NULL, 0);
    TEST_ASSERT_NOT_NULL(co);
    
    TEST_ASSERT_EQUAL_INT(0, g_test_counter);
    
    co_error_t err = co_scheduler_run(sched);
    TEST_ASSERT_EQUAL_INT(CO_OK, err);
    
    // 验证协程执行了
    TEST_ASSERT_EQUAL_INT(1, g_test_counter);
    
    co_scheduler_destroy(sched);
}

/**
 * @brief 测试协程修改参数
 */
void test_routine_modify_argument(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    TEST_ASSERT_NOT_NULL(sched);
    
    int value = 10;
    co_routine_t *co = co_spawn(sched, test_coroutine_with_arg, &value, 0);
    TEST_ASSERT_NOT_NULL(co);
    
    co_error_t err = co_scheduler_run(sched);
    TEST_ASSERT_EQUAL_INT(CO_OK, err);
    
    // 验证参数被修改
    TEST_ASSERT_EQUAL_INT(20, value);
    
    co_scheduler_destroy(sched);
}

// ============================================================================
// 主函数
// ============================================================================

int main(void) {
    UNITY_BEGIN();
    
    // 协程创建测试
    RUN_TEST(test_routine_spawn);
    RUN_TEST(test_routine_argument);
    RUN_TEST(test_routine_custom_stack);
    
    // co_current() 测试
    RUN_TEST(test_current_in_main);
    RUN_TEST(test_current_in_coroutine);
    
    // co_current_scheduler() 测试
    RUN_TEST(test_current_scheduler_in_main);
    RUN_TEST(test_current_scheduler_in_coroutine);
    
    // 生命周期测试
    RUN_TEST(test_routine_completion);
    RUN_TEST(test_routine_modify_argument);
    
    return UNITY_END();
}
