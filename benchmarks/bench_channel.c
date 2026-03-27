/**
 * @file bench_channel.c
 * @brief 测量 Channel 吞吐量（ops/s）
 *
 * 测试两种场景：
 *   1. 带缓冲 Channel（capacity=256）：减少阻塞，测峰值吞吐
 *   2. 无缓冲 rendezvous Channel（capacity=0）：测同步开销
 *
 * 方法：
 *   一对 producer/consumer 协程对打，producer 发送 N 条消息后关闭。
 *   计算总耗时后得出 ops/s。
 *
 * 目标：带缓冲 > 10M ops/s（x86_64 Release）
 */

#include <libco/co.h>
#include <libco/co_sync.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <time.h>
#endif

#define MSG_COUNT  2000000   /* 发送消息总数 */
#define WARMUP_CNT 100000

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
    co_channel_t *ch;
    int           count;
} bench_arg_t;

static void producer_fn(void *arg) {
    bench_arg_t *a = (bench_arg_t *)arg;
    int val = 0;
    for (int i = 0; i < a->count; i++) {
        co_channel_send(a->ch, &val);
    }
    co_channel_close(a->ch);
}

static void consumer_fn(void *arg) {
    bench_arg_t *a = (bench_arg_t *)arg;
    int val;
    while (co_channel_recv(a->ch, &val) == CO_OK) {
        /* 仅消费，不做任何处理 */
    }
}

/* ── 单次测量 ────────────────────────────────────────────────────────────── */

static double run_bench(int count, size_t cap) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    co_channel_t *ch = co_channel_create(sizeof(int), cap);
    bench_arg_t arg = { ch, count };

    co_spawn(sched, producer_fn, &arg, 0);
    co_spawn(sched, consumer_fn, &arg, 0);

    uint64_t t0 = now_ns();
    co_scheduler_run(sched);
    uint64_t t1 = now_ns();

    co_channel_destroy(ch);
    co_scheduler_destroy(sched);

    double elapsed_s = (double)(t1 - t0) / 1e9;
    return (double)count / elapsed_s;  /* ops/s */
}

int main(void) {
    printf("=== bench_channel ===\n");
    printf("Messages: %d\n\n", MSG_COUNT);

    /* ── 带缓冲（capacity=256）── */
    printf("[Buffered channel, cap=256]\n");
    run_bench(WARMUP_CNT, 256);  /* 热身 */

    double sum_buf = 0.0;
    const int ROUNDS = 3;
    for (int r = 0; r < ROUNDS; r++) {
        double ops = run_bench(MSG_COUNT, 256);
        printf("  round %d: %.2f M ops/s\n", r + 1, ops / 1e6);
        sum_buf += ops;
    }
    double avg_buf = sum_buf / ROUNDS;
    printf("Average: %.2f M ops/s  (target: > 10 M ops/s)\n", avg_buf / 1e6);
    printf("Result:  %s\n\n", avg_buf >= 10e6 ? "PASS" : "FAIL (below target)");

    /* ── 无缓冲 rendezvous（capacity=0）── */
    printf("[Rendezvous channel, cap=0]\n");
    run_bench(WARMUP_CNT, 0);  /* 热身 */

    double sum_rdv = 0.0;
    for (int r = 0; r < ROUNDS; r++) {
        /* rendezvous 较慢，减少次数避免耗时过长 */
        double ops = run_bench(MSG_COUNT / 10, 0);
        printf("  round %d: %.2f M ops/s\n", r + 1, ops / 1e6);
        sum_rdv += ops;
    }
    double avg_rdv = sum_rdv / ROUNDS;
    printf("Average: %.2f M ops/s  (无独立目标，仅供参考)\n\n", avg_rdv / 1e6);

    return avg_buf >= 10e6 ? 0 : 1;
}
