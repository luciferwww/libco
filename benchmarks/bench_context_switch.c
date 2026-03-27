/**
 * @file bench_context_switch.c
 * @brief 测量协程上下文切换延迟
 *
 * 方法：
 *   两个协程 ping/pong，互相 yield，共切换 ITERATIONS 次。
 *   总耗时 / ITERATIONS / 2 = 单次切换成本（含 yield + 调度器 resume）。
 *
 * 目标：< 50ns（x86_64 Release）
 */

#include <libco/co.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <time.h>
#endif

#define ITERATIONS 1000000  /* 每轮 yield 次数 */
#define WARMUP     10000    /* 热身次数，不计入计时 */

/* ── 时间工具 ────────────────────────────────────────────────────────────── */

static uint64_t now_ns(void) {
#ifdef _WIN32
    static LARGE_INTEGER freq = {0};
    LARGE_INTEGER cnt;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&cnt);
    return (uint64_t)(cnt.QuadPart * 1000000000LL / freq.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
#endif
}

/* ── 协程体 ──────────────────────────────────────────────────────────────── */

typedef struct {
    int   iters;
    int   done;
} ping_arg_t;

static void pong_fn(void *arg) {
    ping_arg_t *a = (ping_arg_t *)arg;
    while (!a->done) {
        co_yield_now();
    }
}

static void ping_fn(void *arg) {
    ping_arg_t *a = (ping_arg_t *)arg;
    for (int i = 0; i < a->iters; i++) {
        co_yield_now();   /* 切到 pong，pong yield 回来 = 两次切换 */
    }
    a->done = 1;
}

/* ── 主函数 ──────────────────────────────────────────────────────────────── */

static double run_bench(int iters) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    ping_arg_t arg = { iters, 0 };

    co_spawn(sched, ping_fn, &arg, 0);
    co_spawn(sched, pong_fn, &arg, 0);

    uint64_t t0 = now_ns();
    co_scheduler_run(sched);
    uint64_t t1 = now_ns();

    co_scheduler_destroy(sched);

    /* 每个 yield 调度器做两次切换（ping→pong, pong→ping） */
    double total_switches = (double)iters * 2.0;
    return (double)(t1 - t0) / total_switches;
}

int main(void) {
    printf("=== bench_context_switch ===\n");
    printf("Iterations: %d\n\n", ITERATIONS);

    /* 热身 */
    run_bench(WARMUP);

    /* 正式测量，重复 3 轮取平均 */
    double sum = 0.0;
    const int ROUNDS = 3;
    for (int r = 0; r < ROUNDS; r++) {
        double ns = run_bench(ITERATIONS);
        printf("  round %d: %.2f ns/switch\n", r + 1, ns);
        sum += ns;
    }

    double avg = sum / ROUNDS;
    printf("\nAverage: %.2f ns/switch  (target: < 50 ns)\n", avg);
    printf("Result:  %s\n", avg < 50.0 ? "PASS" : "FAIL (above target)");
    return avg < 50.0 ? 0 : 1;
}
