/**
 * @file test_iocp_timeout.c
 * @brief Windows IOCP I/O 超时机制测试
 *
 * 测试 co_read 的 timeout_ms 参数在 IOCP 后端下的正确性。
 * 核心逻辑（CancelIoEx + 二次 swap）位于 iomux_iocp.c 的 co_wait_io_async()。
 *
 * 测试场景：
 *   1. 超时路径：服务端接受连接后不发送任何数据，客户端 co_read 应在
 *      timeout_ms 内超时并返回错误码 WSAETIMEDOUT。
 *   2. 正常路径：服务端发送数据后，客户端 co_read 应在超时前成功读取。
 *
 * 仅在 Windows（_WIN32）下编译完整测试；其它平台打印跳过提示后退出。
 */

#ifdef _WIN32

#ifndef _WIN32_WINNT
#  define _WIN32_WINNT 0x0600   /* GetQueuedCompletionStatusEx 需要 Vista+ */
#endif

#include "unity.h"
#include <libco/co.h>
#include <winsock2.h>
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* 确保链接 Winsock（已在 co_static 的 IOCP 源文件中通过 #pragma comment 引入，
   这里显式声明是为了让测试代码本身创建套接字时也能正确解析符号） */

// ============================================================================
// 全局测试状态（在协程之间传递结果）
// ============================================================================

static co_socket_t g_listen_fd   = INVALID_SOCKET;
static co_socket_t g_client_fd   = INVALID_SOCKET;
static co_socket_t g_accepted_fd = INVALID_SOCKET;
static int         g_server_port = 0;

/* 由客户端协程写入，由测试主函数读取 */
static ssize_t g_read_result = 0;
static int     g_wsa_error   = 0;
static int     g_elapsed_ms  = 0;   /**< co_read 实际等待毫秒数 */

// ============================================================================
// Unity setUp / tearDown
// ============================================================================

void setUp(void) {
    g_listen_fd   = INVALID_SOCKET;
    g_client_fd   = INVALID_SOCKET;
    g_accepted_fd = INVALID_SOCKET;
    g_server_port = 0;
    g_read_result = 0;
    g_wsa_error   = 0;
    g_elapsed_ms  = 0;
}

void tearDown(void) {
    /* 关闭仍然存活的套接字（正常流程中协程已关闭，这里仅作安全网） */
    if (g_listen_fd != INVALID_SOCKET) {
        closesocket(g_listen_fd);
        g_listen_fd = INVALID_SOCKET;
    }
    if (g_client_fd != INVALID_SOCKET) {
        closesocket(g_client_fd);
        g_client_fd = INVALID_SOCKET;
    }
    if (g_accepted_fd != INVALID_SOCKET) {
        closesocket(g_accepted_fd);
        g_accepted_fd = INVALID_SOCKET;
    }
}

// ============================================================================
// 测试辅助：创建监听套接字，返回 OS 分配的端口
// ============================================================================

static co_socket_t create_listen_socket(int *out_port) {
    co_socket_t fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == INVALID_SOCKET) return INVALID_SOCKET;

    BOOL reuse = TRUE;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = 0;   /* 让 OS 选择空闲端口 */

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(fd, 5) != 0) {
        closesocket(fd);
        return INVALID_SOCKET;
    }

    int len = sizeof(addr);
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
 * 避免 TCP RST/FIN 先于超时抵达导致 errno 变成连接错误而非 WSAETIMEDOUT。
 */
static void coro_server_hold_connection(void *arg) {
    (void)arg;
    struct sockaddr_in cli_addr;
    socklen_t cli_len = sizeof(cli_addr);

    g_accepted_fd = co_accept(g_listen_fd, &cli_addr, &cli_len, -1);
    if (g_accepted_fd == CO_INVALID_SOCKET) return;

    /* 保持连接，让客户端的读超时（100ms）能在此期间触发 */
    co_sleep(200);

    closesocket(g_accepted_fd);
    g_accepted_fd = INVALID_SOCKET;
}

/**
 * 客户端协程：连接到服务端，然后以 100ms 超时调用 co_read。
 * 记录返回值、WSA 错误码及实际等待时长。
 */
static void coro_client_read_timeout(void *arg) {
    (void)arg;

    g_client_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_client_fd == INVALID_SOCKET) return;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((u_short)g_server_port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (co_connect(g_client_fd, &addr, sizeof(addr), 2000) != 0) return;

    /* 精确计时 */
    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    char buf[256];
    g_read_result = co_read(g_client_fd, buf, sizeof(buf), 100 /* ms */);
    g_wsa_error   = WSAGetLastError();

    QueryPerformanceCounter(&t1);
    g_elapsed_ms = (int)((t1.QuadPart - t0.QuadPart) * 1000 / freq.QuadPart);
}

void test_iocp_read_times_out(void) {
    /* co_scheduler_create 会在内部初始化 Winsock（通过 co_iomux_create→init_winsock），
       因此必须先建调度器，再创建套接字。 */
    co_scheduler_t *sched = co_scheduler_create(NULL);
    TEST_ASSERT_NOT_NULL(sched);

    g_listen_fd = create_listen_socket(&g_server_port);
    TEST_ASSERT_NOT_EQUAL(INVALID_SOCKET, g_listen_fd);

    co_spawn(sched, coro_server_hold_connection, NULL, 0);
    co_spawn(sched, coro_client_read_timeout,    NULL, 0);

    co_error_t err = co_scheduler_run(sched);
    TEST_ASSERT_EQUAL_INT(CO_OK, err);
    co_scheduler_destroy(sched);

    closesocket(g_listen_fd);
    g_listen_fd = INVALID_SOCKET;

    /* ---- 断言 ---- */

    /* co_read 超时应返回 -1 */
    TEST_ASSERT_TRUE(g_read_result < 0);

    /* WSA 错误应为 WSAETIMEDOUT */
    TEST_ASSERT_EQUAL_INT(WSAETIMEDOUT, g_wsa_error);

    /* 实际等待时长应约为 100ms（60ms ~ 400ms，留宽以容纳 CI 调度抖动） */
    TEST_ASSERT_GREATER_OR_EQUAL_INT(60,  g_elapsed_ms);
    TEST_ASSERT_LESS_THAN_INT(400, g_elapsed_ms);
}

// ============================================================================
// 测试 2：数据在超时前到达，co_read 应正常返回读取字节数
// ============================================================================

static const char k_payload[] = "hello";  /* 5 字节 */

/**
 * 服务端协程：接受连接后立即发送 "hello"，然后关闭连接。
 */
static void coro_server_send_data(void *arg) {
    (void)arg;
    struct sockaddr_in cli_addr;
    socklen_t cli_len = sizeof(cli_addr);

    g_accepted_fd = co_accept(g_listen_fd, &cli_addr, &cli_len, -1);
    if (g_accepted_fd == CO_INVALID_SOCKET) return;

    co_write(g_accepted_fd, k_payload, sizeof(k_payload) - 1, -1);

    closesocket(g_accepted_fd);
    g_accepted_fd = INVALID_SOCKET;
}

/**
 * 客户端协程：连接到服务端，以 500ms 超时调用 co_read。
 * 服务端发送数据的时间远早于 500ms，co_read 应成功读取。
 */
static void coro_client_read_success(void *arg) {
    (void)arg;

    g_client_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_client_fd == INVALID_SOCKET) return;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((u_short)g_server_port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (co_connect(g_client_fd, &addr, sizeof(addr), 2000) != 0) return;

    char buf[64] = {0};
    g_read_result = co_read(g_client_fd, buf, sizeof(buf), 500 /* ms */);
    g_wsa_error   = WSAGetLastError();
}

void test_iocp_read_succeeds_before_timeout(void) {
    co_scheduler_t *sched = co_scheduler_create(NULL);
    TEST_ASSERT_NOT_NULL(sched);

    g_listen_fd = create_listen_socket(&g_server_port);
    TEST_ASSERT_NOT_EQUAL(INVALID_SOCKET, g_listen_fd);

    co_spawn(sched, coro_server_send_data,    NULL, 0);
    co_spawn(sched, coro_client_read_success, NULL, 0);

    co_error_t err = co_scheduler_run(sched);
    TEST_ASSERT_EQUAL_INT(CO_OK, err);
    co_scheduler_destroy(sched);

    closesocket(g_listen_fd);
    g_listen_fd = INVALID_SOCKET;

    /* ---- 断言 ---- */

    /* co_read 应成功返回 5 字节（"hello"） */
    TEST_ASSERT_EQUAL_INT(5, (int)g_read_result);
}

// ============================================================================
// main
// ============================================================================

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_iocp_read_times_out);
    RUN_TEST(test_iocp_read_succeeds_before_timeout);
    return UNITY_END();
}

#else  /* !_WIN32 */

#include <stdio.h>

int main(void) {
    printf("IOCP timeout tests are Windows-only, skipping.\n");
    return 0;
}

#endif /* _WIN32 */
