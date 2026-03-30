/**
 * @file iomux_epoll.c
 * @brief Linux epoll implementation
 * 
 * Implements I/O multiplexing using the Linux epoll API.
 * epoll is a high-performance Linux-specific event notification mechanism well
 * suited for large numbers of concurrent connections.
 */

#ifndef _WIN32  // Build only on Linux/Unix

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
// epoll implementation structure
// ============================================================================

/**
 * @brief epoll I/O multiplexer
 */
struct co_iomux {
    int epfd;                   /**< epoll file descriptor */
    int max_events;             /**< Maximum event count */
    struct epoll_event *events; /**< Event buffer */
};

// ============================================================================
// I/O multiplexer implementation
// ============================================================================

co_iomux_t *co_iomux_create(int max_events) {
    if (max_events <= 0) {
        max_events = 1024;  // Default value
    }
    
    co_iomux_t *iomux = (co_iomux_t *)calloc(1, sizeof(co_iomux_t));
    if (!iomux) {
        return NULL;
    }
    
    // Create the epoll instance
    iomux->epfd = epoll_create1(EPOLL_CLOEXEC);
    if (iomux->epfd == -1) {
        perror("epoll_create1");
        free(iomux);
        return NULL;
    }
    
    iomux->max_events = max_events;
    
    // Allocate the event buffer
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
    
    // Translate event types
    ev.events = 0;
    if (wait_ctx->events & CO_IO_READ) {
        ev.events |= EPOLLIN;
    }
    if (wait_ctx->events & CO_IO_WRITE) {
        ev.events |= EPOLLOUT;
    }
    
    // Use edge-triggered non-blocking mode for better performance
    ev.events |= EPOLLET;
    
    // Store wait_ctx in data.ptr so a ready event can wake the coroutine
    ev.data.ptr = wait_ctx;
    
    // Add the descriptor to epoll
    if (epoll_ctl(iomux->epfd, EPOLL_CTL_ADD, wait_ctx->fd, &ev) == -1) {
        if (errno == EEXIST) {
            // Already present, try to modify it instead
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
    
    // The fourth epoll_ctl argument is ignored for DEL, but some kernel versions still expect non-NULL
    struct epoll_event ev;
    if (epoll_ctl(iomux->epfd, EPOLL_CTL_DEL, fd, &ev) == -1) {
        if (errno != ENOENT) {  // Ignore "not found" errors
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
    
    // Call epoll_wait
    int nfds = epoll_wait(iomux->epfd, iomux->events, iomux->max_events, timeout_ms);
    
    if (nfds == -1) {
        if (errno == EINTR) {
            // Interrupted by a signal, return zero events
            if (out_ready_count) {
                *out_ready_count = 0;
            }
            return CO_OK;
        }
        perror("epoll_wait");
        return CO_ERROR_PLATFORM;
    }
    
    if (nfds == 0) {
        // Timeout
        if (out_ready_count) {
            *out_ready_count = 0;
        }
        return CO_ERROR_TIMEOUT;
    }
    
    // Process ready events
    for (int i = 0; i < nfds; i++) {
        co_io_wait_ctx_t *wait_ctx = (co_io_wait_ctx_t *)iomux->events[i].data.ptr;
        if (!wait_ctx || !wait_ctx->routine) {
            continue;
        }
        
        // Translate event types
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
        
        // Wake the coroutine by moving it from WAITING to READY
        co_routine_t *routine = wait_ctx->routine;
        if (routine->state == CO_STATE_WAITING) {
            routine->state = CO_STATE_READY;
            routine->io_waiting = false;    // Needed for clean lazy timer cancellation
            routine->scheduler->waiting_io_count--;
            // Requeue through the coroutine's own scheduler pointer rather than
            // co_current_scheduler() to make ownership explicit and avoid
            // depending on thread-local global state.
            co_queue_push_back(&routine->scheduler->ready_queue, &routine->queue_node);
        }
    }
    
    if (out_ready_count) {
        *out_ready_count = nfds;
    }
    
    return CO_OK;
}

// ============================================================================
// Helper functions
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
// Coroutine-friendly I/O API implementation
// ============================================================================

/**
 * @brief Wait for an I/O event (internal helper)
 */
static co_error_t co_wait_io(co_socket_t fd, uint32_t events, int64_t timeout_ms) {
    // Get the current coroutine and scheduler
    co_scheduler_t *sched = co_current_scheduler();
    co_routine_t *current = co_current_routine();
    
    if (!sched || !current || !sched->iomux) {
        return CO_ERROR_INVAL;
    }
    
    // The callers co_read/co_write/co_accept/co_connect already call
    // co_set_nonblocking() before the first syscall, so there is no need to
    // repeat it here.
    
    // Create the wait context.
    // Safety note: wait_ctx lives on the coroutine stack, not the syscall
    // stack. After co_context_swap() suspends the coroutine, that stack remains
    // valid because it is owned by the coroutine and managed by co_stack_pool.
    // epoll can therefore safely keep &wait_ctx in ev.data.ptr until the
    // coroutine resumes and calls co_iomux_del().
    co_io_wait_ctx_t wait_ctx;
    memset(&wait_ctx, 0, sizeof(wait_ctx));
    wait_ctx.fd = fd;
    wait_ctx.events = events;
    wait_ctx.timeout_ms = timeout_ms;
    wait_ctx.routine = current;
    
    // Initialize timeout state
    current->timed_out = false;
    current->io_waiting = false;

    // Compute the absolute deadline and register it in the timer heap
    if (timeout_ms >= 0) {
        current->wakeup_time = co_get_monotonic_time_ms() + (uint64_t)timeout_ms;
        current->io_waiting = true;
        if (!co_timer_heap_push(&sched->timer_heap, current)) {
            current->io_waiting = false;  // Restore the flag because timer registration failed
            return CO_ERROR_NOMEM;
        }
    }

    // Register with the multiplexer
    co_error_t err = co_iomux_add(sched->iomux, &wait_ctx);
    if (err != CO_OK) {
        // Registration failed. The timer may already be queued, so clear
        // io_waiting to make the timer handler skip it.
        current->io_waiting = false;
        return err;
    }
    
    // Mark the coroutine as waiting and update waiting_io_count
    current->state = CO_STATE_WAITING;
    sched->waiting_io_count++;
    
    // Switch back to the scheduler and wait for I/O.
    // This must use co_context_swap() instead of co_yield() because co_yield()
    // would blindly place the coroutine back in ready_queue and hide its
    // waiting state from the scheduler.
    co_context_swap(&current->context, &sched->main_ctx);
    
    // Once resumed, the I/O is either ready or timed out. In either case,
    // remove the epoll registration to avoid duplicate wakeups.
    co_iomux_del(sched->iomux, fd);

    // Return TIMEOUT if the timer resumed the coroutine
    if (current->timed_out) {
        return CO_ERROR_TIMEOUT;
    }

    // Check for error events
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
    
    // Ensure fd is non-blocking so the initial read() cannot stall the scheduler thread
    co_set_nonblocking(fd);
    
    // First try a direct read
    ssize_t n = read(fd, buf, count);
    if (n >= 0) {
        return n;  // Read completed successfully
    }
    
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
        return -1;  // Real error
    }
    
    // EAGAIN: data is not ready yet, wait for a readable event
    co_error_t err = co_wait_io(fd, CO_IO_READ, timeout_ms);
    if (err != CO_OK) {
        errno = (err == CO_ERROR_TIMEOUT) ? ETIMEDOUT : EIO;
        return -1;
    }
    
    // Retry the read
    return read(fd, buf, count);
}

ssize_t co_write(co_socket_t fd, const void *buf, size_t count, int64_t timeout_ms) {
    if (fd < 0 || !buf || count == 0) {
        errno = EINVAL;
        return -1;
    }
    
    // Ensure fd is non-blocking so the initial write() cannot stall the scheduler thread
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
    
    // Ensure the listening socket is non-blocking so accept() cannot stall the scheduler thread
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
    
    // The socket must be non-blocking first, otherwise connect() can block and never yield EINPROGRESS
    co_set_nonblocking(sockfd);
    
    int ret = connect(sockfd, (const struct sockaddr *)addr, addrlen);
    if (ret == 0) {
        return 0;  // Immediate success, which is uncommon
    }
    
    if (errno != EINPROGRESS) {
        return -1;  // Real error
    }
    
    // EINPROGRESS: connection is in progress, wait for a writable event
    co_error_t err = co_wait_io(sockfd, CO_IO_WRITE, timeout_ms);
    if (err != CO_OK) {
        errno = (err == CO_ERROR_TIMEOUT) ? ETIMEDOUT : EIO;
        return -1;
    }
    
    // Check whether the connection completed successfully
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
