/**
 * @file context.c
 * @brief Windows 平台上下文切换实现（Fiber API）
 * 
 * Windows 使用 Fiber（纤程）实现协程上下文切换。
 * Fiber 是用户态的轻量级线程，由应用程序调度。
 */

#include "../context.h"
#include <assert.h>
#include <stdio.h>

// ============================================================================
// Fiber 入口包装
// ============================================================================

/**
 * @brief Fiber 入口函数
 * 
 * Windows Fiber API 要求入口函数签名为 VOID CALLBACK(LPVOID)，
 * 这里进行包装以调用用户的 co_entry_func_t 函数。
 */
static VOID CALLBACK fiber_entry_wrapper(LPVOID param) {
    co_context_t *ctx = (co_context_t *)param;
    assert(ctx != NULL);
    assert(ctx->entry != NULL);
    
    // 调用用户的入口函数
    ctx->entry(ctx->arg);
    
    // 注意：Week 4 实现
    // 协程正常返回的处理已通过 co_routine_entry_wrapper() 实现。
    // 该包装函数（在 co_routine.c 中）会：
    // 1. 调用用户的 entry 函数
    // 2. 在函数返回后调用 co_routine_finish()
    // 3. co_routine_finish() 会标记状态并切换回调度器
    //
    // 所以这个 fiber_entry_wrapper 只在直接使用 context API 时
    // （不通过调度器）才会到达这里。这种情况下协程返回是未定义行为。
    //
    // 完整的调度器使用场景中，entry() 来自 co_routine_entry_wrapper，
    // 该函数在用户函数返回后会调用 co_routine_finish()，永不返回到此处。
}

// ============================================================================
// 上下文接口实现
// ============================================================================

co_error_t co_context_init(co_context_t *ctx,
                           void *stack_base,
                           size_t stack_size,
                           co_entry_func_t entry,
                           void *arg) {
    if (ctx == NULL || entry == NULL) {
        return CO_ERROR_INVAL;
    }
    
    // 保存栈信息
    ctx->stack_base = stack_base;
    ctx->stack_size = stack_size;
    ctx->entry = entry;
    ctx->arg = arg;
    
    // 创建 Fiber
    // 注意：实际的 Fiber 创建延迟到第一次切换时，因为需要当前线程是 Fiber
    ctx->fiber = NULL;
    
    return CO_OK;
}

co_error_t co_context_swap(co_context_t *from, co_context_t *to) {
    if (from == NULL || to == NULL) {
        return CO_ERROR_INVAL;
    }
    
    // 首次切换时，将当前线程转换为 Fiber
    if (from->fiber == NULL) {
        from->fiber = ConvertThreadToFiber(from);
        if (from->fiber == NULL) {
            DWORD error = GetLastError();
            // 1280 表示线程已经是 Fiber
            if (error == 1280) {
                // 获取当前 Fiber
                from->fiber = GetCurrentFiber();
                if (from->fiber == NULL || from->fiber == (LPVOID)0x1E00) {
                    // 0x1E00 是 Windows 对非 Fiber 线程返回的魔数
                    fprintf(stderr, "GetCurrentFiber failed or thread not fiber\n");
                    return CO_ERROR_PLATFORM;
                }
            } else {
                fprintf(stderr, "ConvertThreadToFiber failed: %lu\n", error);
                return CO_ERROR_PLATFORM;
            }
        }
    }
    
    // 如果目标上下文还没有创建 Fiber，现在创建它
    if (to->fiber == NULL) {
        to->fiber = CreateFiberEx(
            0,                      // 保留栈大小（系统决定）
            to->stack_size,         // 提交栈大小
            0,                      // 标志
            fiber_entry_wrapper,    // 入口函数
            to                      // 参数（上下文本身）
        );
        
        if (to->fiber == NULL) {
            DWORD error = GetLastError();
            fprintf(stderr, "CreateFiberEx failed: %lu\n", error);
            return CO_ERROR_PLATFORM;
        }
    }
    
    // 切换到目标 Fiber
    SwitchToFiber(to->fiber);
    
    // 注意：这里会在两个地方返回：
    // 1. 立即返回（实际不会，因为 SwitchToFiber 会切换执行流）
    // 2. 当其他协程切换回这里时才会返回
    
    return CO_OK;
}

void co_context_destroy(co_context_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    
    // 删除 Fiber
    if (ctx->fiber != NULL) {
        // 注意：不能删除当前正在运行的 Fiber
        // 调用者需要确保不在此 Fiber 中调用 destroy
        DeleteFiber(ctx->fiber);
        ctx->fiber = NULL;
    }
    
    ctx->entry = NULL;
    ctx->arg = NULL;
}

// ============================================================================
// 辅助函数
// ============================================================================

/**
 * @brief 检查当前线程是否是 Fiber
 * @return true 是 Fiber，false 不是
 */
bool co_is_fiber(void) {
    return IsThreadAFiber();
}

/**
 * @brief 将当前线程转换为 Fiber（如果还不是）
 * @return CO_OK 成功，其他值表示错误
 */
co_error_t co_convert_thread_to_fiber(void) {
    if (IsThreadAFiber()) {
        return CO_OK;  // 已经是 Fiber
    }
    
    LPVOID fiber = ConvertThreadToFiber(NULL);
    if (fiber == NULL) {
        DWORD error = GetLastError();
        fprintf(stderr, "ConvertThreadToFiber failed: %lu\n", error);
        return CO_ERROR_PLATFORM;
    }
    
    return CO_OK;
}
