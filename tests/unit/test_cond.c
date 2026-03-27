/**
 * @file test_cond.c
 * @brief 协程条件变量单元测试
 */

#include "unity.h"
#include <libco/co.h>
#include <libco/co_sync.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// 测试辅助
// ============================================================================

static int g_order[16];
static int g_order_idx = 0;

void setUp(void) {
    g_order_idx = 0;
    memset(g_order, 0, sizeof(g_order));
}

void tearDown(void) {}

// ============================================================================
// 测试：signal 唤醒单个等待者
// ============================================================================

typedef struct {
    co_mutex_t *mutex;
    co_cond_t  *cond;
    int        *flag;
} cond_arg_t;

static void coroutine_waiter(void *arg) {
    cond_arg_t *a = (cond_arg_t *)arg;
    co_mutex_lock(a->mutex);
    while (*(a->flag) == 0) {
        co_cond_wait(a->cond, a->mutex);  // 释放锁并挂起
    }
    g_order[g_order_idx++] = 1;           // 被唤醒
    co_mutex_unlock(a->mutex);
}

static void coroutine_signaler(void *arg) {
    cond_arg_t *a = (cond_arg_t *)arg;
    co_yield();                            // 先让 waiter 阻塞

    co_mutex_lock(a->mutex);
    *(a->flag) = 1;
    co_cond_signal(a->cond);
    co_mutex_unlock(a->mutex);
}

void test_cond_signal_wakes_one(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    co_mutex_t *mutex = co_mutex_create(NULL);
    co_cond_t  *cond  = co_cond_create(NULL);
    int flag = 0;

    cond_arg_t arg = {mutex, cond, &flag};

    co_spawn(sched, coroutine_waiter,   &arg, 0);
    co_spawn(sched, coroutine_signaler, &arg, 0);

    co_scheduler_run(sched);

    TEST_ASSERT_EQUAL_INT(1, g_order_idx);   // waiter 被唤醒一次
    TEST_ASSERT_EQUAL_INT(1, g_order[0]);

    co_cond_destroy(cond);
    co_mutex_destroy(mutex);
    co_scheduler_destroy(sched);
}

// ============================================================================
// 测试：broadcast 唤醒多个等待者
// ============================================================================

static void coroutine_waiter_multi(void *arg) {
    cond_arg_t *a = (cond_arg_t *)arg;
    co_mutex_lock(a->mutex);
    while (*(a->flag) == 0) {
        co_cond_wait(a->cond, a->mutex);
    }
    g_order[g_order_idx++] = 1;
    co_mutex_unlock(a->mutex);
}

static void coroutine_broadcaster(void *arg) {
    cond_arg_t *a = (cond_arg_t *)arg;
    co_yield();
    co_yield();   // 等两个 waiter 都挂起

    co_mutex_lock(a->mutex);
    *(a->flag) = 1;
    co_cond_broadcast(a->cond);
    co_mutex_unlock(a->mutex);
}

void test_cond_broadcast_wakes_all(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    co_mutex_t *mutex = co_mutex_create(NULL);
    co_cond_t  *cond  = co_cond_create(NULL);
    int flag = 0;

    cond_arg_t arg = {mutex, cond, &flag};

    co_spawn(sched, coroutine_waiter_multi, &arg, 0);
    co_spawn(sched, coroutine_waiter_multi, &arg, 0);
    co_spawn(sched, coroutine_broadcaster,  &arg, 0);

    co_scheduler_run(sched);

    TEST_ASSERT_EQUAL_INT(2, g_order_idx);  // 两个 waiter 都被唤醒

    co_cond_destroy(cond);
    co_mutex_destroy(mutex);
    co_scheduler_destroy(sched);
}

// ============================================================================
// 测试：signal 无等待者时不崩溃
// ============================================================================

void test_cond_signal_no_waiter(void) {
    co_cond_t *cond = co_cond_create(NULL);
    TEST_ASSERT_EQUAL_INT(CO_OK, co_cond_signal(cond));   // 应静默返回
    TEST_ASSERT_EQUAL_INT(CO_OK, co_cond_broadcast(cond));
    co_cond_destroy(cond);
}

// ============================================================================
// 测试：timedwait 超时后返回 CO_ERROR_TIMEOUT
// ============================================================================

typedef struct {
    co_mutex_t *mutex;
    co_cond_t  *cond;
    co_error_t  result;
} timedwait_arg_t;

static void coroutine_timedwait_timeout(void *arg) {
    timedwait_arg_t *a = (timedwait_arg_t *)arg;
    co_mutex_lock(a->mutex);
    // 等待 50ms，没有人会 signal，预期超时
    a->result = co_cond_timedwait(a->cond, a->mutex, 50);
    co_mutex_unlock(a->mutex);
}

void test_cond_timedwait_timeout(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    co_mutex_t *mutex = co_mutex_create(NULL);
    co_cond_t  *cond  = co_cond_create(NULL);
    timedwait_arg_t arg = {mutex, cond, CO_OK};

    co_spawn(sched, coroutine_timedwait_timeout, &arg, 0);
    co_scheduler_run(sched);

    TEST_ASSERT_EQUAL_INT(CO_ERROR_TIMEOUT, arg.result);

    co_cond_destroy(cond);
    co_mutex_destroy(mutex);
    co_scheduler_destroy(sched);
}

// ============================================================================
// 测试：timedwait 在超时前被 signal，返回 CO_OK
// ============================================================================

typedef struct {
    co_mutex_t *mutex;
    co_cond_t  *cond;
    co_error_t  result;
    int         signaled;
} timedwait_signal_arg_t;

static void coroutine_timedwait_waiter(void *arg) {
    timedwait_signal_arg_t *a = (timedwait_signal_arg_t *)arg;
    co_mutex_lock(a->mutex);
    // 等待最多 500ms，但 signaler 会在短时间内唤醒它
    a->result = co_cond_timedwait(a->cond, a->mutex, 500);
    a->signaled = 1;
    co_mutex_unlock(a->mutex);
}

static void coroutine_timedwait_signaler(void *arg) {
    timedwait_signal_arg_t *a = (timedwait_signal_arg_t *)arg;
    co_sleep(10);  // 短暂休眠后 signal

    co_mutex_lock(a->mutex);
    co_cond_signal(a->cond);
    co_mutex_unlock(a->mutex);
}

void test_cond_timedwait_signaled_before_timeout(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    co_mutex_t *mutex = co_mutex_create(NULL);
    co_cond_t  *cond  = co_cond_create(NULL);
    timedwait_signal_arg_t arg = {mutex, cond, CO_ERROR_TIMEOUT, 0};

    co_spawn(sched, coroutine_timedwait_waiter,   &arg, 0);
    co_spawn(sched, coroutine_timedwait_signaler, &arg, 0);
    co_scheduler_run(sched);

    TEST_ASSERT_EQUAL_INT(CO_OK, arg.result);   // 被 signal 唤醒，非超时
    TEST_ASSERT_EQUAL_INT(1, arg.signaled);

    co_cond_destroy(cond);
    co_mutex_destroy(mutex);
    co_scheduler_destroy(sched);
}

// ============================================================================
// 测试：两个 timedwait，一个超时、一个被 signal，验证队列不互相干扰
// ============================================================================

typedef struct {
    co_mutex_t *mutex;
    co_cond_t  *cond;
    co_error_t  result_a;   // 第一个 waiter（超时）
    co_error_t  result_b;   // 第二个 waiter（被 signal）
} two_timedwait_arg_t;

static void coroutine_timedwait_short(void *arg) {
    two_timedwait_arg_t *a = (two_timedwait_arg_t *)arg;
    co_mutex_lock(a->mutex);
    a->result_a = co_cond_timedwait(a->cond, a->mutex, 30); // 30ms 超时
    co_mutex_unlock(a->mutex);
}

static void coroutine_timedwait_long(void *arg) {
    two_timedwait_arg_t *a = (two_timedwait_arg_t *)arg;
    co_mutex_lock(a->mutex);
    a->result_b = co_cond_timedwait(a->cond, a->mutex, 500); // 500ms，应被 signal 唤醒
    co_mutex_unlock(a->mutex);
}

static void coroutine_signal_after_short_timeout(void *arg) {
    two_timedwait_arg_t *a = (two_timedwait_arg_t *)arg;
    // 等 50ms（超过 short 的 30ms 超时），此时 short 已超时
    co_sleep(50);
    // signal：此时只有 long waiter 在队列中
    co_mutex_lock(a->mutex);
    co_cond_signal(a->cond);
    co_mutex_unlock(a->mutex);
}

void test_cond_two_timedwait_independent(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    co_mutex_t *mutex = co_mutex_create(NULL);
    co_cond_t  *cond  = co_cond_create(NULL);
    two_timedwait_arg_t arg = {mutex, cond, CO_OK, CO_OK};

    co_spawn(sched, coroutine_timedwait_short,          &arg, 0);
    co_spawn(sched, coroutine_timedwait_long,           &arg, 0);
    co_spawn(sched, coroutine_signal_after_short_timeout, &arg, 0);

    co_scheduler_run(sched);

    // short waiter 应超时
    TEST_ASSERT_EQUAL_INT(CO_ERROR_TIMEOUT, arg.result_a);
    // long waiter 应被 signal 唤醒
    TEST_ASSERT_EQUAL_INT(CO_OK, arg.result_b);

    co_cond_destroy(cond);
    co_mutex_destroy(mutex);
    co_scheduler_destroy(sched);
}

// ============================================================================
// 测试入口
// ============================================================================

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_cond_signal_wakes_one);
    RUN_TEST(test_cond_broadcast_wakes_all);
    RUN_TEST(test_cond_signal_no_waiter);
    RUN_TEST(test_cond_timedwait_timeout);
    RUN_TEST(test_cond_timedwait_signaled_before_timeout);
    RUN_TEST(test_cond_two_timedwait_independent);
    return UNITY_END();
}
