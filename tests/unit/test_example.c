/**
 * @file test_example.c
 * @brief Unity测试框架示例 - C语言单元测试
 * 
 * 这是一个示例测试文件，用于验证Unity测试框架集成是否成功。
 * Phase 2 将添加实际的协程库测试。
 */

#include "unity.h"
#include <libco/config.h>
#include <stdio.h>
#include <string.h>

/**
 * @brief 每个测试用例执行前调用
 */
void setUp(void) {
    // 测试前的初始化工作
}

/**
 * @brief 每个测试用例执行后调用
 */
void tearDown(void) {
    // 测试后的清理工作
}

/**
 * @brief 测试基本断言
 */
void test_basic_assertions(void) {
    TEST_ASSERT_TRUE(1);
    TEST_ASSERT_FALSE(0);
    TEST_ASSERT_EQUAL_INT(42, 42);
    TEST_ASSERT_NOT_EQUAL_INT(1, 2);
}

/**
 * @brief 测试字符串断言
 */
void test_string_assertions(void) {
    TEST_ASSERT_EQUAL_STRING("hello", "hello");
    // Unity 没有 TEST_ASSERT_NOT_EQUAL_STRING，使用strcmp
    TEST_ASSERT_TRUE(strcmp("hello", "world") != 0);
}

/**
 * @brief 测试浮点数断言
 */
void test_float_assertions(void) {
    TEST_ASSERT_EQUAL_FLOAT(3.14f, 3.14f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.14f, 3.145f);
}

/**
 * @brief 测试指针断言
 */
void test_pointer_assertions(void) {
    int value = 42;
    int* ptr = &value;
    
    TEST_ASSERT_NOT_NULL(ptr);
    TEST_ASSERT_EQUAL_PTR(&value, ptr);
}

/**
 * @brief 测试配置文件
 * 
 * 验证 config.h 是否正确生成
 */
void test_config_values(void) {
    // 检查版本号
    TEST_ASSERT_EQUAL_INT(2, LIBCO_VERSION_MAJOR);
    TEST_ASSERT_EQUAL_INT(0, LIBCO_VERSION_MINOR);
    TEST_ASSERT_EQUAL_INT(0, LIBCO_VERSION_PATCH);
    
    // 检查版本字符串
    TEST_ASSERT_EQUAL_STRING("2.0.0", LIBCO_VERSION_STRING);
    
    // 检查平台宏（至少有一个应该被定义）
    #if defined(LIBCO_PLATFORM_LINUX)
    printf("Platform: Linux\n");
    TEST_ASSERT_TRUE(1);
    #elif defined(LIBCO_PLATFORM_MACOS)
    printf("Platform: macOS\n");
    TEST_ASSERT_TRUE(1);
    #elif defined(LIBCO_PLATFORM_WINDOWS)
    printf("Platform: Windows\n");
    TEST_ASSERT_TRUE(1);
    #else
    TEST_FAIL_MESSAGE("No platform macro defined!");
    #endif
}

/**
 * @brief 主函数 - 运行所有测试
 */
int main(void) {
    UNITY_BEGIN();
    
    // 基础断言测试
    RUN_TEST(test_basic_assertions);
    RUN_TEST(test_string_assertions);
    RUN_TEST(test_float_assertions);
    RUN_TEST(test_pointer_assertions);
    
    // 配置文件测试
    RUN_TEST(test_config_values);
    
    return UNITY_END();
}
