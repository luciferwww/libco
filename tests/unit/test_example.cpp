/**
 * @file test_example.cpp
 * @brief Google Test framework example - C++ unit tests
 * 
 * This example test file verifies that the Google Test integration is working
 * correctly. Real C++ wrapper tests will be added in Phase 2.
 */

#include <gtest/gtest.h>
#include <libco/config.h>
#include <string>
#include <vector>
#include <memory>

/**
 * @brief Basic test case
 */
TEST(BasicTest, Assertions) {
    EXPECT_TRUE(true);
    EXPECT_FALSE(false);
    EXPECT_EQ(42, 42);
    EXPECT_NE(1, 2);
    EXPECT_LT(1, 2);
    EXPECT_LE(1, 1);
    EXPECT_GT(2, 1);
    EXPECT_GE(2, 2);
}

/**
 * @brief String tests
 */
TEST(BasicTest, Strings) {
    std::string hello = "hello";
    EXPECT_EQ("hello", hello);
    EXPECT_STREQ("hello", hello.c_str());
    EXPECT_STRNE("world", hello.c_str());
}

/**
 * @brief Floating-point tests
 */
TEST(BasicTest, FloatingPoint) {
    EXPECT_FLOAT_EQ(3.14f, 3.14f);
    EXPECT_DOUBLE_EQ(3.14159, 3.14159);
    EXPECT_NEAR(3.14, 3.145, 0.01);
}

/**
 * @brief Exception tests
 */
TEST(BasicTest, Exceptions) {
    EXPECT_THROW({
        throw std::runtime_error("error");
    }, std::runtime_error);
    
    EXPECT_NO_THROW({
        int x = 42;
        (void)x;
    });
}

/**
 * @brief Configuration file tests
 */
TEST(ConfigTest, VersionCheck) {
    EXPECT_EQ(2, LIBCO_VERSION_MAJOR);
    EXPECT_EQ(0, LIBCO_VERSION_MINOR);
    EXPECT_EQ(0, LIBCO_VERSION_PATCH);
    EXPECT_STREQ("2.0.0", LIBCO_VERSION_STRING);
}

/**
 * @brief Platform detection tests
 */
TEST(ConfigTest, PlatformCheck) {
    #if defined(LIBCO_PLATFORM_LINUX)
    std::cout << "Platform: Linux" << std::endl;
    SUCCEED();
    #elif defined(LIBCO_PLATFORM_MACOS)
    std::cout << "Platform: macOS" << std::endl;
    SUCCEED();
    #elif defined(LIBCO_PLATFORM_WINDOWS)
    std::cout << "Platform: Windows" << std::endl;
    SUCCEED();
    #else
    FAIL() << "No platform macro defined!";
    #endif
}

/**
 * @brief Fixture example
 * 
 * A fixture provides SetUp and TearDown hooks for per-test preparation and cleanup.
 */
class VectorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Run before each test
        vec.push_back(1);
        vec.push_back(2);
        vec.push_back(3);
    }

    void TearDown() override {
        // Run after each test
        vec.clear();
    }

    std::vector<int> vec;
};

TEST_F(VectorTest, Size) {
    EXPECT_EQ(3, vec.size());
}

TEST_F(VectorTest, Elements) {
    EXPECT_EQ(1, vec[0]);
    EXPECT_EQ(2, vec[1]);
    EXPECT_EQ(3, vec[2]);
}

TEST_F(VectorTest, PushBack) {
    vec.push_back(4);
    EXPECT_EQ(4, vec.size());
    EXPECT_EQ(4, vec[3]);
}

/**
 * @brief Parameterized test example
 */
class SquareTest : public ::testing::TestWithParam<int> {
};

TEST_P(SquareTest, Compute) {
    int n = GetParam();
    EXPECT_GE(n * n, 0);
}

INSTANTIATE_TEST_SUITE_P(
    Numbers,
    SquareTest,
    ::testing::Values(0, 1, 2, 3, 5, 10)
);

/**
 * @brief Smart pointer tests for C++ features
 */
TEST(CppFeaturesTest, UniquePtr) {
    auto ptr = std::make_unique<int>(42);
    EXPECT_NE(nullptr, ptr);
    EXPECT_EQ(42, *ptr);
    
    // unique_ptr releases memory automatically
}

TEST(CppFeaturesTest, SharedPtr) {
    auto ptr1 = std::make_shared<int>(42);
    auto ptr2 = ptr1;
    
    EXPECT_EQ(2, ptr1.use_count());
    EXPECT_EQ(42, *ptr1);
    EXPECT_EQ(42, *ptr2);
}

/**
 * @brief Lambda tests for C++11 features
 */
TEST(CppFeaturesTest, Lambda) {
    auto add = [](int a, int b) { return a + b; };
    EXPECT_EQ(5, add(2, 3));
    
    int x = 10;
    auto capture = [&x]() { return x * 2; };
    EXPECT_EQ(20, capture());
}

// main() is provided by gtest_main and does not need to be written manually
