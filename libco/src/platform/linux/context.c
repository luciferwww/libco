/**
 * @file context.c
 * @brief Linux 平台上下文切换实现（ucontext API）
 * 
 * Linux 使用 POSIX ucontext API 实现协程上下文切换。
 * ucontext提供了用户态的上下文保存和恢复功能。
 * 
 * @note ucontext 在某些系统上已被标记为废弃（deprecated），
 *       但仍然是最常用的协程实现方式。
 */

#define _XOPEN_SOURCE 600  // 启用 ucontext API
#include "../context.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

// ============================================================================
// ucontext 入口包装
// ============================================================================

/**
 * @brief ucontext 入口函数包装
 * 
 * makecontext() 要求入口函数使用特定的签名和调用约定，
 * 这里进行包装以调用用户的 co_entry_func_t 函数。
 * 
 * @param arg_low 参数的低32位（在64位系统上是低32位）
 * @param arg_high 参数的高32位（在64位系统上是高32位）
 */
static void ucontext_entry_wrapper(uint32_t arg_low, uint32_t arg_high) {
    // 将两个32位整数重组为指针
    uintptr_t ptr = ((uintptr_t)arg_high << 32) | arg_low;
    co_context_t *ctx = (co_context_t *)ptr;
    
    assert(ctx != NULL);
    
    // 从栈基址提取用户参数
    // 注意：在 co_context_init 中，我们将 entry 和 arg 存储在栈顶
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
    
    // 将 entry 和 arg 存储在栈顶（用于包装函数访问）
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
    ctx->uctx.uc_stack.ss_sp = stack_base;
    ctx->uctx.uc_stack.ss_size = stack_size;
    ctx->uctx.uc_link = NULL;  // 协程结束后不跳转
    
    // 创建新上下文
    // makecontext 的参数传递比较复杂：
    // - 64位系统：指针被拆分为两个32位整数
    // - 32位系统：指针占一个32位整数
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
    
    // 注意：这里会在两个地方返回：
    // 1. 立即返回（实际不会，因为 swapcontext 会切换执行流）
    // 2. 当其他协程切换回这里时才会返回
    
    return CO_OK;
}

void co_context_destroy(co_context_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    
    // ucontext 不需要显式清理
    // 只需清空字段
    memset(&ctx->uctx, 0, sizeof(ctx->uctx));
    ctx->stack_base = NULL;
    ctx->stack_size = 0;
}

// ============================================================================
// 辅助函数
// ============================================================================

/**
 * @brief 获取当前上下文（用于保存主线程上下文）
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
    
    ctx->stack_base = NULL;  // 主线程使用系统栈
    ctx->stack_size = 0;
    
    return CO_OK;
}
