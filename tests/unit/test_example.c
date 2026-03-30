/**
 * @file test_example.c
 * @brief Unity test framework example - C unit tests
 * 
 * This example test file verifies that the Unity test framework integration is
 * working correctly. Real coroutine library tests will be added in Phase 2.
 */

#include "unity.h"
#include <libco/config.h>
#include <stdio.h>
#include <string.h>

/**
 * @brief Called before each test case
 */
void setUp(void) {
    // Per-test initialization work
}

/**
 * @brief Called after each test case
 */
void tearDown(void) {
    // Per-test cleanup work
}

/**
 * @brief Test basic assertions
 */
void test_basic_assertions(void) {
    TEST_ASSERT_TRUE(1);
    TEST_ASSERT_FALSE(0);
    TEST_ASSERT_EQUAL_INT(42, 42);
    TEST_ASSERT_NOT_EQUAL_INT(1, 2);
}

/**
 * @brief Test string assertions
 */
void test_string_assertions(void) {
    TEST_ASSERT_EQUAL_STRING("hello", "hello");
    // Unity has no TEST_ASSERT_NOT_EQUAL_STRING, so use strcmp instead
    TEST_ASSERT_TRUE(strcmp("hello", "world") != 0);
}

/**
 * @brief Test floating-point assertions
 */
void test_float_assertions(void) {
    TEST_ASSERT_EQUAL_FLOAT(3.14f, 3.14f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.14f, 3.145f);
}

/**
 * @brief Test pointer assertions
 */
void test_pointer_assertions(void) {
    int value = 42;
    int* ptr = &value;
    
    TEST_ASSERT_NOT_NULL(ptr);
    TEST_ASSERT_EQUAL_PTR(&value, ptr);
}

/**
 * @brief Test the generated configuration file
 * 
 * Verify that config.h was generated correctly.
 */
void test_config_values(void) {
    // Check the version numbers
    TEST_ASSERT_EQUAL_INT(2, LIBCO_VERSION_MAJOR);
    TEST_ASSERT_EQUAL_INT(0, LIBCO_VERSION_MINOR);
    TEST_ASSERT_EQUAL_INT(0, LIBCO_VERSION_PATCH);
    
    // Check the version string
    TEST_ASSERT_EQUAL_STRING("2.0.0", LIBCO_VERSION_STRING);
    
    // Check platform macros; at least one should be defined
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
 * @brief Main function that runs all tests
 */
int main(void) {
    UNITY_BEGIN();
    
    // Basic assertion tests
    RUN_TEST(test_basic_assertions);
    RUN_TEST(test_string_assertions);
    RUN_TEST(test_float_assertions);
    RUN_TEST(test_pointer_assertions);
    
    // Configuration tests
    RUN_TEST(test_config_values);
    
    return UNITY_END();
}
