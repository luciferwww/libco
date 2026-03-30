/**
 * @file demo_producer_consumer.c
 * @brief Producer-consumer demo
 * 
 * Demonstrates cooperation between multiple coroutines:
 * - producer coroutines periodically generate data
 * - consumer coroutines wait for and consume data
 * - co_sleep() simulates work latency
 */

#include <libco/co.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// ============================================================================
// Shared queue implemented as a simple ring buffer
// ============================================================================

#define QUEUE_SIZE 10

typedef struct {
    int buffer[QUEUE_SIZE];
    int head;
    int tail;
    int count;
} Queue;

static Queue g_queue = {0};

// Enqueue
static bool queue_push(int value) {
    if (g_queue.count >= QUEUE_SIZE) {
        return false;  // Queue is full
    }
    
    g_queue.buffer[g_queue.tail] = value;
    g_queue.tail = (g_queue.tail + 1) % QUEUE_SIZE;
    g_queue.count++;
    return true;
}

// Dequeue
static bool queue_pop(int *value) {
    if (g_queue.count == 0) {
        return false;  // Queue is empty
    }
    
    *value = g_queue.buffer[g_queue.head];
    g_queue.head = (g_queue.head + 1) % QUEUE_SIZE;
    g_queue.count--;
    return true;
}

// ============================================================================
// Producer coroutine
// ============================================================================

typedef struct {
    int id;
    int produce_count;
    int interval_ms;
} ProducerArgs;

static void producer_routine(void *arg) {
    ProducerArgs *args = (ProducerArgs *)arg;
    
    printf("[Producer %d] Started\n", args->id);
    
    for (int i = 0; i < args->produce_count; i++) {
        // Try to produce an item
        while (!queue_push(args->id * 100 + i)) {
            printf("[Producer %d] Queue full, waiting...\n", args->id);
            co_sleep(10);  // Queue is full, wait briefly
        }
        
        printf("[Producer %d] Produced: %d (queue: %d/%d)\n", 
               args->id, args->id * 100 + i, g_queue.count, QUEUE_SIZE);
        
        // Simulate production latency
        co_sleep(args->interval_ms);
    }
    
    printf("[Producer %d] Finished\n", args->id);
}

// ============================================================================
// Consumer coroutine
// ============================================================================

typedef struct {
    int id;
    int consume_count;
    int interval_ms;
} ConsumerArgs;

static void consumer_routine(void *arg) {
    ConsumerArgs *args = (ConsumerArgs *)arg;
    
    printf("[Consumer %d] Started\n", args->id);
    
    for (int i = 0; i < args->consume_count; i++) {
        int value;
        
        // Try to consume an item
        while (!queue_pop(&value)) {
            printf("[Consumer %d] Queue empty, waiting...\n", args->id);
            co_sleep(10);  // Queue is empty, wait briefly
        }
        
        printf("[Consumer %d] Consumed: %d (queue: %d/%d)\n",
               args->id, value, g_queue.count, QUEUE_SIZE);
        
        // Simulate consumption latency
        co_sleep(args->interval_ms);
    }
    
    printf("[Consumer %d] Finished\n", args->id);
}

// ============================================================================
// Main function
// ============================================================================

int main(void) {
    printf("=== Producer-Consumer Demo ===\n\n");
    
    // Create the scheduler
    co_scheduler_t *sched = co_scheduler_create(NULL);
    if (!sched) {
        fprintf(stderr, "Failed to create scheduler\n");
        return 1;
    }
    
    // Set up producer parameters
    ProducerArgs producer1 = { .id = 1, .produce_count = 5, .interval_ms = 30 };
    ProducerArgs producer2 = { .id = 2, .produce_count = 3, .interval_ms = 50 };
    
    // Set up consumer parameters
    ConsumerArgs consumer1 = { .id = 1, .consume_count = 4, .interval_ms = 40 };
    ConsumerArgs consumer2 = { .id = 2, .consume_count = 4, .interval_ms = 60 };
    
    // Create producer coroutines
    co_spawn(sched, producer_routine, &producer1, 0);
    co_spawn(sched, producer_routine, &producer2, 0);
    
    // Create consumer coroutines
    co_spawn(sched, consumer_routine, &consumer1, 0);
    co_spawn(sched, consumer_routine, &consumer2, 0);
    
    printf("Starting scheduler...\n\n");
    
    // Run the scheduler
    clock_t start = clock();
    co_error_t err = co_scheduler_run(sched);
    clock_t end = clock();
    
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
    
    printf("\n=== Summary ===\n");
    printf("Result: %s\n", err == CO_OK ? "OK" : "ERROR");
    printf("Elapsed time: %.2f seconds\n", elapsed);
    printf("Final queue count: %d\n", g_queue.count);
    
    // Cleanup
    co_scheduler_destroy(sched);
    
    return err == CO_OK ? 0 : 1;
}
