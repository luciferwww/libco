/**
 * @file iomux_epoll.c
 * @brief Linux epoll 实现
 * 
 * 使用 Linux 的 epoll API 实现 I/O 多路复用。
 * epoll 是 Linux 特有的高性能事件通知机制，适合处理大量并发连接。
 */

#ifndef _WIN32  // 仅在 Linux/Unix 上编译

#include "../../co_iomux.h"
#include "../../co_routine.h"
#include "../../co_scheduler.h"
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

// ============================================================================
// epoll 实现结构
// ============================================================================

/**
 * @brief epoll I/O 多路复用器
 */
struct co_iomux {
    int epfd;                   /**< epoll 文件描述符 */
    int max_events;             /**< 最大事件数 */
    struct epoll_event *events; /**< 事件缓冲区 */
};

// ============================================================================
// I/O 多路复用器实现
// ============================================================================

co_iomux_t *co_iomux_create(int max_events) {
    if (max_events <= 0) {
        max_events = 1024;  // 默认值
    }
    
    co_iomux_t *iomux = (co_iomux_t *)calloc(1, sizeof(co_iomux_t));
    if (!iomux) {
        return NULL;
    }
    
    // 创建 epoll 实例
    iomux->epfd = epoll_create1(EPOLL_CLOEXEC);
    if (iomux->epfd == -1) {
        perror("epoll_create1");
        free(iomux);
        return NULL;
    }
    
    iomux->max_events = max_events;
    
    // 分配事件缓冲区
    iomux->events = (struct epoll_event *)calloc(max_events, sizeof(struct epoll_event));
    if (!iomux->events) {
        close(iomux->epfd);
        free(iomux);
        return NULL;
    }
    
    return iomux;
}

void co_iomux_destroy(co_iomux_t *iomux) {
    if (!iomux) {
        return;
    }
    
    if (iomux->epfd != -1) {
        close(iomux->epfd);
    }
    
    free(iomux->events);
    free(iomux);
}

co_error_t co_iomux_add(co_iomux_t *iomux, co_io_wait_ctx_t *wait_ctx) {
    if (!iomux || !wait_ctx) {
        return CO_ERROR_INVAL;
    }
    
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    
    // 转换事件类型
    ev.events = 0;
    if (wait_ctx->events & CO_IO_READ) {
        ev.events |= EPOLLIN;
    }
    if (wait_ctx->events & CO_IO_WRITE) {
        ev.events |= EPOLLOUT;
    }
    
    // 使用边缘触发 + 非阻塞模式（高性能）
    ev.events |= EPOLLET;
    
    // data.ptr 存储 wait_ctx，事件就绪时通过它唤醒协程
    ev.data.ptr = wait_ctx;
    
    // 添加到 epoll
    if (epoll_ctl(iomux->epfd, EPOLL_CTL_ADD, wait_ctx->fd, &ev) == -1) {
        if (errno == EEXIST) {
            // 已存在，尝试修改
            return co_iomux_mod(iomux, wait_ctx);
        }
        perror("epoll_ctl ADD");
        return CO_ERROR_PLATFORM;
    }
    
    return CO_OK;
}

co_error_t co_iomux_mod(co_iomux_t *iomux, co_io_wait_ctx_t *wait_ctx) {
    if (!iomux || !wait_ctx) {
        return CO_ERROR_INVAL;
    }
    
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    
    ev.events = 0;
    if (wait_ctx->events & CO_IO_READ) {
        ev.events |= EPOLLIN;
    }
    if (wait_ctx->events & CO_IO_WRITE) {
        ev.events |= EPOLLOUT;
    }
    ev.events |= EPOLLET;
    ev.data.ptr = wait_ctx;
    
    if (epoll_ctl(iomux->epfd, EPOLL_CTL_MOD, wait_ctx->fd, &ev) == -1) {
        perror("epoll_ctl MOD");
        return CO_ERROR_PLATFORM;
    }
    
    return CO_OK;
}

co_error_t co_iomux_del(co_iomux_t *iomux, co_socket_t fd) {
    if (!iomux || fd < 0) {
        return CO_ERROR_INVAL;
    }
    
    // epoll_ctl 的第四个参数在 DEL 时会被忽略，但仍需传递非 NULL（某些内核版本）
    struct epoll_event ev;
    if (epoll_ctl(iomux->epfd, EPOLL_CTL_DEL, fd, &ev) == -1) {
        if (errno != ENOENT) {  // 忽略 "不存在" 错误
            perror("epoll_ctl DEL");
            return CO_ERROR_PLATFORM;
        }
    }
    
    return CO_OK;
}

co_error_t co_iomux_poll(co_iomux_t *iomux, int timeout_ms, int *out_ready_count) {
    if (!iomux) {
        return CO_ERROR_INVAL;
    }
    
    // 调用 epoll_wait
    int nfds = epoll_wait(iomux->epfd, iomux->events, iomux->max_events, timeout_ms);
    
    if (nfds == -1) {
        if (errno == EINTR) {
            // 被信号中断，返回 0 个事件
            if (out_ready_count) {
                *out_ready_count = 0;
            }
            return CO_OK;
        }
        perror("epoll_wait");
        return CO_ERROR_PLATFORM;
    }
    
    if (nfds == 0) {
        // 超时
        if (out_ready_count) {
            *out_ready_count = 0;
        }
        return CO_ERROR_TIMEOUT;
    }
    
    // 处理就绪事件
    for (int i = 0; i < nfds; i++) {
        co_io_wait_ctx_t *wait_ctx = (co_io_wait_ctx_t *)iomux->events[i].data.ptr;
        if (!wait_ctx || !wait_ctx->routine) {
            continue;
        }
        
        // 转换事件类型
        wait_ctx->revents = 0;
        if (iomux->events[i].events & EPOLLIN) {
            wait_ctx->revents |= CO_IO_READ;
        }
        if (iomux->events[i].events & EPOLLOUT) {
            wait_ctx->revents |= CO_IO_WRITE;
        }
        if (iomux->events[i].events & (EPOLLERR | EPOLLHUP)) {
            wait_ctx->revents |= CO_IO_ERROR;
        }
        
        // 唤醒协程（将协程从 WAITING 状态移到 READY 状态）
        co_routine_t *routine = wait_ctx->routine;
        if (routine->state == CO_STATE_WAITING) {
            routine->state = CO_STATE_READY;
            routine->io_waiting = false;    // 干净定时器惰性取消所需的标志
            routine->scheduler->waiting_io_count--;
            // 通过协程自身的 scheduler 指针加入就绪队列，
            // 比 co_current_scheduler() 更精确：明确表达"放回协程所属的调度器"，
            // 且不依赖线程局部全局变量。
            co_queue_push_back(&routine->scheduler->ready_queue, &routine->queue_node);
        }
    }
    
    if (out_ready_count) {
        *out_ready_count = nfds;
    }
    
    return CO_OK;
}

// ============================================================================
// 辅助函数
// ============================================================================

co_error_t co_set_nonblocking(co_socket_t fd) {
    if (fd < 0) {
        return CO_ERROR_INVAL;
    }
    
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        return CO_ERROR_PLATFORM;
    }
    
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL O_NONBLOCK");
        return CO_ERROR_PLATFORM;
    }
    
    return CO_OK;
}

co_error_t co_set_blocking(co_socket_t fd) {
    if (fd < 0) {
        return CO_ERROR_INVAL;
    }
    
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        return CO_ERROR_PLATFORM;
    }
    
    if (fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL (remove O_NONBLOCK)");
        return CO_ERROR_PLATFORM;
    }
    
    return CO_OK;
}

// ============================================================================
// 协程式 I/O API 实现
// ============================================================================

/**
 * @brief 等待 I/O 事件（内部辅助函数）
 */
static co_error_t co_wait_io(co_socket_t fd, uint32_t events, int64_t timeout_ms) {
    // 获取当前协程和调度器
    co_scheduler_t *sched = co_current_scheduler();
    co_routine_t *current = co_current_routine();
    
    if (!sched || !current || !sched->iomux) {
        return CO_ERROR_INVAL;
    }
    
    // 注意：调用方（co_read/co_write/co_accept/co_connect）
    // 在首次 syscall 前已调用 co_set_nonblocking()，此处无需重复。
    
    // 创建等待上下文
    // 安全性说明：wait_ctx 分配在协程栈上，而非系统调用栈。
    // co_context_swap() 挂起本协程后，协程栈始终保持有效（协程栈由
    // co_stack_pool 管理，生命周期与协程绑定，不会被调度器回收）。
    // 因此 epoll 通过 ev.data.ptr 持有的 &wait_ctx 指针在协程恢复前
    // 一直有效。协程恢复后 co_iomux_del() 会将 fd 从 epoll 移除，
    // 此后 epoll 不再访问该指针，wait_ctx 随栈帧安全销毁。
    co_io_wait_ctx_t wait_ctx;
    memset(&wait_ctx, 0, sizeof(wait_ctx));
    wait_ctx.fd = fd;
    wait_ctx.events = events;
    wait_ctx.timeout_ms = timeout_ms;
    wait_ctx.routine = current;
    
    // 初始化超时状态
    current->timed_out = false;
    current->io_waiting = false;

    // 计算绝对截止时间，并注册到定时器堆
    if (timeout_ms >= 0) {
        current->wakeup_time = co_get_monotonic_time_ms() + (uint64_t)timeout_ms;
        current->io_waiting = true;
        if (!co_timer_heap_push(&sched->timer_heap, current)) {
            current->io_waiting = false;  // 恢复标志：push 失败，定时器未注册
            return CO_ERROR_NOMEM;
        }
    }

    // 注册到多路复用器
    co_error_t err = co_iomux_add(sched->iomux, &wait_ctx);
    if (err != CO_OK) {
        // 注册失败：定时器已入堆（若有），但 io_waiting 清零让 timer handler 跳过
        current->io_waiting = false;
        return err;
    }
    
    // 标记协程为等待状态，并更新调度器的 waiting_io_count
    current->state = CO_STATE_WAITING;
    sched->waiting_io_count++;
    
    // 切换回调度器，等待 I/O 事件
    // 必须用 co_context_swap() 直接切换到调度器，而非 co_yield()。
    // co_yield() 会无条件地将协程重新加入 ready_queue，导致调度器
    // 无法感知协程处于等待状态，从而永远不会调用 co_iomux_poll()。
    // co_context_swap() 直接切换到调度器主上下文，协程保持 WAITING
    // 状态，调度器在 ready_queue 为空时调用 co_iomux_poll()，触发
    // epoll 等待，直到 I/O 就绪后将协程重新加入 ready_queue。
    co_context_swap(&current->context, &sched->main_ctx);
    
    // 恢复执行时，I/O 已就绪或超时
    // 不论哪种情况，都需要移除 epoll 注册（防止重复触发）
    co_iomux_del(sched->iomux, fd);

    // 如果是超时唤醒，返回 TIMEOUT
    if (current->timed_out) {
        return CO_ERROR_TIMEOUT;
    }

    // 检查是否是错误事件
    if (wait_ctx.revents & CO_IO_ERROR) {
        return CO_ERROR;
    }
    
    return CO_OK;
}

ssize_t co_read(co_socket_t fd, void *buf, size_t count, int64_t timeout_ms) {
    if (fd < 0 || !buf || count == 0) {
        errno = EINVAL;
        return -1;
    }
    
    // 确保 fd 为非阻塞，防止首次 read() 阻塞整个调度器线程
    co_set_nonblocking(fd);
    
    // 先尝试直接读取
    ssize_t n = read(fd, buf, count);
    if (n >= 0) {
        return n;  // 成功读取
    }
    
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
        return -1;  // 真实错误
    }
    
    // EAGAIN: 数据未就绪，等待可读事件
    co_error_t err = co_wait_io(fd, CO_IO_READ, timeout_ms);
    if (err != CO_OK) {
        errno = (err == CO_ERROR_TIMEOUT) ? ETIMEDOUT : EIO;
        return -1;
    }
    
    // 重新尝试读取
    return read(fd, buf, count);
}

ssize_t co_write(co_socket_t fd, const void *buf, size_t count, int64_t timeout_ms) {
    if (fd < 0 || !buf || count == 0) {
        errno = EINVAL;
        return -1;
    }
    
    // 确保 fd 为非阻塞，防止首次 write() 阻塞整个调度器线程
    co_set_nonblocking(fd);
    
    ssize_t n = write(fd, buf, count);
    if (n >= 0) {
        return n;
    }
    
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
        return -1;
    }
    
    co_error_t err = co_wait_io(fd, CO_IO_WRITE, timeout_ms);
    if (err != CO_OK) {
        errno = (err == CO_ERROR_TIMEOUT) ? ETIMEDOUT : EIO;
        return -1;
    }
    
    return write(fd, buf, count);
}

co_socket_t co_accept(co_socket_t sockfd, void *addr, socklen_t *addrlen, int64_t timeout_ms) {
    if (sockfd < 0) {
        errno = EINVAL;
        return -1;
    }
    
    // 确保监听套接字为非阻塞，否则 accept() 会阻塞整个调度器线程
    co_set_nonblocking(sockfd);
    
    co_socket_t client_fd = accept(sockfd, (struct sockaddr *)addr, addrlen);
    if (client_fd >= 0) {
        return client_fd;
    }
    
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
        return -1;
    }
    
    co_error_t err = co_wait_io(sockfd, CO_IO_READ, timeout_ms);
    if (err != CO_OK) {
        errno = (err == CO_ERROR_TIMEOUT) ? ETIMEDOUT : EIO;
        return -1;
    }
    
    return accept(sockfd, (struct sockaddr *)addr, addrlen);
}

int co_connect(co_socket_t sockfd, const void *addr, socklen_t addrlen, int64_t timeout_ms) {
    if (sockfd < 0 || !addr) {
        errno = EINVAL;
        return -1;
    }
    
    // 必须先设为非阻塞，否则 connect() 会阻塞，无法得到 EINPROGRESS
    co_set_nonblocking(sockfd);
    
    int ret = connect(sockfd, (const struct sockaddr *)addr, addrlen);
    if (ret == 0) {
        return 0;  // 立即成功（很少见）
    }
    
    if (errno != EINPROGRESS) {
        return -1;  // 真实错误
    }
    
    // EINPROGRESS: 连接进行中，等待可写事件
    co_error_t err = co_wait_io(sockfd, CO_IO_WRITE, timeout_ms);
    if (err != CO_OK) {
        errno = (err == CO_ERROR_TIMEOUT) ? ETIMEDOUT : EIO;
        return -1;
    }
    
    // 检查连接是否成功
    int so_error = 0;
    socklen_t so_error_len = sizeof(so_error);
    if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &so_error, &so_error_len) == -1) {
        return -1;
    }
    
    if (so_error != 0) {
        errno = so_error;
        return -1;
    }
    
    return 0;
}

#endif // !_WIN32
