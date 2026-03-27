/**
 * @file demo_scheduler.c
 * @brief 定时任务调度器示例
 * 
 * 演示：
 * - 多个定时任务并发执行
 * - co_sleep() 精确控制执行时间
 * - 模拟 cron 风格的任务调度
 */

#include <libco/co.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
static uint64_t get_time_ms(void) { return GetTickCount64(); }
#else
#include <sys/time.h>
static uint64_t get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}
#endif

// ============================================================================
// 任务定义
// ============================================================================

typedef struct {
    const char *name;
    int interval_ms;     // 执行间隔（毫秒）
    int total_runs;      // 总执行次数
    int run_count;       // 已执行次数
    uint64_t last_run;   // 上次执行时间
} Task;

// ============================================================================
// 任务协程
// ============================================================================

static void task_routine(void *arg) {
    Task *task = (Task *)arg;
    uint64_t start_time = get_time_ms();
    
    printf("[%s] Started (interval: %dms, runs: %d)\n", 
           task->name, task->interval_ms, task->total_runs);
    
    while (task->run_count < task->total_runs) {
        // 执行任务
        uint64_t now = get_time_ms();
        printf("[%s] Run #%d (elapsed: %llums)\n", 
               task->name, task->run_count + 1, 
               (unsigned long long)(now - start_time));
        
        task->run_count++;
        task->last_run = now;
        
        // 如果还有任务要执行，休眠到下次执行时间
        if (task->run_count < task->total_runs) {
            co_sleep(task->interval_ms);
        }
    }
    
    uint64_t end_time = get_time_ms();
    printf("[%s] Completed (total time: %llums)\n",
           task->name, (unsigned long long)(end_time - start_time));
}

// ============================================================================
// 监控协程
// ============================================================================

typedef struct {
    Task *tasks;
    int task_count;
    int report_interval_ms;
} MonitorArgs;

static void monitor_routine(void *arg) {
    MonitorArgs *args = (MonitorArgs *)arg;
    
    printf("[Monitor] Started (interval: %dms)\n\n", args->report_interval_ms);
    
    while (1) {
        co_sleep(args->report_interval_ms);
        
        // 检查所有任务是否完成
        int completed = 0;
        for (int i = 0; i < args->task_count; i++) {
            if (args->tasks[i].run_count >= args->tasks[i].total_runs) {
                completed++;
            }
        }
        
        if (completed == args->task_count) {
            printf("\n[Monitor] All tasks completed!\n");
            break;
        }
        
        // 打印进度
        printf("\n[Monitor] Progress Report:\n");
        for (int i = 0; i < args->task_count; i++) {
            Task *t = &args->tasks[i];
            printf("  %s: %d/%d runs (%.0f%%)\n",
                   t->name, t->run_count, t->total_runs,
                   (t->run_count * 100.0) / t->total_runs);
        }
        printf("\n");
    }
}

// ============================================================================
// 主函数
// ============================================================================

int main(void) {
    printf("=== Task Scheduler Demo ===\n\n");
    
    // 创建调度器
    co_scheduler_t *sched = co_scheduler_create(NULL);
    if (!sched) {
        fprintf(stderr, "Failed to create scheduler\n");
        return 1;
    }
    
    // 定义任务
    Task tasks[] = {
        { .name = "FastTask",   .interval_ms = 50,  .total_runs = 10, .run_count = 0 },
        { .name = "MediumTask", .interval_ms = 100, .total_runs = 5,  .run_count = 0 },
        { .name = "SlowTask",   .interval_ms = 200, .total_runs = 3,  .run_count = 0 },
        { .name = "OneShot",    .interval_ms = 0,   .total_runs = 1,  .run_count = 0 },
    };
    int task_count = sizeof(tasks) / sizeof(tasks[0]);
    
    // 创建任务协程
    for (int i = 0; i < task_count; i++) {
        co_spawn(sched, task_routine, &tasks[i], 0);
    }
    
    // 创建监控协程
    MonitorArgs monitor_args = {
        .tasks = tasks,
        .task_count = task_count,
        .report_interval_ms = 150
    };
    co_spawn(sched, monitor_routine, &monitor_args, 0);
    
    printf("Starting task scheduler...\n\n");
    
    // 运行调度器
    clock_t start = clock();
    co_error_t err = co_scheduler_run(sched);
    clock_t end = clock();
    
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
    
    printf("\n=== Summary ===\n");
    printf("Result: %s\n", err == CO_OK ? "OK" : "ERROR");
    printf("Total time: %.2f seconds\n", elapsed);
    
    // 验证所有任务完成
    bool all_completed = true;
    for (int i = 0; i < task_count; i++) {
        printf("  %s: %d/%d runs\n", 
               tasks[i].name, tasks[i].run_count, tasks[i].total_runs);
        if (tasks[i].run_count < tasks[i].total_runs) {
            all_completed = false;
        }
    }
    
    if (all_completed) {
        printf("✓ All tasks completed successfully!\n");
    } else {
        printf("✗ Some tasks did not complete!\n");
    }
    
    // 清理
    co_scheduler_destroy(sched);
    
    return err == CO_OK && all_completed ? 0 : 1;
}
