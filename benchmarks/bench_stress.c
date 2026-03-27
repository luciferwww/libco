/**
 * @file bench_stress.c
 * @brief 10K 协程并发压力测试
 *
 * 验证调度器在大量协程下的稳定性与内存正确性：
 *   1. 同时 spawn 10000 个协程，每个做少量工作后退出
 *   2. 验证所有协程都正确执行（计数器 == N）
 *   3. 报告总耗时和每协程平均耗时
 *
 * 同时也测试：
 *   - 大批 spawn 后调度器的 ready_queue 处理能力
 *   - 协程销毁路径（destroy 和 free）在高并发下无异常
 */

#include <libco/co.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <time.h>
#endif

#define N_COROUTINES  10000
#define WORK_ITERS    100    /* 每个协程做的"工作"量 */

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

/* ── 共享状态 ──────────────────────────────────────────────────────────────── */

static volatile int g_counter = 0;  /* 协程完成计数（单线程调度器，无需原子） */

typedef struct {
    int id;
} worker_arg_t;

static void worker_fn(void *arg) {
    worker_arg_t *a = (worker_arg_t *)arg;
    (void)a;

    /* 模拟少量计算工作 */
    volatile int x = 0;
    for (int i = 0; i < WORK_ITERS; i++) {
        x += i;
    }
    (void)x;

    /* 偶尔让出，模拟协作式调度 */
    if (a->id % 100 == 0) {
        co_yield_now();
    }

    g_counter++;
}

int main(void) {
    printf("=== bench_stress ===\n");
    printf("Coroutines: %d, work_iters_each: %d\n\n", N_COROUTINES, WORK_ITERS);

    worker_arg_t *args = (worker_arg_t *)malloc(N_COROUTINES * sizeof(worker_arg_t));
    if (!args) {
        fprintf(stderr, "malloc failed\n");
        return 1;
    }

    g_counter = 0;

    co_scheduler_t *sched = co_scheduler_create(NULL);

    /* spawn 全部协程 */
    uint64_t t_spawn0 = now_ns();
    for (int i = 0; i < N_COROUTINES; i++) {
        args[i].id = i;
        co_routine_t *r = co_spawn(sched, worker_fn, &args[i], 0);
        if (!r) {
            fprintf(stderr, "co_spawn failed at i=%d\n", i);
            free(args);
            co_scheduler_destroy(sched);
            return 1;
        }
    }
    uint64_t t_spawn1 = now_ns();

    /* 运行 */
    uint64_t t_run0 = now_ns();
    co_scheduler_run(sched);
    uint64_t t_run1 = now_ns();

    co_scheduler_destroy(sched);
    free(args);

    /* 结果 */
    double spawn_ms  = (double)(t_spawn1 - t_spawn0) / 1e6;
    double run_ms    = (double)(t_run1   - t_run0)   / 1e6;
    double ns_each   = (double)(t_run1   - t_run0)   / N_COROUTINES;

    printf("Spawn %d coroutines: %.2f ms  (%.2f μs/coroutine)\n",
           N_COROUTINES, spawn_ms, spawn_ms * 1000.0 / N_COROUTINES);
    printf("Run   %d coroutines: %.2f ms  (%.2f ns/coroutine)\n",
           N_COROUTINES, run_ms, ns_each);
    printf("Completed: %d / %d\n", g_counter, N_COROUTINES);

    int ok = (g_counter == N_COROUTINES);
    printf("\nResult: %s\n", ok ? "PASS" : "FAIL (counter mismatch)");
    return ok ? 0 : 1;
}
