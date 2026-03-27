# Integration Tests

# 生产者-消费者集成测试 (Week 9)
add_executable(test_producer_consumer
    test_producer_consumer.c
)

target_link_libraries(test_producer_consumer
    PRIVATE
        unity
        co_static
)

target_include_directories(test_producer_consumer
    PRIVATE
        ${CMAKE_SOURCE_DIR}/libco/include
        ${CMAKE_SOURCE_DIR}/libco/src
        ${CMAKE_BINARY_DIR}/include
)

add_test(NAME test_producer_consumer COMMAND test_producer_consumer)
