/**
 * @file demo_concurrent.c
 * @brief 并发协程示例
 * 
 * 演示：
 * - 创建大量协程
 * - 协程交替执行
 * - 验证调度器性能
 */

#include <libco/co.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// ============================================================================
// 全局计数器
// ============================================================================

static int g_counter = 0;
static int g_completed = 0;

// ============================================================================
// 计数协程
// ============================================================================

typedef struct {
    int id;
    int iterations;
} CounterArgs;

static void counter_routine(void *arg) {
    CounterArgs *args = (CounterArgs *)arg;
    
    for (int i = 0; i < args->iterations; i++) {
        g_counter++;
        
        // 每次递增后让出 CPU
        co_yield();
    }
    
    g_completed++;
}

// ============================================================================
// 打印进度协程
// ============================================================================

static void progress_routine(void *arg) {
    int total = *(int *)arg;
    int last_progress = 0;
    
    while (g_completed < total) {
        int progress = (g_completed * 100) / total;
        if (progress != last_progress && progress % 10 == 0) {
            printf("Progress: %d%% (%d/%d coroutines completed)\n", 
                   progress, g_completed, total);
            last_progress = progress;
        }
        
        co_sleep(50);  // 每 50ms 检查一次
    }
    
    printf("Progress: 100%% (%d/%d coroutines completed)\n", total, total);
}

// ============================================================================
// 主函数
// ============================================================================

int main(int argc, char *argv[]) {
    // 解析命令行参数
    int num_coroutines = 100;
    int iterations_per_coroutine = 100;
    
    if (argc >= 2) {
        num_coroutines = atoi(argv[1]);
    }
    if (argc >= 3) {
        iterations_per_coroutine = atoi(argv[2]);
    }
    
    printf("=== Concurrent Coroutines Demo ===\n");
    printf("Coroutines: %d\n", num_coroutines);
    printf("Iterations per coroutine: %d\n", iterations_per_coroutine);
    printf("Expected counter value: %d\n\n", num_coroutines * iterations_per_coroutine);
    
    // 创建调度器
    co_scheduler_t *sched = co_scheduler_create(NULL);
    if (!sched) {
        fprintf(stderr, "Failed to create scheduler\n");
        return 1;
    }
    
    // 创建进度监控协程
    co_spawn(sched, progress_routine, &num_coroutines, 0);
    
    // 创建大量计数协程
    CounterArgs *args_array = malloc(num_coroutines * sizeof(CounterArgs));
    
    for (int i = 0; i < num_coroutines; i++) {
        args_array[i].id = i;
        args_array[i].iterations = iterations_per_coroutine;
        co_spawn(sched, counter_routine, &args_array[i], 0);
    }
    
    printf("Starting scheduler with %d coroutines...\n\n", num_coroutines);
    
    // 运行调度器
    clock_t start = clock();
    co_error_t err = co_scheduler_run(sched);
    clock_t end = clock();
    
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
    
    printf("\n=== Summary ===\n");
    printf("Result: %s\n", err == CO_OK ? "OK" : "ERROR");
    printf("Elapsed time: %.3f seconds\n", elapsed);
    printf("Counter value: %d (expected: %d)\n", 
           g_counter, num_coroutines * iterations_per_coroutine);
    printf("Completed coroutines: %d\n", g_completed);
    
    if (g_counter == num_coroutines * iterations_per_coroutine) {
        printf("✓ Counter value is correct!\n");
    } else {
        printf("✗ Counter value mismatch!\n");
    }
    
    // 性能统计
    double coroutines_per_sec = num_coroutines / elapsed;
    double switches_per_sec = (num_coroutines * iterations_per_coroutine) / elapsed;
    
    printf("\nPerformance:\n");
    printf("  %.0f coroutines/second\n", coroutines_per_sec);
    printf("  %.0f context switches/second\n", switches_per_sec);
    
    // 清理
    free(args_array);
    co_scheduler_destroy(sched);
    
    return err == CO_OK && g_counter == num_coroutines * iterations_per_coroutine ? 0 : 1;
}
