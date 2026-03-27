/**
 * @file co_queue.h
 * @brief 协程就绪队列（双向链表）
 * 
 * 用于管理就绪状态的协程。
 */

#ifndef LIBCO_CO_QUEUE_H
#define LIBCO_CO_QUEUE_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// 链表节点
// ============================================================================

/**
 * @brief 双向链表节点
 * 
 * 这是一个侵入式链表节点，通常嵌入在其他结构体中。
 */
typedef struct co_queue_node {
    struct co_queue_node *next;
    struct co_queue_node *prev;
} co_queue_node_t;

/**
 * @brief 初始化链表节点
 */
static inline void co_queue_node_init(co_queue_node_t *node) {
    node->next = node;
    node->prev = node;
}

/**
 * @brief 检查节点是否在链表中
 */
static inline bool co_queue_node_is_linked(const co_queue_node_t *node) {
    return node->next != node;
}

// ============================================================================
// 队列（双向链表）
// ============================================================================

/**
 * @brief 协程队列（双向循环链表）
 */
typedef struct co_queue {
    co_queue_node_t head;    /**< 哨兵节点 */
    size_t size;             /**< 队列大小 */
} co_queue_t;

/**
 * @brief 初始化队列
 */
static inline void co_queue_init(co_queue_t *queue) {
    co_queue_node_init(&queue->head);
    queue->size = 0;
}

/**
 * @brief 检查队列是否为空
 */
static inline bool co_queue_empty(const co_queue_t *queue) {
    return queue->size == 0;
}

/**
 * @brief 获取队列大小
 */
static inline size_t co_queue_size(const co_queue_t *queue) {
    return queue->size;
}

/**
 * @brief 在队尾添加节点
 */
static inline void co_queue_push_back(co_queue_t *queue, co_queue_node_t *node) {
    co_queue_node_t *tail = queue->head.prev;
    
    node->next = &queue->head;
    node->prev = tail;
    tail->next = node;
    queue->head.prev = node;
    
    queue->size++;
}

/**
 * @brief 在队首添加节点
 */
static inline void co_queue_push_front(co_queue_t *queue, co_queue_node_t *node) {
    co_queue_node_t *front = queue->head.next;
    
    node->next = front;
    node->prev = &queue->head;
    front->prev = node;
    queue->head.next = node;
    
    queue->size++;
}

/**
 * @brief 从队列中移除节点
 */
static inline void co_queue_remove(co_queue_t *queue, co_queue_node_t *node) {
    if (!co_queue_node_is_linked(node)) {
        return;
    }
    
    node->prev->next = node->next;
    node->next->prev = node->prev;
    
    co_queue_node_init(node);  // 重置为未链接状态
    queue->size--;
}

/**
 * @brief 弹出队首节点
 */
static inline co_queue_node_t *co_queue_pop_front(co_queue_t *queue) {
    if (co_queue_empty(queue)) {
        return NULL;
    }
    
    co_queue_node_t *node = queue->head.next;
    co_queue_remove(queue, node);
    return node;
}

/**
 * @brief 获取队首节点（不移除）
 */
static inline co_queue_node_t *co_queue_front(const co_queue_t *queue) {
    if (co_queue_empty(queue)) {
        return NULL;
    }
    return queue->head.next;
}

/**
 * @brief 获取队尾节点（不移除）
 */
static inline co_queue_node_t *co_queue_back(const co_queue_t *queue) {
    if (co_queue_empty(queue)) {
        return NULL;
    }
    return queue->head.prev;
}

// ============================================================================
// 遍历宏
// ============================================================================

/**
 * @brief 遍历队列
 * @param queue 队列指针
 * @param node 当前节点指针
 */
#define co_queue_foreach(queue, node) \
    for (co_queue_node_t *node = (queue)->head.next; \
         node != &(queue)->head; \
         node = node->next)

/**
 * @brief 安全遍历队列（可以在遍历中删除节点）
 * @param queue 队列指针
 * @param node 当前节点指针
 * @param temp 临时节点指针
 */
#define co_queue_foreach_safe(queue, node, temp) \
    for (co_queue_node_t *node = (queue)->head.next, *temp = node->next; \
         node != &(queue)->head; \
         node = temp, temp = node->next)

#ifdef __cplusplus
}
#endif

#endif // LIBCO_CO_QUEUE_H
