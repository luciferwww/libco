/**
 * @file context.c
 * @brief Windows context switching implementation using fibers
 * 
 * Windows uses fibers to implement coroutine context switching.
 * A fiber is a user-space lightweight thread scheduled by the application.
 */

#include "../context.h"
#include <assert.h>
#include <stdio.h>

// ============================================================================
// Fiber entry wrapper
// ============================================================================

/**
 * @brief Fiber entry function
 * 
 * The Windows Fiber API requires the entry function to use the
 * VOID CALLBACK(LPVOID) signature, so this wrapper adapts it to call the
 * user's co_entry_func_t.
 */
static VOID CALLBACK fiber_entry_wrapper(LPVOID param) {
    co_context_t *ctx = (co_context_t *)param;
    assert(ctx != NULL);
    assert(ctx->entry != NULL);
    
    // Invoke the user entry function
    ctx->entry(ctx->arg);
    
    // Note: Week 4 implementation
    // Normal coroutine return handling is implemented by
    // co_routine_entry_wrapper() in co_routine.c. That wrapper:
    // 1. calls the user's entry function
    // 2. calls co_routine_finish() after it returns
    // 3. lets co_routine_finish() mark the state and switch back to the scheduler
    //
    // Therefore this wrapper only reaches this point when using the context API
    // directly without the scheduler, where returning from the coroutine is
    // undefined behavior.
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
    if (ctx == NULL || entry == NULL) {
        return CO_ERROR_INVAL;
    }
    
    // Save stack information
    ctx->stack_base = stack_base;
    ctx->stack_size = stack_size;
    ctx->entry = entry;
    ctx->arg = arg;
    
    // Defer fiber creation until the first switch because the current thread
    // must already be a fiber.
    ctx->fiber = NULL;
    
    return CO_OK;
}

co_error_t co_context_swap(co_context_t *from, co_context_t *to) {
    if (from == NULL || to == NULL) {
        return CO_ERROR_INVAL;
    }
    
    // On the first switch, convert the current thread to a fiber
    if (from->fiber == NULL) {
        from->fiber = ConvertThreadToFiber(from);
        if (from->fiber == NULL) {
            DWORD error = GetLastError();
            // 1280 indicates that the thread is already a fiber
            if (error == 1280) {
                // Retrieve the current fiber
                from->fiber = GetCurrentFiber();
                if (from->fiber == NULL || from->fiber == (LPVOID)0x1E00) {
                    // 0x1E00 is the Windows magic value returned for non-fiber threads
                    fprintf(stderr, "GetCurrentFiber failed or thread not fiber\n");
                    return CO_ERROR_PLATFORM;
                }
            } else {
                fprintf(stderr, "ConvertThreadToFiber failed: %lu\n", error);
                return CO_ERROR_PLATFORM;
            }
        }
    }
    
    // Create the target fiber lazily on first use
    if (to->fiber == NULL) {
        to->fiber = CreateFiberEx(
            0,                      // Reserve stack size chosen by the system
            to->stack_size,         // Commit stack size
            0,                      // Flags
            fiber_entry_wrapper,    // Entry function
            to                      // Parameter: the context itself
        );
        
        if (to->fiber == NULL) {
            DWORD error = GetLastError();
            fprintf(stderr, "CreateFiberEx failed: %lu\n", error);
            return CO_ERROR_PLATFORM;
        }
    }
    
    // Switch to the target fiber
    SwitchToFiber(to->fiber);
    
    // Control returns here later when another coroutine switches back.
    
    return CO_OK;
}

void co_context_destroy(co_context_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    
    // Delete the fiber
    if (ctx->fiber != NULL) {
        // The caller must ensure this is not the currently running fiber
        DeleteFiber(ctx->fiber);
        ctx->fiber = NULL;
    }
    
    ctx->entry = NULL;
    ctx->arg = NULL;
}

// ============================================================================
// Helper functions
// ============================================================================

/**
 * @brief Check whether the current thread is a fiber
 * @return true if it is a fiber, false otherwise
 */
bool co_is_fiber(void) {
    return IsThreadAFiber();
}

/**
 * @brief Convert the current thread to a fiber if needed
 * @return CO_OK on success, other values indicate errors
 */
co_error_t co_convert_thread_to_fiber(void) {
    if (IsThreadAFiber()) {
        return CO_OK;  // Already a fiber
    }
    
    LPVOID fiber = ConvertThreadToFiber(NULL);
    if (fiber == NULL) {
        DWORD error = GetLastError();
        fprintf(stderr, "ConvertThreadToFiber failed: %lu\n", error);
        return CO_ERROR_PLATFORM;
    }
    
    return CO_OK;
}
