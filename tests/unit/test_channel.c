/**
 * @file test_channel.c
 * @brief Coroutine channel unit tests
 *
 * Coverage:
 *   1. Buffered channel: basic send/recv
 *   2. Buffered channel: send blocks when the buffer is full
 *   3. Buffered channel: FIFO ordering
 *   4. Unbuffered channel: rendezvous send/recv handshake
 *   5. Closed channel: recv drains buffered data and then returns CLOSED
 *   6. Non-blocking trysend / tryrecv
 */

#include "unity.h"
#include <libco/co.h>
#include <libco/co_sync.h>
#include <string.h>

// ============================================================================
// Helpers
// ============================================================================

static int g_result[32];
static int g_result_idx = 0;

void setUp(void) {
    g_result_idx = 0;
    memset(g_result, 0, sizeof(g_result));
}

void tearDown(void) {}

// ============================================================================
// Test 1: buffered channel basic send / recv
// ============================================================================

typedef struct {
    co_channel_t *ch;
    int           val;
} ch_arg_t;

static void coroutine_sender(void *arg) {
    ch_arg_t *a = (ch_arg_t *)arg;
    co_channel_send(a->ch, &a->val);
}

static void coroutine_receiver(void *arg) {
    ch_arg_t *a = (ch_arg_t *)arg;
    int out = 0;
    co_channel_recv(a->ch, &out);
    g_result[g_result_idx++] = out;
}

void test_channel_buffered_basic(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    co_channel_t   *ch    = co_channel_create(sizeof(int), 4);

    ch_arg_t send_arg = {ch, 42};
    ch_arg_t recv_arg = {ch, 0};

    co_spawn(sched, coroutine_sender,   &send_arg, 0);
    co_spawn(sched, coroutine_receiver, &recv_arg, 0);

    co_scheduler_run(sched);

    TEST_ASSERT_EQUAL_INT(1,  g_result_idx);
    TEST_ASSERT_EQUAL_INT(42, g_result[0]);
    TEST_ASSERT_EQUAL_INT(0,  (int)co_channel_len(ch));

    co_channel_destroy(ch);
    co_scheduler_destroy(sched);
}

// ============================================================================
// Test 2: send blocks when the buffer is full and resumes after recv wakes it
// ============================================================================

static co_channel_t *g_ch = NULL;

static void sender_many(void *arg) {
    int n = *(int *)arg;
    for (int i = 1; i <= n; i++) {
        co_channel_send(g_ch, &i);
    }
}

static void receiver_many(void *arg) {
    int n = *(int *)arg;
    for (int i = 0; i < n; i++) {
        int val = 0;
        co_channel_recv(g_ch, &val);
        g_result[g_result_idx++] = val;
    }
}

void test_channel_buffered_blocks_when_full(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    g_ch = co_channel_create(sizeof(int), 2);  /* Capacity is only 2 */

    int n = 6;
    co_spawn(sched, sender_many,   &n, 0);
    co_spawn(sched, receiver_many, &n, 0);

    co_scheduler_run(sched);

    TEST_ASSERT_EQUAL_INT(6, g_result_idx);
    /* Verify the received sum: 1+2+3+4+5+6 = 21 */
    int sum = 0;
    for (int i = 0; i < 6; i++) sum += g_result[i];
    TEST_ASSERT_EQUAL_INT(21, sum);

    co_channel_destroy(g_ch);
    g_ch = NULL;
    co_scheduler_destroy(sched);
}

// ============================================================================
// Test 3: FIFO ordering
// ============================================================================

void test_channel_fifo_order(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    g_ch = co_channel_create(sizeof(int), 8);

    /* Send 1 through 5 into the buffer first; capacity is sufficient */
    int vals[5] = {1, 2, 3, 4, 5};
    ch_arg_t args[5];
    for (int i = 0; i < 5; i++) {
        args[i].ch  = g_ch;
        args[i].val = vals[i];
        co_spawn(sched, coroutine_sender, &args[i], 0);
    }

    int n = 5;
    co_spawn(sched, receiver_many, &n, 0);

    co_scheduler_run(sched);

    TEST_ASSERT_EQUAL_INT(5, g_result_idx);
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_INT(i + 1, g_result[i]);
    }

    co_channel_destroy(g_ch);
    g_ch = NULL;
    co_scheduler_destroy(sched);
}

// ============================================================================
// Test 4: unbuffered channel (rendezvous)
// ============================================================================

void test_channel_unbuffered_rendezvous(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    co_channel_t   *ch    = co_channel_create(sizeof(int), 0);

    ch_arg_t send_arg = {ch, 99};
    ch_arg_t recv_arg = {ch, 0};

    /* Spawn the sender first; it should suspend on send until a receiver arrives */
    co_spawn(sched, coroutine_sender,   &send_arg, 0);
    co_spawn(sched, coroutine_receiver, &recv_arg, 0);

    co_scheduler_run(sched);

    TEST_ASSERT_EQUAL_INT(1,  g_result_idx);
    TEST_ASSERT_EQUAL_INT(99, g_result[0]);

    co_channel_destroy(ch);
    co_scheduler_destroy(sched);
}

// ============================================================================
// Test 5: recv drains a closed channel before returning CLOSED
// ============================================================================

static void producer_then_close(void *arg) {
    co_channel_t *ch = (co_channel_t *)arg;
    int val = 7;
    co_channel_send(ch, &val);
    co_channel_close(ch);
}

static void drain_until_closed(void *arg) {
    co_channel_t *ch = (co_channel_t *)arg;
    int val = 0;
    co_error_t ret;
    while ((ret = co_channel_recv(ch, &val)) == CO_OK) {
        g_result[g_result_idx++] = val;
    }
    /* The final receive should return CO_ERROR_CLOSED */
    g_result[g_result_idx++] = (ret == CO_ERROR_CLOSED) ? -1 : -2;
}

void test_channel_close_drains_then_closed(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    co_channel_t   *ch    = co_channel_create(sizeof(int), 4);

    co_spawn(sched, producer_then_close, ch, 0);
    co_spawn(sched, drain_until_closed,  ch, 0);

    co_scheduler_run(sched);

    /* Expect one value (7), then -1 to mark CLOSED */
    TEST_ASSERT_EQUAL_INT(2,  g_result_idx);
    TEST_ASSERT_EQUAL_INT(7,  g_result[0]);
    TEST_ASSERT_EQUAL_INT(-1, g_result[1]);

    co_channel_destroy(ch);
    co_scheduler_destroy(sched);
}

// ============================================================================
// Test 6: non-blocking trysend / tryrecv
// ============================================================================

void test_channel_trysend_tryrecv(void) {
    co_channel_t *ch = co_channel_create(sizeof(int), 2);

    int val = 10;
    /* trysend should succeed on an empty channel */
    TEST_ASSERT_EQUAL_INT(CO_OK,       co_channel_trysend(ch, &val));
    val = 20;
    TEST_ASSERT_EQUAL_INT(CO_OK,       co_channel_trysend(ch, &val));
    /* Once full, trysend should return BUSY */
    val = 30;
    TEST_ASSERT_EQUAL_INT(CO_ERROR_BUSY, co_channel_trysend(ch, &val));

    /* tryrecv should succeed while items are available */
    int out = 0;
    TEST_ASSERT_EQUAL_INT(CO_OK,       co_channel_tryrecv(ch, &out));
    TEST_ASSERT_EQUAL_INT(10, out);
    TEST_ASSERT_EQUAL_INT(CO_OK,       co_channel_tryrecv(ch, &out));
    TEST_ASSERT_EQUAL_INT(20, out);
    /* Once empty, tryrecv should return BUSY */
    TEST_ASSERT_EQUAL_INT(CO_ERROR_BUSY, co_channel_tryrecv(ch, &out));

    co_channel_destroy(ch);
}

// ============================================================================
// Test entry
// ============================================================================

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_channel_buffered_basic);
    RUN_TEST(test_channel_buffered_blocks_when_full);
    RUN_TEST(test_channel_fifo_order);
    RUN_TEST(test_channel_unbuffered_rendezvous);
    RUN_TEST(test_channel_close_drains_then_closed);
    RUN_TEST(test_channel_trysend_tryrecv);
    return UNITY_END();
}
