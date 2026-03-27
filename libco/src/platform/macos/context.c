/**
 * @file context.c
 * @brief macOS 平台上下文切换实现（ucontext API）
 * 
 * macOS 使用 POSIX ucontext API 实现协程上下文切换。
 * 
 * @note Apple 从 macOS 10.6 开始将 ucontext 标记为 deprecated，
 *       但它仍然可用。可以通过编译选项 -Wno-deprecated-declarations 抑制警告。
 *       
 * @note macOS 实现与 Linux 基本相同，但可能有一些微妙的差异。
 */

#define _XOPEN_SOURCE 600  // 启用 ucontext API
#include "../context.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

// 抑制 deprecated 警告
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

// ============================================================================
// ucontext 入口包装
// ============================================================================

/**
 * @brief ucontext 入口函数包装
 * 
 * makecontext() 要求入口函数使用特定的签名和调用约定，
 * 这里进行包装以调用用户的 co_entry_func_t 函数。
 * 
 * @param arg_low 参数的低32位
 * @param arg_high 参数的高32位
 */
static void ucontext_entry_wrapper(uint32_t arg_low, uint32_t arg_high) {
    // 将两个32位整数重组为指针
    uintptr_t ptr = ((uintptr_t)arg_high << 32) | arg_low;
    co_context_t *ctx = (co_context_t *)ptr;
    
    assert(ctx != NULL);
    
    // 从栈基址提取用户参数
    co_entry_func_t *entry_ptr = (co_entry_func_t *)ctx->stack_base;
    void **arg_ptr = (void **)((char *)ctx->stack_base + sizeof(co_entry_func_t));
    
    co_entry_func_t entry = *entry_ptr;
    void *arg = *arg_ptr;
    
    assert(entry != NULL);
    
    // 调用用户的入口函数
    entry(arg);
    
    // 注意：Week 4 实现
    // 协程正常返回的处理已通过 co_routine_entry_wrapper() 实现。
    // 该包装函数（在 co_routine.c 中）会：
    // 1. 调用用户的 entry 函数
    // 2. 在函数返回后调用 co_routine_finish()
    // 3. co_routine_finish() 会标记状态并切换回调度器
    //
    // 所以这个 ucontext_entry_wrapper 只在直接使用 context API 时
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
    if (ctx == NULL || entry == NULL || stack_base == NULL || stack_size == 0) {
        return CO_ERROR_INVAL;
    }
    
    // 保存栈信息
    ctx->stack_base = stack_base;
    ctx->stack_size = stack_size;
    
    // 将 entry 和 arg 存储在栈顶
    co_entry_func_t *entry_ptr = (co_entry_func_t *)stack_base;
    void **arg_ptr = (void **)((char *)stack_base + sizeof(co_entry_func_t));
    *entry_ptr = entry;
    *arg_ptr = arg;
    
    // 获取当前上下文
    if (getcontext(&ctx->uctx) == -1) {
        perror("getcontext");
        return CO_ERROR_PLATFORM;
    }
    
    // 设置栈
    // macOS 可能对栈对齐有更严格的要求
    ctx->uctx.uc_stack.ss_sp = stack_base;
    ctx->uctx.uc_stack.ss_size = stack_size;
    ctx->uctx.uc_link = NULL;
    
    // 创建新上下文
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
    
    // 保存当前上下文到 from，恢复 to 的上下文并切换
    if (swapcontext(&from->uctx, &to->uctx) == -1) {
        perror("swapcontext");
        return CO_ERROR_PLATFORM;
    }
    
    return CO_OK;
}

void co_context_destroy(co_context_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    
    // ucontext 不需要显式清理
    memset(&ctx->uctx, 0, sizeof(ctx->uctx));
    ctx->stack_base = NULL;
    ctx->stack_size = 0;
}

// ============================================================================
// 辅助函数
// ============================================================================

/**
 * @brief 获取当前上下文
 * @param ctx 上下文指针
 * @return CO_OK 成功，其他值表示错误
 */
co_error_t co_context_get_current(co_context_t *ctx) {
    if (ctx == NULL) {
        return CO_ERROR_INVAL;
    }
    
    if (getcontext(&ctx->uctx) == -1) {
        perror("getcontext");
        return CO_ERROR_PLATFORM;
    }
    
    ctx->stack_base = NULL;
    ctx->stack_size = 0;
    
    return CO_OK;
}

#pragma GCC diagnostic pop
