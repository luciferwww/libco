/**
 * @file iomux_iocp.c
 * @brief Windows IOCP (I/O Completion Ports) 实现
 * 
 * 使用 Windows 的 IOCP 实现 I/O 多路复用。
 * IOCP 是 Windows 的高性能异步 I/O 机制。
 * 
 * 注意：IOCP 是完全异步的（Asynchronous I/O），
 * 而 epoll/kqueue 是同步非阻塞的（Synchronous Non-blocking I/O）。
 * 这里我们将 IOCP 的异步模型适配为协程的同步风格。
 */

#ifdef _WIN32

// GetQueuedCompletionStatusEx 需要 Windows Vista (0x0600) 或更高版本
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0600
#  undef _WIN32_WINNT
#  define _WIN32_WINNT 0x0600
#endif

#include "../../co_iomux.h"
#include "../../co_routine.h"
#include "../../co_scheduler.h"
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

// 链接 Winsock 库
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "mswsock.lib")

// ============================================================================
// IOCP 实现结构
// ============================================================================

/**
 * @brief IOCP I/O 多路复用器
 */
struct co_iomux {
    HANDLE iocp;                    /**< IOCP 句柄 */
    int max_events;                 /**< 最大事件数（一次 poll 最多取出的完成包数） */
    OVERLAPPED_ENTRY *entries;      /**< 批量完成包缓冲区（GetQueuedCompletionStatusEx 用） */

    // Winsock 初始化标志
    bool wsa_initialized;
    
    // 已关联的套接字集合（简化实现：使用固定数组）
    // 实际应用中应该使用哈希表
    co_socket_t associated_sockets[1024];
    int associated_count;
};

// ============================================================================
// AcceptEx/ConnectEx 函数指针（运行时获取）
// ============================================================================

// AcceptEx 函数原型
typedef BOOL (WINAPI *LPFN_ACCEPTEX)(
    SOCKET sListenSocket,
    SOCKET sAcceptSocket,
    PVOID lpOutputBuffer,
    DWORD dwReceiveDataLength,
    DWORD dwLocalAddressLength,
    DWORD dwRemoteAddressLength,
    LPDWORD lpdwBytesReceived,
    LPOVERLAPPED lpOverlapped
);

// ConnectEx 函数原型
typedef BOOL (WINAPI *LPFN_CONNECTEX)(
    SOCKET s,
    const struct sockaddr *name,
    int namelen,
    PVOID lpSendBuffer,
    DWORD dwSendDataLength,
    LPDWORD lpdwBytesSent,
    LPOVERLAPPED lpOverlapped
);

// GetAcceptExSockaddrs 函数原型
typedef VOID (WINAPI *LPFN_GETACCEPTEXSOCKADDRS)(
    PVOID lpOutputBuffer,
    DWORD dwReceiveDataLength,
    DWORD dwLocalAddressLength,
    DWORD dwRemoteAddressLength,
    struct sockaddr **LocalSockaddr,
    LPINT LocalSockaddrLength,
    struct sockaddr **RemoteSockaddr,
    LPINT RemoteSockaddrLength
);

// 全局函数指针（延迟加载）
static LPFN_ACCEPTEX g_AcceptEx = NULL;
static LPFN_CONNECTEX g_ConnectEx = NULL;
static LPFN_GETACCEPTEXSOCKADDRS g_GetAcceptExSockaddrs = NULL;
static bool g_ext_funcs_loaded = false;

/**
 * @brief 加载 AcceptEx/ConnectEx 扩展函数
 */
static bool load_extension_functions(void) {
    if (g_ext_funcs_loaded) {
        return true;
    }
    
    // 创建临时套接字用于获取函数指针
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "Failed to create temp socket for extension functions\n");
        return false;
    }
    
    DWORD bytes = 0;
    
    // 获取 AcceptEx 函数指针
    GUID acceptex_guid = WSAID_ACCEPTEX;
    if (WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
                 &acceptex_guid, sizeof(acceptex_guid),
                 &g_AcceptEx, sizeof(g_AcceptEx),
                 &bytes, NULL, NULL) != 0) {
        fprintf(stderr, "Failed to get AcceptEx function pointer: %d\n", WSAGetLastError());
        closesocket(sock);
        return false;
    }
    
    // 获取 ConnectEx 函数指针
    GUID connectex_guid = WSAID_CONNECTEX;
    if (WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
                 &connectex_guid, sizeof(connectex_guid),
                 &g_ConnectEx, sizeof(g_ConnectEx),
                 &bytes, NULL, NULL) != 0) {
        fprintf(stderr, "Failed to get ConnectEx function pointer: %d\n", WSAGetLastError());
        closesocket(sock);
        return false;
    }
    
    // 获取 GetAcceptExSockaddrs 函数指针
    GUID getacceptexsockaddrs_guid = WSAID_GETACCEPTEXSOCKADDRS;
    if (WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
                 &getacceptexsockaddrs_guid, sizeof(getacceptexsockaddrs_guid),
                 &g_GetAcceptExSockaddrs, sizeof(g_GetAcceptExSockaddrs),
                 &bytes, NULL, NULL) != 0) {
        fprintf(stderr, "Failed to get GetAcceptExSockaddrs function pointer: %d\n", WSAGetLastError());
        closesocket(sock);
        return false;
    }
    
    closesocket(sock);
    g_ext_funcs_loaded = true;
    
    return true;
}

// ============================================================================
// 全局 Winsock 初始化
// ============================================================================

static bool g_wsa_init = false;

static bool init_winsock(void) {
    if (g_wsa_init) {
        return true;
    }
    
    WSADATA wsa_data;
    int ret = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (ret != 0) {
        fprintf(stderr, "WSAStartup failed: %d\n", ret);
        return false;
    }
    
    g_wsa_init = true;
    return true;
}

// ============================================================================
// I/O 多路复用器实现
// ============================================================================

co_iomux_t *co_iomux_create(int max_events) {
    if (!init_winsock()) {
        return NULL;
    }
    
    if (max_events <= 0) {
        max_events = 1024;
    }
    
    co_iomux_t *iomux = (co_iomux_t *)calloc(1, sizeof(co_iomux_t));
    if (!iomux) {
        return NULL;
    }
    
    // 创建 IOCP
    // 参数：
    // 1. INVALID_HANDLE_VALUE - 创建新的 IOCP
    // 2. NULL - 现有 IOCP（这里是新建）
    // 3. 0 - 并发线程数（0 = CPU 核心数）
    iomux->iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (iomux->iocp == NULL) {
        fprintf(stderr, "CreateIoCompletionPort failed: %lu\n", GetLastError());
        free(iomux);
        return NULL;
    }
    
    iomux->max_events = max_events;
    iomux->wsa_initialized = true;
    iomux->associated_count = 0;
    memset(iomux->associated_sockets, 0, sizeof(iomux->associated_sockets));

    // 分配批量完成包缓冲区
    iomux->entries = (OVERLAPPED_ENTRY *)calloc(max_events, sizeof(OVERLAPPED_ENTRY));
    if (!iomux->entries) {
        CloseHandle(iomux->iocp);
        free(iomux);
        return NULL;
    }

    return iomux;
}

void co_iomux_destroy(co_iomux_t *iomux) {
    if (!iomux) {
        return;
    }
    
    if (iomux->iocp) {
        CloseHandle(iomux->iocp);
    }

    free(iomux->entries);
    free(iomux);
}

co_error_t co_iomux_add(co_iomux_t *iomux, co_io_wait_ctx_t *wait_ctx) {
    if (!iomux || !wait_ctx) {
        return CO_ERROR_INVAL;
    }
    
    // 检查套接字是否已关联
    bool already_associated = false;
    for (int i = 0; i < iomux->associated_count; i++) {
        if (iomux->associated_sockets[i] == wait_ctx->fd) {
            already_associated = true;
            break;
        }
    }
    
    if (already_associated) {
        // 已经关联过，跳过
        return CO_OK;
    }
    
    // 将套接字关联到 IOCP
    // 参数：
    // 1. (HANDLE)wait_ctx->fd - 文件句柄（套接字可以转为 HANDLE）
    // 2. iomux->iocp - IOCP 句柄
    // 3. 0 - Completion Key（使用 0，通过 OVERLAPPED 来定位 wait_ctx）
    // 4. 0 - 并发线程数（0 = 使用创建时的值）
    // 
    // 注意：我们使用 0 作为 CompletionKey，因为对于监听套接字的 AcceptEx，
    // 每次操作的 wait_ctx 都不同，不能用作 CompletionKey。
    // 我们通过 OVERLAPPED 指针来定位具体的 wait_ctx。
    HANDLE h = CreateIoCompletionPort((HANDLE)wait_ctx->fd, iomux->iocp, 0, 0);
    if (h == NULL) {
        fprintf(stderr, "CreateIoCompletionPort (associate) failed: %lu\n", GetLastError());
        return CO_ERROR_PLATFORM;
    }
    
    // 记录已关联的套接字
    if (iomux->associated_count < 1024) {
        iomux->associated_sockets[iomux->associated_count++] = wait_ctx->fd;
    }
    
    // 初始化 OVERLAPPED 结构
    memset(&wait_ctx->overlapped, 0, sizeof(wait_ctx->overlapped));
    
    return CO_OK;
}

co_error_t co_iomux_mod(co_iomux_t *iomux, co_io_wait_ctx_t *wait_ctx) {
    // IOCP 不需要修改操作，每次 I/O 都会投递新的异步请求
    (void)iomux;     // 未使用
    (void)wait_ctx;  // 未使用
    return CO_OK;
}

co_error_t co_iomux_del(co_iomux_t *iomux, co_socket_t fd) {
    // IOCP 不需要显式删除操作
    // 当套接字关闭时，关联会自动解除
    // 但需要从 associated_sockets 数组中移除记录，防止数组满后无法跟踪新套接字
    if (!iomux) {
        return CO_ERROR_INVAL;
    }
    for (int i = 0; i < iomux->associated_count; i++) {
        if (iomux->associated_sockets[i] == fd) {
            // 用最后一个元素填补空缺（O(1) 删除）
            iomux->associated_sockets[i] = iomux->associated_sockets[--iomux->associated_count];
            break;
        }
    }
    return CO_OK;
}

co_error_t co_iomux_poll(co_iomux_t *iomux, int timeout_ms, int *out_ready_count) {
    if (!iomux) {
        return CO_ERROR_INVAL;
    }

    ULONG removed = 0;
    DWORD timeout = (timeout_ms < 0) ? INFINITE : (DWORD)timeout_ms;

    // 批量取出完成包（Windows Vista+），与 epoll_wait 语义对齐：
    // 一次调用可取出多个完成事件，避免高并发时每次只处理一个的瓶颈。
    BOOL ret = GetQueuedCompletionStatusEx(
        iomux->iocp,
        iomux->entries,
        (ULONG)iomux->max_events,
        &removed,
        timeout,
        FALSE   // fAlertable：不使用 APC
    );

    if (!ret) {
        DWORD error = GetLastError();
        if (error == WAIT_TIMEOUT) {
            if (out_ready_count) *out_ready_count = 0;
            return CO_ERROR_TIMEOUT;
        }
        fprintf(stderr, "GetQueuedCompletionStatusEx failed: %lu\n", error);
        return CO_ERROR_PLATFORM;
    }

    int woken = 0;
    for (ULONG i = 0; i < removed; i++) {
        LPOVERLAPPED overlapped = iomux->entries[i].lpOverlapped;
        if (!overlapped) {
            continue;
        }

        // 通过 OVERLAPPED 成员偏移还原 wait_ctx 指针
        co_io_wait_ctx_t *wait_ctx =
            (co_io_wait_ctx_t *)((char *)overlapped - offsetof(co_io_wait_ctx_t, overlapped));

        if (!wait_ctx || !wait_ctx->routine) {
            continue;
        }

        wait_ctx->bytes_transferred = iomux->entries[i].dwNumberOfBytesTransferred;

        // 设置事件类型
        wait_ctx->revents = 0;
        if (wait_ctx->op_type == CO_IO_OP_READ) {
            wait_ctx->revents = CO_IO_READ;
        } else if (wait_ctx->op_type == CO_IO_OP_WRITE) {
            wait_ctx->revents = CO_IO_WRITE;
        } else if (wait_ctx->op_type == CO_IO_OP_ACCEPT) {
            wait_ctx->revents = CO_IO_READ;
        } else if (wait_ctx->op_type == CO_IO_OP_CONNECT) {
            wait_ctx->revents = CO_IO_WRITE;
        }

        // Internal 字段存储 NTSTATUS，非 0 表示 I/O 操作失败
        if (iomux->entries[i].Internal != 0) {
            wait_ctx->revents |= CO_IO_ERROR;
        }

        // 唤醒协程
        co_routine_t *routine = wait_ctx->routine;
        if (routine->state == CO_STATE_WAITING) {
            routine->state = CO_STATE_READY;
            routine->io_waiting = false;
            routine->scheduler->waiting_io_count--;
            co_queue_push_back(&routine->scheduler->ready_queue, &routine->queue_node);
            woken++;
        }
    }

    if (out_ready_count) {
        *out_ready_count = woken;
    }

    return CO_OK;
}

// ============================================================================
// 辅助函数
// ============================================================================

co_error_t co_set_nonblocking(co_socket_t fd) {
    // Windows 套接字设置非阻塞
    u_long mode = 1;  // 1 = 非阻塞
    if (ioctlsocket(fd, FIONBIO, &mode) != 0) {
        fprintf(stderr, "ioctlsocket FIONBIO failed: %d\n", WSAGetLastError());
        return CO_ERROR_PLATFORM;
    }
    return CO_OK;
}

co_error_t co_set_blocking(co_socket_t fd) {
    u_long mode = 0;  // 0 = 阻塞
    if (ioctlsocket(fd, FIONBIO, &mode) != 0) {
        fprintf(stderr, "ioctlsocket FIONBIO failed: %d\n", WSAGetLastError());
        return CO_ERROR_PLATFORM;
    }
    return CO_OK;
}

// ============================================================================
// 协程式 I/O API 实现
// ============================================================================

/**
 * @brief 等待 I/O 完成（内部辅助函数）
 */
static co_error_t co_wait_io_async(co_socket_t fd, co_io_op_t op_type, 
                                   void *buffer, size_t buffer_size,
                                   int64_t timeout_ms,
                                   size_t *out_bytes_transferred) {
    // 获取当前协程和调度器
    co_scheduler_t *sched = co_current_scheduler();
    co_routine_t *current = co_current_routine();
    
    if (!sched || !current || !sched->iomux) {
        return CO_ERROR_INVAL;
    }
    
    // 初始化超时状态（Windows IOCP 超时完整实现见下方说明）
    current->timed_out = false;
    current->io_waiting = false;
    // TODO(Windows I/O 超时): IOCP 超时比 epoll 复杂得多，需要以下步骤：
    //   1. 将协程注册到定时器堆（wakeup_time = now + timeout_ms，io_waiting = true）
    //   2. 定时器触发时：调度器的 timer handler 设置 timed_out = true 并唤醒协程
    //   3. 协程在 co_wait_io_async 中检测到 timed_out：
    //      a. 调用 CancelIoEx((HANDLE)fd, &wait_ctx.overlapped) 请求取消未完成的异步 I/O
    //      b. 做第二次 co_context_swap 等待 IOCP 投递取消完成包（ERROR_OPERATION_ABORTED）
    //         （此步骤至关重要：wait_ctx 是栈上变量，IOCP 必须完成对它的访问才能安全返回）
    //      c. 第二次恢复后返回 CO_ERROR_TIMEOUT
    //   当前行为：timeout_ms 被忽略，co_wait_io_async 无限等待直到 I/O 完成。

    // 创建等待上下文
    co_io_wait_ctx_t wait_ctx;
    memset(&wait_ctx, 0, sizeof(wait_ctx));
    wait_ctx.fd = fd;
    wait_ctx.op_type = op_type;
    wait_ctx.buffer = buffer;
    wait_ctx.buffer_size = buffer_size;
    wait_ctx.timeout_ms = timeout_ms;
    wait_ctx.routine = current;
    
    // 关联到 IOCP（如果尚未关联）
    if (co_iomux_add(sched->iomux, &wait_ctx) != CO_OK) {
        return CO_ERROR_PLATFORM;
    }
    
    // 投递异步 I/O 操作
    DWORD bytes_transferred = 0;
    DWORD flags = 0;
    BOOL success = FALSE;
    
    if (op_type == CO_IO_OP_READ) {
        // 异步读取
        WSABUF wsa_buf;
        wsa_buf.buf = (char *)buffer;
        wsa_buf.len = (ULONG)buffer_size;
        
        int ret = WSARecv(fd, &wsa_buf, 1, &bytes_transferred, &flags,
                         &wait_ctx.overlapped, NULL);
        
        if (ret == 0) {
            // 立即完成（很少见）
            success = TRUE;
        } else if (WSAGetLastError() == WSA_IO_PENDING) {
            // 异步操作已投递
            success = TRUE;
        } else {
            // 错误
            fprintf(stderr, "WSARecv failed: %d\n", WSAGetLastError());
            return CO_ERROR_PLATFORM;
        }
        
    } else if (op_type == CO_IO_OP_WRITE) {
        // 异步写入
        WSABUF wsa_buf;
        wsa_buf.buf = (char *)buffer;
        wsa_buf.len = (ULONG)buffer_size;
        
        int ret = WSASend(fd, &wsa_buf, 1, &bytes_transferred, flags,
                         &wait_ctx.overlapped, NULL);
        
        if (ret == 0) {
            success = TRUE;
        } else if (WSAGetLastError() == WSA_IO_PENDING) {
            success = TRUE;
        } else {
            fprintf(stderr, "WSASend failed: %d\n", WSAGetLastError());
            return CO_ERROR_PLATFORM;
        }
    }
    
    if (!success) {
        return CO_ERROR_PLATFORM;
    }
    
    // 标记协程为等待状态（不加入就绪队列）
    current->state = CO_STATE_WAITING;
    sched->waiting_io_count++;
    
    // 直接切换回调度器，等待 IOCP 完成事件唤醒
    // 注意：不能用 co_yield()，因为它会将协程重新加入就绪队列
    co_context_swap(&current->context, &sched->main_ctx);
    
    // 恢复执行时，I/O 已完成
    if (wait_ctx.revents & CO_IO_ERROR) {
        return CO_ERROR;
    }
    
    if (out_bytes_transferred) {
        *out_bytes_transferred = wait_ctx.bytes_transferred;
    }
    
    return CO_OK;
}

ssize_t co_read(co_socket_t fd, void *buf, size_t count, int64_t timeout_ms) {
    if (fd == CO_INVALID_SOCKET || !buf || count == 0) {
        WSASetLastError(WSAEINVAL);
        return -1;
    }
    
    size_t bytes_read = 0;
    co_error_t err = co_wait_io_async(fd, CO_IO_OP_READ, buf, count, 
                                      timeout_ms, &bytes_read);
    
    if (err != CO_OK) {
        if (err == CO_ERROR_TIMEOUT) {
            WSASetLastError(WSAETIMEDOUT);
        }
        return -1;
    }
    
    return (ssize_t)bytes_read;
}

ssize_t co_write(co_socket_t fd, const void *buf, size_t count, int64_t timeout_ms) {
    if (fd == CO_INVALID_SOCKET || !buf || count == 0) {
        WSASetLastError(WSAEINVAL);
        return -1;
    }
    
    size_t bytes_written = 0;
    co_error_t err = co_wait_io_async(fd, CO_IO_OP_WRITE, (void *)buf, count,
                                      timeout_ms, &bytes_written);
    
    if (err != CO_OK) {
        if (err == CO_ERROR_TIMEOUT) {
            WSASetLastError(WSAETIMEDOUT);
        }
        return -1;
    }
    
    return (ssize_t)bytes_written;
}

co_socket_t co_accept(co_socket_t sockfd, void *addr, socklen_t *addrlen, int64_t timeout_ms) {
    if (sockfd == CO_INVALID_SOCKET) {
        WSASetLastError(WSAEINVAL);
        return CO_INVALID_SOCKET;
    }
    
    // 确保扩展函数已加载
    if (!load_extension_functions()) {
        return CO_INVALID_SOCKET;
    }
    
    // 获取当前协程和调度器
    co_scheduler_t *sched = co_current_scheduler();
    co_routine_t *current = co_current_routine();
    
    if (!sched || !current || !sched->iomux) {
        WSASetLastError(WSAEINVAL);
        return CO_INVALID_SOCKET;
    }
    
    // 预先创建客户端套接字（AcceptEx 要求）
    co_socket_t client_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client_fd == CO_INVALID_SOCKET) {
        return CO_INVALID_SOCKET;
    }
    
    // 堆分配等待上下文（必须在整个异步操作期间存活）
    co_io_wait_ctx_t *wait_ctx = (co_io_wait_ctx_t *)calloc(1, sizeof(co_io_wait_ctx_t));
    if (!wait_ctx) {
        closesocket(client_fd);
        return CO_INVALID_SOCKET;
    }
    
    wait_ctx->fd = sockfd;
    wait_ctx->op_type = CO_IO_OP_ACCEPT;
    wait_ctx->timeout_ms = timeout_ms;
    wait_ctx->routine = current;
    wait_ctx->accept_socket = client_fd;
    
    // 尝试关联监听套接字到 IOCP（如果尚未关联）
    // 如果已经关联过，co_iomux_add 会跳过
    if (co_iomux_add(sched->iomux, wait_ctx) != CO_OK) {
        fprintf(stderr, "Failed to associate listen socket\n");
        closesocket(client_fd);
        free(wait_ctx);
        return CO_INVALID_SOCKET;
    }
    
    // 投递 AcceptEx 操作
    DWORD bytes_received = 0;
    DWORD addr_len = sizeof(struct sockaddr_in) + 16;
    
    BOOL ret = g_AcceptEx(
        sockfd,
        client_fd,
        wait_ctx->accept_buffer,
        0,
        addr_len,
        addr_len,
        &bytes_received,
        &wait_ctx->overlapped
    );
    
    if (!ret) {
        DWORD error = WSAGetLastError();
        if (error != WSA_IO_PENDING) {
            fprintf(stderr, "AcceptEx failed immediately: %d\n", error);
            closesocket(client_fd);
            free(wait_ctx);
            return CO_INVALID_SOCKET;
        }
    }
    
    // 标记协程为等待状态（不加入就绪队列）
    current->state = CO_STATE_WAITING;
    sched->waiting_io_count++;
    
    // 直接切换回调度器，等待 IOCP 完成事件唤醒
    // 注意：不能用 co_yield()，因为它会将协程重新加入就绪队列
    co_context_swap(&current->context, &sched->main_ctx);
    
    // 恢复执行时，AcceptEx 已完成
    if (wait_ctx->revents & CO_IO_ERROR) {
        fprintf(stderr, "AcceptEx completed with error\n");
        closesocket(client_fd);
        free(wait_ctx);
        return CO_INVALID_SOCKET;
    }
    
    // AcceptEx 成功，更新客户端套接字上下文
    if (setsockopt(client_fd, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                   (char *)&sockfd, sizeof(sockfd)) != 0) {
        fprintf(stderr, "setsockopt SO_UPDATE_ACCEPT_CONTEXT failed: %d\n", WSAGetLastError());
        closesocket(client_fd);
        free(wait_ctx);
        return CO_INVALID_SOCKET;
    }
    
    // 提取客户端地址（如果需要）
    if (addr && addrlen) {
        struct sockaddr *local_addr = NULL;
        struct sockaddr *remote_addr = NULL;
        INT local_addr_len = 0;
        INT remote_addr_len = 0;
        
        g_GetAcceptExSockaddrs(
            wait_ctx->accept_buffer,
            0,
            addr_len,
            addr_len,
            &local_addr,
            &local_addr_len,
            &remote_addr,
            &remote_addr_len
        );
        
        if (remote_addr && remote_addr_len > 0) {
            int copy_len = (remote_addr_len < (INT)*addrlen) ? remote_addr_len : (INT)*addrlen;
            memcpy(addr, remote_addr, copy_len);
            *addrlen = copy_len;
        }
    }
    
    // 释放等待上下文
    free(wait_ctx);
    
    return client_fd;
}

int co_connect(co_socket_t sockfd, const void *addr, socklen_t addrlen, int64_t timeout_ms) {
    if (sockfd == CO_INVALID_SOCKET || !addr) {
        WSASetLastError(WSAEINVAL);
        return -1;
    }
    
    // 确保扩展函数已加载
    if (!load_extension_functions()) {
        return -1;
    }
    
    // 获取当前协程和调度器
    co_scheduler_t *sched = co_current_scheduler();
    co_routine_t *current = co_current_routine();
    
    if (!sched || !current || !sched->iomux) {
        WSASetLastError(WSAEINVAL);
        return -1;
    }
    
    // 堆分配等待上下文
    co_io_wait_ctx_t *wait_ctx = (co_io_wait_ctx_t *)calloc(1, sizeof(co_io_wait_ctx_t));
    if (!wait_ctx) {
        return -1;
    }
    
    // ConnectEx 要求套接字必须先 bind 到本地地址
    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = 0;
    
    if (bind(sockfd, (struct sockaddr *)&local_addr, sizeof(local_addr)) != 0) {
        int error = WSAGetLastError();
        if (error != WSAEINVAL) {
            fprintf(stderr, "bind failed before ConnectEx: %d\n", error);
            free(wait_ctx);
            return -1;
        }
    }
    
    wait_ctx->fd = sockfd;
    wait_ctx->op_type = CO_IO_OP_CONNECT;
    wait_ctx->timeout_ms = timeout_ms;
    wait_ctx->routine = current;
    
    // 关联套接字到 IOCP
    co_iomux_add(sched->iomux, wait_ctx);
    
    // 投递 ConnectEx 操作
    DWORD bytes_sent = 0;
    
    BOOL ret = g_ConnectEx(
        sockfd,
        (const struct sockaddr *)addr,
        addrlen,
        NULL,
        0,
        &bytes_sent,
        &wait_ctx->overlapped
    );
    
    if (!ret) {
        DWORD error = WSAGetLastError();
        if (error != WSA_IO_PENDING) {
            fprintf(stderr, "ConnectEx failed: %d\n", error);
            free(wait_ctx);
            return -1;
        }
    }
    
    // 标记协程为等待状态（不加入就绪队列）
    current->state = CO_STATE_WAITING;
    sched->waiting_io_count++;
    
    // 直接切换回调度器，等待 IOCP 完成事件唤醒
    // 注意：不能用 co_yield()，因为它会将协程重新加入就绪队列
    co_context_swap(&current->context, &sched->main_ctx);
    
    // 恢复执行时，ConnectEx 已完成
    if (wait_ctx->revents & CO_IO_ERROR) {
        free(wait_ctx);
        return -1;
    }
    
    // ConnectEx 成功，更新套接字上下文
    if (setsockopt(sockfd, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT,
                   NULL, 0) != 0) {
        fprintf(stderr, "setsockopt SO_UPDATE_CONNECT_CONTEXT failed: %d\n", WSAGetLastError());
        free(wait_ctx);
        return -1;
    }
    
    // 释放等待上下文
    free(wait_ctx);
    
    return 0;
}

#endif // _WIN32
