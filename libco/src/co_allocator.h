/**
 * @file co_allocator.h
 * @brief 自定义内存分配器接口
 * 
 * 允许用户提供自定义的内存分配函数，用于所有 libco 的内存分配。
 * 这在需要内存跟踪、自定义分配策略或内存池时非常有用。
 */

#ifndef LIBCO_ALLOCATOR_H
#define LIBCO_ALLOCATOR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// 类型定义
// ============================================================================

/**
 * @brief 内存分配函数类型
 * 
 * @param size 要分配的字节数
 * @param userdata 用户数据指针
 * @return 分配的内存指针，失败返回 NULL
 */
typedef void *(*co_malloc_fn)(size_t size, void *userdata);

/**
 * @brief 内存重新分配函数类型
 * 
 * @param ptr 原内存指针（可以为 NULL）
 * @param size 新的大小
 * @param userdata 用户数据指针
 * @return 重新分配的内存指针，失败返回 NULL
 */
typedef void *(*co_realloc_fn)(void *ptr, size_t size, void *userdata);

/**
 * @brief 内存释放函数类型
 * 
 * @param ptr 要释放的内存指针
 * @param userdata 用户数据指针
 */
typedef void (*co_free_fn)(void *ptr, void *userdata);

/**
 * @brief 自定义分配器结构
 */
typedef struct co_allocator {
    co_malloc_fn malloc_fn;     /**< 分配函数 */
    co_realloc_fn realloc_fn;   /**< 重新分配函数 */
    co_free_fn free_fn;         /**< 释放函数 */
    void *userdata;             /**< 用户数据指针 */
} co_allocator_t;

// ============================================================================
// 分配器管理
// ============================================================================

/**
 * @brief 设置全局分配器
 * 
 * 必须在创建任何 libco 对象之前调用。
 * 如果不调用此函数，libco 将使用标准的 malloc/realloc/free。
 * 
 * @param allocator 自定义分配器指针（可以为 NULL 以恢复默认分配器）
 */
void co_set_allocator(const co_allocator_t *allocator);

/**
 * @brief 获取当前全局分配器
 * 
 * @return 当前分配器指针（如果使用默认分配器则返回 NULL）
 */
const co_allocator_t *co_get_allocator(void);

// ============================================================================
// 内部分配函数（供 libco 内部使用）
// ============================================================================

/**
 * @brief 内部分配函数
 * 
 * 使用当前设置的分配器分配内存。
 * 
 * @param size 要分配的字节数
 * @return 分配的内存指针，失败返回 NULL
 */
void *co_malloc(size_t size);

/**
 * @brief 内部分配并清零函数
 * 
 * 使用当前设置的分配器分配内存并清零。
 * 
 * @param count 元素数量
 * @param size 每个元素的大小
 * @return 分配的内存指针，失败返回 NULL
 */
void *co_calloc(size_t count, size_t size);

/**
 * @brief 内部重新分配函数
 * 
 * 使用当前设置的分配器重新分配内存。
 * 
 * @param ptr 原内存指针
 * @param size 新的大小
 * @return 重新分配的内存指针，失败返回 NULL
 */
void *co_realloc(void *ptr, size_t size);

/**
 * @brief 内部释放函数
 * 
 * 使用当前设置的分配器释放内存。
 * 
 * @param ptr 要释放的内存指针
 */
void co_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif // LIBCO_ALLOCATOR_H
