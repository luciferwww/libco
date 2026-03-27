/**
 * @file co.h
 * @brief libco - High-performance stackful coroutine library for C
 * @version 2.0.0
 * 
 * libco 是一个高性能的有栈协程库，支持 Linux、macOS 和 Windows 平台。
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
// 类型定义
// ============================================================================

/**
 * @brief 错误码
 */
typedef enum co_error {
    CO_OK = 0,                  /**< 成功 */
    CO_ERROR = -1,              /**< 通用错误 */
    CO_ERROR_NOMEM = -2,        /**< 内存不足 */
    CO_ERROR_INVAL = -3,        /**< 无效参数 */
    CO_ERROR_PLATFORM = -4,     /**< 平台相关错误 */
    CO_ERROR_TIMEOUT = -5,      /**< 超时 */
    CO_ERROR_CANCELLED = -6,    /**< 已取消 */
    CO_ERROR_BUSY = -7,         /**< 资源暂时不可用（锁已被占用）*/
    CO_ERROR_CLOSED = -8,       /**< channel 已关闭 */
} co_error_t;

/**
 * @brief 协程状态
 */
typedef enum co_state {
    CO_STATE_READY = 0,         /**< 就绪 */
    CO_STATE_RUNNING,           /**< 运行中 */
    CO_STATE_SLEEPING,          /**< 休眠中 */
    CO_STATE_WAITING,           /**< 等待中 */
    CO_STATE_DEAD,              /**< 已结束 */
} co_state_t;

/**
 * @brief 不透明的调度器句柄
 */
typedef struct co_scheduler co_scheduler_t;

/**
 * @brief 不透明的协程句柄
 */
typedef struct co_routine co_routine_t;

/**
 * @brief 协程入口函数类型
 * @param arg 用户参数
 */
typedef void (*co_entry_func_t)(void *arg);

// ============================================================================
// 调度器 API
// ============================================================================

/**
 * @brief 创建调度器
 * @param config 配置参数（可为 NULL 使用默认配置）
 * @return 调度器句柄，失败返回 NULL
 */
co_scheduler_t *co_scheduler_create(void *config);

/**
 * @brief 销毁调度器
 * @param sched 调度器句柄
 */
void co_scheduler_destroy(co_scheduler_t *sched);

/**
 * @brief 运行调度器（阻塞直到所有协程结束）
 * @param sched 调度器句柄
 * @return CO_OK 成功，其他值表示错误
 */
co_error_t co_scheduler_run(co_scheduler_t *sched);

// ============================================================================
// 协程 API
// ============================================================================

/**
 * @brief 创建并启动协程
 * @param sched 调度器句柄，NULL 表示使用当前线程的调度器
 * @param entry 入口函数
 * @param arg 传递给入口函数的参数
 * @param stack_size 栈大小（0 = 使用默认值）
 * @return 协程句柄，失败返回 NULL
 */
co_routine_t *co_spawn(co_scheduler_t *sched,
                       co_entry_func_t entry,
                       void *arg,
                       size_t stack_size);

/**
 * @brief 让出 CPU（切换到其他协程）
 * @return CO_OK 成功，其他值表示错误
 *
 * @note `co_yield` 在 C++ 中（尤其是 MSVC C++17）是保留关键字。
 *       C++ 代码应直接调用 co_yield_now()。
 *       C 代码可以继续使用 co_yield()（宏别名，等同于 co_yield_now()）。
 */
co_error_t co_yield_now(void);

#ifndef __cplusplus
/** @brief co_yield_now 的 C 别名；C++ 中 co_yield 是保留关键字，不能用作标识符。 */
#  define co_yield co_yield_now
#endif

/**
 * @brief 休眠指定的毫秒数（Week 6）
 * 
 * 将当前协程挂起指定时间，期间调度器可以运行其他协程。
 * 时间到期后，协程会自动唤醒并继续执行。
 * 
 * @param msec 休眠时间（毫秒）
 * @return CO_OK 成功，其他值表示错误
 */
co_error_t co_sleep(uint32_t msec);

/**
 * @brief 获取当前协程
 * @return 当前协程句柄，如果不在协程中则返回 NULL
 */
co_routine_t *co_current(void);

/**
 * @brief 获取当前调度器
 * @return 当前调度器句柄，如果不在调度器中则返回 NULL
 */
co_scheduler_t *co_current_scheduler(void);

// ============================================================================
// I/O API（Week 7）
// ============================================================================

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET co_socket_t;
typedef int socklen_t;  // Windows 使用 int 作为 socklen_t
typedef SSIZE_T ssize_t;  // Windows 使用 SSIZE_T
#define CO_INVALID_SOCKET INVALID_SOCKET
#else
#include <sys/types.h>
#include <sys/socket.h>
typedef int co_socket_t;
#define CO_INVALID_SOCKET (-1)
#endif

/**
 * @brief 协程式读取
 * 
 * 从文件描述符读取数据。如果数据未就绪，挂起当前协程。
 * 
 * @param fd 文件描述符/套接字
 * @param buf 缓冲区
 * @param count 要读取的字节数
 * @param timeout_ms 超时时间（毫秒），-1 表示无限等待
 * @return 实际读取的字节数，-1 表示错误
 */
ssize_t co_read(co_socket_t fd, void *buf, size_t count, int64_t timeout_ms);

/**
 * @brief 协程式写入
 * 
 * 向文件描述符写入数据。如果缓冲区已满，挂起当前协程。
 * 
 * @param fd 文件描述符/套接字
 * @param buf 缓冲区
 * @param count 要写入的字节数
 * @param timeout_ms 超时时间（毫秒），-1 表示无限等待
 * @return 实际写入的字节数，-1 表示错误
 */
ssize_t co_write(co_socket_t fd, const void *buf, size_t count, int64_t timeout_ms);

/**
 * @brief 协程式接受连接
 * 
 * 接受新的客户端连接。如果没有连接到达，挂起当前协程。
 * 
 * @param sockfd 监听套接字
 * @param addr [out] 客户端地址（可为 NULL）
 * @param addrlen [in/out] 地址长度（可为 NULL）
 * @param timeout_ms 超时时间（毫秒），-1 表示无限等待
 * @return 新连接的套接字，-1 表示错误
 */
co_socket_t co_accept(co_socket_t sockfd, void *addr, socklen_t *addrlen, int64_t timeout_ms);

/**
 * @brief 协程式连接
 * 
 * 连接到远程服务器。如果连接未完成，挂起当前协程。
 * 
 * @param sockfd 套接字
 * @param addr 服务器地址
 * @param addrlen 地址长度
 * @param timeout_ms 超时时间（毫秒），-1 表示无限等待
 * @return 0 成功，-1 失败
 */
int co_connect(co_socket_t sockfd, const void *addr, socklen_t addrlen, int64_t timeout_ms);

// ============================================================================
// 版本信息
// ============================================================================

/**
 * @brief 获取版本字符串
 * @return 版本字符串（如 "2.0.0"）
 */
const char *co_version(void);

#ifdef __cplusplus
}
#endif

#endif // LIBCO_CO_H
