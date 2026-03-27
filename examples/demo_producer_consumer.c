/**
 * @file demo_producer_consumer.c
 * @brief 生产者-消费者示例
 * 
 * 演示多个协程协作：
 * - 生产者协程定期生产数据
 * - 消费者协程等待并消费数据
 * - 使用 co_sleep() 模拟工作延迟
 */

#include <libco/co.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// ============================================================================
// 共享队列（简单的环形缓冲区）
// ============================================================================

#define QUEUE_SIZE 10

typedef struct {
    int buffer[QUEUE_SIZE];
    int head;
    int tail;
    int count;
} Queue;

static Queue g_queue = {0};

// 入队
static bool queue_push(int value) {
    if (g_queue.count >= QUEUE_SIZE) {
        return false;  // 队列满
    }
    
    g_queue.buffer[g_queue.tail] = value;
    g_queue.tail = (g_queue.tail + 1) % QUEUE_SIZE;
    g_queue.count++;
    return true;
}

// 出队
static bool queue_pop(int *value) {
    if (g_queue.count == 0) {
        return false;  // 队列空
    }
    
    *value = g_queue.buffer[g_queue.head];
    g_queue.head = (g_queue.head + 1) % QUEUE_SIZE;
    g_queue.count--;
    return true;
}

// ============================================================================
// 生产者协程
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
        // 尝试生产
        while (!queue_push(args->id * 100 + i)) {
            printf("[Producer %d] Queue full, waiting...\n", args->id);
            co_sleep(10);  // 队列满，短暂等待
        }
        
        printf("[Producer %d] Produced: %d (queue: %d/%d)\n", 
               args->id, args->id * 100 + i, g_queue.count, QUEUE_SIZE);
        
        // 模拟生产延迟
        co_sleep(args->interval_ms);
    }
    
    printf("[Producer %d] Finished\n", args->id);
}

// ============================================================================
// 消费者协程
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
        
        // 尝试消费
        while (!queue_pop(&value)) {
            printf("[Consumer %d] Queue empty, waiting...\n", args->id);
            co_sleep(10);  // 队列空，短暂等待
        }
        
        printf("[Consumer %d] Consumed: %d (queue: %d/%d)\n",
               args->id, value, g_queue.count, QUEUE_SIZE);
        
        // 模拟消费延迟
        co_sleep(args->interval_ms);
    }
    
    printf("[Consumer %d] Finished\n", args->id);
}

// ============================================================================
// 主函数
// ============================================================================

int main(void) {
    printf("=== Producer-Consumer Demo ===\n\n");
    
    // 创建调度器
    co_scheduler_t *sched = co_scheduler_create(NULL);
    if (!sched) {
        fprintf(stderr, "Failed to create scheduler\n");
        return 1;
    }
    
    // 创建生产者参数
    ProducerArgs producer1 = { .id = 1, .produce_count = 5, .interval_ms = 30 };
    ProducerArgs producer2 = { .id = 2, .produce_count = 3, .interval_ms = 50 };
    
    // 创建消费者参数
    ConsumerArgs consumer1 = { .id = 1, .consume_count = 4, .interval_ms = 40 };
    ConsumerArgs consumer2 = { .id = 2, .consume_count = 4, .interval_ms = 60 };
    
    // 创建生产者协程
    co_spawn(sched, producer_routine, &producer1, 0);
    co_spawn(sched, producer_routine, &producer2, 0);
    
    // 创建消费者协程
    co_spawn(sched, consumer_routine, &consumer1, 0);
    co_spawn(sched, consumer_routine, &consumer2, 0);
    
    printf("Starting scheduler...\n\n");
    
    // 运行调度器
    clock_t start = clock();
    co_error_t err = co_scheduler_run(sched);
    clock_t end = clock();
    
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
    
    printf("\n=== Summary ===\n");
    printf("Result: %s\n", err == CO_OK ? "OK" : "ERROR");
    printf("Elapsed time: %.2f seconds\n", elapsed);
    printf("Final queue count: %d\n", g_queue.count);
    
    // 清理
    co_scheduler_destroy(sched);
    
    return err == CO_OK ? 0 : 1;
}
