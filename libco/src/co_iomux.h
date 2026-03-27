/**
 * @file co_iomux.h
 * @brief Unified I/O multiplexing interface
 * 
 * Provides a cross-platform abstraction for I/O multiplexing:
 * - Linux: epoll
 * - macOS: kqueue
 * - Windows: IOCP (I/O Completion Ports)
 * 
 * Week 7 implementation goals:
 * 1. A unified event registration and deregistration API
 * 2. Integration with the scheduler event loop
 * 3. Coroutine-friendly blocking I/O (co_read/co_write)
 * 4. Timeout support
 */

#ifndef LIBCO_CO_IOMUX_H
#define LIBCO_CO_IOMUX_H

#include <libco/co.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
typedef SOCKET co_socket_t;
#define CO_INVALID_SOCKET INVALID_SOCKET
#else
typedef int co_socket_t;
#define CO_INVALID_SOCKET (-1)
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Type definitions
// ============================================================================

/**
 * @brief I/O event types
 */
typedef enum co_io_event {
    CO_IO_READ  = 0x01,     /**< Readable event */
    CO_IO_WRITE = 0x02,     /**< Writable event */
    CO_IO_ERROR = 0x04,     /**< Error event */
} co_io_event_t;

/**
 * @brief I/O operation types used by Windows IOCP
 */
typedef enum co_io_op {
    CO_IO_OP_READ = 1,      /**< Read operation */
    CO_IO_OP_WRITE = 2,     /**< Write operation */
    CO_IO_OP_ACCEPT = 3,    /**< Accept operation */
    CO_IO_OP_CONNECT = 4,   /**< Connect operation */
} co_io_op_t;

/**
 * @brief Opaque I/O multiplexer handle
 */
typedef struct co_iomux co_iomux_t;

// Forward declaration
struct co_routine;

/**
 * @brief I/O wait context
 * 
 * Attached to a coroutine while it is waiting for I/O, recording the target
 * file descriptor and the events of interest.
 */
typedef struct co_io_wait_ctx {
    co_socket_t fd;             /**< File descriptor or socket being waited on */
    uint32_t events;            /**< Requested events (CO_IO_READ | CO_IO_WRITE) */
    uint32_t revents;           /**< Events that actually occurred */
    int64_t timeout_ms;         /**< Timeout in milliseconds, or -1 for no limit */
    uint64_t deadline_ms;       /**< Absolute deadline in milliseconds */
    struct co_routine *routine; /**< Waiting coroutine */
    
    // Windows IOCP-specific fields
#ifdef _WIN32
    OVERLAPPED overlapped;      /**< IOCP overlapped structure */
    co_io_op_t op_type;         /**< Operation type */
    void *buffer;               /**< I/O buffer */
    size_t buffer_size;         /**< Buffer size */
    size_t bytes_transferred;   /**< Bytes transferred */
    
    // AcceptEx-specific fields
    co_socket_t accept_socket;  /**< Accepted client socket for AcceptEx */
    char accept_buffer[128];    /**< Address buffer for AcceptEx */
#endif
} co_io_wait_ctx_t;

// ============================================================================
// I/O multiplexer API
// ============================================================================

/**
 * @brief Create an I/O multiplexer
 * @param max_events Maximum number of events as a hint; the implementation may ignore it
 * @return I/O multiplexer handle, or NULL on failure
 */
co_iomux_t *co_iomux_create(int max_events);

/**
 * @brief Destroy an I/O multiplexer
 * @param iomux I/O multiplexer handle
 */
void co_iomux_destroy(co_iomux_t *iomux);

/**
 * @brief Register interest in I/O events
 * 
 * Register a file descriptor and the events to watch with the multiplexer.
 * 
 * @param iomux I/O multiplexer handle
 * @param wait_ctx I/O wait context containing fd, events, routine, and related state
 * @return CO_OK on success, other values indicate errors
 */
co_error_t co_iomux_add(co_iomux_t *iomux, co_io_wait_ctx_t *wait_ctx);

/**
 * @brief Modify registered I/O events
 * @param iomux I/O multiplexer handle
 * @param wait_ctx I/O wait context
 * @return CO_OK on success, other values indicate errors
 */
co_error_t co_iomux_mod(co_iomux_t *iomux, co_io_wait_ctx_t *wait_ctx);

/**
 * @brief Unregister I/O event interest
 * @param iomux I/O multiplexer handle
 * @param fd File descriptor
 * @return CO_OK on success, other values indicate errors
 */
co_error_t co_iomux_del(co_iomux_t *iomux, co_socket_t fd);

/**
 * @brief Poll for I/O events
 * 
 * Check for ready I/O events and wake the corresponding coroutines.
 * 
 * @param iomux I/O multiplexer handle
 * @param timeout_ms Timeout in milliseconds; 0 is non-blocking, -1 waits indefinitely
 * @param out_ready_count [out] Number of ready events
 * @return CO_OK on success, CO_ERROR_TIMEOUT on timeout, other values indicate errors
 */
co_error_t co_iomux_poll(co_iomux_t *iomux, int timeout_ms, int *out_ready_count);

/**
 * @brief Set a file descriptor to non-blocking mode
 * @param fd File descriptor
 * @return CO_OK on success, other values indicate errors
 */
co_error_t co_set_nonblocking(co_socket_t fd);

/**
 * @brief Set a file descriptor to blocking mode
 * @param fd File descriptor
 * @return CO_OK on success, other values indicate errors
 */
co_error_t co_set_blocking(co_socket_t fd);

// ============================================================================
// Coroutine-friendly I/O API (Week 7 core functionality)
// ============================================================================

/**
 * @brief Coroutine-aware read
 * 
 * Read data from a file descriptor. If data is not ready, suspend the current
 * coroutine until it becomes readable or the operation times out.
 * 
 * @param fd File descriptor
 * @param buf Buffer
 * @param count Number of bytes to read
 * @param timeout_ms Timeout in milliseconds, or -1 to wait indefinitely
 * @return Number of bytes read, or -1 on error (check errno/WSAGetLastError)
 */
ssize_t co_read(co_socket_t fd, void *buf, size_t count, int64_t timeout_ms);

/**
 * @brief Coroutine-aware write
 * 
 * Write data to a file descriptor. If the buffer is full, suspend the current
 * coroutine until it becomes writable or the operation times out.
 * 
 * @param fd File descriptor
 * @param buf Buffer
 * @param count Number of bytes to write
 * @param timeout_ms Timeout in milliseconds, or -1 to wait indefinitely
 * @return Number of bytes written, or -1 on error
 */
ssize_t co_write(co_socket_t fd, const void *buf, size_t count, int64_t timeout_ms);

/**
 * @brief Coroutine-aware accept
 * 
 * Accept a new client connection. If no connection arrives, suspend the
 * current coroutine.
 * 
 * @param sockfd Listening socket
 * @param addr [out] Client address, or NULL
 * @param addrlen [in/out] Address length, or NULL
 * @param timeout_ms Timeout in milliseconds, or -1 to wait indefinitely
 * @return New file descriptor, or -1 on error
 */
co_socket_t co_accept(co_socket_t sockfd, void *addr, socklen_t *addrlen, int64_t timeout_ms);

/**
 * @brief Coroutine-aware connect
 * 
 * Connect to a remote server. If the connection does not complete
 * immediately, suspend the current coroutine.
 * 
 * @param sockfd Socket
 * @param addr Server address
 * @param addrlen Address length
 * @param timeout_ms Timeout in milliseconds, or -1 to wait indefinitely
 * @return 0 on success, -1 on failure
 */
int co_connect(co_socket_t sockfd, const void *addr, socklen_t addrlen, int64_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif // LIBCO_CO_IOMUX_H
