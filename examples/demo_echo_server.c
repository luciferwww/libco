/**
 * @file demo_echo_server.c
 * @brief TCP echo server demo (Week 7 - I/O multiplexing)
 * 
 * Demonstrates:
 * - coroutine-friendly network I/O via co_accept, co_read, and co_write
 * - one coroutine per client connection
 * - concurrent handling of multiple clients
 * 
 * How to test:
 *   Server: ./demo_echo_server 8080
 *   Client: telnet localhost 8080
 *   Or: echo "Hello" | nc localhost 8080
 */

#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS  // Allow inet_ntoa
#endif

#include <libco/co.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

// Windows requires explicit Winsock initialization
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
// Global state
// ============================================================================

static volatile int g_server_running = 1;
static int g_total_clients = 0;
static int g_active_clients = 0;

// ============================================================================
// Client handler coroutine
// ============================================================================

typedef struct {
    co_socket_t client_fd;
    int client_id;
    struct sockaddr_in client_addr;
} ClientContext;

/**
 * @brief Client handler coroutine
 * 
 * Read data from the client and send it back unchanged.
 */
static void client_handler_routine(void *arg) {
    ClientContext *ctx = (ClientContext *)arg;
    char buffer[1024];
    
    printf("[Client %d] Connected from %s:%d\n",
           ctx->client_id,
           inet_ntoa(ctx->client_addr.sin_addr),
           ntohs(ctx->client_addr.sin_port));
    
    g_active_clients++;
    
    // Echo loop
    while (1) {
        // Read data with a 30-second timeout
        ssize_t n = co_read(ctx->client_fd, buffer, sizeof(buffer) - 1, 30000);
        
        if (n <= 0) {
            // The connection closed or an error occurred
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
        
        // Echo the data back
        ssize_t sent = co_write(ctx->client_fd, buffer, n, 30000);
        if (sent != n) {
            printf("[Client %d] Write error\n", ctx->client_id);
            break;
        }
    }
    
    // Cleanup
    closesocket(ctx->client_fd);
    g_active_clients--;
    printf("[Client %d] Disconnected (active: %d, total: %d)\n",
           ctx->client_id, g_active_clients, g_total_clients);
    
    free(ctx);
}

// ============================================================================
// Main server coroutine
// ============================================================================

typedef struct {
    uint16_t port;
} ServerContext;

/**
 * @brief Main server coroutine
 * 
 * Listen on the server port, accept connections, and spawn one coroutine per client.
 */
static void server_routine(void *arg) {
    ServerContext *ctx = (ServerContext *)arg;
    
    // Create the listening socket
    co_socket_t listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == CO_INVALID_SOCKET) {
        fprintf(stderr, "Failed to create socket\n");
        return;
    }
    
    // Enable address reuse
    int reuse = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));
    
    // Bind the server address
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
    
    // Start listening
    if (listen(listen_fd, 128) != 0) {
        fprintf(stderr, "Failed to listen\n");
        closesocket(listen_fd);
        return;
    }
    
    printf("Echo server listening on port %u...\n", ctx->port);
    printf("Press Ctrl+C to stop\n\n");
    
    // Accept loop
    while (g_server_running) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        
        // Coroutine-friendly accept with a 1-second timeout so stop signals can be handled
        co_socket_t client_fd = co_accept(listen_fd,
                                          (void *)&client_addr,
                                          &client_addr_len,
                                          1000);
        
        if (client_fd == CO_INVALID_SOCKET) {
            // Timeout or error
#ifdef _WIN32
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK) {
                continue;  // Timeout, keep looping
            }
            // Retry on other Windows errors as well during development
            fprintf(stderr, "[Server] Accept error: %d, retrying...\n", err);
            Sleep(100);  // Wait a bit to avoid a busy loop
            continue;
#else
            if (errno == ETIMEDOUT || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            // Real error
            fprintf(stderr, "Accept failed: %s\n", strerror(errno));
            break;
#endif
        }
        
        // Create a handler coroutine for the new connection
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
    
    // Cleanup
    closesocket(listen_fd);
    printf("\nServer stopped\n");
}

// ============================================================================
// Signal handling
// ============================================================================

#ifndef _WIN32
static void signal_handler(int sig) {
    (void)sig;
    g_server_running = 0;
}
#endif

// ============================================================================
// Main function
// ============================================================================

int main(int argc, char *argv[]) {
    // Parse command-line arguments
    uint16_t port = 8080;
    if (argc > 1) {
        port = (uint16_t)atoi(argv[1]);
        if (port == 0) {
            fprintf(stderr, "Invalid port number\n");
            return 1;
        }
    }
    
    // Initialize the networking library
    if (init_winsock() != 0) {
        fprintf(stderr, "Failed to initialize Winsock\n");
        return 1;
    }
    
#ifndef _WIN32
    // Set up signal handling for Ctrl+C
    signal(SIGINT, signal_handler);
    signal(SIGPIPE, SIG_IGN);  // Ignore SIGPIPE
#endif
    
    // Create the scheduler
    co_scheduler_t *sched = co_scheduler_create(NULL);
    if (!sched) {
        fprintf(stderr, "Failed to create scheduler\n");
        cleanup_winsock();
        return 1;
    }
    
    // Create the server coroutine
    ServerContext server_ctx = { .port = port };
    co_routine_t *server_co = co_spawn(sched, server_routine, &server_ctx, 0);
    if (!server_co) {
        fprintf(stderr, "Failed to spawn server coroutine\n");
        co_scheduler_destroy(sched);
        cleanup_winsock();
        return 1;
    }
    
    // Run the scheduler
    co_scheduler_run(sched);
    
    // Cleanup
    co_scheduler_destroy(sched);
    cleanup_winsock();
    
    printf("Final stats: %d total clients served\n", g_total_clients);
    
    return 0;
}
