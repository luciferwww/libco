/**
 * @file demo_scheduler.c
 * @brief Timed task scheduler demo
 * 
 * Demonstrates:
 * - concurrent execution of multiple timed tasks
 * - precise execution timing via co_sleep()
 * - a cron-style task scheduling pattern
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
// Task definition
// ============================================================================

typedef struct {
    const char *name;
    int interval_ms;     // Execution interval in milliseconds
    int total_runs;      // Total number of runs
    int run_count;       // Number of completed runs
    uint64_t last_run;   // Last execution timestamp
} Task;

// ============================================================================
// Task coroutine
// ============================================================================

static void task_routine(void *arg) {
    Task *task = (Task *)arg;
    uint64_t start_time = get_time_ms();
    
    printf("[%s] Started (interval: %dms, runs: %d)\n", 
           task->name, task->interval_ms, task->total_runs);
    
    while (task->run_count < task->total_runs) {
        // Run the task
        uint64_t now = get_time_ms();
        printf("[%s] Run #%d (elapsed: %llums)\n", 
               task->name, task->run_count + 1, 
               (unsigned long long)(now - start_time));
        
        task->run_count++;
        task->last_run = now;
        
        // If more runs remain, sleep until the next execution time
        if (task->run_count < task->total_runs) {
            co_sleep(task->interval_ms);
        }
    }
    
    uint64_t end_time = get_time_ms();
    printf("[%s] Completed (total time: %llums)\n",
           task->name, (unsigned long long)(end_time - start_time));
}

// ============================================================================
// Monitor coroutine
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
        
        // Check whether all tasks have completed
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
        
        // Print progress
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
// Main function
// ============================================================================

int main(void) {
    printf("=== Task Scheduler Demo ===\n\n");
    
    // Create the scheduler
    co_scheduler_t *sched = co_scheduler_create(NULL);
    if (!sched) {
        fprintf(stderr, "Failed to create scheduler\n");
        return 1;
    }
    
    // Define tasks
    Task tasks[] = {
        { .name = "FastTask",   .interval_ms = 50,  .total_runs = 10, .run_count = 0 },
        { .name = "MediumTask", .interval_ms = 100, .total_runs = 5,  .run_count = 0 },
        { .name = "SlowTask",   .interval_ms = 200, .total_runs = 3,  .run_count = 0 },
        { .name = "OneShot",    .interval_ms = 0,   .total_runs = 1,  .run_count = 0 },
    };
    int task_count = sizeof(tasks) / sizeof(tasks[0]);
    
    // Create task coroutines
    for (int i = 0; i < task_count; i++) {
        co_spawn(sched, task_routine, &tasks[i], 0);
    }
    
    // Create the monitor coroutine
    MonitorArgs monitor_args = {
        .tasks = tasks,
        .task_count = task_count,
        .report_interval_ms = 150
    };
    co_spawn(sched, monitor_routine, &monitor_args, 0);
    
    printf("Starting task scheduler...\n\n");
    
    // Run the scheduler
    clock_t start = clock();
    co_error_t err = co_scheduler_run(sched);
    clock_t end = clock();
    
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
    
    printf("\n=== Summary ===\n");
    printf("Result: %s\n", err == CO_OK ? "OK" : "ERROR");
    printf("Total time: %.2f seconds\n", elapsed);
    
    // Verify that all tasks completed
    bool all_completed = true;
    for (int i = 0; i < task_count; i++) {
        printf("  %s: %d/%d runs\n", 
               tasks[i].name, tasks[i].run_count, tasks[i].total_runs);
        if (tasks[i].run_count < tasks[i].total_runs) {
            all_completed = false;
        }
    }
    
    if (all_completed) {
        printf("[OK] All tasks completed successfully!\n");
    } else {
        printf("[FAIL] Some tasks did not complete!\n");
    }
    
    // Cleanup
    co_scheduler_destroy(sched);
    
    return err == CO_OK && all_completed ? 0 : 1;
}
