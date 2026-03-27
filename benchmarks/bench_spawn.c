/**
 * @file bench_spawn.c
 * @brief 测量协程创建（spawn）开销
 *
 * 方法：
 *   批量 spawn N 个空协程，让调度器运行完后计时。
 *   总耗时 / N = 单次 spawn + 运行 + 销毁的摊销成本。
 *
 * 目标：< 1μs（x86_64 Release）
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

#define BATCH_SIZE  100000   /* 每批 spawn 数量 */
#define WARMUP_SIZE 1000     /* 热身批次大小 */

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

static void noop_fn(void *arg) {
    (void)arg;
    /* 空协程：立即返回 */
}

static double run_bench(int n) {
    co_scheduler_t *sched = co_scheduler_create(NULL);

    uint64_t t0 = now_ns();
    for (int i = 0; i < n; i++) {
        co_spawn(sched, noop_fn, NULL, 0);
    }
    co_scheduler_run(sched);
    uint64_t t1 = now_ns();

    co_scheduler_destroy(sched);
    return (double)(t1 - t0) / (double)n;
}

int main(void) {
    printf("=== bench_spawn ===\n");
    printf("Batch size: %d\n\n", BATCH_SIZE);

    /* 热身 */
    run_bench(WARMUP_SIZE);

    /* 正式测量，重复 3 轮 */
    double sum = 0.0;
    const int ROUNDS = 3;
    for (int r = 0; r < ROUNDS; r++) {
        double ns = run_bench(BATCH_SIZE);
        printf("  round %d: %.2f ns/coroutine\n", r + 1, ns);
        sum += ns;
    }

    double avg = sum / ROUNDS;
    printf("\nAverage: %.2f ns/coroutine  (%.3f μs)  (target: < 1000 ns)\n",
           avg, avg / 1000.0);
    printf("Result:  %s\n", avg < 1000.0 ? "PASS" : "FAIL (above target)");
    return avg < 1000.0 ? 0 : 1;
}
