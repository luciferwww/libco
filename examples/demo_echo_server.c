/**
 * @file demo_echo_server.c
 * @brief TCP Echo Server 示例（Week 7 - I/O 多路复用）
 * 
 * 演示：
 * - 协程式网络 I/O (co_accept, co_read, co_write)
 * - 每个客户端连接一个协程
 * - 并发处理多个客户端
 * 
 * 测试方法：
 *   服务端: ./demo_echo_server 8080
 *   客户端: telnet localhost 8080
 *   或: echo "Hello" | nc localhost 8080
 */

#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS  // 允许使用 inet_ntoa
#endif

#include <libco/co.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

// Windows 需要初始化 Winsock
static int init_winsock(void) {
    WSADATA wsa_data;
    return WSAStartup(MAKEWORD(2, 2), &wsa_data);
}

static void cleanup_winsock(void) {
    WSACleanup();
}

#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>

#define closesocket close

static int init_winsock(void) { return 0; }
static void cleanup_winsock(void) {}
#endif

// ============================================================================
// 全局变量
// ============================================================================

static volatile int g_server_running = 1;
static int g_total_clients = 0;
static int g_active_clients = 0;

// ============================================================================
// 客户端处理协程
// ============================================================================

typedef struct {
    co_socket_t client_fd;
    int client_id;
    struct sockaddr_in client_addr;
} ClientContext;

/**
 * @brief 客户端处理协程
 * 
 * 读取客户端数据并原样返回（echo）
 */
static void client_handler_routine(void *arg) {
    ClientContext *ctx = (ClientContext *)arg;
    char buffer[1024];
    
    printf("[Client %d] Connected from %s:%d\n",
           ctx->client_id,
           inet_ntoa(ctx->client_addr.sin_addr),
           ntohs(ctx->client_addr.sin_port));
    
    g_active_clients++;
    
    // Echo 循环
    while (1) {
        // 读取数据（超时 30 秒）
        ssize_t n = co_read(ctx->client_fd, buffer, sizeof(buffer) - 1, 30000);
        
        if (n <= 0) {
            // 连接关闭或错误
            if (n < 0) {
#ifdef _WIN32
                int err = WSAGetLastError();
                if (err == WSAETIMEDOUT) {
                    printf("[Client %d] Timeout\n", ctx->client_id);
                } else {
                    printf("[Client %d] Read error: %d\n", ctx->client_id, err);
                }
#else
                if (errno == ETIMEDOUT) {
                    printf("[Client %d] Timeout\n", ctx->client_id);
                } else {
                    printf("[Client %d] Read error: %s\n", ctx->client_id, strerror(errno));
                }
#endif
            } else {
                printf("[Client %d] Connection closed\n", ctx->client_id);
            }
            break;
        }
        
        buffer[n] = '\0';
        printf("[Client %d] Received %zd bytes: %s", ctx->client_id, n, buffer);
        
        // 回显数据
        ssize_t sent = co_write(ctx->client_fd, buffer, n, 30000);
        if (sent != n) {
            printf("[Client %d] Write error\n", ctx->client_id);
            break;
        }
    }
    
    // 清理
    closesocket(ctx->client_fd);
    g_active_clients--;
    printf("[Client %d] Disconnected (active: %d, total: %d)\n",
           ctx->client_id, g_active_clients, g_total_clients);
    
    free(ctx);
}

// ============================================================================
// 服务器主协程
// ============================================================================

typedef struct {
    uint16_t port;
} ServerContext;

/**
 * @brief 服务器主协程
 * 
 * 监听端口，接受连接，为每个客户端创建协程
 */
static void server_routine(void *arg) {
    ServerContext *ctx = (ServerContext *)arg;
    
    // 创建监听套接字
    co_socket_t listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == CO_INVALID_SOCKET) {
        fprintf(stderr, "Failed to create socket\n");
        return;
    }
    
    // 设置地址重用
    int reuse = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));
    
    // 绑定地址
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(ctx->port);
    
    if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
        fprintf(stderr, "Failed to bind to port %u\n", ctx->port);
        closesocket(listen_fd);
        return;
    }
    
    // 开始监听
    if (listen(listen_fd, 128) != 0) {
        fprintf(stderr, "Failed to listen\n");
        closesocket(listen_fd);
        return;
    }
    
    printf("Echo server listening on port %u...\n", ctx->port);
    printf("Press Ctrl+C to stop\n\n");
    
    // 接受连接循环
    while (g_server_running) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        
        // 协程式 accept（超时 1 秒，以便能响应停止信号）
        co_socket_t client_fd = co_accept(listen_fd,
                                          (void *)&client_addr,
                                          &client_addr_len,
                                          1000);
        
        if (client_fd == CO_INVALID_SOCKET) {
            // 超时或错误
#ifdef _WIN32
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK) {
                continue;  // 超时，继续循环
            }
            // Windows 其他错误也继续尝试（调试阶段）
            fprintf(stderr, "[Server] Accept error: %d, retrying...\n", err);
            Sleep(100);  // 稍微等待避免忙循环
            continue;
#else
            if (errno == ETIMEDOUT || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            // 真实错误
            fprintf(stderr, "Accept failed: %s\n", strerror(errno));
            break;
#endif
        }
        
        // 为新连接创建处理协程
        ClientContext *client_ctx = (ClientContext *)malloc(sizeof(ClientContext));
        if (!client_ctx) {
            fprintf(stderr, "Out of memory\n");
            closesocket(client_fd);
            continue;
        }
        
        client_ctx->client_fd = client_fd;
        client_ctx->client_id = ++g_total_clients;
        client_ctx->client_addr = client_addr;
        
        co_routine_t *client_co = co_spawn(NULL, client_handler_routine, client_ctx, 0);
        if (!client_co) {
            fprintf(stderr, "Failed to spawn client coroutine\n");
            closesocket(client_fd);
            free(client_ctx);
            continue;
        }
    }
    
    // 清理
    closesocket(listen_fd);
    printf("\nServer stopped\n");
}

// ============================================================================
// 信号处理
// ============================================================================

#ifndef _WIN32
static void signal_handler(int sig) {
    (void)sig;
    g_server_running = 0;
}
#endif

// ============================================================================
// 主函数
// ============================================================================

int main(int argc, char *argv[]) {
    // 解析命令行参数
    uint16_t port = 8080;
    if (argc > 1) {
        port = (uint16_t)atoi(argv[1]);
        if (port == 0) {
            fprintf(stderr, "Invalid port number\n");
            return 1;
        }
    }
    
    // 初始化网络库
    if (init_winsock() != 0) {
        fprintf(stderr, "Failed to initialize Winsock\n");
        return 1;
    }
    
#ifndef _WIN32
    // 设置信号处理（Ctrl+C）
    signal(SIGINT, signal_handler);
    signal(SIGPIPE, SIG_IGN);  // 忽略 SIGPIPE
#endif
    
    // 创建调度器
    co_scheduler_t *sched = co_scheduler_create(NULL);
    if (!sched) {
        fprintf(stderr, "Failed to create scheduler\n");
        cleanup_winsock();
        return 1;
    }
    
    // 创建服务器协程
    ServerContext server_ctx = { .port = port };
    co_routine_t *server_co = co_spawn(sched, server_routine, &server_ctx, 0);
    if (!server_co) {
        fprintf(stderr, "Failed to spawn server coroutine\n");
        co_scheduler_destroy(sched);
        cleanup_winsock();
        return 1;
    }
    
    // 运行调度器
    co_scheduler_run(sched);
    
    // 清理
    co_scheduler_destroy(sched);
    cleanup_winsock();
    
    printf("Final stats: %d total clients served\n", g_total_clients);
    
    return 0;
}
