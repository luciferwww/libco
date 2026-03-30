/**
 * @file co.h
 * @brief libco - High-performance stackful coroutine library for C
 * @version 2.0.0
 * 
 * libco is a high-performance stackful coroutine library for Linux, macOS,
 * and Windows.
 */

#ifndef LIBCO_CO_H
#define LIBCO_CO_H

#include <libco/config.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Type definitions
// ============================================================================

/**
 * @brief Error codes
 */
typedef enum co_error {
    CO_OK = 0,                  /**< Success */
    CO_ERROR = -1,              /**< Generic error */
    CO_ERROR_NOMEM = -2,        /**< Out of memory */
    CO_ERROR_INVAL = -3,        /**< Invalid argument */
    CO_ERROR_PLATFORM = -4,     /**< Platform-specific error */
    CO_ERROR_TIMEOUT = -5,      /**< Timed out */
    CO_ERROR_CANCELLED = -6,    /**< Cancelled */
    CO_ERROR_BUSY = -7,         /**< Resource temporarily unavailable (lock already held) */
    CO_ERROR_CLOSED = -8,       /**< Channel is closed */
} co_error_t;

/**
 * @brief Coroutine state
 */
typedef enum co_state {
    CO_STATE_READY = 0,         /**< Ready */
    CO_STATE_RUNNING,           /**< Running */
    CO_STATE_SLEEPING,          /**< Sleeping */
    CO_STATE_WAITING,           /**< Waiting */
    CO_STATE_DEAD,              /**< Finished */
} co_state_t;

/**
 * @brief Opaque scheduler handle
 */
typedef struct co_scheduler co_scheduler_t;

/**
 * @brief Opaque coroutine handle
 */
typedef struct co_routine co_routine_t;

/**
 * @brief Coroutine entry function type
 * @param arg User argument
 */
typedef void (*co_entry_func_t)(void *arg);

// ============================================================================
// Scheduler API
// ============================================================================

/**
 * @brief Create a scheduler
 * @param config Configuration parameters, or NULL to use defaults
 * @return Scheduler handle, or NULL on failure
 */
co_scheduler_t *co_scheduler_create(void *config);

/**
 * @brief Destroy a scheduler
 * @param sched Scheduler handle
 */
void co_scheduler_destroy(co_scheduler_t *sched);

/**
 * @brief Run the scheduler until all coroutines finish
 * @param sched Scheduler handle
 * @return CO_OK on success, other values indicate errors
 */
co_error_t co_scheduler_run(co_scheduler_t *sched);

// ============================================================================
// Coroutine API
// ============================================================================

/**
 * @brief Create and start a coroutine
 * @param sched Scheduler handle, or NULL to use the current thread scheduler
 * @param entry Entry function
 * @param arg Argument passed to the entry function
 * @param stack_size Stack size, or 0 to use the default
 * @return Coroutine handle, or NULL on failure
 */
co_routine_t *co_spawn(co_scheduler_t *sched,
                       co_entry_func_t entry,
                       void *arg,
                       size_t stack_size);

/**
 * @brief Yield the CPU and switch to another coroutine
 * @return CO_OK on success, other values indicate errors
 *
 * @note `co_yield` is a reserved keyword in C++ (especially with MSVC C++17).
 *       C++ code should call co_yield_now() directly.
 *       C code can continue using co_yield() as a macro alias of
 *       co_yield_now().
 */
co_error_t co_yield_now(void);

#ifndef __cplusplus
/** @brief C alias for co_yield_now; in C++, co_yield is reserved and cannot be used as an identifier. */
#  define co_yield co_yield_now
#endif

/**
 * @brief Sleep for the specified number of milliseconds (Week 6)
 * 
 * Suspend the current coroutine for the specified duration while allowing the
 * scheduler to run other coroutines. The coroutine is resumed automatically
 * after the timeout expires.
 * 
 * @param msec Sleep time in milliseconds
 * @return CO_OK on success, other values indicate errors
 */
co_error_t co_sleep(uint32_t msec);

/**
 * @brief Get the current coroutine
 * @return Current coroutine handle, or NULL if not in a coroutine
 */
co_routine_t *co_current(void);

/**
 * @brief Get the current scheduler
 * @return Current scheduler handle, or NULL if not in a scheduler
 */
co_scheduler_t *co_current_scheduler(void);

// ============================================================================
// I/O API (Week 7)
// ============================================================================

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET co_socket_t;
typedef int socklen_t;  // Windows uses int for socklen_t
typedef SSIZE_T ssize_t;  // Windows uses SSIZE_T
#define CO_INVALID_SOCKET INVALID_SOCKET
#else
#include <sys/types.h>
#include <sys/socket.h>
typedef int co_socket_t;
#define CO_INVALID_SOCKET (-1)
#endif

/**
 * @brief Coroutine-aware read
 * 
 * Read data from a file descriptor. If data is not ready, suspend the current
 * coroutine.
 * 
 * @param fd File descriptor or socket
 * @param buf Buffer
 * @param count Number of bytes to read
 * @param timeout_ms Timeout in milliseconds, or -1 to wait indefinitely
 * @return Number of bytes read, or -1 on error
 */
ssize_t co_read(co_socket_t fd, void *buf, size_t count, int64_t timeout_ms);

/**
 * @brief Coroutine-aware write
 * 
 * Write data to a file descriptor. If the buffer is full, suspend the current
 * coroutine.
 * 
 * @param fd File descriptor or socket
 * @param buf Buffer
 * @param count Number of bytes to write
 * @param timeout_ms Timeout in milliseconds, or -1 to wait indefinitely
 * @return Number of bytes written, or -1 on error
 */
ssize_t co_write(co_socket_t fd, const void *buf, size_t count, int64_t timeout_ms);

/**
 * @brief Coroutine-aware accept
 * 
 * Accept a new client connection. If no connection is available, suspend the
 * current coroutine.
 * 
 * @param sockfd Listening socket
 * @param addr [out] Client address, or NULL
 * @param addrlen [in/out] Address length, or NULL
 * @param timeout_ms Timeout in milliseconds, or -1 to wait indefinitely
 * @return New connection socket, or -1 on error
 */
co_socket_t co_accept(co_socket_t sockfd, void *addr, socklen_t *addrlen, int64_t timeout_ms);

/**
 * @brief Coroutine-aware connect
 * 
 * Connect to a remote server. If the connection is still in progress,
 * suspend the current coroutine.
 * 
 * @param sockfd Socket
 * @param addr Server address
 * @param addrlen Address length
 * @param timeout_ms Timeout in milliseconds, or -1 to wait indefinitely
 * @return 0 on success, -1 on failure
 */
int co_connect(co_socket_t sockfd, const void *addr, socklen_t addrlen, int64_t timeout_ms);

// ============================================================================
// Version information
// ============================================================================

/**
 * @brief Get the version string
 * @return Version string, for example "2.0.0"
 */
const char *co_version(void);

#ifdef __cplusplus
}
#endif

#endif // LIBCO_CO_H
