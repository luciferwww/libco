/**
 * @file context.h
 * @brief Coroutine context switching internal interface
 * 
 * Defines the cross-platform context switching interface implemented by
 * platform-specific code.
 */

#ifndef LIBCO_INTERNAL_CONTEXT_H
#define LIBCO_INTERNAL_CONTEXT_H

#include <libco/co.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Platform-specific context structure
// ============================================================================

#if defined(LIBCO_PLATFORM_LINUX) || defined(LIBCO_PLATFORM_MACOS)
// Unix-like platforms use ucontext
#include <ucontext.h>

typedef struct co_context {
    ucontext_t uctx;            /**< ucontext state */
    void *stack_base;           /**< Stack base address */
    size_t stack_size;          /**< Stack size */
} co_context_t;

#elif defined(LIBCO_PLATFORM_WINDOWS)
// Windows uses the Fiber API
#include <windows.h>

typedef struct co_context {
    LPVOID fiber;               /**< Fiber handle */
    void *stack_base;           /**< Stack base address */
    size_t stack_size;          /**< Stack size */
    co_entry_func_t entry;      /**< Entry function */
    void *arg;                  /**< User argument */
} co_context_t;

#else
#error "Unsupported platform"
#endif

// ============================================================================
// Context interface
// ============================================================================

/**
 * @brief Initialize a coroutine context
 * 
 * @param ctx Context pointer
 * @param stack_base Stack base address
 * @param stack_size Stack size
 * @param entry Entry function
 * @param arg User argument
 * @return CO_OK on success, other values indicate errors
 */
co_error_t co_context_init(co_context_t *ctx,
                           void *stack_base,
                           size_t stack_size,
                           co_entry_func_t entry,
                           void *arg);

/**
 * @brief Switch contexts
 * 
 * Save the current context into from, restore to, and switch execution.
 * 
 * @param from Current context save point
 * @param to Target context restore point
 * @return CO_OK on success, other values indicate errors
 * 
 * @note This function returns in two situations:
 *       1. immediately after switching to to
 *       2. later when another coroutine switches back to from
 */
co_error_t co_context_swap(co_context_t *from, co_context_t *to);

/**
 * @brief Destroy a context
 * 
 * @param ctx Context pointer
 */
void co_context_destroy(co_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif // LIBCO_INTERNAL_CONTEXT_H
