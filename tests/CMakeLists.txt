# Tests

# ============================================================================
# Unity - C Testing Framework
# ============================================================================
include(FetchContent)

FetchContent_Declare(
    unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG v2.5.2
)

FetchContent_MakeAvailable(unity)

# 修复 Unity 在 MSVC 下的编译警告
# Unity 代码在高警告级别(/W4)下有多个警告，禁用"警告视为错误"和特定警告
if(TARGET unity AND MSVC)
    target_compile_options(unity PRIVATE 
        /WX-           # 禁用"警告视为错误"
        /wd4061        # switch中未处理的枚举器
        /wd4711        # 自动内联展开
        /wd5045        # Spectre缓解
    )
    target_compile_definitions(unity PUBLIC
        UNITY_SUPPORT_64
    )
endif()

# ============================================================================
# Google Test - C++ Testing Framework
# ============================================================================
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

# ============================================================================
# Test Subdirectories
# ============================================================================
add_subdirectory(unit)
add_subdirectory(integration)

# TODO: Phase 2 - 基准测试
# add_subdirectory(benchmarks)
