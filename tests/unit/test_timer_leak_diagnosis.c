/**
 * @file test_timer_leak_diagnosis.c
 * @brief Timer leak诊断测试 - 用于分析Windows/Linux平台差异
 *
 * 使用较大的timeout值（5秒）来测试timer残留的影响：
 * - 如果有timer leak，调度器应该在5秒后才能退出
 * - 如果timer被正确清理，调度器应该立即退出（<100ms）
 *
 * 通过对比两个平台的实际耗时，可以判断：
 * 1. timer leak是否存在
 * 2. 平台之间是否有行为差异
 * 3. 差异的具体表现（挂死 vs 延迟退出）
 */

#include "unity.h"
#include <libco/co.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
typedef SOCKET socket_t;
#define CLOSE_SOCKET closesocket
#define INVALID_SOCK INVALID_SOCKET
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
typedef int socket_t;
#define CLOSE_SOCKET close
#define INVALID_SOCK -1
#endif

// ============================================================================
// 全局状态
// ============================================================================

static socket_t g_listen_fd   = INVALID_SOCK;
static socket_t g_client_fd   = INVALID_SOCK;
static socket_t g_accepted_fd = INVALID_SOCK;
static int      g_server_port = 0;
static ssize_t  g_read_result = 0;
static int      g_total_elapsed_ms = 0;  // 总耗时（包括调度器退出时间）

// ============================================================================
// 跨平台时间测量
// ============================================================================

#ifdef _WIN32
static LARGE_INTEGER g_freq;
static LARGE_INTEGER g_test_start;

static void timer_init(void) {
    QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&g_test_start);
}

static int timer_elapsed_ms(void) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (int)((now.QuadPart - g_test_start.QuadPart) * 1000 / g_freq.QuadPart);
}
#else
static struct timespec g_test_start;

static void timer_init(void) {
    clock_gettime(CLOCK_MONOTONIC, &g_test_start);
}

static int timer_elapsed_ms(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int)((now.tv_sec - g_test_start.tv_sec) * 1000 + 
                 (now.tv_nsec - g_test_start.tv_nsec) / 1000000);
}
#endif

// ============================================================================
// Unity setUp / tearDown
// ============================================================================

void setUp(void) {
    g_listen_fd   = INVALID_SOCK;
    g_client_fd   = INVALID_SOCK;
    g_accepted_fd = INVALID_SOCK;
    g_server_port = 0;
    g_read_result = 0;
    g_total_elapsed_ms = 0;
}

void tearDown(void) {
    printf("[TEARDOWN] Cleaning up\n");
    if (g_listen_fd != INVALID_SOCK) {
        CLOSE_SOCKET(g_listen_fd);
        g_listen_fd = INVALID_SOCK;
    }
    if (g_client_fd != INVALID_SOCK) {
        CLOSE_SOCKET(g_client_fd);
        g_client_fd = INVALID_SOCK;
    }
    if (g_accepted_fd != INVALID_SOCK) {
        CLOSE_SOCKET(g_accepted_fd);
        g_accepted_fd = INVALID_SOCK;
    }
}

// ============================================================================
// 辅助函数
// ============================================================================

static socket_t create_listen_socket(int *out_port) {
    socket_t fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == INVALID_SOCK) return INVALID_SOCK;

#ifdef _WIN32
    BOOL reuse = TRUE;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse));
#else
    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void *)&reuse, sizeof(reuse));
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = 0;

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(fd, 5) != 0) {
        CLOSE_SOCKET(fd);
        return INVALID_SOCK;
    }

#ifdef _WIN32
    int len = sizeof(addr);
#else
    socklen_t len = sizeof(addr);
#endif
    getsockname(fd, (struct sockaddr *)&addr, &len);
    *out_port = ntohs(addr.sin_port);
    return fd;
}

// ============================================================================
// 诊断测试：大timeout值（5秒）的I/O操作
// ============================================================================

static void coro_server_send_quickly(void *arg) {
    (void)arg;
    struct sockaddr_in cli_addr;
#ifdef _WIN32
    int cli_len = sizeof(cli_addr);
#else
    socklen_t cli_len = sizeof(cli_addr);
#endif

    printf("[SERVER] [%dms] Accepting connection...\n", timer_elapsed_ms());
    g_accepted_fd = co_accept(g_listen_fd, &cli_addr, &cli_len, -1);
    if (g_accepted_fd == CO_INVALID_SOCKET) {
        printf("[SERVER] [%dms] Accept failed\n", timer_elapsed_ms());
        return;
    }
    printf("[SERVER] [%dms] Connection accepted\n", timer_elapsed_ms());
    
    // 等待2秒后发送数据（远早于客户端的10秒timeout）
    printf("[SERVER] [%dms] Sleeping for 2 seconds...\n", timer_elapsed_ms());
    co_sleep(2000);
    
    // 然后发送数据
    printf("[SERVER] [%dms] Sending data...\n", timer_elapsed_ms());
    const char *msg = "hello";
    co_write(g_accepted_fd, msg, 5, -1);
    printf("[SERVER] [%dms] Data sent, server finished\n", timer_elapsed_ms());
    
    CLOSE_SOCKET(g_accepted_fd);
    g_accepted_fd = INVALID_SOCK;
}

static void coro_client_long_timeout(void *arg) {
    (void)arg;
    
    printf("[CLIENT] [%dms] Creating socket...\n", timer_elapsed_ms());
    g_client_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_client_fd == INVALID_SOCK) {
        printf("[CLIENT] [%dms] Socket creation failed\n", timer_elapsed_ms());
        return;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((unsigned short)g_server_port);
#ifdef _WIN32
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
#else
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
#endif

    printf("[CLIENT] [%dms] Connecting...\n", timer_elapsed_ms());
    if (co_connect(g_client_fd, &addr, sizeof(addr), 3000) != 0) {
        printf("[CLIENT] [%dms] Connect failed\n", timer_elapsed_ms());
        return;
    }
    printf("[CLIENT] [%dms] Connected\n", timer_elapsed_ms());

    printf("[CLIENT] [%dms] Reading with 10000ms (10 sec) timeout...\n", timer_elapsed_ms());
    char buf[256];
    g_read_result = co_read(g_client_fd, buf, sizeof(buf), 10000 /* 10秒timeout */);
    printf("[CLIENT] [%dms] Read returned: %zd\n", timer_elapsed_ms(), g_read_result);
    
    CLOSE_SOCKET(g_client_fd);
    g_client_fd = INVALID_SOCK;
    printf("[CLIENT] [%dms] Client finished\n", timer_elapsed_ms());
}

void test_timer_leak_with_long_timeout(void) {
    printf("\n========================================\n");
    printf("Timer Leak Diagnosis Test\n");
    printf("Platform: %s\n", 
#ifdef _WIN32
        "Windows (IOCP)"
#else
        "Linux (epoll)"
#endif
    );
    printf("========================================\n");
    
    timer_init();
    
    printf("[TEST] [%dms] Creating scheduler...\n", timer_elapsed_ms());
    co_scheduler_t *sched = co_scheduler_create(NULL);
    TEST_ASSERT_NOT_NULL(sched);

    printf("[TEST] [%dms] Creating listen socket...\n", timer_elapsed_ms());
    g_listen_fd = create_listen_socket(&g_server_port);
    TEST_ASSERT_NOT_EQUAL(INVALID_SOCK, g_listen_fd);
    printf("[TEST] [%dms] Listening on port %d\n", timer_elapsed_ms(), g_server_port);

    printf("[TEST] [%dms] Spawning coroutines...\n", timer_elapsed_ms());
    co_spawn(sched, coro_server_send_quickly,     NULL, 0);
    co_spawn(sched, coro_client_long_timeout,     NULL, 0);

    printf("[TEST] [%dms] Running scheduler...\n", timer_elapsed_ms());
    co_error_t err = co_scheduler_run(sched);
    
    g_total_elapsed_ms = timer_elapsed_ms();
    printf("[TEST] [%dms] Scheduler returned (error=%d)\n", g_total_elapsed_ms, err);
    
    TEST_ASSERT_EQUAL_INT(CO_OK, err);
    
    printf("[TEST] [%dms] Destroying scheduler...\n", timer_elapsed_ms());
    co_scheduler_destroy(sched);
    
    printf("[TEST] [%dms] Closing listen socket...\n", timer_elapsed_ms());
    CLOSE_SOCKET(g_listen_fd);
    g_listen_fd = INVALID_SOCK;
    
    int final_elapsed = timer_elapsed_ms();
    printf("[TEST] [%dms] Test complete\n", final_elapsed);
    
    printf("\n========================================\n");
    printf("DIAGNOSIS RESULTS:\n");
    printf("========================================\n");
    printf("co_read result: %zd (expected: 5 for success)\n", g_read_result);
    printf("Total elapsed: %dms\n", g_total_elapsed_ms);
    printf("\nANALYSIS:\n");
    printf("Scenario: Server sends data after 2000ms, client has 10000ms timeout\n");
    printf("Expected: I/O completes at ~2000ms, NO timeout\n\n");
    
    // co_read应该成功读取5字节（"hello"）
    TEST_ASSERT_EQUAL_INT(5, (int)g_read_result);
    
    if (g_total_elapsed_ms < 3000) {
        printf("✓ GOOD: Scheduler exited quickly (%dms, ~2 seconds)\n", g_total_elapsed_ms);
        printf("  Timer was properly removed after I/O completion.\n");
        printf("  No residual timer in heap.\n");
    } else if (g_total_elapsed_ms >= 9000) {
        printf("✗ TIMER LEAK DETECTED: Scheduler waited ~10 seconds!\n");
        printf("  I/O completed at 2 sec but 10-second timer remained in heap!\n");
        printf("  Scheduler had to wait 8 extra seconds for timer expiry.\n");
        printf("\n  ROOT CAUSE: Timer not removed in normal I/O completion path.\n");
    } else {
        printf("? PARTIAL DELAY: (%dms, between 3-9 seconds)\n", g_total_elapsed_ms);
        printf("  May indicate timer removal issue or other problems.\n");
    }
    printf("========================================\n");
}

// ============================================================================
// main
// ============================================================================

int main(void) {
#ifdef _WIN32
    // Windows需要初始化Winsock（调度器创建时会做，但这里显式说明）
    printf("Running on Windows (IOCP backend)\n");
#else
    printf("Running on Linux (epoll backend)\n");
#endif
    
    printf("\nNOTE: This test uses a 10-second timeout but I/O completes in 2 seconds.\n");
    printf("Testing the critical case: I/O SUCCESS before timeout.\n\n");
    printf("If timer leak exists (timer not removed on normal completion):\n");
    printf("  - Scheduler must wait ~10 seconds for timer to expire\n");
    printf("  - Total time: ~10000ms (extra 8 seconds wasted!)\n");
    printf("If timer is properly cleaned on I/O completion:\n");
    printf("  - Scheduler exits when I/O completes\n");
    printf("  - Total time: ~2000ms\n");
    printf("\n");
    
    UNITY_BEGIN();
    RUN_TEST(test_timer_leak_with_long_timeout);
    return UNITY_END();
}
