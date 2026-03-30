/**
 * @file iomux_iocp.c
 * @brief Windows IOCP (I/O Completion Ports) implementation
 * 
 * Implements I/O multiplexing with Windows IOCP.
 * IOCP is Windows' high-performance asynchronous I/O mechanism.
 * 
 * Note: IOCP is fully asynchronous, while epoll/kqueue are synchronous
 * non-blocking models. This file adapts IOCP's asynchronous model to a
 * synchronous coroutine-friendly style.
 */

#ifdef _WIN32

// GetQueuedCompletionStatusEx requires Windows Vista (0x0600) or newer
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

// Link Winsock libraries
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "mswsock.lib")

// ============================================================================
// IOCP implementation structure
// ============================================================================

/**
 * @brief IOCP I/O multiplexer
 */
struct co_iomux {
    HANDLE iocp;                    /**< IOCP handle */
    int max_events;                 /**< Maximum completions returned by one poll */
    OVERLAPPED_ENTRY *entries;      /**< Completion packet buffer for GetQueuedCompletionStatusEx */

    // Winsock initialization flag
    bool wsa_initialized;
    
    // Associated socket set, simplified as a fixed array.
    // Real-world code should use a hash table.
    co_socket_t associated_sockets[1024];
    int associated_count;
};

// ============================================================================
// AcceptEx/ConnectEx function pointers loaded at runtime
// ============================================================================

// AcceptEx function prototype
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

// ConnectEx function prototype
typedef BOOL (WINAPI *LPFN_CONNECTEX)(
    SOCKET s,
    const struct sockaddr *name,
    int namelen,
    PVOID lpSendBuffer,
    DWORD dwSendDataLength,
    LPDWORD lpdwBytesSent,
    LPOVERLAPPED lpOverlapped
);

// GetAcceptExSockaddrs function prototype
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

// Global function pointers loaded lazily
static LPFN_ACCEPTEX g_AcceptEx = NULL;
static LPFN_CONNECTEX g_ConnectEx = NULL;
static LPFN_GETACCEPTEXSOCKADDRS g_GetAcceptExSockaddrs = NULL;
static bool g_ext_funcs_loaded = false;

/**
 * @brief Load AcceptEx and ConnectEx extension functions
 */
static bool load_extension_functions(void) {
    if (g_ext_funcs_loaded) {
        return true;
    }
    
    // Create a temporary socket to query the function pointers
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "Failed to create temp socket for extension functions\n");
        return false;
    }
    
    DWORD bytes = 0;
    
    // Query the AcceptEx function pointer
    GUID acceptex_guid = WSAID_ACCEPTEX;
    if (WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
                 &acceptex_guid, sizeof(acceptex_guid),
                 &g_AcceptEx, sizeof(g_AcceptEx),
                 &bytes, NULL, NULL) != 0) {
        fprintf(stderr, "Failed to get AcceptEx function pointer: %d\n", WSAGetLastError());
        closesocket(sock);
        return false;
    }
    
    // Query the ConnectEx function pointer
    GUID connectex_guid = WSAID_CONNECTEX;
    if (WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
                 &connectex_guid, sizeof(connectex_guid),
                 &g_ConnectEx, sizeof(g_ConnectEx),
                 &bytes, NULL, NULL) != 0) {
        fprintf(stderr, "Failed to get ConnectEx function pointer: %d\n", WSAGetLastError());
        closesocket(sock);
        return false;
    }
    
    // Query the GetAcceptExSockaddrs function pointer
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
// Global Winsock initialization
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
// I/O multiplexer implementation
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
    
    // Create the IOCP.
    // Parameters:
    // 1. INVALID_HANDLE_VALUE - create a new IOCP
    // 2. NULL - existing IOCP handle, omitted here
    // 3. 0 - concurrency level, meaning use the CPU core count
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

    // Allocate the completion packet buffer
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
    
    // Check whether the socket has already been associated
    bool already_associated = false;
    for (int i = 0; i < iomux->associated_count; i++) {
        if (iomux->associated_sockets[i] == wait_ctx->fd) {
            already_associated = true;
            break;
        }
    }
    
    if (already_associated) {
        // Already associated, nothing to do
        return CO_OK;
    }
    
    // Associate the socket with the IOCP.
    // Parameters:
    // 1. (HANDLE)wait_ctx->fd - file handle, sockets can be cast to HANDLE
    // 2. iomux->iocp - the IOCP handle
    // 3. 0 - completion key; OVERLAPPED is used to recover wait_ctx instead
    // 4. 0 - keep the concurrency level from IOCP creation
    //
    // CompletionKey is left as 0 because AcceptEx on a listening socket may
    // use a different wait_ctx for each operation. The OVERLAPPED pointer is
    // used to identify the specific wait_ctx.
    HANDLE h = CreateIoCompletionPort((HANDLE)wait_ctx->fd, iomux->iocp, 0, 0);
    if (h == NULL) {
        fprintf(stderr, "CreateIoCompletionPort (associate) failed: %lu\n", GetLastError());
        return CO_ERROR_PLATFORM;
    }
    
    // Record the associated socket
    if (iomux->associated_count < 1024) {
        iomux->associated_sockets[iomux->associated_count++] = wait_ctx->fd;
    }
    
    // Initialize the OVERLAPPED structure
    memset(&wait_ctx->overlapped, 0, sizeof(wait_ctx->overlapped));
    
    return CO_OK;
}

co_error_t co_iomux_mod(co_iomux_t *iomux, co_io_wait_ctx_t *wait_ctx) {
    // IOCP does not need a modify operation; every I/O issues a fresh async request
    (void)iomux;     // Unused
    (void)wait_ctx;  // Unused
    return CO_OK;
}

co_error_t co_iomux_del(co_iomux_t *iomux, co_socket_t fd) {
    // IOCP does not require explicit deletion.
    // Closing the socket removes the association automatically, but the local
    // bookkeeping array still needs to be updated.
    if (!iomux) {
        return CO_ERROR_INVAL;
    }
    for (int i = 0; i < iomux->associated_count; i++) {
        if (iomux->associated_sockets[i] == fd) {
            // Fill the gap with the last element for O(1) removal
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

    // Fetch completion packets in batches (Windows Vista+) to better match
    // epoll_wait semantics and avoid handling only one completion per call.
    BOOL ret = GetQueuedCompletionStatusEx(
        iomux->iocp,
        iomux->entries,
        (ULONG)iomux->max_events,
        &removed,
        timeout,
        FALSE   // fAlertable: do not use APC delivery
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

        // Recover the wait_ctx pointer from the OVERLAPPED member offset
        co_io_wait_ctx_t *wait_ctx =
            (co_io_wait_ctx_t *)((char *)overlapped - offsetof(co_io_wait_ctx_t, overlapped));

        if (!wait_ctx || !wait_ctx->routine) {
            continue;
        }

        wait_ctx->bytes_transferred = iomux->entries[i].dwNumberOfBytesTransferred;

        // Set the resulting event type
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

        // The Internal field stores NTSTATUS; non-zero indicates I/O failure
        if (iomux->entries[i].Internal != 0) {
            wait_ctx->revents |= CO_IO_ERROR;
        }

        // Wake the coroutine
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
// Helper functions
// ============================================================================

co_error_t co_set_nonblocking(co_socket_t fd) {
    // Set a Windows socket to non-blocking mode
    u_long mode = 1;  // 1 = non-blocking
    if (ioctlsocket(fd, FIONBIO, &mode) != 0) {
        fprintf(stderr, "ioctlsocket FIONBIO failed: %d\n", WSAGetLastError());
        return CO_ERROR_PLATFORM;
    }
    return CO_OK;
}

co_error_t co_set_blocking(co_socket_t fd) {
    u_long mode = 0;  // 0 = blocking
    if (ioctlsocket(fd, FIONBIO, &mode) != 0) {
        fprintf(stderr, "ioctlsocket FIONBIO failed: %d\n", WSAGetLastError());
        return CO_ERROR_PLATFORM;
    }
    return CO_OK;
}

// ============================================================================
// Coroutine-friendly I/O API implementation
// ============================================================================

/**
 * @brief Wait for I/O completion (internal helper)
 */
static co_error_t co_wait_io_async(co_socket_t fd, co_io_op_t op_type, 
                                   void *buffer, size_t buffer_size,
                                   int64_t timeout_ms,
                                   size_t *out_bytes_transferred) {
    // Get the current coroutine and scheduler
    co_scheduler_t *sched = co_current_scheduler();
    co_routine_t *current = co_current_routine();
    
    if (!sched || !current || !sched->iomux) {
        return CO_ERROR_INVAL;
    }
    
    // Initialize timeout state
    current->timed_out = false;
    current->io_waiting = false;

    // Register a timeout timer if a deadline was requested.
    // When the timer fires, the scheduler's timer handler will set:
    //   timed_out = true, io_waiting = false, waiting_io_count--
    // and enqueue the coroutine into ready_queue, resuming it from the
    // first co_context_swap below.
    if (timeout_ms >= 0) {
        current->wakeup_time = co_get_monotonic_time_ms() + (uint64_t)timeout_ms;
        current->io_waiting = true;
        if (!co_timer_heap_push(&sched->timer_heap, current)) {
            current->io_waiting = false;
            return CO_ERROR_NOMEM;
        }
    }

    // Create the wait context
    co_io_wait_ctx_t wait_ctx;
    memset(&wait_ctx, 0, sizeof(wait_ctx));
    wait_ctx.fd = fd;
    wait_ctx.op_type = op_type;
    wait_ctx.buffer = buffer;
    wait_ctx.buffer_size = buffer_size;
    wait_ctx.timeout_ms = timeout_ms;
    wait_ctx.routine = current;
    
    // Associate with the IOCP if needed
    if (co_iomux_add(sched->iomux, &wait_ctx) != CO_OK) {
        current->io_waiting = false;  // Timer is already in the heap; clearing the flag lets the timer handler skip this entry
        return CO_ERROR_PLATFORM;
    }
    
    // Submit the asynchronous I/O operation
    DWORD bytes_transferred = 0;
    DWORD flags = 0;
    BOOL success = FALSE;
    
    if (op_type == CO_IO_OP_READ) {
        // Asynchronous read
        WSABUF wsa_buf;
        wsa_buf.buf = (char *)buffer;
        wsa_buf.len = (ULONG)buffer_size;
        
        int ret = WSARecv(fd, &wsa_buf, 1, &bytes_transferred, &flags,
                         &wait_ctx.overlapped, NULL);
        
        if (ret == 0) {
            // Completed immediately, which is uncommon
            success = TRUE;
        } else if (WSAGetLastError() == WSA_IO_PENDING) {
            // Async operation successfully queued
            success = TRUE;
        } else {
            // Error
            fprintf(stderr, "WSARecv failed: %d\n", WSAGetLastError());
            return CO_ERROR_PLATFORM;
        }
        
    } else if (op_type == CO_IO_OP_WRITE) {
        // Asynchronous write
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
    
    // Mark the coroutine as waiting without requeueing it
    current->state = CO_STATE_WAITING;
    sched->waiting_io_count++;
    
    // First swap: yield to the scheduler and wait for an IOCP completion or a timeout.
    // co_yield() cannot be used because it would requeue the coroutine immediately.
    co_context_swap(&current->context, &sched->main_ctx);
    
    // -----------------------------------------------------------------------
    // Timeout path
    // -----------------------------------------------------------------------
    // When the scheduler's timer handler fires it sets timed_out=true,
    // io_waiting=false, decrements waiting_io_count, and enqueues this
    // coroutine into the ready_queue (state becomes READY).
    // At that point the IOCP operation is still in flight (WSARecv/WSASend
    // has not completed). We must cancel it and then perform a second
    // co_context_swap to wait for the cancellation completion packet,
    // guaranteeing that the stack-local wait_ctx (which contains the
    // embedded OVERLAPPED) is not destroyed before IOCP is done with it.
    if (current->timed_out) {
        // Request cancellation of the specific OVERLAPPED on fd.
        // - Success: IOCP will deliver a STATUS_CANCELLED (ERROR_OPERATION_ABORTED)
        //   completion packet shortly.
        // - ERROR_NOT_FOUND: the operation already completed on the kernel side;
        //   its completion packet is still in the IOCP queue, not yet consumed
        //   by iomux_poll (the single-threaded model guarantees this). IOCP will
        //   still deliver the packet.
        // Either way, a second swap is required to drain the completion packet.
        CancelIoEx((HANDLE)fd, &wait_ctx.overlapped);

        current->state = CO_STATE_WAITING;
        sched->waiting_io_count++;
        co_context_swap(&current->context, &sched->main_ctx);

        // Second resume: IOCP has delivered the completion packet
        // (STATUS_CANCELLED or a race-won normal completion). wait_ctx is
        // now safe and the stack frame can be released.
        return CO_ERROR_TIMEOUT;
    }

    // -----------------------------------------------------------------------
    // Normal completion path
    // -----------------------------------------------------------------------
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
    
    // Ensure extension functions are loaded
    if (!load_extension_functions()) {
        return CO_INVALID_SOCKET;
    }
    
    // Get the current coroutine and scheduler
    co_scheduler_t *sched = co_current_scheduler();
    co_routine_t *current = co_current_routine();
    
    if (!sched || !current || !sched->iomux) {
        WSASetLastError(WSAEINVAL);
        return CO_INVALID_SOCKET;
    }
    
    // Create the client socket up front as required by AcceptEx
    co_socket_t client_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client_fd == CO_INVALID_SOCKET) {
        return CO_INVALID_SOCKET;
    }
    
    // Heap-allocate the wait context so it survives the full async operation
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
    
    // Try associating the listening socket with IOCP if it is not already associated
    if (co_iomux_add(sched->iomux, wait_ctx) != CO_OK) {
        fprintf(stderr, "Failed to associate listen socket\n");
        closesocket(client_fd);
        free(wait_ctx);
        return CO_INVALID_SOCKET;
    }
    
    // Submit the AcceptEx operation
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
    
    // Mark the coroutine as waiting without requeueing it
    current->state = CO_STATE_WAITING;
    sched->waiting_io_count++;
    
    // Switch directly back to the scheduler and wait for IOCP completion.
    // co_yield() cannot be used because it would requeue the coroutine.
    co_context_swap(&current->context, &sched->main_ctx);
    
    // Once resumed, AcceptEx has completed
    if (wait_ctx->revents & CO_IO_ERROR) {
        fprintf(stderr, "AcceptEx completed with error\n");
        closesocket(client_fd);
        free(wait_ctx);
        return CO_INVALID_SOCKET;
    }
    
    // AcceptEx succeeded, update the accepted socket context
    if (setsockopt(client_fd, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                   (char *)&sockfd, sizeof(sockfd)) != 0) {
        fprintf(stderr, "setsockopt SO_UPDATE_ACCEPT_CONTEXT failed: %d\n", WSAGetLastError());
        closesocket(client_fd);
        free(wait_ctx);
        return CO_INVALID_SOCKET;
    }
    
    // Extract the client address if requested
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
    
    // Release the wait context
    free(wait_ctx);
    
    return client_fd;
}

int co_connect(co_socket_t sockfd, const void *addr, socklen_t addrlen, int64_t timeout_ms) {
    if (sockfd == CO_INVALID_SOCKET || !addr) {
        WSASetLastError(WSAEINVAL);
        return -1;
    }
    
    // Ensure extension functions are loaded
    if (!load_extension_functions()) {
        return -1;
    }
    
    // Get the current coroutine and scheduler
    co_scheduler_t *sched = co_current_scheduler();
    co_routine_t *current = co_current_routine();
    
    if (!sched || !current || !sched->iomux) {
        WSASetLastError(WSAEINVAL);
        return -1;
    }
    
    // Heap-allocate the wait context
    co_io_wait_ctx_t *wait_ctx = (co_io_wait_ctx_t *)calloc(1, sizeof(co_io_wait_ctx_t));
    if (!wait_ctx) {
        return -1;
    }
    
    // ConnectEx requires the socket to be bound to a local address first
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
    
    // Associate the socket with the IOCP
    co_iomux_add(sched->iomux, wait_ctx);
    
    // Submit the ConnectEx operation
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
    
    // Mark the coroutine as waiting without requeueing it
    current->state = CO_STATE_WAITING;
    sched->waiting_io_count++;
    
    // Switch directly back to the scheduler and wait for IOCP completion.
    // co_yield() cannot be used because it would requeue the coroutine.
    co_context_swap(&current->context, &sched->main_ctx);
    
    // Once resumed, ConnectEx has completed
    if (wait_ctx->revents & CO_IO_ERROR) {
        free(wait_ctx);
        return -1;
    }
    
    // ConnectEx succeeded, update the socket context
    if (setsockopt(sockfd, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT,
                   NULL, 0) != 0) {
        fprintf(stderr, "setsockopt SO_UPDATE_CONNECT_CONTEXT failed: %d\n", WSAGetLastError());
        free(wait_ctx);
        return -1;
    }
    
    // Release the wait context
    free(wait_ctx);
    
    return 0;
}

#endif // _WIN32
