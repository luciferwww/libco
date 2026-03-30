/**
 * @file co_queue.h
 * @brief Coroutine ready queue (doubly linked list)
 * 
 * Used to manage coroutines in the ready state.
 */

#ifndef LIBCO_CO_QUEUE_H
#define LIBCO_CO_QUEUE_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// List nodes
// ============================================================================

/**
 * @brief Doubly linked list node
 * 
 * This is an intrusive list node, typically embedded in another structure.
 */
typedef struct co_queue_node {
    struct co_queue_node *next;
    struct co_queue_node *prev;
} co_queue_node_t;

/**
 * @brief Initialize a list node
 */
static inline void co_queue_node_init(co_queue_node_t *node) {
    node->next = node;
    node->prev = node;
}

/**
 * @brief Check whether a node is linked into a list
 */
static inline bool co_queue_node_is_linked(const co_queue_node_t *node) {
    return node->next != node;
}

// ============================================================================
// Queue (doubly linked list)
// ============================================================================

/**
 * @brief Coroutine queue (circular doubly linked list)
 */
typedef struct co_queue {
    co_queue_node_t head;    /**< Sentinel node */
    size_t size;             /**< Queue size */
} co_queue_t;

/**
 * @brief Initialize a queue
 */
static inline void co_queue_init(co_queue_t *queue) {
    co_queue_node_init(&queue->head);
    queue->size = 0;
}

/**
 * @brief Check whether a queue is empty
 */
static inline bool co_queue_empty(const co_queue_t *queue) {
    return queue->size == 0;
}

/**
 * @brief Get the queue size
 */
static inline size_t co_queue_size(const co_queue_t *queue) {
    return queue->size;
}

/**
 * @brief Append a node to the back of the queue
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
 * @brief Insert a node at the front of the queue
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
 * @brief Remove a node from the queue
 */
static inline void co_queue_remove(co_queue_t *queue, co_queue_node_t *node) {
    if (!co_queue_node_is_linked(node)) {
        return;
    }
    
    node->prev->next = node->next;
    node->next->prev = node->prev;
    
    co_queue_node_init(node);  // Reset to the unlinked state
    queue->size--;
}

/**
 * @brief Pop the front node
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
 * @brief Get the front node without removing it
 */
static inline co_queue_node_t *co_queue_front(const co_queue_t *queue) {
    if (co_queue_empty(queue)) {
        return NULL;
    }
    return queue->head.next;
}

/**
 * @brief Get the back node without removing it
 */
static inline co_queue_node_t *co_queue_back(const co_queue_t *queue) {
    if (co_queue_empty(queue)) {
        return NULL;
    }
    return queue->head.prev;
}

// ============================================================================
// Iteration macros
// ============================================================================

/**
 * @brief Iterate over a queue
 * @param queue Queue pointer
 * @param node Current node pointer
 */
#define co_queue_foreach(queue, node) \
    for (co_queue_node_t *node = (queue)->head.next; \
         node != &(queue)->head; \
         node = node->next)

/**
 * @brief Safely iterate over a queue while allowing node removal
 * @param queue Queue pointer
 * @param node Current node pointer
 * @param temp Temporary node pointer
 */
#define co_queue_foreach_safe(queue, node, temp) \
    for (co_queue_node_t *node = (queue)->head.next, *temp = node->next; \
         node != &(queue)->head; \
         node = temp, temp = node->next)

#ifdef __cplusplus
}
#endif

#endif // LIBCO_CO_QUEUE_H
