# Benchmarks (Week 13)
# 使用 Release 模式编译以获得准确的性能数据。
# 通过 -DLIBCO_BUILD_BENCHMARKS=ON 开启。

message(STATUS "Building benchmarks")

# 通用辅助函数：创建一个 benchmark 可执行文件
function(add_benchmark name src)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE co_static)
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/libco/include
        ${CMAKE_BINARY_DIR}/include
    )
    if(WIN32)
        target_link_libraries(${name} PRIVATE winmm)
    endif()
endfunction()

# ── 1. 上下文切换延迟 ────────────────────────────────────────────────────────
add_benchmark(bench_context_switch bench_context_switch.c)

# ── 2. 协程创建开销 ──────────────────────────────────────────────────────────
add_benchmark(bench_spawn bench_spawn.c)

# ── 3. Channel 吞吐量 ────────────────────────────────────────────────────────
add_benchmark(bench_channel bench_channel.c)

# ── 4. 10K 协程压力测试 ──────────────────────────────────────────────────────
add_benchmark(bench_stress bench_stress.c)
