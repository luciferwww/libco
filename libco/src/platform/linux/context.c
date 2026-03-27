/**
 * @file context.c
 * @brief Linux context switching implementation using ucontext
 * 
 * Linux uses the POSIX ucontext API to implement coroutine context switching.
 * ucontext provides user-space context save and restore functionality.
 * 
 * @note ucontext is deprecated on some systems, but it is still a common way
 *       to implement coroutines.
 */

#define _XOPEN_SOURCE 600  // Enable the ucontext API
#include "../context.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

// ============================================================================
// ucontext entry wrapper
// ============================================================================

/**
 * @brief ucontext entry wrapper
 * 
 * makecontext() requires a specific entry signature and calling convention,
 * so this wrapper adapts it to invoke the user's co_entry_func_t.
 * 
 * @param arg_low Low 32 bits of the argument pointer
 * @param arg_high High 32 bits of the argument pointer
 */
static void ucontext_entry_wrapper(uint32_t arg_low, uint32_t arg_high) {
     // Reconstruct the pointer from two 32-bit integers
    uintptr_t ptr = ((uintptr_t)arg_high << 32) | arg_low;
    co_context_t *ctx = (co_context_t *)ptr;
    
    assert(ctx != NULL);
    
    // Extract the user arguments from the stack base.
    // Note: co_context_init stores entry and arg at the stack top.
    co_entry_func_t *entry_ptr = (co_entry_func_t *)ctx->stack_base;
    void **arg_ptr = (void **)((char *)ctx->stack_base + sizeof(co_entry_func_t));
    
    co_entry_func_t entry = *entry_ptr;
    void *arg = *arg_ptr;
    
    assert(entry != NULL);
    
    // Invoke the user entry function
    entry(arg);
    
    // Note: Week 4 implementation
    // Normal coroutine return handling is implemented by
    // co_routine_entry_wrapper() in co_routine.c. That wrapper:
    // 1. calls the user's entry function
    // 2. calls co_routine_finish() after it returns
    // 3. lets co_routine_finish() mark the state and switch back to the scheduler
    //
    // Therefore this ucontext_entry_wrapper only reaches this point when the
    // context API is used directly without the scheduler. In that case,
    // returning from the coroutine is undefined behavior.
    //
    // In normal scheduler-driven execution, entry() comes from
    // co_routine_entry_wrapper and never returns here.
}

// ============================================================================
// Context interface implementation
// ============================================================================

co_error_t co_context_init(co_context_t *ctx,
                           void *stack_base,
                           size_t stack_size,
                           co_entry_func_t entry,
                           void *arg) {
    if (ctx == NULL || entry == NULL || stack_base == NULL || stack_size == 0) {
        return CO_ERROR_INVAL;
    }
    
    // Save stack information
    ctx->stack_base = stack_base;
    ctx->stack_size = stack_size;
    
    // Store entry and arg at the stack top for the wrapper to access
    co_entry_func_t *entry_ptr = (co_entry_func_t *)stack_base;
    void **arg_ptr = (void **)((char *)stack_base + sizeof(co_entry_func_t));
    *entry_ptr = entry;
    *arg_ptr = arg;
    
    // Capture the current context
    if (getcontext(&ctx->uctx) == -1) {
        perror("getcontext");
        return CO_ERROR_PLATFORM;
    }
    
    // Configure the stack
    ctx->uctx.uc_stack.ss_sp = stack_base;
    ctx->uctx.uc_stack.ss_size = stack_size;
    ctx->uctx.uc_link = NULL;  // Do not chain anywhere when the coroutine ends
    
    // Build the new context.
    // makecontext parameter passing is tricky:
    // - on 64-bit systems, the pointer is split into two 32-bit integers
    // - on 32-bit systems, the pointer fits in one 32-bit integer
    uintptr_t ptr = (uintptr_t)ctx;
    uint32_t arg_low = (uint32_t)(ptr & 0xFFFFFFFF);
    uint32_t arg_high = (uint32_t)(ptr >> 32);
    
    makecontext(&ctx->uctx, (void (*)())ucontext_entry_wrapper, 2, arg_low, arg_high);
    
    return CO_OK;
}

co_error_t co_context_swap(co_context_t *from, co_context_t *to) {
    if (from == NULL || to == NULL) {
        return CO_ERROR_INVAL;
    }
    
    // Save the current context into from, restore to, and switch
    if (swapcontext(&from->uctx, &to->uctx) == -1) {
        perror("swapcontext");
        return CO_ERROR_PLATFORM;
    }
    
    // Control returns here later when another coroutine switches back.
    
    return CO_OK;
}

void co_context_destroy(co_context_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    
    // ucontext does not require explicit cleanup; just clear the fields
    memset(&ctx->uctx, 0, sizeof(ctx->uctx));
    ctx->stack_base = NULL;
    ctx->stack_size = 0;
}

// ============================================================================
// Helper functions
// ============================================================================

/**
 * @brief Capture the current context for the main thread
 * @param ctx Context pointer
 * @return CO_OK on success, other values indicate errors
 */
co_error_t co_context_get_current(co_context_t *ctx) {
    if (ctx == NULL) {
        return CO_ERROR_INVAL;
    }
    
    if (getcontext(&ctx->uctx) == -1) {
        perror("getcontext");
        return CO_ERROR_PLATFORM;
    }
    
    ctx->stack_base = NULL;  // The main thread uses the system stack
    ctx->stack_size = 0;
    
    return CO_OK;
}
