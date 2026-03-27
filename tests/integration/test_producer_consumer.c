/**
 * @file test_producer_consumer.c
 * @brief Producer-consumer integration test
 *
 * Verifies how co_mutex and co_cond cooperate in a realistic scenario:
 *   - bounded buffer with capacity = 4
 *   - 1 producer generating N elements
 *   - 2 consumers splitting the total consumption of N elements
 *   - producer waits when the buffer is full and consumers wait when empty
 */

#include "unity.h"
#include <libco/co.h>
#include <libco/co_sync.h>
#include <string.h>

// ============================================================================
// Bounded buffer
// ============================================================================

#define BUF_CAP    4
#define TOTAL_ITEMS 12

typedef struct {
    int       data[BUF_CAP];
    int       head;          // Next dequeue position
    int       tail;          // Next enqueue position
    int       count;         // Current element count
    co_mutex_t *mutex;
    co_cond_t  *not_full;    // Signaled when the buffer is not full
    co_cond_t  *not_empty;   // Signaled when the buffer is not empty
} bounded_buf_t;

static bounded_buf_t g_buf;
static int           g_produced  = 0;   // Values produced, accumulated as sequence numbers
static int           g_consumed  = 0;   // Values consumed, accumulated as sequence numbers

static void buf_init(bounded_buf_t *b) {
    memset(b, 0, sizeof(*b));
    b->mutex     = co_mutex_create(NULL);
    b->not_full  = co_cond_create(NULL);
    b->not_empty = co_cond_create(NULL);
}

static void buf_destroy(bounded_buf_t *b) {
    co_cond_destroy(b->not_empty);
    co_cond_destroy(b->not_full);
    co_mutex_destroy(b->mutex);
}

// Enqueue and suspend when the buffer is full
static void buf_put(bounded_buf_t *b, int val) {
    co_mutex_lock(b->mutex);
    while (b->count == BUF_CAP) {
        co_cond_wait(b->not_full, b->mutex);
    }
    b->data[b->tail] = val;
    b->tail = (b->tail + 1) % BUF_CAP;
    b->count++;
    co_cond_signal(b->not_empty);
    co_mutex_unlock(b->mutex);
}

// Dequeue and suspend when the buffer is empty
static int buf_get(bounded_buf_t *b) {
    co_mutex_lock(b->mutex);
    while (b->count == 0) {
        co_cond_wait(b->not_empty, b->mutex);
    }
    int val = b->data[b->head];
    b->head = (b->head + 1) % BUF_CAP;
    b->count--;
    co_cond_signal(b->not_full);
    co_mutex_unlock(b->mutex);
    return val;
}

// ============================================================================
// Coroutine entry functions
// ============================================================================

// Producer: generate values 1..TOTAL_ITEMS
static void producer(void *arg) {
    (void)arg;
    for (int i = 1; i <= TOTAL_ITEMS; i++) {
        buf_put(&g_buf, i);
        g_produced += i;
    }
}

// Consumer: consume the requested number of items
static void consumer(void *arg) {
    int n = *(int *)arg;
    for (int i = 0; i < n; i++) {
        int val = buf_get(&g_buf);
        g_consumed += val;
    }
}

// ============================================================================
// Test cases
// ============================================================================

void setUp(void) {
    buf_init(&g_buf);
    g_produced = 0;
    g_consumed = 0;
}

void tearDown(void) {
    buf_destroy(&g_buf);
}

// Basic case: 1 producer + 1 consumer
void test_producer_consumer_basic(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);

    int items = TOTAL_ITEMS;
    co_spawn(sched, producer,  NULL,   0);
    co_spawn(sched, consumer,  &items, 0);

    co_scheduler_run(sched);
    co_scheduler_destroy(sched);

    // Produced and consumed sums should match: 1+2+...+12 = 78
    TEST_ASSERT_EQUAL_INT(78, g_produced);
    TEST_ASSERT_EQUAL_INT(78, g_consumed);
    TEST_ASSERT_EQUAL_INT(0,  g_buf.count);   // Buffer should be empty at the end
}

// Multiple consumers: 1 producer + 2 consumers
void test_producer_consumer_multi_consumer(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);

    // Each consumer handles half of the items
    int half = TOTAL_ITEMS / 2;
    co_spawn(sched, producer,  NULL,  0);
    co_spawn(sched, consumer,  &half, 0);
    co_spawn(sched, consumer,  &half, 0);

    co_scheduler_run(sched);
    co_scheduler_destroy(sched);

    TEST_ASSERT_EQUAL_INT(78, g_produced);
    TEST_ASSERT_EQUAL_INT(78, g_consumed);
    TEST_ASSERT_EQUAL_INT(0,  g_buf.count);
}

// Producer suspends when full and resumes after a consumer wakes it
void test_producer_blocks_when_full(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);

    // Produce BUF_CAP+2 items; with capacity BUF_CAP, suspension must occur
    int extra = BUF_CAP + 2;
    co_spawn(sched, producer,  NULL,   0);
    co_spawn(sched, consumer,  &extra, 0);

    // The remaining TOTAL_ITEMS - extra items go to the second consumer
    int rest = TOTAL_ITEMS - extra;
    co_spawn(sched, consumer,  &rest,  0);

    co_scheduler_run(sched);
    co_scheduler_destroy(sched);

    TEST_ASSERT_EQUAL_INT(78, g_produced);
    TEST_ASSERT_EQUAL_INT(78, g_consumed);
    TEST_ASSERT_EQUAL_INT(0,  g_buf.count);
}

// ============================================================================
// Test entry
// ============================================================================

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_producer_consumer_basic);
    RUN_TEST(test_producer_consumer_multi_consumer);
    RUN_TEST(test_producer_blocks_when_full);
    return UNITY_END();
}
