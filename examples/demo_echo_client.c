/**
 * @file demo_echo_client.c
 * @brief TCP Echo Client 测试程序（Week 8）
 * 
 * 用于测试 echo server 的简单客户端
 */

#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif

#include <libco/co.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

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

#define closesocket close

static int init_winsock(void) { return 0; }
static void cleanup_winsock(void) {}
#endif

// ============================================================================
// 客户端协程
// ============================================================================

typedef struct {
    const char *host;
    uint16_t port;
    int client_id;
} ClientArgs;

static void client_routine(void *arg) {
    ClientArgs *args = (ClientArgs *)arg;
    
    printf("[Client %d] Connecting to %s:%d...\n", args->client_id, args->host, args->port);
    
    // 创建套接字
    co_socket_t sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == CO_INVALID_SOCKET) {
        fprintf(stderr, "[Client %d] Failed to create socket\n", args->client_id);
        return;
    }
    
    // 设置服务器地址
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(args->port);
    
    // 转换 IP 地址
#ifdef _WIN32
    server_addr.sin_addr.s_addr = inet_addr(args->host);
#else
    if (inet_pton(AF_INET, args->host, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "[Client %d] Invalid address\n", args->client_id);
        closesocket(sockfd);
        return;
    }
#endif
    
    // 连接服务器（协程式）
    if (co_connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr), 5000) != 0) {
#ifdef _WIN32
        fprintf(stderr, "[Client %d] Connect failed: %d\n", args->client_id, WSAGetLastError());
#else
        fprintf(stderr, "[Client %d] Connect failed: %s\n", args->client_id, strerror(errno));
#endif
        closesocket(sockfd);
        return;
    }
    
    printf("[Client %d] Connected!\n", args->client_id);
    
    // 发送测试消息
    char send_buf[128];
    snprintf(send_buf, sizeof(send_buf), "Hello from client %d!\n", args->client_id);
    
    ssize_t sent = co_write(sockfd, send_buf, strlen(send_buf), 5000);
    if (sent <= 0) {
        fprintf(stderr, "[Client %d] Send failed\n", args->client_id);
        closesocket(sockfd);
        return;
    }
    
    printf("[Client %d] Sent: %s", args->client_id, send_buf);
    
    // 接收回显
    char recv_buf[128];
    ssize_t received = co_read(sockfd, recv_buf, sizeof(recv_buf) - 1, 5000);
    if (received <= 0) {
        fprintf(stderr, "[Client %d] Receive failed\n", args->client_id);
        closesocket(sockfd);
        return;
    }
    
    recv_buf[received] = '\0';
    printf("[Client %d] Received: %s", args->client_id, recv_buf);
    
    // 验证回显是否正确
    if (strcmp(send_buf, recv_buf) == 0) {
        printf("[Client %d] ✓ Echo correct!\n", args->client_id);
    } else {
        printf("[Client %d] ✗ Echo mismatch!\n", args->client_id);
    }
    
    // 关闭连接
    closesocket(sockfd);
    printf("[Client %d] Disconnected\n", args->client_id);
}

// ============================================================================
// 主函数
// ============================================================================

int main(int argc, char *argv[]) {
    const char *host = "127.0.0.1";
    uint16_t port = 8080;
    int num_clients = 1;
    
    // 解析命令行参数
    if (argc > 1) {
        host = argv[1];
    }
    if (argc > 2) {
        port = (uint16_t)atoi(argv[2]);
    }
    if (argc > 3) {
        num_clients = atoi(argv[3]);
        if (num_clients < 1 || num_clients > 100) {
            fprintf(stderr, "Number of clients must be 1-100\n");
            return 1;
        }
    }
    
    // 初始化网络库
    if (init_winsock() != 0) {
        fprintf(stderr, "Failed to initialize Winsock\n");
        return 1;
    }
    
    printf("=== Echo Client Test ===\n");
    printf("Target: %s:%d\n", host, port);
    printf("Clients: %d\n\n", num_clients);
    
    // 创建调度器
    co_scheduler_t *sched = co_scheduler_create(NULL);
    if (!sched) {
        fprintf(stderr, "Failed to create scheduler\n");
        cleanup_winsock();
        return 1;
    }
    
    // 创建客户端协程
    ClientArgs *args_array = (ClientArgs *)malloc(sizeof(ClientArgs) * num_clients);
    for (int i = 0; i < num_clients; i++) {
        args_array[i].host = host;
        args_array[i].port = port;
        args_array[i].client_id = i + 1;
        
        co_routine_t *client_co = co_spawn(sched, client_routine, &args_array[i], 0);
        if (!client_co) {
            fprintf(stderr, "Failed to spawn client %d\n", i + 1);
        }
    }
    
    // 运行调度器
    co_scheduler_run(sched);
    
    // 清理
    free(args_array);
    co_scheduler_destroy(sched);
    cleanup_winsock();
    
    printf("\n=== Test Complete ===\n");
    
    return 0;
}
