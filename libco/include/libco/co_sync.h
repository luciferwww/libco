/**
 * @file co_sync.h
 * @brief libco 同步原语 API
 * 
 * 提供协程级别的互斥锁和条件变量。
 * 这些原语在协程之间同步，不会阻塞整个线程。
 * 锁冲突时，当前协程挂起并切回调度器，让其他协程继续运行。
 */

#ifndef LIBCO_CO_SYNC_H
#define LIBCO_CO_SYNC_H

#include <libco/co.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// 不透明类型声明
// ============================================================================

/**
 * @brief 协程互斥锁（不透明句柄）
 */
typedef struct co_mutex co_mutex_t;

/**
 * @brief 协程条件变量（不透明句柄）
 */
typedef struct co_cond co_cond_t;

// ============================================================================
// 互斥锁 API
// ============================================================================

/**
 * @brief 创建互斥锁
 * @param attr 预留扩展参数（当前传 NULL，未来用于递归锁等属性）
 * @return 互斥锁句柄，失败返回 NULL
 */
co_mutex_t *co_mutex_create(const void *attr);

/**
 * @brief 销毁互斥锁
 * @param mutex 互斥锁句柄
 * @return CO_OK 成功，CO_ERROR_INVAL 参数无效
 * @note 销毁时必须确保没有协程正在等待此锁
 */
co_error_t co_mutex_destroy(co_mutex_t *mutex);

/**
 * @brief 加锁（阻塞直到成功）
 * 
 * 如果锁已被其他协程持有，当前协程挂起，切回调度器，
 * 等待锁释放后再唤醒。不会阻塞调度器线程。
 * 
 * @param mutex 互斥锁句柄
 * @return CO_OK 成功，CO_ERROR_INVAL 参数无效
 */
co_error_t co_mutex_lock(co_mutex_t *mutex);

/**
 * @brief 尝试加锁（非阻塞）
 * 
 * @param mutex 互斥锁句柄
 * @return CO_OK 成功获取到锁，CO_ERROR_BUSY 锁已被占用
 */
co_error_t co_mutex_trylock(co_mutex_t *mutex);

/**
 * @brief 解锁
 * 
 * 释放锁，如果有协程正在等待，唤醒其中一个（FIFO 顺序）。
 * 
 * @param mutex 互斥锁句柄
 * @return CO_OK 成功，CO_ERROR_INVAL 参数无效
 */
co_error_t co_mutex_unlock(co_mutex_t *mutex);

// ============================================================================
// 条件变量 API
// ============================================================================

/**
 * @brief 创建条件变量
 * @param attr 预留扩展参数（当前传 NULL）
 * @return 条件变量句柄，失败返回 NULL
 */
co_cond_t *co_cond_create(const void *attr);

/**
 * @brief 销毁条件变量
 * @param cond 条件变量句柄
 * @return CO_OK 成功，CO_ERROR_INVAL 参数无效
 */
co_error_t co_cond_destroy(co_cond_t *cond);

/**
 * @brief 等待条件变量
 * 
 * 原子地释放 mutex 并挂起当前协程。
 * 被 co_cond_signal/broadcast 唤醒后，重新持有 mutex 再返回。
 * 
 * @param cond  条件变量句柄
 * @param mutex 已持有的互斥锁
 * @return CO_OK 成功
 */
co_error_t co_cond_wait(co_cond_t *cond, co_mutex_t *mutex);

/**
 * @brief 带超时的等待
 * 
 * @param cond       条件变量句柄
 * @param mutex      已持有的互斥锁
 * @param timeout_ms 相对等待时长（毫秒，从调用时刻开始计算）
 * @return CO_OK 成功，CO_ERROR_TIMEOUT 超时
 */
co_error_t co_cond_timedwait(co_cond_t *cond, co_mutex_t *mutex,
                              uint32_t timeout_ms);

/**
 * @brief 唤醒一个等待的协程（FIFO 顺序）
 * @param cond 条件变量句柄
 * @return CO_OK 成功
 */
co_error_t co_cond_signal(co_cond_t *cond);

/**
 * @brief 唤醒所有等待的协程
 * @param cond 条件变量句柄
 * @return CO_OK 成功
 */
co_error_t co_cond_broadcast(co_cond_t *cond);

// ============================================================================
// Channel API（Go 风格，Week 10）
// ============================================================================

/**
 * @brief 不透明的 channel 句柄
 */
typedef struct co_channel co_channel_t;

/**
 * @brief 创建 channel
 * @param elem_size 每个元素的字节数（必须 > 0）
 * @param capacity  缓冲区容量，0 表示无缓冲 channel（rendezvous 语义）
 * @return channel 句柄，失败返回 NULL
 */
co_channel_t *co_channel_create(size_t elem_size, size_t capacity);

/**
 * @brief 销毁 channel（必须在所有协程停止使用后调用）
 * @param ch channel 句柄
 * @return CO_OK 成功，CO_ERROR_INVAL 参数无效
 */
co_error_t co_channel_destroy(co_channel_t *ch);

/**
 * @brief 发送数据到 channel（阻塞直到成功）
 *
 * 缓冲区未满时立即写入并返回；满时挂起当前协程，等接收者消费后唤醒。
 *
 * @param ch   channel 句柄
 * @param data 指向待发送数据的指针（复制 elem_size 字节）
 * @return CO_OK 成功，CO_ERROR_CLOSED channel 已关闭，CO_ERROR_INVAL 参数无效
 */
co_error_t co_channel_send(co_channel_t *ch, const void *data);

/**
 * @brief 从 channel 接收数据（阻塞直到成功）
 *
 * 缓冲区有数据时立即读取并返回；空时挂起当前协程，等发送者写入后唤醒。
 * channel 关闭且缓冲区清空后返回 CO_ERROR_CLOSED。
 *
 * @param ch   channel 句柄
 * @param data 接收缓冲区指针（复制 elem_size 字节到此处）
 * @return CO_OK 成功，CO_ERROR_CLOSED channel 已关闭且无数据
 */
co_error_t co_channel_recv(co_channel_t *ch, void *data);

/**
 * @brief 尝试发送（非阻塞）
 * @return CO_OK 成功，CO_ERROR_BUSY 缓冲区满，CO_ERROR_CLOSED 已关闭
 */
co_error_t co_channel_trysend(co_channel_t *ch, const void *data);

/**
 * @brief 尝试接收（非阻塞）
 * @return CO_OK 成功，CO_ERROR_BUSY 缓冲区空，CO_ERROR_CLOSED 已关闭且无数据
 */
co_error_t co_channel_tryrecv(co_channel_t *ch, void *data);

/**
 * @brief 关闭 channel
 *
 * 唤醒所有等待的接收者（返回 CO_ERROR_CLOSED）和发送者（返回 CO_ERROR_CLOSED）。
 * 对已关闭的 channel 调用此函数返回 CO_ERROR_CLOSED。
 *
 * @param ch channel 句柄
 * @return CO_OK 成功，CO_ERROR_CLOSED 已关闭
 */
co_error_t co_channel_close(co_channel_t *ch);

/**
 * @brief 获取 channel 当前缓冲元素数量
 */
size_t co_channel_len(const co_channel_t *ch);

/**
 * @brief 获取 channel 缓冲区容量
 */
size_t co_channel_cap(const co_channel_t *ch);

/**
 * @brief 检查 channel 是否已关闭
 */
bool co_channel_is_closed(const co_channel_t *ch);

#ifdef __cplusplus
}
#endif

#endif // LIBCO_CO_SYNC_H
