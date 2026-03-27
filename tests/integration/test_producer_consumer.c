/**
 * @file test_producer_consumer.c
 * @brief 生产者-消费者集成测试
 *
 * 验证 co_mutex + co_cond 在实际场景中的协作：
 *   - 有界缓冲区（capacity = 4）
 *   - 1 个生产者：生产 N 个元素
 *   - 2 个消费者：各取若干元素，消费总量 = N
 *   - 生产者在缓冲区满时等待，消费者在缓冲区空时等待
 */

#include "unity.h"
#include <libco/co.h>
#include <libco/co_sync.h>
#include <string.h>

// ============================================================================
// 有界缓冲区
// ============================================================================

#define BUF_CAP    4
#define TOTAL_ITEMS 12

typedef struct {
    int       data[BUF_CAP];
    int       head;          // 下一个出队位置
    int       tail;          // 下一个入队位置
    int       count;         // 当前元素数
    co_mutex_t *mutex;
    co_cond_t  *not_full;    // 缓冲区不满时 signal
    co_cond_t  *not_empty;   // 缓冲区不空时 signal
} bounded_buf_t;

static bounded_buf_t g_buf;
static int           g_produced  = 0;   // 生产者发出的值（累加序号）
static int           g_consumed  = 0;   // 消费者收到的值（累加序号）

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

// 入队（缓冲区满时挂起）
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

// 出队（缓冲区空时挂起）
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
// 协程入口
// ============================================================================

// 生产者：生成 1..TOTAL_ITEMS
static void producer(void *arg) {
    (void)arg;
    for (int i = 1; i <= TOTAL_ITEMS; i++) {
        buf_put(&g_buf, i);
        g_produced += i;
    }
}

// 消费者：消费指定数量的元素
static void consumer(void *arg) {
    int n = *(int *)arg;
    for (int i = 0; i < n; i++) {
        int val = buf_get(&g_buf);
        g_consumed += val;
    }
}

// ============================================================================
// 测试用例
// ============================================================================

void setUp(void) {
    buf_init(&g_buf);
    g_produced = 0;
    g_consumed = 0;
}

void tearDown(void) {
    buf_destroy(&g_buf);
}

// 基本：1 生产者 + 1 消费者
void test_producer_consumer_basic(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);

    int items = TOTAL_ITEMS;
    co_spawn(sched, producer,  NULL,   0);
    co_spawn(sched, consumer,  &items, 0);

    co_scheduler_run(sched);
    co_scheduler_destroy(sched);

    // 生产和消费的值之和相等（1+2+...+12 = 78）
    TEST_ASSERT_EQUAL_INT(78, g_produced);
    TEST_ASSERT_EQUAL_INT(78, g_consumed);
    TEST_ASSERT_EQUAL_INT(0,  g_buf.count);   // 缓冲区最终为空
}

// 多消费者：1 生产者 + 2 消费者
void test_producer_consumer_multi_consumer(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);

    // 两个消费者各消费一半
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

// 缓冲区满时生产者正确挂起、消费者唤醒后继续
void test_producer_blocks_when_full(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);

    // 生产 BUF_CAP+2 个元素，缓冲区只能放 BUF_CAP 个，必须有挂起
    int extra = BUF_CAP + 2;
    co_spawn(sched, producer,  NULL,   0);
    co_spawn(sched, consumer,  &extra, 0);

    // 剩余 TOTAL_ITEMS - extra 个交给第二个消费者
    int rest = TOTAL_ITEMS - extra;
    co_spawn(sched, consumer,  &rest,  0);

    co_scheduler_run(sched);
    co_scheduler_destroy(sched);

    TEST_ASSERT_EQUAL_INT(78, g_produced);
    TEST_ASSERT_EQUAL_INT(78, g_consumed);
    TEST_ASSERT_EQUAL_INT(0,  g_buf.count);
}

// ============================================================================
// 测试入口
// ============================================================================

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_producer_consumer_basic);
    RUN_TEST(test_producer_consumer_multi_consumer);
    RUN_TEST(test_producer_blocks_when_full);
    return UNITY_END();
}
