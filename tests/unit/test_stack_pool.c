/**
 * @file test_stack_pool.c
 * @brief 栈池单元测试
 * 
 * 测试栈池的分配、释放和缓存功能。
 */

#include "unity.h"
#include "co_stack_pool.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

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
// 栈池创建和销毁测试
// ============================================================================

/**
 * @brief 测试栈池创建和销毁
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
 * @brief 测试创建零容量栈池
 */
void test_stack_pool_zero_capacity(void) {
    co_stack_pool_t *pool = co_stack_pool_create(64 * 1024, 0);
    TEST_ASSERT_NOT_NULL(pool);
    
    TEST_ASSERT_EQUAL_UINT(0, co_stack_pool_capacity(pool));
    TEST_ASSERT_EQUAL_UINT(0, co_stack_pool_available(pool));
    
    co_stack_pool_destroy(pool);
}

/**
 * @brief 测试创建无效参数
 */
void test_stack_pool_invalid_params(void) {
    co_stack_pool_t *pool = co_stack_pool_create(0, 10);
    TEST_ASSERT_NULL(pool);
}

// ============================================================================
// 栈分配和释放测试
// ============================================================================

/**
 * @brief 测试单个栈分配和释放
 */
void test_stack_pool_alloc_free(void) {
    co_stack_pool_t *pool = co_stack_pool_create(128 * 1024, 10);
    TEST_ASSERT_NOT_NULL(pool);
    
    // 分配栈
    void *stack = co_stack_pool_alloc(pool);
    TEST_ASSERT_NOT_NULL(stack);
    
    // 验证可以写入该栈
    memset(stack, 0xAA, 1024);
    
    // 释放栈
    co_stack_pool_free(pool, stack);
    
    // 验证栈被缓存
    TEST_ASSERT_EQUAL_UINT(1, co_stack_pool_available(pool));
    
    co_stack_pool_destroy(pool);
}

/**
 * @brief 测试多个栈分配和释放
 */
void test_stack_pool_multiple_alloc_free(void) {
    const size_t count = 5;
    co_stack_pool_t *pool = co_stack_pool_create(64 * 1024, 10);
    TEST_ASSERT_NOT_NULL(pool);
    
    void **stacks = (void **)malloc(count * sizeof(void *));
    TEST_ASSERT_NOT_NULL(stacks);
    
    // 分配多个栈
    for (size_t i = 0; i < count; i++) {
        stacks[i] = co_stack_pool_alloc(pool);
        TEST_ASSERT_NOT_NULL(stacks[i]);
    }
    
    // 释放所有栈
    for (size_t i = 0; i < count; i++) {
        co_stack_pool_free(pool, stacks[i]);
    }
    
    // 验证所有栈被缓存
    TEST_ASSERT_EQUAL_UINT(count, co_stack_pool_available(pool));
    
    free(stacks);
    co_stack_pool_destroy(pool);
}

/**
 * @brief 测试栈重用
 */
void test_stack_pool_reuse(void) {
    co_stack_pool_t *pool = co_stack_pool_create(64 * 1024, 10);
    TEST_ASSERT_NOT_NULL(pool);
    
    // 第一次分配
    void *stack1 = co_stack_pool_alloc(pool);
    TEST_ASSERT_NOT_NULL(stack1);
    
    // 释放
    co_stack_pool_free(pool, stack1);
    TEST_ASSERT_EQUAL_UINT(1, co_stack_pool_available(pool));
    
    // 再次分配应该得到相同的栈
    void *stack2 = co_stack_pool_alloc(pool);
    TEST_ASSERT_NOT_NULL(stack2);
    TEST_ASSERT_EQUAL_PTR(stack1, stack2);
    
    // 验证池现在为空
    TEST_ASSERT_EQUAL_UINT(0, co_stack_pool_available(pool));
    
    co_stack_pool_free(pool, stack2);
    co_stack_pool_destroy(pool);
}

/**
 * @brief 测试超过容量后直接释放
 */
void test_stack_pool_overflow(void) {
    const size_t capacity = 3;
    co_stack_pool_t *pool = co_stack_pool_create(32 * 1024, capacity);
    TEST_ASSERT_NOT_NULL(pool);
    
    void **stacks = (void **)malloc((capacity + 2) * sizeof(void *));
    TEST_ASSERT_NOT_NULL(stacks);
    
    // 分配比容量更多的栈
    for (size_t i = 0; i < capacity + 2; i++) {
        stacks[i] = co_stack_pool_alloc(pool);
        TEST_ASSERT_NOT_NULL(stacks[i]);
    }
    
    // 释放所有栈
    for (size_t i = 0; i < capacity + 2; i++) {
        co_stack_pool_free(pool, stacks[i]);
    }
    
    // 验证只缓存了容量范围内的栈
    TEST_ASSERT_EQUAL_UINT(capacity, co_stack_pool_available(pool));
    
    free(stacks);
    co_stack_pool_destroy(pool);
}

// ============================================================================
// 统计信息测试
// ============================================================================

/**
 * @brief 测试统计信息
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
    
    // 分配3个栈
    void *s1 = co_stack_pool_alloc(pool);
    void *s2 = co_stack_pool_alloc(pool);
    void *s3 = co_stack_pool_alloc(pool);
    
    co_stack_pool_get_stats(pool, &stats);
    TEST_ASSERT_EQUAL_UINT(3, stats.total_allocs);
    TEST_ASSERT_EQUAL_UINT(3, stats.cache_misses);  // 池为空，全部未命中
    TEST_ASSERT_EQUAL_UINT(3, stats.current_used);
    
    // 释放2个
    co_stack_pool_free(pool, s1);
    co_stack_pool_free(pool, s2);
    
    co_stack_pool_get_stats(pool, &stats);
    TEST_ASSERT_EQUAL_UINT(2, stats.total_frees);
    TEST_ASSERT_EQUAL_UINT(1, stats.current_used);
    
    // 再分配1个（应该命中缓存）
    void *s4 = co_stack_pool_alloc(pool);
    
    co_stack_pool_get_stats(pool, &stats);
    TEST_ASSERT_EQUAL_UINT(4, stats.total_allocs);
    TEST_ASSERT_EQUAL_UINT(1, stats.cache_hits);    // 命中1次
    TEST_ASSERT_EQUAL_UINT(3, stats.cache_misses);
    TEST_ASSERT_EQUAL_UINT(2, stats.current_used);
    
    co_stack_pool_free(pool, s3);
    co_stack_pool_free(pool, s4);
    co_stack_pool_destroy(pool);
}

/**
 * @brief 测试重置统计信息
 */
void test_stack_pool_reset_stats(void) {
    co_stack_pool_t *pool = co_stack_pool_create(64 * 1024, 10);
    TEST_ASSERT_NOT_NULL(pool);
    
    // 进行一些操作
    void *s1 = co_stack_pool_alloc(pool);
    void *s2 = co_stack_pool_alloc(pool);
    co_stack_pool_free(pool, s1);
    
    // 重置统计
    co_stack_pool_reset_stats(pool);
    
    co_stack_pool_stats_t stats;
    co_stack_pool_get_stats(pool, &stats);
    
    // total_allocs等应该被清零，但 current_used 应该保留
    TEST_ASSERT_EQUAL_UINT(0, stats.total_allocs);
    TEST_ASSERT_EQUAL_UINT(0, stats.total_frees);
    TEST_ASSERT_EQUAL_UINT(0, stats.cache_hits);
    TEST_ASSERT_EQUAL_UINT(0, stats.cache_misses);
    TEST_ASSERT_EQUAL_UINT(1, stats.current_used);
    
    co_stack_pool_free(pool, s2);
    co_stack_pool_destroy(pool);
}

// ============================================================================
// 主函数
// ============================================================================

int main(void) {
    UNITY_BEGIN();
    
    // 创建和销毁
    RUN_TEST(test_stack_pool_create_destroy);
    RUN_TEST(test_stack_pool_zero_capacity);
    RUN_TEST(test_stack_pool_invalid_params);
    
    // 分配和释放
    RUN_TEST(test_stack_pool_alloc_free);
    RUN_TEST(test_stack_pool_multiple_alloc_free);
    RUN_TEST(test_stack_pool_reuse);
    RUN_TEST(test_stack_pool_overflow);
    
    // 统计信息
    RUN_TEST(test_stack_pool_stats);
    RUN_TEST(test_stack_pool_reset_stats);
    
    return UNITY_END();
}
