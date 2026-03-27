# libco - C 核心协程库

# ============================================================================
# 源文件收集
# ============================================================================

set(LIBCO_SOURCES
    # Week 4: 调度器核心
    src/co_scheduler.c
    src/co_routine.c
    
    # Week 5: 内存管理
    src/co_stack_pool.c
    src/co_allocator.c
    
    # Week 6: 定时器
    src/co_timer.c

    # Week 9: 同步原语
    src/sync/co_mutex.c
    src/sync/co_cond.c

    # Week 10: Channel
    src/sync/co_channel.c
)

# 平台特定源文件
if(LIBCO_PLATFORM_LINUX)
    list(APPEND LIBCO_SOURCES
        src/platform/linux/context.c
        src/platform/linux/iomux_epoll.c  # Week 7: epoll 实现
    )
elseif(LIBCO_PLATFORM_MACOS)
    # TODO(kqueue)：实现 src/platform/macos/iomux_kqueue.c 后支持 macOS
    # 届时将 context.c 和 iomux_kqueue.c 加入 LIBCO_SOURCES
    message(FATAL_ERROR "macOS is not yet supported: kqueue I/O multiplexer not implemented.")
elseif(LIBCO_PLATFORM_WINDOWS)
    list(APPEND LIBCO_SOURCES
        src/platform/windows/context.c
        src/platform/windows/iomux_iocp.c  # Week 7: IOCP 实现
    )
endif()

# ============================================================================
# 创建库目标
# ============================================================================

# 静态库
add_library(co_static STATIC ${LIBCO_SOURCES})

target_include_directories(co_static
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 平台特定链接库
if(LIBCO_PLATFORM_WINDOWS)
    # Week 7: Windows IOCP 需要 Winsock 库
    target_link_libraries(co_static PUBLIC ws2_32 mswsock)
elseif(LIBCO_PLATFORM_LINUX OR LIBCO_PLATFORM_MACOS)
    # Unix 平台不需要额外的链接库（ucontext 是 libc 的一部分）
endif()

# 设置输出名称
set_target_properties(co_static PROPERTIES
    OUTPUT_NAME co
    POSITION_INDEPENDENT_CODE ON
)

# 创建接口库别名（用于测试和示例）
add_library(co ALIAS co_static)

# ============================================================================
# 安装
# ============================================================================

# TODO: Phase 2 完成后启用安装规则
# install(TARGETS co_static
#     EXPORT libco-targets
#     ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
#     LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
#     RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
# )

# install(
#     DIRECTORY include/
#     DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
#     FILES_MATCHING PATTERN "*.h"
# )

# TODO: Week 4 - 添加调度器实现
# TODO: Week 5 - 添加栈管理
# TODO: Week 6 - 添加定时器
# TODO: Week 7 - 添加生命周期管理
