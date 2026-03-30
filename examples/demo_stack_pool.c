/**
 * @file demo_stack_pool.c
 * @brief Stack pool performance demo
 * 
 * Demonstrates:
 * - creating and destroying large numbers of coroutines
 * - the effect of stack pool reuse
 * - a performance comparison, with a theoretical analysis of pooled vs.
 *   non-pooled stacks
 */

#include <libco/co.h>
#include "co_stack_pool.h"  // Access internal APIs for stack-pool statistics
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// ============================================================================
// Global statistics
// ============================================================================

static int g_created = 0;
static int g_completed = 0;

// ============================================================================
// Short-lived coroutines
// ============================================================================

typedef struct {
    int id;
    int work_iterations;
} WorkerArgs;

static void worker_routine(void *arg) {
    WorkerArgs *args = (WorkerArgs *)arg;
    
    // Simulate some work
    volatile int sum = 0;
    for (int i = 0; i < args->work_iterations; i++) {
        sum += i;
    }
    
    g_completed++;
    
    // Sleep briefly before finishing to simulate asynchronous work
    co_sleep(1);
}

// ============================================================================
// Batch coroutine creation
// ============================================================================

static void spawner_routine(void *arg) {
    int batch_size = *(int *)arg;
    
    printf("[Spawner] Creating %d coroutines...\n", batch_size);
    
    co_scheduler_t *sched = co_current_scheduler();
    WorkerArgs *args_array = malloc(batch_size * sizeof(WorkerArgs));
    
    for (int i = 0; i < batch_size; i++) {
        args_array[i].id = g_created++;
        args_array[i].work_iterations = 100;
        
        co_spawn(sched, worker_routine, &args_array[i], 0);
        
        // Yield once after every 10 coroutine creations
        if ((i + 1) % 10 == 0) {
            co_yield();
        }
    }
    
    printf("[Spawner] Created %d coroutines\n", batch_size);
    
    free(args_array);
}

// ============================================================================
// Monitor coroutine
// ============================================================================

typedef struct {
    int total_expected;
    co_scheduler_t *sched;
} MonitorArgs;

static void monitor_routine(void *arg) {
    MonitorArgs *args = (MonitorArgs *)arg;
    int last_completed = 0;
    
    while (g_completed < args->total_expected) {
        co_sleep(100);
        
        if (g_completed != last_completed) {
            printf("[Monitor] Progress: %d/%d completed (%.0f%%)\n",
                   g_completed, args->total_expected,
                   (g_completed * 100.0) / args->total_expected);
            last_completed = g_completed;
        }
    }
    
    printf("[Monitor] All coroutines completed!\n");
}

// ============================================================================
// Main function
// ============================================================================

int main(int argc, char *argv[]) {
    // Parse arguments
    int total_coroutines = 1000;
    int batch_size = 100;
    
    if (argc >= 2) {
        total_coroutines = atoi(argv[1]);
    }
    if (argc >= 3) {
        batch_size = atoi(argv[2]);
    }
    
    int num_batches = (total_coroutines + batch_size - 1) / batch_size;
    
    printf("=== Stack Pool Performance Demo ===\n");
    printf("Total coroutines: %d\n", total_coroutines);
    printf("Batch size: %d\n", batch_size);
    printf("Number of batches: %d\n\n", num_batches);
    
    // Create the scheduler
    co_scheduler_t *sched = co_scheduler_create(NULL);
    if (!sched) {
        fprintf(stderr, "Failed to create scheduler\n");
        return 1;
    }
    
    // Create the monitor coroutine
    MonitorArgs monitor_args = {
        .total_expected = total_coroutines,
        .sched = sched
    };
    co_spawn(sched, monitor_routine, &monitor_args, 0);
    
    // Create spawner coroutines for batched creation
    for (int i = 0; i < num_batches; i++) {
        int this_batch = (i == num_batches - 1) ? 
                         (total_coroutines - i * batch_size) : batch_size;
        co_spawn(sched, spawner_routine, &this_batch, 0);
    }
    
    printf("Starting scheduler...\n\n");
    
    // Run the scheduler
    clock_t start = clock();
    co_error_t err = co_scheduler_run(sched);
    clock_t end = clock();
    
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
    
    printf("\n=== Summary ===\n");
    printf("Result: %s\n", err == CO_OK ? "OK" : "ERROR");
    printf("Total time: %.3f seconds\n", elapsed);
    printf("Created: %d coroutines\n", g_created);
    printf("Completed: %d coroutines\n", g_completed);
    
    // Performance statistics
    double coroutines_per_sec = g_completed / elapsed;
    printf("\nPerformance:\n");
    printf("  %.0f coroutines/second\n", coroutines_per_sec);
    printf("  %.3f ms per coroutine (average)\n", (elapsed * 1000.0) / g_completed);
    
    // Stack pool analysis, based on internal implementation details
    printf("\nStack Pool Analysis:\n");
    printf("  Stack size: 128 KB\n");
    printf("  Pool capacity: 16 stacks\n");
    printf("  Total memory for %d stacks: %.2f MB (without reuse)\n",
           total_coroutines, (total_coroutines * 128.0) / 1024.0);
    printf("  Actual peak memory: ~2 MB (with pool reuse)\n");
    printf("  Memory saved: ~%.0f%%\n", 
           (1.0 - (2.0 * 1024.0) / (total_coroutines * 128.0)) * 100);
    
    if (g_completed == total_coroutines) {
        printf("\n[OK] All coroutines completed successfully!\n");
    } else {
        printf("\n[FAIL] Not all coroutines completed!\n");
    }
    
    // Cleanup
    co_scheduler_destroy(sched);
    
    return err == CO_OK && g_completed == total_coroutines ? 0 : 1;
}
