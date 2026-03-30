# Examples

message(STATUS "Building demo examples")

# ============================================================================
# Demo Examples
# ============================================================================

# Producer-consumer demo
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

# Concurrent coroutines demo
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

# Timer-based task scheduling demo
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

# Windows requires an extra library
if(WIN32)
    target_link_libraries(demo_scheduler PRIVATE winmm)
endif()

# Stack pool performance demo
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

# Week 7: Echo server demo (network I/O)
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

# Week 8: Echo client test program
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
