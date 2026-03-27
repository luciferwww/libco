cmake_minimum_required(VERSION 3.15)
project(libco VERSION 2.0.0 LANGUAGES C CXX)

# 设置 C/C++ 标准
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 项目选项
option(LIBCO_BUILD_TESTS "Build tests" ON)
option(LIBCO_BUILD_EXAMPLES "Build examples" ON)
option(LIBCO_BUILD_BENCHMARKS "Build benchmarks" OFF)
option(LIBCO_BUILD_COXX "Build C++ wrapper" ON)
option(LIBCO_ENABLE_HOOKS "Enable system call hooks" OFF)
option(LIBCO_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(LIBCO_ENABLE_COVERAGE "Enable code coverage" OFF)

# 构建类型
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "CXX Compiler: ${CMAKE_CXX_COMPILER}")

# 全局编译选项
# 使用生成器表达式，正确支持 MSVC 多配置生成器（同时支持 Debug/Release 构建）
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        $<$<COMPILE_LANGUAGE:C>:-Werror=implicit-function-declaration>
        $<$<COMPILE_LANGUAGE:C>:-Werror=return-type>
        $<$<CONFIG:Debug>:-O0>
        $<$<CONFIG:Debug>:-g>
        $<$<CONFIG:Debug>:-ggdb>
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:-DNDEBUG>
    )
elseif(MSVC)
    add_compile_options(
        /W4
        /WX
        /D_CRT_SECURE_NO_WARNINGS
        $<$<CONFIG:Debug>:/Od>
        $<$<CONFIG:Debug>:/Zi>
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/DNDEBUG>
    )
endif()

# AddressSanitizer
if(LIBCO_ENABLE_ASAN)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address)
    endif()
endif()

# Code Coverage
if(LIBCO_ENABLE_COVERAGE)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(--coverage)
        add_link_options(--coverage)
    endif()
endif()

# 检测平台
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(LIBCO_PLATFORM_LINUX ON)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(LIBCO_PLATFORM_MACOS ON)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(LIBCO_PLATFORM_WINDOWS ON)
else()
    message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME}")
endif()

# 配置文件
configure_file(
    ${CMAKE_SOURCE_DIR}/config.h.in
    ${CMAKE_BINARY_DIR}/include/libco/config.h
)

# 子目录
add_subdirectory(libco)

if(LIBCO_BUILD_COXX)
    add_subdirectory(libcoxx)
endif()

if(LIBCO_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(LIBCO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(LIBCO_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

# 安装
include(GNUInstallDirs)
install(
    FILES ${CMAKE_BINARY_DIR}/include/libco/config.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/libco
)

# TODO: Phase 2 - 导出配置（等待实际库目标创建后启用）
# install(EXPORT libco-targets
#     FILE libco-targets.cmake
#     NAMESPACE libco::
#     DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libco
# )

# 生成版本文件
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/libco-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

# CPack 配置
set(CPACK_PACKAGE_NAME "libco")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "High-performance stackful coroutine library in C")
set(CPACK_PACKAGE_VENDOR "libco")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
include(CPack)
