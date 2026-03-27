/**
 * @file test_cond.c
 * @brief Coroutine condition variable unit tests
 */

#include "unity.h"
#include <libco/co.h>
#include <libco/co_sync.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Test helpers
// ============================================================================

static int g_order[16];
static int g_order_idx = 0;

void setUp(void) {
    g_order_idx = 0;
    memset(g_order, 0, sizeof(g_order));
}

void tearDown(void) {}

// ============================================================================
// Test: signal wakes one waiter
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
        co_cond_wait(a->cond, a->mutex);  // Release the lock and suspend
    }
    g_order[g_order_idx++] = 1;           // Resumed
    co_mutex_unlock(a->mutex);
}

static void coroutine_signaler(void *arg) {
    cond_arg_t *a = (cond_arg_t *)arg;
    co_yield();                            // Let the waiter block first

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

    TEST_ASSERT_EQUAL_INT(1, g_order_idx);   // The waiter should wake exactly once
    TEST_ASSERT_EQUAL_INT(1, g_order[0]);

    co_cond_destroy(cond);
    co_mutex_destroy(mutex);
    co_scheduler_destroy(sched);
}

// ============================================================================
// Test: broadcast wakes multiple waiters
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
    co_yield();   // Wait until both waiters are suspended

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

    TEST_ASSERT_EQUAL_INT(2, g_order_idx);  // Both waiters should be resumed

    co_cond_destroy(cond);
    co_mutex_destroy(mutex);
    co_scheduler_destroy(sched);
}

// ============================================================================
// Test: signal is safe when there are no waiters
// ============================================================================

void test_cond_signal_no_waiter(void) {
    co_cond_t *cond = co_cond_create(NULL);
    TEST_ASSERT_EQUAL_INT(CO_OK, co_cond_signal(cond));   // Should return quietly
    TEST_ASSERT_EQUAL_INT(CO_OK, co_cond_broadcast(cond));
    co_cond_destroy(cond);
}

// ============================================================================
// Test: timedwait returns CO_ERROR_TIMEOUT after timing out
// ============================================================================

typedef struct {
    co_mutex_t *mutex;
    co_cond_t  *cond;
    co_error_t  result;
} timedwait_arg_t;

static void coroutine_timedwait_timeout(void *arg) {
    timedwait_arg_t *a = (timedwait_arg_t *)arg;
    co_mutex_lock(a->mutex);
    // Wait 50 ms with no signaler; timeout is expected
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
// Test: timedwait is signaled before timeout and returns CO_OK
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
    // Wait up to 500 ms, but the signaler should wake it much sooner
    a->result = co_cond_timedwait(a->cond, a->mutex, 500);
    a->signaled = 1;
    co_mutex_unlock(a->mutex);
}

static void coroutine_timedwait_signaler(void *arg) {
    timedwait_signal_arg_t *a = (timedwait_signal_arg_t *)arg;
    co_sleep(10);  // Sleep briefly before signaling

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

    TEST_ASSERT_EQUAL_INT(CO_OK, arg.result);   // Signaled rather than timed out
    TEST_ASSERT_EQUAL_INT(1, arg.signaled);

    co_cond_destroy(cond);
    co_mutex_destroy(mutex);
    co_scheduler_destroy(sched);
}

// ============================================================================
// Test: two timedwait calls, one timing out and one signaled, without queue interference
// ============================================================================

typedef struct {
    co_mutex_t *mutex;
    co_cond_t  *cond;
    co_error_t  result_a;   // First waiter, expected to time out
    co_error_t  result_b;   // Second waiter, expected to be signaled
} two_timedwait_arg_t;

static void coroutine_timedwait_short(void *arg) {
    two_timedwait_arg_t *a = (two_timedwait_arg_t *)arg;
    co_mutex_lock(a->mutex);
    a->result_a = co_cond_timedwait(a->cond, a->mutex, 30); // 30 ms timeout
    co_mutex_unlock(a->mutex);
}

static void coroutine_timedwait_long(void *arg) {
    two_timedwait_arg_t *a = (two_timedwait_arg_t *)arg;
    co_mutex_lock(a->mutex);
    a->result_b = co_cond_timedwait(a->cond, a->mutex, 500); // 500 ms, should be signaled
    co_mutex_unlock(a->mutex);
}

static void coroutine_signal_after_short_timeout(void *arg) {
    two_timedwait_arg_t *a = (two_timedwait_arg_t *)arg;
    // Wait 50 ms, which exceeds the 30 ms short timeout
    co_sleep(50);
    // Signal now; only the long waiter should remain in the queue
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

    // The short waiter should time out
    TEST_ASSERT_EQUAL_INT(CO_ERROR_TIMEOUT, arg.result_a);
    // The long waiter should be resumed by signal
    TEST_ASSERT_EQUAL_INT(CO_OK, arg.result_b);

    co_cond_destroy(cond);
    co_mutex_destroy(mutex);
    co_scheduler_destroy(sched);
}

// ============================================================================
// Test entry
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
