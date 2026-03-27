# 构建系统

## CMake 项目结构

### 根 CMakeLists.txt

```cmake
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
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Werror=implicit-function-declaration
        -Werror=return-type
    )
    
    # Debug 选项
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-O0 -g -ggdb)
    endif()
    
    # Release 选项
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3 -DNDEBUG)
    endif()
elseif(MSVC)
    add_compile_options(
        /W4
        /WX
        /D_CRT_SECURE_NO_WARNINGS
    )
    
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(/Od /Zi)
    endif()
    
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(/O2 /DNDEBUG)
    endif()
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

# 导出配置
install(EXPORT libco-targets
    FILE libco-targets.cmake
    NAMESPACE libco::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libco
)

# 生成版本文件
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/libco-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

install(
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/libco-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/libco-config-version.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libco
)

# CPack 配置
set(CPACK_PACKAGE_NAME "libco")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "High-performance stackful coroutine library in C")
set(CPACK_PACKAGE_VENDOR "Your Name")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
include(CPack)
```

### libco/CMakeLists.txt

```cmake
# 收集源文件
set(LIBCO_SOURCES
    src/co.c
    src/co_sched.c
    src/co_routine.c
    src/co_timer.c
    src/co_memory.c
    src/co_error.c
)

# 平台特定源文件
if(LIBCO_PLATFORM_LINUX)
    list(APPEND LIBCO_SOURCES
        src/platform/linux/context.c
        src/platform/linux/iomux_epoll.c
        src/platform/linux/timer.c
    )
elseif(LIBCO_PLATFORM_MACOS)
    list(APPEND LIBCO_SOURCES
        src/platform/macos/context.c
        src/platform/macos/iomux_kqueue.c
        src/platform/macos/timer.c
    )
elseif(LIBCO_PLATFORM_WINDOWS)
    list(APPEND LIBCO_SOURCES
        src/platform/windows/context.c
        src/platform/windows/iomux_iocp.c
        src/platform/windows/timer.c
    )
endif()

# 同步原语（可选）
list(APPEND LIBCO_SOURCES
    src/sync/co_mutex.c
    src/sync/co_cond.c
    src/sync/co_channel.c
    src/sync/co_waitgroup.c
)

# Hook（可选）
if(LIBCO_ENABLE_HOOKS)
    if(LIBCO_PLATFORM_LINUX OR LIBCO_PLATFORM_MACOS)
        list(APPEND LIBCO_SOURCES
            src/hooks/unix/co_hooks.c
        )
    elseif(LIBCO_PLATFORM_WINDOWS)
        list(APPEND LIBCO_SOURCES
            src/hooks/windows/co_hooks.c
        )
    endif()
endif()

# 创建静态库
add_library(co_static STATIC ${LIBCO_SOURCES})
target_include_directories(co_static
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 创建共享库
add_library(co_shared SHARED ${LIBCO_SOURCES})
target_include_directories(co_shared
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 平台特定链接库
if(LIBCO_PLATFORM_LINUX)
    target_link_libraries(co_static PUBLIC pthread)
    target_link_libraries(co_shared PUBLIC pthread)
elseif(LIBCO_PLATFORM_WINDOWS)
    target_link_libraries(co_static PUBLIC ws2_32)
    target_link_libraries(co_shared PUBLIC ws2_32)
endif()

# 设置输出名称
set_target_properties(co_static PROPERTIES OUTPUT_NAME co)
set_target_properties(co_shared PROPERTIES OUTPUT_NAME co)

# 安装
install(TARGETS co_static co_shared
    EXPORT libco-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(
    DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)
```

### libcoxx/CMakeLists.txt

```cmake
set(LIBCOXX_SOURCES
    src/coxx.cpp
    src/scheduler.cpp
    src/sync.cpp
)

add_library(coxx_static STATIC ${LIBCOXX_SOURCES})
target_link_libraries(coxx_static PUBLIC co_static)
target_include_directories(coxx_static
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

add_library(coxx_shared SHARED ${LIBCOXX_SOURCES})
target_link_libraries(coxx_shared PUBLIC co_shared)
target_include_directories(coxx_shared
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

set_target_properties(coxx_static PROPERTIES OUTPUT_NAME coxx)
set_target_properties(coxx_shared PROPERTIES OUTPUT_NAME coxx)

install(TARGETS coxx_static coxx_shared
    EXPORT libco-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(
    DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp"
)
```

## 配置文件

### config.h.in

```c
#ifndef LIBCO_CONFIG_H
#define LIBCO_CONFIG_H

// Version
#define LIBCO_VERSION_MAJOR @PROJECT_VERSION_MAJOR@
#define LIBCO_VERSION_MINOR @PROJECT_VERSION_MINOR@
#define LIBCO_VERSION_PATCH @PROJECT_VERSION_PATCH@
#define LIBCO_VERSION_STRING "@PROJECT_VERSION@"

// Platform
#cmakedefine LIBCO_PLATFORM_LINUX
#cmakedefine LIBCO_PLATFORM_MACOS
#cmakedefine LIBCO_PLATFORM_WINDOWS

// Features
#cmakedefine LIBCO_ENABLE_HOOKS

// Build type
#cmakedefine CMAKE_BUILD_TYPE "@CMAKE_BUILD_TYPE@"

#endif // LIBCO_CONFIG_H
```

## 构建脚本

### build.sh (Unix)

```bash
#!/bin/bash

set -e

BUILD_TYPE=${1:-Release}
BUILD_DIR="build-${BUILD_TYPE}"

echo "Building libco (${BUILD_TYPE})..."

# 创建构建目录
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# 配置
cmake .. \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DLIBCO_BUILD_TESTS=ON \
    -DLIBCO_BUILD_EXAMPLES=ON \
    -DLIBCO_BUILD_BENCHMARKS=OFF

# 编译
cmake --build . -j$(nproc)

# 测试
ctest --output-on-failure

echo "Build completed!"
```

### build.ps1 (Windows)

```powershell
param(
    [string]$BuildType = "Release"
)

$ErrorActionPreference = "Stop"

$BuildDir = "build-$BuildType"

Write-Host "Building libco ($BuildType)..." -ForegroundColor Green

# 创建构建目录
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
Set-Location $BuildDir

# 配置
cmake .. `
    -DCMAKE_BUILD_TYPE="$BuildType" `
    -DLIBCO_BUILD_TESTS=ON `
    -DLIBCO_BUILD_EXAMPLES=ON `
    -DLIBCO_BUILD_BENCHMARKS=OFF

# 编译
cmake --build . --config $BuildType

# 测试
ctest -C $BuildType --output-on-failure

Write-Host "Build completed!" -ForegroundColor Green
```

## 包管理器支持

### vcpkg

```json
{
  "name": "libco",
  "version": "2.0.0",
  "description": "High-performance stackful coroutine library in C",
  "homepage": "https://github.com/yourname/libco",
  "dependencies": []
}
```

```cmake
# portfile.cmake for vcpkg

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO yourname/libco
    REF v2.0.0
    SHA512 ...
    HEAD_REF main
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DLIBCO_BUILD_TESTS=OFF
        -DLIBCO_BUILD_EXAMPLES=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/libco)
vcpkg_copy_pdbs()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(INSTALL "${SOURCE_PATH}/LICENSE" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
```

### Conan

```python
# conanfile.py

from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout

class LibcoConan(ConanFile):
    name = "libco"
    version = "2.0.0"
    description = "High-performance stackful coroutine library in C"
    license = "MIT"
    url = "https://github.com/yourname/libco"
    settings = "os", "compiler", "build_type", "arch"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
        "enable_hooks": [True, False]
    }
    default_options = {
        "shared": False,
        "fPIC": True,
        "enable_hooks": False
    }
    
    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC
    
    def layout(self):
        cmake_layout(self)
    
    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables["LIBCO_BUILD_TESTS"] = False
        tc.variables["LIBCO_BUILD_EXAMPLES"] = False
        tc.variables["LIBCO_ENABLE_HOOKS"] = self.options.enable_hooks
        tc.generate()
    
    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
    
    def package(self):
        cmake = CMake(self)
        cmake.install()
    
    def package_info(self):
        self.cpp_info.libs = ["co", "coxx"]
```

## 交叉编译

### ARM64 Toolchain

```cmake
# cmake/toolchain-arm64.cmake

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
```

```bash
# 交叉编译
cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm64.cmake ..
make
```

## Docker 构建

### Dockerfile

```dockerfile
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    ninja-build \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

COPY . .

RUN mkdir build && cd build && \
    cmake .. -GNinja \
        -DCMAKE_BUILD_TYPE=Release \
        -DLIBCO_BUILD_TESTS=ON && \
    ninja && \
    ctest --output-on-failure

CMD ["/bin/bash"]
```

```bash
# 构建镜像
docker build -t libco:latest .

# 运行测试
docker run --rm libco:latest ctest --test-dir build --output-on-failure
```

## 持续集成

### GitHub Actions

```yaml
# .github/workflows/build.yml

name: Build

on:
  push:
    branches: [ main, develop ]
  pull_request:
    branches: [ main ]

jobs:
  build:
    strategy:
      matrix:
        os: [ubuntu-22.04, macos-13, windows-2022]
        build_type: [Debug, Release]
        compiler:
          - { cc: gcc, cxx: g++ }
          - { cc: clang, cxx: clang++ }
        exclude:
          - os: macos-13
            compiler: { cc: gcc, cxx: g++ }
          - os: windows-2022
            compiler: { cc: gcc, cxx: g++ }
          - os: windows-2022
            compiler: { cc: clang, cxx: clang++ }
    
    runs-on: ${{ matrix.os }}
    
    env:
      CC: ${{ matrix.compiler.cc }}
      CXX: ${{ matrix.compiler.cxx }}
    
    steps:
      - uses: actions/checkout@v3
      
      - name: Install dependencies (Ubuntu)
        if: runner.os == 'Linux'
        run: |
          sudo apt-get update
          sudo apt-get install -y cmake ninja-build
      
      - name: Install dependencies (macOS)
        if: runner.os == 'macOS'
        run: |
          brew install cmake ninja
      
      - name: Configure
        run: |
          cmake -B build -G Ninja \
            -DCMAKE_BUILD_TYPE=${{ matrix.build_type }} \
            -DLIBCO_BUILD_TESTS=ON \
            -DLIBCO_BUILD_EXAMPLES=ON
      
      - name: Build
        run: cmake --build build
      
      - name: Test
        run: ctest --test-dir build --output-on-failure
      
      - name: Install
        run: cmake --install build --prefix install
```

## 文档生成

### Doxygen

```cmake
# docs/CMakeLists.txt

find_package(Doxygen)

if(DOXYGEN_FOUND)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in
        ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
        @ONLY
    )
    
    add_custom_target(doc
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )
endif()
```

生成文档：
```bash
cmake --build build --target doc
```

## 下一步

参见：
- [06-roadmap.md](./06-roadmap.md) - 实施路线图
