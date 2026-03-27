/**
 * @file context.h
 * @brief 协程上下文切换 - 内部接口
 * 
 * 定义跨平台的上下文切换接口，由平台特定代码实现。
 */

#ifndef LIBCO_INTERNAL_CONTEXT_H
#define LIBCO_INTERNAL_CONTEXT_H

#include <libco/co.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// 平台特定的上下文结构
// ============================================================================

#if defined(LIBCO_PLATFORM_LINUX) || defined(LIBCO_PLATFORM_MACOS)
// Unix-like 平台使用 ucontext
#include <ucontext.h>

typedef struct co_context {
    ucontext_t uctx;            /**< ucontext 上下文 */
    void *stack_base;           /**< 栈基址 */
    size_t stack_size;          /**< 栈大小 */
} co_context_t;

#elif defined(LIBCO_PLATFORM_WINDOWS)
// Windows 平台使用 Fiber API
#include <windows.h>

typedef struct co_context {
    LPVOID fiber;               /**< Fiber 句柄 */
    void *stack_base;           /**< 栈基址 */
    size_t stack_size;          /**< 栈大小 */
    co_entry_func_t entry;      /**< 入口函数 */
    void *arg;                  /**< 用户参数 */
} co_context_t;

#else
#error "Unsupported platform"
#endif

// ============================================================================
// 上下文接口
// ============================================================================

/**
 * @brief 初始化协程上下文
 * 
 * @param ctx 上下文指针
 * @param stack_base 栈基址
 * @param stack_size 栈大小
 * @param entry 入口函数
 * @param arg 用户参数
 * @return CO_OK 成功，其他值表示错误
 */
co_error_t co_context_init(co_context_t *ctx,
                           void *stack_base,
                           size_t stack_size,
                           co_entry_func_t entry,
                           void *arg);

/**
 * @brief 切换上下文
 * 
 * 保存当前上下文到 from，恢复 to 的上下文并切换执行流。
 * 
 * @param from 当前上下文（保存点）
 * @param to 目标上下文（恢复点）
 * @return CO_OK 成功，其他值表示错误
 * 
 * @note 此函数会在两个地方返回：
 *       1. 立即返回（切换到 to 后）
 *       2. 稍后返回（当其他协程切换回 from 时）
 */
co_error_t co_context_swap(co_context_t *from, co_context_t *to);

/**
 * @brief 销毁上下文
 * 
 * @param ctx 上下文指针
 */
void co_context_destroy(co_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif // LIBCO_INTERNAL_CONTEXT_H
