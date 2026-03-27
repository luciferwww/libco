/**
 * @file demo_concurrent.c
 * @brief Concurrent coroutine demo
 * 
 * Demonstrates:
 * - creating large numbers of coroutines
 * - interleaved coroutine execution
 * - scheduler performance validation
 */

#include <libco/co.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// ============================================================================
// Global counters
// ============================================================================

static int g_counter = 0;
static int g_completed = 0;

// ============================================================================
// Counter coroutine
// ============================================================================

typedef struct {
    int id;
    int iterations;
} CounterArgs;

static void counter_routine(void *arg) {
    CounterArgs *args = (CounterArgs *)arg;
    
    for (int i = 0; i < args->iterations; i++) {
        g_counter++;
        
        // Yield the CPU after each increment
        co_yield();
    }
    
    g_completed++;
}

// ============================================================================
// Progress-reporting coroutine
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
        
        co_sleep(50);  // Check once every 50 ms
    }
    
    printf("Progress: 100%% (%d/%d coroutines completed)\n", total, total);
}

// ============================================================================
// Main function
// ============================================================================

int main(int argc, char *argv[]) {
    // Parse command-line arguments
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
    
    // Create the scheduler
    co_scheduler_t *sched = co_scheduler_create(NULL);
    if (!sched) {
        fprintf(stderr, "Failed to create scheduler\n");
        return 1;
    }
    
    // Create the progress-monitor coroutine
    co_spawn(sched, progress_routine, &num_coroutines, 0);
    
    // Create a large number of counter coroutines
    CounterArgs *args_array = malloc(num_coroutines * sizeof(CounterArgs));
    
    for (int i = 0; i < num_coroutines; i++) {
        args_array[i].id = i;
        args_array[i].iterations = iterations_per_coroutine;
        co_spawn(sched, counter_routine, &args_array[i], 0);
    }
    
    printf("Starting scheduler with %d coroutines...\n\n", num_coroutines);
    
    // Run the scheduler
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
        printf("[OK] Counter value is correct!\n");
    } else {
        printf("[FAIL] Counter value mismatch!\n");
    }
    
    // Performance statistics
    double coroutines_per_sec = num_coroutines / elapsed;
    double switches_per_sec = (num_coroutines * iterations_per_coroutine) / elapsed;
    
    printf("\nPerformance:\n");
    printf("  %.0f coroutines/second\n", coroutines_per_sec);
    printf("  %.0f context switches/second\n", switches_per_sec);
    
    // Cleanup
    free(args_array);
    co_scheduler_destroy(sched);
    
    return err == CO_OK && g_counter == num_coroutines * iterations_per_coroutine ? 0 : 1;
}
