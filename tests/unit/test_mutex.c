/**
 * @file test_mutex.c
 * @brief 协程互斥锁单元测试
 */

#include "unity.h"
#include <libco/co.h>
#include <libco/co_sync.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// 测试辅助
// ============================================================================

static int g_counter = 0;
static int g_order[16];
static int g_order_idx = 0;

void setUp(void) {
    g_counter = 0;
    g_order_idx = 0;
    memset(g_order, 0, sizeof(g_order));
}

void tearDown(void) {}

// ============================================================================
// 测试：基本加锁/解锁
// ============================================================================

typedef struct {
    co_mutex_t *mutex;
    int id;
} mutex_arg_t;

static void coroutine_lock_unlock(void *arg) {
    mutex_arg_t *a = (mutex_arg_t *)arg;
    co_mutex_lock(a->mutex);
    g_order[g_order_idx++] = a->id;  // 记录进入临界区的顺序
    g_counter++;
    co_yield();                        // 持锁让出，验证其他协程被阻塞
    g_counter++;
    co_mutex_unlock(a->mutex);
}

void test_mutex_basic_lock_unlock(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    co_mutex_t *mutex = co_mutex_create(NULL);

    mutex_arg_t a1 = {mutex, 1};
    mutex_arg_t a2 = {mutex, 2};

    co_spawn(sched, coroutine_lock_unlock, &a1, 0);
    co_spawn(sched, coroutine_lock_unlock, &a2, 0);

    co_scheduler_run(sched);

    // 每个协程在临界区执行两次 ++，共 4 次
    TEST_ASSERT_EQUAL_INT(4, g_counter);

    co_mutex_destroy(mutex);
    co_scheduler_destroy(sched);
}

// ============================================================================
// 测试：FIFO 等待顺序
// ============================================================================

static void coroutine_fifo(void *arg) {
    mutex_arg_t *a = (mutex_arg_t *)arg;
    co_mutex_lock(a->mutex);
    g_order[g_order_idx++] = a->id;
    co_mutex_unlock(a->mutex);
}

void test_mutex_fifo_order(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    co_mutex_t *mutex = co_mutex_create(NULL);

    // 先手动锁住，spawn 三个协程，它们会按顺序排队
    co_mutex_lock(mutex);

    mutex_arg_t a1 = {mutex, 1};
    mutex_arg_t a2 = {mutex, 2};
    mutex_arg_t a3 = {mutex, 3};

    co_spawn(sched, coroutine_fifo, &a1, 0);
    co_spawn(sched, coroutine_fifo, &a2, 0);
    co_spawn(sched, coroutine_fifo, &a3, 0);

    co_mutex_unlock(mutex);

    co_scheduler_run(sched);

    // 应按创建顺序 1->2->3 进入临界区
    TEST_ASSERT_EQUAL_INT(1, g_order[0]);
    TEST_ASSERT_EQUAL_INT(2, g_order[1]);
    TEST_ASSERT_EQUAL_INT(3, g_order[2]);

    co_mutex_destroy(mutex);
    co_scheduler_destroy(sched);
}

// ============================================================================
// 测试：trylock
// ============================================================================

void test_mutex_trylock(void) {
    co_mutex_t *mutex = co_mutex_create(NULL);

    // 第一次 trylock 应成功
    co_error_t ret = co_mutex_trylock(mutex);
    TEST_ASSERT_EQUAL_INT(CO_OK, ret);

    // 锁被持有，再次 trylock 应返回 CO_ERROR_BUSY
    ret = co_mutex_trylock(mutex);
    TEST_ASSERT_EQUAL_INT(CO_ERROR_BUSY, ret);

    co_mutex_unlock(mutex);

    // 解锁后应可再次 trylock
    ret = co_mutex_trylock(mutex);
    TEST_ASSERT_EQUAL_INT(CO_OK, ret);

    co_mutex_unlock(mutex);
    co_mutex_destroy(mutex);
}

// ============================================================================
// 测试：临界区互斥
// ============================================================================

static co_mutex_t *g_shared_mutex = NULL;
static int g_shared_value = 0;

static void coroutine_critical_section(void *arg) {
    int iters = *(int *)arg;
    for (int i = 0; i < iters; i++) {
        co_mutex_lock(g_shared_mutex);
        int tmp = g_shared_value;
        co_yield();          // 临界区内让出，验证其他协程不能同时修改
        g_shared_value = tmp + 1;
        co_mutex_unlock(g_shared_mutex);
    }
}

void test_mutex_protects_critical_section(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    g_shared_mutex = co_mutex_create(NULL);
    g_shared_value = 0;

    int iters = 5;
    co_spawn(sched, coroutine_critical_section, &iters, 0);
    co_spawn(sched, coroutine_critical_section, &iters, 0);

    co_scheduler_run(sched);

    // 两个协程各执行 5 次 +1，总计 10
    // 如果临界区保护失效（出现 ABA 问题），结果会 < 10
    TEST_ASSERT_EQUAL_INT(10, g_shared_value);

    co_mutex_destroy(g_shared_mutex);
    g_shared_mutex = NULL;
    co_scheduler_destroy(sched);
}

// ============================================================================
// 测试入口
// ============================================================================

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_mutex_basic_lock_unlock);
    RUN_TEST(test_mutex_fifo_order);
    RUN_TEST(test_mutex_trylock);
    RUN_TEST(test_mutex_protects_critical_section);
    return UNITY_END();
}
