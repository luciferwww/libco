/**
 * @file test_epoll_timeout.c
 * @brief Linux epoll I/O 超时机制测试
 *
 * 测试 co_read 的 timeout_ms 参数在 epoll 后端下的正确性。
 * 核心逻辑位于 iomux_epoll.c 的 co_wait_io()。
 *
 * 测试场景：
 *   1. 超时路径：服务端接受连接后不发送任何数据，客户端 co_read 应在
 *      timeout_ms 内超时并返回错误码 ETIMEDOUT。
 *   2. 正常路径：服务端发送数据后，客户端 co_read 应在超时前成功读取。
 *
 * 仅在 Linux（非 _WIN32）下编译完整测试；Windows 平台打印跳过提示后退出。
 */

#ifndef _WIN32

#include "unity.h"
#include <libco/co.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// ============================================================================
// 全局测试状态（在协程之间传递结果）
// ============================================================================

static co_socket_t g_listen_fd   = -1;
static co_socket_t g_client_fd   = -1;
static co_socket_t g_accepted_fd = -1;
static int         g_server_port = 0;

/* 由客户端协程写入，由测试主函数读取 */
static ssize_t g_read_result = 0;
static int     g_errno_saved = 0;
static int     g_elapsed_ms  = 0;   /**< co_read 实际等待毫秒数 */

// ============================================================================
// Unity setUp / tearDown
// ============================================================================

void setUp(void) {
    g_listen_fd   = -1;
    g_client_fd   = -1;
    g_accepted_fd = -1;
    g_server_port = 0;
    g_read_result = 0;
    g_errno_saved = 0;
    g_elapsed_ms  = 0;
}

void tearDown(void) {
    /* 关闭仍然存活的套接字（正常流程中协程已关闭，这里仅作安全网） */
    printf("[TEARDOWN] Cleaning up resources\n");
    if (g_listen_fd >= 0) {
        printf("[TEARDOWN] Closing listen_fd=%d\n", g_listen_fd);
        close(g_listen_fd);
        g_listen_fd = -1;
    }
    if (g_client_fd >= 0) {
        printf("[TEARDOWN] Closing client_fd=%d\n", g_client_fd);
        close(g_client_fd);
        g_client_fd = -1;
    }
    if (g_accepted_fd >= 0) {
        printf("[TEARDOWN] Closing accepted_fd=%d\n", g_accepted_fd);
        close(g_accepted_fd);
        g_accepted_fd = -1;
    }
    printf("[TEARDOWN] tearDown finished\n");
}

// ============================================================================
// 测试辅助：创建监听套接字，返回 OS 分配的端口
// ============================================================================

static co_socket_t create_listen_socket(int *out_port) {
    co_socket_t fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) return -1;

    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void *)&reuse, sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = 0;   /* 让 OS 选择空闲端口 */

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(fd, 5) != 0) {
        close(fd);
        return -1;
    }

    socklen_t len = sizeof(addr);
    getsockname(fd, (struct sockaddr *)&addr, &len);
    *out_port = ntohs(addr.sin_port);
    return fd;
}

// ============================================================================
// 测试 1：co_read 超时
// 服务端接受连接后保持沉默，客户端的 co_read 应在 100ms 超时后返回错误。
// ============================================================================

/**
 * 服务端协程：接受一个连接，然后静默等待 200ms（不发送任何数据）。
 * 200ms > 客户端 100ms 超时，确保连接在客户端超时期间保持存活，
 * 避免 TCP RST/FIN 先于超时抵达导致 errno 变成连接错误而非 ETIMEDOUT。
 */
static void coro_server_hold_connection(void *arg) {
    printf("[SERVER] Starting server coroutine\n");
    (void)arg;
    struct sockaddr_in cli_addr;
    socklen_t cli_len = sizeof(cli_addr);

    printf("[SERVER] Calling co_accept...\n");
    g_accepted_fd = co_accept(g_listen_fd, &cli_addr, &cli_len, -1);
    if (g_accepted_fd == CO_INVALID_SOCKET) {
        printf("[SERVER] co_accept failed\n");
        return;
    }
    printf("[SERVER] co_accept succeeded, fd=%d\n", g_accepted_fd);

    /* 保持连接，让客户端的读超时（100ms）能在此期间触发 */
    printf("[SERVER] Sleeping for 200ms...\n");
    co_sleep(200);
    printf("[SERVER] Sleep finished, closing connection\n");

    close(g_accepted_fd);
    g_accepted_fd = -1;
    printf("[SERVER] Server coroutine finished\n");
}

/**
 * 客户端协程：连接到服务端，然后以 100ms 超时调用 co_read。
 * 记录返回值、errno 及实际等待时长。
 */
static void coro_client_read_timeout(void *arg) {
    printf("[CLIENT] Starting client coroutine\n");
    (void)arg;

    g_client_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_client_fd < 0) {
        printf("[CLIENT] Failed to create socket\n");
        return;
    }
    printf("[CLIENT] Socket created, fd=%d\n", g_client_fd);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((uint16_t)g_server_port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    printf("[CLIENT] Calling co_connect...\n");
    if (co_connect(g_client_fd, &addr, sizeof(addr), 2000) != 0) {
        printf("[CLIENT] co_connect failed\n");
        return;
    }
    printf("[CLIENT] co_connect succeeded\n");

    /* 精确计时 */
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    printf("[CLIENT] Calling co_read with 100ms timeout...\n");
    char buf[256];
    g_read_result = co_read(g_client_fd, buf, sizeof(buf), 100 /* ms */);
    g_errno_saved = errno;

    clock_gettime(CLOCK_MONOTONIC, &t1);
    g_elapsed_ms = (int)((t1.tv_sec - t0.tv_sec) * 1000 + 
                         (t1.tv_nsec - t0.tv_nsec) / 1000000);
    printf("[CLIENT] co_read returned %zd, errno=%d, elapsed=%dms\n",
           g_read_result, g_errno_saved, g_elapsed_ms);
    
    printf("[CLIENT] Closing client socket\n");
    close(g_client_fd);
    g_client_fd = -1;
    printf("[CLIENT] Client coroutine finished\n");
}

void test_epoll_read_times_out(void) {
    printf("\n[TEST] Starting test_epoll_read_times_out\n");
    
    co_scheduler_t *sched = co_scheduler_create(NULL);
    TEST_ASSERT_NOT_NULL(sched);
    printf("[TEST] Scheduler created\n");

    g_listen_fd = create_listen_socket(&g_server_port);
    TEST_ASSERT_TRUE(g_listen_fd >= 0);
    printf("[TEST] Listen socket created on port %d\n", g_server_port);

    co_spawn(sched, coro_server_hold_connection, NULL, 0);
    co_spawn(sched, coro_client_read_timeout,    NULL, 0);
    printf("[TEST] Coroutines spawned, running scheduler\n");

    co_error_t err = co_scheduler_run(sched);
    printf("[TEST] Scheduler finished with error: %d\n", err);
    TEST_ASSERT_EQUAL_INT(CO_OK, err);
    co_scheduler_destroy(sched);

    close(g_listen_fd);
    g_listen_fd = -1;

    /* ---- 断言 ---- */
    printf("[TEST] Checking results: read_result=%zd, errno=%d, elapsed=%dms\n",
           g_read_result, g_errno_saved, g_elapsed_ms);

    /* co_read 超时应返回 -1 */
    TEST_ASSERT_TRUE(g_read_result < 0);

    /* errno 应为 ETIMEDOUT */
    TEST_ASSERT_EQUAL_INT(ETIMEDOUT, g_errno_saved);

    /* 实际等待时长应约为 100ms（60ms ~ 400ms，留宽以容纳 CI 调度抖动） */
    TEST_ASSERT_GREATER_OR_EQUAL_INT(60,  g_elapsed_ms);
    TEST_ASSERT_LESS_THAN_INT(400, g_elapsed_ms);
    
    printf("[TEST] test_epoll_read_times_out PASSED\n");
}

// ============================================================================
// 测试 2：数据在超时前到达，co_read 应正常返回读取字节数
// ============================================================================

static const char k_payload[] = "hello";  /* 5 字节 */

/**
 * 服务端协程：接受连接后立即发送 "hello"，然后关闭连接。
 */
static void coro_server_send_data(void *arg) {
    printf("[SERVER2] Starting server send data coroutine\n");
    (void)arg;
    struct sockaddr_in cli_addr;
    socklen_t cli_len = sizeof(cli_addr);

    printf("[SERVER2] Calling co_accept...\n");
    g_accepted_fd = co_accept(g_listen_fd, &cli_addr, &cli_len, -1);
    if (g_accepted_fd == CO_INVALID_SOCKET) {
        printf("[SERVER2] co_accept failed\n");
        return;
    }
    printf("[SERVER2] co_accept succeeded, sending data\n");

    co_write(g_accepted_fd, k_payload, sizeof(k_payload) - 1, -1);
    printf("[SERVER2] Data sent, closing connection\n");

    close(g_accepted_fd);
    g_accepted_fd = -1;
    printf("[SERVER2] Server send data coroutine finished\n");
}

/**
 * 客户端协程：连接到服务端，以 500ms 超时调用 co_read。
 * 服务端发送数据的时间远早于 500ms，co_read 应成功读取。
 */
static void coro_client_read_success(void *arg) {
    printf("[CLIENT2] Starting client read success coroutine\n");
    (void)arg;

    g_client_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_client_fd < 0) {
        printf("[CLIENT2] Failed to create socket\n");
        return;
    }
    printf("[CLIENT2] Socket created\n");

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((uint16_t)g_server_port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    printf("[CLIENT2] Calling co_connect...\n");
    if (co_connect(g_client_fd, &addr, sizeof(addr), 2000) != 0) {
        printf("[CLIENT2] co_connect failed\n");
        return;
    }
    printf("[CLIENT2] co_connect succeeded, reading data\n");

    char buf[64] = {0};
    g_read_result = co_read(g_client_fd, buf, sizeof(buf), 500 /* ms */);
    g_errno_saved = errno;
    printf("[CLIENT2] co_read returned %zd, errno=%d\n",
           g_read_result, g_errno_saved);
    
    printf("[CLIENT2] Closing client socket\n");
    close(g_client_fd);
    g_client_fd = -1;
    printf("[CLIENT2] Client read success coroutine finished\n");
}

void test_epoll_read_succeeds_before_timeout(void) {
    printf("\n[TEST] Starting test_epoll_read_succeeds_before_timeout\n");
    
    co_scheduler_t *sched = co_scheduler_create(NULL);
    TEST_ASSERT_NOT_NULL(sched);
    printf("[TEST] Scheduler created\n");

    g_listen_fd = create_listen_socket(&g_server_port);
    TEST_ASSERT_TRUE(g_listen_fd >= 0);
    printf("[TEST] Listen socket created on port %d\n", g_server_port);

    co_spawn(sched, coro_server_send_data,    NULL, 0);
    co_spawn(sched, coro_client_read_success, NULL, 0);
    printf("[TEST] Coroutines spawned, running scheduler\n");

    co_error_t err = co_scheduler_run(sched);
    printf("[TEST] Scheduler finished with error: %d\n", err);
    TEST_ASSERT_EQUAL_INT(CO_OK, err);
    co_scheduler_destroy(sched);

    close(g_listen_fd);
    g_listen_fd = -1;

    /* ---- 断言 ---- */
    printf("[TEST] Checking results: read_result=%zd\n", g_read_result);

    /* co_read 应成功返回 5 字节（"hello"） */
    TEST_ASSERT_EQUAL_INT(5, (int)g_read_result);
    
    printf("[TEST] test_epoll_read_succeeds_before_timeout PASSED\n");
}

// ============================================================================
// main
// ============================================================================

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_epoll_read_times_out);
    RUN_TEST(test_epoll_read_succeeds_before_timeout);
    return UNITY_END();
}

#else  /* _WIN32 */

#include <stdio.h>

int main(void) {
    printf("epoll timeout tests are Linux-only, skipping.\n");
    return 0;
}

#endif /* !_WIN32 */
