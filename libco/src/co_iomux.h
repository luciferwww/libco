/**
 * @file co_iomux.h
 * @brief I/O 多路复用统一接口
 * 
 * 提供跨平台的 I/O 多路复用抽象层：
 * - Linux: epoll
 * - macOS: kqueue
 * - Windows: IOCP (I/O Completion Ports)
 * 
 * Week 7 实现目标：
 * 1. 统一的事件注册/注销 API
 * 2. 集成到调度器的事件循环
 * 3. 协程式的阻塞 I/O（co_read/co_write）
 * 4. 超时支持
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
// 类型定义
// ============================================================================

/**
 * @brief I/O 事件类型
 */
typedef enum co_io_event {
    CO_IO_READ  = 0x01,     /**< 可读事件 */
    CO_IO_WRITE = 0x02,     /**< 可写事件 */
    CO_IO_ERROR = 0x04,     /**< 错误事件 */
} co_io_event_t;

/**
 * @brief I/O 操作类型（用于 Windows IOCP）
 */
typedef enum co_io_op {
    CO_IO_OP_READ = 1,      /**< 读操作 */
    CO_IO_OP_WRITE = 2,     /**< 写操作 */
    CO_IO_OP_ACCEPT = 3,    /**< 接受连接 */
    CO_IO_OP_CONNECT = 4,   /**< 建立连接 */
} co_io_op_t;

/**
 * @brief 不透明的 I/O 多路复用器句柄
 */
typedef struct co_iomux co_iomux_t;

// 前向声明
struct co_routine;

/**
 * @brief I/O 等待上下文
 * 
 * 当协程等待 I/O 时，将此结构挂在协程上，
 * 记录等待的文件描述符和事件类型。
 */
typedef struct co_io_wait_ctx {
    co_socket_t fd;             /**< 等待的文件描述符/套接字 */
    uint32_t events;            /**< 等待的事件（CO_IO_READ | CO_IO_WRITE） */
    uint32_t revents;           /**< 实际发生的事件 */
    int64_t timeout_ms;         /**< 超时时间（毫秒），-1 表示无限等待 */
    uint64_t deadline_ms;       /**< 绝对截止时间（毫秒时间戳） */
    struct co_routine *routine; /**< 等待的协程 */
    
    // Windows IOCP 专用字段
#ifdef _WIN32
    OVERLAPPED overlapped;      /**< IOCP 重叠结构 */
    co_io_op_t op_type;         /**< 操作类型 */
    void *buffer;               /**< I/O 缓冲区 */
    size_t buffer_size;         /**< 缓冲区大小 */
    size_t bytes_transferred;   /**< 已传输字节数 */
    
    // AcceptEx 专用字段
    co_socket_t accept_socket;  /**< AcceptEx 的客户端套接字 */
    char accept_buffer[128];    /**< AcceptEx 地址缓冲区 */
#endif
} co_io_wait_ctx_t;

// ============================================================================
// I/O 多路复用器 API
// ============================================================================

/**
 * @brief 创建 I/O 多路复用器
 * @param max_events 最大事件数（提示值，实现可能忽略）
 * @return I/O  多路复用器句柄，失败返回 NULL
 */
co_iomux_t *co_iomux_create(int max_events);

/**
 * @brief 销毁 I/O 多路复用器
 * @param iomux I/O 多路复用器句柄
 */
void co_iomux_destroy(co_iomux_t *iomux);

/**
 * @brief 注册 I/O 事件监听
 * 
 * 将文件描述符和感兴趣的事件注册到多路复用器。
 * 
 * @param iomux I/O 多路复用器句柄
 * @param wait_ctx I/O 等待上下文（包含 fd、events、routine 等）
 * @return CO_OK 成功，其他值表示错误
 */
co_error_t co_iomux_add(co_iomux_t *iomux, co_io_wait_ctx_t *wait_ctx);

/**
 * @brief 修改 I/O 事件监听
 * @param iomux I/O 多路复用器句柄
 * @param wait_ctx I/O 等待上下文
 * @return CO_OK 成功，其他值表示错误
 */
co_error_t co_iomux_mod(co_iomux_t *iomux, co_io_wait_ctx_t *wait_ctx);

/**
 * @brief 注销 I/O 事件监听
 * @param iomux I/O 多路复用器句柄
 * @param fd 文件描述符
 * @return CO_OK 成功，其他值表示错误
 */
co_error_t co_iomux_del(co_iomux_t *iomux, co_socket_t fd);

/**
 * @brief 轮询 I/O 事件
 * 
 * 检查是否有 I/O 事件就绪，并唤醒对应的协程。
 * 
 * @param iomux I/O 多路复用器句柄
 * @param timeout_ms 超时时间（毫秒），0 表示非阻塞立即返回，-1 表示无限等待
 * @param out_ready_count [out] 就绪的事件数量
 * @return CO_OK 成功，CO_ERROR_TIMEOUT 超时，其他值表示错误
 */
co_error_t co_iomux_poll(co_iomux_t *iomux, int timeout_ms, int *out_ready_count);

/**
 * @brief 设置文件描述符为非阻塞模式
 * @param fd 文件描述符
 * @return CO_OK 成功，其他值表示错误
 */
co_error_t co_set_nonblocking(co_socket_t fd);

/**
 * @brief 设置文件描述符为阻塞模式
 * @param fd 文件描述符
 * @return CO_OK 成功，其他值表示错误
 */
co_error_t co_set_blocking(co_socket_t fd);

// ============================================================================
// 协程式 I/O API（Week 7 核心功能）
// ============================================================================

/**
 * @brief 协程式读取
 * 
 * 从文件描述符读取数据。如果数据未就绪，挂起当前协程，
 * 直到数据可读或超时。
 * 
 * @param fd 文件描述符
 * @param buf 缓冲区
 * @param count 要读取的字节数
 * @param timeout_ms 超时时间（毫秒），-1 表示无限等待
 * @return 实际读取的字节数，-1 表示错误（检查 errno/WSAGetLastError）
 */
ssize_t co_read(co_socket_t fd, void *buf, size_t count, int64_t timeout_ms);

/**
 * @brief 协程式写入
 * 
 * 向文件描述符写入数据。如果缓冲区已满，挂起当前协程，
 * 直到可写或超时。
 * 
 * @param fd 文件描述符
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
 * @return 新连接的文件描述符，-1 表示错误
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

#ifdef __cplusplus
}
#endif

#endif // LIBCO_CO_IOMUX_H
