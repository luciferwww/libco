# Unit Tests

# ============================================================================
# C Unit Tests (Unity)
# ============================================================================

# 示例测试 - C
add_executable(test_example_c
    test_example.c
)

target_link_libraries(test_example_c
    PRIVATE
        unity
        co
)

target_include_directories(test_example_c
    PRIVATE
        ${CMAKE_SOURCE_DIR}/libco/include
        ${CMAKE_BINARY_DIR}/include
)

add_test(NAME test_example_c COMMAND test_example_c)

# 上下文切换测试 - C
add_executable(test_context
    test_context.c
)

target_link_libraries(test_context
    PRIVATE
        unity
        co_static
)

target_include_directories(test_context
    PRIVATE
        ${CMAKE_SOURCE_DIR}/libco/include
        ${CMAKE_SOURCE_DIR}/libco/src
        ${CMAKE_BINARY_DIR}/include
)

add_test(NAME test_context COMMAND test_context)

# 调度器测试 - C (Week 4)
add_executable(test_scheduler
    test_scheduler.c
)

target_link_libraries(test_scheduler
    PRIVATE
        unity
        co_static
)

target_include_directories(test_scheduler
    PRIVATE
        ${CMAKE_SOURCE_DIR}/libco/include
        ${CMAKE_SOURCE_DIR}/libco/src
        ${CMAKE_BINARY_DIR}/include
)

add_test(NAME test_scheduler COMMAND test_scheduler)

# 协程测试 - C (Week 4)
add_executable(test_routine
    test_routine.c
)

target_link_libraries(test_routine
    PRIVATE
        unity
        co_static
)

target_include_directories(test_routine
    PRIVATE
        ${CMAKE_SOURCE_DIR}/libco/include
        ${CMAKE_SOURCE_DIR}/libco/src
        ${CMAKE_BINARY_DIR}/include
)

add_test(NAME test_routine COMMAND test_routine)

# 栈池测试 - C (Week 5)
add_executable(test_stack_pool
    test_stack_pool.c
)

target_link_libraries(test_stack_pool
    PRIVATE
        unity
        co_static
)

target_include_directories(test_stack_pool
    PRIVATE
        ${CMAKE_SOURCE_DIR}/libco/include
        ${CMAKE_SOURCE_DIR}/libco/src
        ${CMAKE_BINARY_DIR}/include
)

add_test(NAME test_stack_pool COMMAND test_stack_pool)

# 分配器测试 - C (Week 5)
add_executable(test_allocator
    test_allocator.c
)

target_link_libraries(test_allocator
    PRIVATE
        unity
        co_static
)

target_include_directories(test_allocator
    PRIVATE
        ${CMAKE_SOURCE_DIR}/libco/include
        ${CMAKE_SOURCE_DIR}/libco/src
        ${CMAKE_BINARY_DIR}/include
)

add_test(NAME test_allocator COMMAND test_allocator)

# 定时器堆测试 - C (Week 6)
add_executable(test_timer
    test_timer.c
)

target_link_libraries(test_timer
    PRIVATE
        unity
        co_static
)

target_include_directories(test_timer
    PRIVATE
        ${CMAKE_SOURCE_DIR}/libco/include
        ${CMAKE_SOURCE_DIR}/libco/src
        ${CMAKE_BINARY_DIR}/include
)

add_test(NAME test_timer COMMAND test_timer)

# co_sleep 测试 - C (Week 6)
add_executable(test_sleep
    test_sleep.c
)

target_link_libraries(test_sleep
    PRIVATE
        unity
        co_static
)

target_include_directories(test_sleep
    PRIVATE
        ${CMAKE_SOURCE_DIR}/libco/include
        ${CMAKE_SOURCE_DIR}/libco/src
        ${CMAKE_BINARY_DIR}/include
)

# Windows 需要链接额外的库
if(WIN32)
    target_link_libraries(test_sleep PRIVATE winmm)
endif()

add_test(NAME test_sleep COMMAND test_sleep)

# ============================================================================
# C++ Unit Tests (Google Test)
# ============================================================================

# 示例测试 - C++
add_executable(test_example_cpp
    test_example.cpp
)

target_link_libraries(test_example_cpp
    PRIVATE
        GTest::gtest
        GTest::gtest_main
        co
)

target_include_directories(test_example_cpp
    PRIVATE
        ${CMAKE_SOURCE_DIR}/libco/include
        ${CMAKE_SOURCE_DIR}/libcoxx/include
        ${CMAKE_BINARY_DIR}/include
)

add_test(NAME test_example_cpp COMMAND test_example_cpp)

# 互斥锁测试 - C (Week 9)
add_executable(test_mutex
    test_mutex.c
)

target_link_libraries(test_mutex
    PRIVATE
        unity
        co_static
)

target_include_directories(test_mutex
    PRIVATE
        ${CMAKE_SOURCE_DIR}/libco/include
        ${CMAKE_SOURCE_DIR}/libco/src
        ${CMAKE_BINARY_DIR}/include
)

add_test(NAME test_mutex COMMAND test_mutex)

# 条件变量测试 - C (Week 9)
add_executable(test_cond
    test_cond.c
)

target_link_libraries(test_cond
    PRIVATE
        unity
        co_static
)

target_include_directories(test_cond
    PRIVATE
        ${CMAKE_SOURCE_DIR}/libco/include
        ${CMAKE_SOURCE_DIR}/libco/src
        ${CMAKE_BINARY_DIR}/include
)

add_test(NAME test_cond COMMAND test_cond)

# Channel 测试 - C (Week 10)
add_executable(test_channel
    test_channel.c
)

target_link_libraries(test_channel
    PRIVATE
        unity
        co_static
)

target_include_directories(test_channel
    PRIVATE
        ${CMAKE_SOURCE_DIR}/libco/include
        ${CMAKE_SOURCE_DIR}/libco/src
        ${CMAKE_BINARY_DIR}/include
)

add_test(NAME test_channel COMMAND test_channel)

# IOCP 超时测试 - C (Windows)
# 在非 Windows 平台，测试入口的 main() 会打印"跳过"后直接退出（返回 0），
# 因此可跨平台注册而不会在 Linux/macOS 上失败。
add_executable(test_iocp_timeout
    test_iocp_timeout.c
)

target_link_libraries(test_iocp_timeout
    PRIVATE
        unity
        co_static
)

target_include_directories(test_iocp_timeout
    PRIVATE
        ${CMAKE_SOURCE_DIR}/libco/include
        ${CMAKE_SOURCE_DIR}/libco/src
        ${CMAKE_BINARY_DIR}/include
)

if(WIN32)
    target_link_libraries(test_iocp_timeout PRIVATE ws2_32)
endif()

add_test(NAME test_iocp_timeout COMMAND test_iocp_timeout)

# ============================================================================
# Google Test discovery (自动发现测试)
# ============================================================================
include(GoogleTest)
gtest_discover_tests(test_example_cpp)

# C++ 包装器测试 — Week 12 (Google Test)
add_executable(test_coxx
    test_coxx.cpp
)

target_link_libraries(test_coxx
    PRIVATE
        GTest::gtest
        GTest::gtest_main
        coxx
)

target_include_directories(test_coxx
    PRIVATE
        ${CMAKE_SOURCE_DIR}/libco/include
        ${CMAKE_SOURCE_DIR}/libcoxx/include
        ${CMAKE_BINARY_DIR}/include
)

add_test(NAME test_coxx COMMAND test_coxx)
gtest_discover_tests(test_coxx)
