/**
 * @file test_allocator.c
 * @brief 自定义分配器单元测试
 * 
 * 测试自定义内存分配器功能。
 */

#include "unity.h"
#include "co_allocator.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// ============================================================================
// 测试辅助
// ============================================================================

// 自定义分配器的统计信息
static struct {
    size_t malloc_count;
    size_t realloc_count;
    size_t free_count;
    size_t bytes_allocated;
} g_alloc_stats;

/**
 * @brief 重置统计信息
 */
static void reset_stats(void) {
    memset(&g_alloc_stats, 0, sizeof(g_alloc_stats));
}

/**
 * @brief 自定义 malloc 函数
 */
static void *custom_malloc(size_t size, void *userdata) {
    (void)userdata;
    g_alloc_stats.malloc_count++;
    g_alloc_stats.bytes_allocated += size;
    return malloc(size);
}

/**
 * @brief 自定义 realloc 函数
 */
static void *custom_realloc(void *ptr, size_t size, void *userdata) {
    (void)userdata;
    g_alloc_stats.realloc_count++;
    return realloc(ptr, size);
}

/**
 * @brief 自定义 free 函数
 */
static void custom_free(void *ptr, void *userdata) {
    (void)userdata;
    g_alloc_stats.free_count++;
    free(ptr);
}

void setUp(void) {
    reset_stats();
    // 恢复默认分配器
    co_set_allocator(NULL);
}

void tearDown(void) {
    // 恢复默认分配器
    co_set_allocator(NULL);
}

// ============================================================================
// 分配器设置和获取测试
// ============================================================================

/**
 * @brief 测试默认分配器
 */
void test_default_allocator(void) {
    const co_allocator_t *allocator = co_get_allocator();
    TEST_ASSERT_NULL(allocator);  // 默认情况下应该返回 NULL
}

/**
 * @brief 测试设置自定义分配器
 */
void test_set_custom_allocator(void) {
    co_allocator_t allocator = {
        .malloc_fn = custom_malloc,
        .realloc_fn = custom_realloc,
        .free_fn = custom_free,
        .userdata = NULL
    };
    
    co_set_allocator(&allocator);
    
    const co_allocator_t *current = co_get_allocator();
    TEST_ASSERT_NOT_NULL(current);
    TEST_ASSERT_EQUAL_PTR(custom_malloc, current->malloc_fn);
    TEST_ASSERT_EQUAL_PTR(custom_realloc, current->realloc_fn);
    TEST_ASSERT_EQUAL_PTR(custom_free, current->free_fn);
}

/**
 * @brief 测试恢复默认分配器
 */
void test_restore_default_allocator(void) {
    co_allocator_t allocator = {
        .malloc_fn = custom_malloc,
        .realloc_fn = custom_realloc,
        .free_fn = custom_free,
        .userdata = NULL
    };
    
    // 设置自定义分配器
    co_set_allocator(&allocator);
    TEST_ASSERT_NOT_NULL(co_get_allocator());
    
    // 恢复默认分配器
    co_set_allocator(NULL);
    TEST_ASSERT_NULL(co_get_allocator());
}

// ============================================================================
// 分配器功能测试
// ============================================================================

/**
 * @brief 测试使用默认分配器
 */
void test_use_default_allocator(void) {
    // 使用默认分配器分配内存
    void *ptr = co_malloc(1024);
    TEST_ASSERT_NOT_NULL(ptr);
    
    // 验证统计（默认分配器不计数）
    TEST_ASSERT_EQUAL_UINT(0, g_alloc_stats.malloc_count);
    
    co_free(ptr);
}

/**
 * @brief 测试使用自定义分配器
 */
void test_use_custom_allocator(void) {
    co_allocator_t allocator = {
        .malloc_fn = custom_malloc,
        .realloc_fn = custom_realloc,
        .free_fn = custom_free,
        .userdata = NULL
    };
    co_set_allocator(&allocator);
    
    // 分配内存
    void *ptr = co_malloc(1024);
    TEST_ASSERT_NOT_NULL(ptr);
    
    // 验证统计
    TEST_ASSERT_EQUAL_UINT(1, g_alloc_stats.malloc_count);
    TEST_ASSERT_EQUAL_UINT(1024, g_alloc_stats.bytes_allocated);
    
    // 释放内存
    co_free(ptr);
    TEST_ASSERT_EQUAL_UINT(1, g_alloc_stats.free_count);
}

/**
 * @brief 测试 co_calloc
 */
void test_co_calloc(void) {
    co_allocator_t allocator = {
        .malloc_fn = custom_malloc,
        .realloc_fn = custom_realloc,
        .free_fn = custom_free,
        .userdata = NULL
    };
    co_set_allocator(&allocator);
    
    // 分配并清零
    size_t count = 10;
    size_t size = sizeof(int);
    int *arr = (int *)co_calloc(count, size);
    TEST_ASSERT_NOT_NULL(arr);
    
    // 验证已清零
    for (size_t i = 0; i < count; i++) {
        TEST_ASSERT_EQUAL_INT(0, arr[i]);
    }
    
    // 验证统计
    TEST_ASSERT_EQUAL_UINT(1, g_alloc_stats.malloc_count);
    TEST_ASSERT_EQUAL_UINT(count * size, g_alloc_stats.bytes_allocated);
    
    co_free(arr);
}

/**
 * @brief 测试 co_realloc
 */
void test_co_realloc(void) {
    co_allocator_t allocator = {
        .malloc_fn = custom_malloc,
        .realloc_fn = custom_realloc,
        .free_fn = custom_free,
        .userdata = NULL
    };
    co_set_allocator(&allocator);
    
    // 初始分配
    void *ptr = co_malloc(512);
    TEST_ASSERT_NOT_NULL(ptr);
    TEST_ASSERT_EQUAL_UINT(1, g_alloc_stats.malloc_count);
    
    // 重新分配
    void *new_ptr = co_realloc(ptr, 1024);
    TEST_ASSERT_NOT_NULL(new_ptr);
    TEST_ASSERT_EQUAL_UINT(1, g_alloc_stats.realloc_count);
    
    co_free(new_ptr);
}

/**
 * @brief 测试释放 NULL 指针
 */
void test_free_null(void) {
    co_allocator_t allocator = {
        .malloc_fn = custom_malloc,
        .realloc_fn = custom_realloc,
        .free_fn = custom_free,
        .userdata = NULL
    };
    co_set_allocator(&allocator);
    
    // 释放 NULL 应该安全
    co_free(NULL);
    
    // 不应该调用自定义的 free 函数
    TEST_ASSERT_EQUAL_UINT(0, g_alloc_stats.free_count);
}

// ============================================================================
// 用户数据测试
// ============================================================================

/**
 * @brief 测试分配器用户数据
 */
void test_allocator_userdata(void) {
    int userdata_value = 12345;
    
    co_allocator_t allocator = {
        .malloc_fn = custom_malloc,
        .realloc_fn = custom_realloc,
        .free_fn = custom_free,
        .userdata = &userdata_value
    };
    co_set_allocator(&allocator);
    
    const co_allocator_t *current = co_get_allocator();
    TEST_ASSERT_NOT_NULL(current);
    TEST_ASSERT_EQUAL_PTR(&userdata_value, current->userdata);
}

// ============================================================================
// 多次分配测试
// ============================================================================

/**
 * @brief 测试多次分配和释放
 */
void test_multiple_alloc_free(void) {
    co_allocator_t allocator = {
        .malloc_fn = custom_malloc,
        .realloc_fn = custom_realloc,
        .free_fn = custom_free,
        .userdata = NULL
    };
    co_set_allocator(&allocator);
    
    const int count = 10;
    void **ptrs = (void **)malloc(count * sizeof(void *));
    TEST_ASSERT_NOT_NULL(ptrs);
    
    // 多次分配
    for (int i = 0; i < count; i++) {
        ptrs[i] = co_malloc(1024 * (i + 1));
        TEST_ASSERT_NOT_NULL(ptrs[i]);
    }
    
    TEST_ASSERT_EQUAL_UINT(count, g_alloc_stats.malloc_count);
    
    // 多次释放
    for (int i = 0; i < count; i++) {
        co_free(ptrs[i]);
    }
    
    TEST_ASSERT_EQUAL_UINT(count, g_alloc_stats.free_count);
    
    free(ptrs);
}

// ============================================================================
// 主函数
// ============================================================================

int main(void) {
    UNITY_BEGIN();
    
    // 设置和获取
    RUN_TEST(test_default_allocator);
    RUN_TEST(test_set_custom_allocator);
    RUN_TEST(test_restore_default_allocator);
    
    // 功能测试
    RUN_TEST(test_use_default_allocator);
    RUN_TEST(test_use_custom_allocator);
    RUN_TEST(test_co_calloc);
    RUN_TEST(test_co_realloc);
    RUN_TEST(test_free_null);
    
    // 用户数据
    RUN_TEST(test_allocator_userdata);
    
    // 多次分配
    RUN_TEST(test_multiple_alloc_free);
    
    return UNITY_END();
}
