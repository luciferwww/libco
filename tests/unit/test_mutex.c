/**
 * @file test_mutex.c
 * @brief Coroutine mutex unit tests
 */

#include "unity.h"
#include <libco/co.h>
#include <libco/co_sync.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Test helpers
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
// Test: basic lock/unlock
// ============================================================================

typedef struct {
    co_mutex_t *mutex;
    int id;
} mutex_arg_t;

static void coroutine_lock_unlock(void *arg) {
    mutex_arg_t *a = (mutex_arg_t *)arg;
    co_mutex_lock(a->mutex);
    g_order[g_order_idx++] = a->id;  // Record critical-section entry order
    g_counter++;
    co_yield();                        // Yield while holding the lock to ensure others block
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

    // Each coroutine increments twice in the critical section, for a total of 4
    TEST_ASSERT_EQUAL_INT(4, g_counter);

    co_mutex_destroy(mutex);
    co_scheduler_destroy(sched);
}

// ============================================================================
// Test: FIFO wait order
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

    // Lock manually first, then spawn three coroutines so they queue in order
    co_mutex_lock(mutex);

    mutex_arg_t a1 = {mutex, 1};
    mutex_arg_t a2 = {mutex, 2};
    mutex_arg_t a3 = {mutex, 3};

    co_spawn(sched, coroutine_fifo, &a1, 0);
    co_spawn(sched, coroutine_fifo, &a2, 0);
    co_spawn(sched, coroutine_fifo, &a3, 0);

    co_mutex_unlock(mutex);

    co_scheduler_run(sched);

    // They should enter the critical section in creation order: 1->2->3
    TEST_ASSERT_EQUAL_INT(1, g_order[0]);
    TEST_ASSERT_EQUAL_INT(2, g_order[1]);
    TEST_ASSERT_EQUAL_INT(3, g_order[2]);

    co_mutex_destroy(mutex);
    co_scheduler_destroy(sched);
}

// ============================================================================
// Test: trylock
// ============================================================================

void test_mutex_trylock(void) {
    co_mutex_t *mutex = co_mutex_create(NULL);

    // The first trylock should succeed
    co_error_t ret = co_mutex_trylock(mutex);
    TEST_ASSERT_EQUAL_INT(CO_OK, ret);

    // Once the lock is held, trylock should return CO_ERROR_BUSY
    ret = co_mutex_trylock(mutex);
    TEST_ASSERT_EQUAL_INT(CO_ERROR_BUSY, ret);

    co_mutex_unlock(mutex);

    // After unlocking, trylock should succeed again
    ret = co_mutex_trylock(mutex);
    TEST_ASSERT_EQUAL_INT(CO_OK, ret);

    co_mutex_unlock(mutex);
    co_mutex_destroy(mutex);
}

// ============================================================================
// Test: critical-section mutual exclusion
// ============================================================================

static co_mutex_t *g_shared_mutex = NULL;
static int g_shared_value = 0;

static void coroutine_critical_section(void *arg) {
    int iters = *(int *)arg;
    for (int i = 0; i < iters; i++) {
        co_mutex_lock(g_shared_mutex);
        int tmp = g_shared_value;
        co_yield();          // Yield inside the critical section to verify mutual exclusion
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

    // Two coroutines each perform five increments, for a total of 10.
    // If critical-section protection fails, the final value would be < 10.
    TEST_ASSERT_EQUAL_INT(10, g_shared_value);

    co_mutex_destroy(g_shared_mutex);
    g_shared_mutex = NULL;
    co_scheduler_destroy(sched);
}

// ============================================================================
// Test entry
// ============================================================================

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_mutex_basic_lock_unlock);
    RUN_TEST(test_mutex_fifo_order);
    RUN_TEST(test_mutex_trylock);
    RUN_TEST(test_mutex_protects_critical_section);
    return UNITY_END();
}
