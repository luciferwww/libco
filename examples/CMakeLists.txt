# Examples

message(STATUS "Building demo examples")

# ============================================================================
# Demo Examples
# ============================================================================

# 生产者-消费者示例
add_executable(demo_producer_consumer
    demo_producer_consumer.c
)

target_link_libraries(demo_producer_consumer
    PRIVATE
        co_static
)

target_include_directories(demo_producer_consumer
    PRIVATE
        ${CMAKE_SOURCE_DIR}/libco/include
        ${CMAKE_BINARY_DIR}/include
)

# 并发协程示例
add_executable(demo_concurrent
    demo_concurrent.c
)

target_link_libraries(demo_concurrent
    PRIVATE
        co_static
)

target_include_directories(demo_concurrent
    PRIVATE
        ${CMAKE_SOURCE_DIR}/libco/include
        ${CMAKE_BINARY_DIR}/include
)

# 定时任务调度示例
add_executable(demo_scheduler
    demo_scheduler.c
)

target_link_libraries(demo_scheduler
    PRIVATE
        co_static
)

target_include_directories(demo_scheduler
    PRIVATE
        ${CMAKE_SOURCE_DIR}/libco/include
        ${CMAKE_BINARY_DIR}/include
)

# Windows 需要额外的库
if(WIN32)
    target_link_libraries(demo_scheduler PRIVATE winmm)
endif()

# 栈池性能测试
add_executable(demo_stack_pool
    demo_stack_pool.c
)

target_link_libraries(demo_stack_pool
    PRIVATE
        co_static
)

target_include_directories(demo_stack_pool
    PRIVATE
        ${CMAKE_SOURCE_DIR}/libco/include
        ${CMAKE_SOURCE_DIR}/libco/src
        ${CMAKE_BINARY_DIR}/include
)

# Week 7: Echo Server 示例（网络 I/O）
add_executable(demo_echo_server
    demo_echo_server.c
)

target_link_libraries(demo_echo_server
    PRIVATE
        co_static
)

target_include_directories(demo_echo_server
    PRIVATE
        ${CMAKE_SOURCE_DIR}/libco/include
        ${CMAKE_BINARY_DIR}/include
)

# Week 8: Echo Client 测试程序
add_executable(demo_echo_client
    demo_echo_client.c
)

target_link_libraries(demo_echo_client
    PRIVATE
        co_static
)

target_include_directories(demo_echo_client
    PRIVATE
        ${CMAKE_SOURCE_DIR}/libco/include
        ${CMAKE_BINARY_DIR}/include
)

# Week 12: C++ wrapper demo
if(LIBCO_BUILD_COXX)
    add_executable(demo_coxx
        demo_coxx.cpp
    )

    target_link_libraries(demo_coxx
        PRIVATE
            coxx
    )

    target_include_directories(demo_coxx
        PRIVATE
            ${CMAKE_BINARY_DIR}/include
    )
endif()

# ============================================================================
# Installation (optional)
# ============================================================================

# install(TARGETS 
#     demo_producer_consumer
#     demo_concurrent
#     demo_scheduler
#     demo_stack_pool
#     demo_echo_server
#     demo_echo_client
#     RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/examples
# )
