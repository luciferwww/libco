/**
 * @file test_timer.c
 * @brief 定时器堆单元测试
 * 
 * 测试定时器堆的基本功能。
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
// 测试辅助
// ============================================================================

void setUp(void) {
    // 每个测试前的初始化
}

void tearDown(void) {
    // 每个测试后的清理
}

// ============================================================================
// 定时器堆基本测试
// ============================================================================

/**
 * @brief 测试定时器堆创建和销毁
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
 * @brief 测试定时器堆插入和弹出
 */
void test_timer_heap_push_pop(void) {
    co_timer_heap_t heap;
    co_timer_heap_init(&heap, 4);
    
    // 创建几个模拟协程（只需要 wakeup_time 字段）
    co_routine_t r1 = { .wakeup_time = 100 };
    co_routine_t r2 = { .wakeup_time = 50 };
    co_routine_t r3 = { .wakeup_time = 150 };
    
    // 插入
    TEST_ASSERT_TRUE(co_timer_heap_push(&heap, &r1));
    TEST_ASSERT_TRUE(co_timer_heap_push(&heap, &r2));
    TEST_ASSERT_TRUE(co_timer_heap_push(&heap, &r3));
    
    TEST_ASSERT_EQUAL_UINT(3, co_timer_heap_size(&heap));
    TEST_ASSERT_FALSE(co_timer_heap_empty(&heap));
    
    // 弹出（应该按时间顺序）
    co_routine_t *popped = co_timer_heap_pop(&heap);
    TEST_ASSERT_NOT_NULL(popped);
    TEST_ASSERT_EQUAL_UINT64(50, popped->wakeup_time);
    
    popped = co_timer_heap_pop(&heap);
    TEST_ASSERT_NOT_NULL(popped);
    TEST_ASSERT_EQUAL_UINT64(100, popped->wakeup_time);
    
    popped = co_timer_heap_pop(&heap);
    TEST_ASSERT_NOT_NULL(popped);
    TEST_ASSERT_EQUAL_UINT64(150, popped->wakeup_time);
    
    TEST_ASSERT_TRUE(co_timer_heap_empty(&heap));
    
    co_timer_heap_destroy(&heap);
}

/**
 * @brief 测试 peek（查看而不弹出）
 */
void test_timer_heap_peek(void) {
    co_timer_heap_t heap;
    co_timer_heap_init(&heap, 4);
    
    co_routine_t r1 = { .wakeup_time = 100 };
    co_routine_t r2 = { .wakeup_time = 50 };
    
    co_timer_heap_push(&heap, &r1);
    co_timer_heap_push(&heap, &r2);
    
    // peek 不改变堆
    co_routine_t *peeked = co_timer_heap_peek(&heap);
    TEST_ASSERT_NOT_NULL(peeked);
    TEST_ASSERT_EQUAL_UINT64(50, peeked->wakeup_time);
    TEST_ASSERT_EQUAL_UINT(2, co_timer_heap_size(&heap));
    
    // 再次 peek 得到同样的元素
    peeked = co_timer_heap_peek(&heap);
    TEST_ASSERT_EQUAL_UINT64(50, peeked->wakeup_time);
    
    co_timer_heap_destroy(&heap);
}

/**
 * @brief 测试堆的自动扩容
 */
void test_timer_heap_resize(void) {
    co_timer_heap_t heap;
    co_timer_heap_init(&heap, 2);  // 初始容量很小
    
    co_routine_t routines[10];
    for (int i = 0; i < 10; i++) {
        routines[i].wakeup_time = i * 10;
        TEST_ASSERT_TRUE(co_timer_heap_push(&heap, &routines[i]));
    }
    
    TEST_ASSERT_EQUAL_UINT(10, co_timer_heap_size(&heap));
    
    // 验证顺序（应该从小到大弹出）
    for (int i = 0; i < 10; i++) {
        co_routine_t *popped = co_timer_heap_pop(&heap);
        TEST_ASSERT_NOT_NULL(popped);
        TEST_ASSERT_EQUAL_UINT64(i * 10, popped->wakeup_time);
    }
    
    co_timer_heap_destroy(&heap);
}

/**
 * @brief 测试乱序插入
 */
void test_timer_heap_random_order(void) {
    co_timer_heap_t heap;
    co_timer_heap_init(&heap, 8);
    
    // 乱序插入
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
    
    // 应该按递增顺序弹出
    TEST_ASSERT_EQUAL_UINT64(100, co_timer_heap_pop(&heap)->wakeup_time);
    TEST_ASSERT_EQUAL_UINT64(200, co_timer_heap_pop(&heap)->wakeup_time);
    TEST_ASSERT_EQUAL_UINT64(300, co_timer_heap_pop(&heap)->wakeup_time);
    TEST_ASSERT_EQUAL_UINT64(400, co_timer_heap_pop(&heap)->wakeup_time);
    TEST_ASSERT_EQUAL_UINT64(500, co_timer_heap_pop(&heap)->wakeup_time);
    
    co_timer_heap_destroy(&heap);
}

// ============================================================================
// 时间函数测试
// ============================================================================

/**
 * @brief 测试单调时间函数
 */
void test_get_monotonic_time(void) {
    uint64_t t1 = co_get_monotonic_time_ms();
    
    // 短暂延迟
    #ifdef _WIN32
    Sleep(10);  // 10ms
    #else
    struct timespec ts = {0, 10000000};  // 10ms
    nanosleep(&ts, NULL);
    #endif
    
    uint64_t t2 = co_get_monotonic_time_ms();
    
    // 时间应该递增
    TEST_ASSERT_GREATER_OR_EQUAL(t1, t2);
    
    // 至少过了 8ms（考虑系统调度误差）
    TEST_ASSERT_GREATER_OR_EQUAL(8, t2 - t1);
}

/**
 * @brief 测试时间精度
 */
void test_time_precision(void) {
    uint64_t t1 = co_get_monotonic_time_ms();
    uint64_t t2 = co_get_monotonic_time_ms();
    
    // 两次调用时间差应该很小（< 5ms）
    TEST_ASSERT_LESS_THAN(5, t2 - t1);
}

// ============================================================================
// 主函数
// ============================================================================

int main(void) {
    UNITY_BEGIN();
    
    // 基本测试
    RUN_TEST(test_timer_heap_create_destroy);
    RUN_TEST(test_timer_heap_push_pop);
    RUN_TEST(test_timer_heap_peek);
    RUN_TEST(test_timer_heap_resize);
    RUN_TEST(test_timer_heap_random_order);
    
    // 时间函数测试
    RUN_TEST(test_get_monotonic_time);
    RUN_TEST(test_time_precision);
    
    return UNITY_END();
}
