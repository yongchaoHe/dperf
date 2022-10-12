#ifndef _LIST_H_
#define _LIST_H_

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

struct list_item_t {
    struct list_item_t* prev;
    struct list_item_t* next;
};
typedef struct list_item_t list_item_t;

#define list_get_next(item) \
    ((item) ? ((list_item_t*) ((list_item_t*)(item))->next) : NULL)

#define list_get_prev(item) \
    ((item) ? ((list_item_t*) ((list_item_t*)(item))->prev) : NULL)

struct list_t {
    list_item_t sentinel;
    volatile size_t length;
};
typedef struct list_t list_t;

/**
 * Loop over a list.
 *
 * @param[in] item Storage for each item
 * @param[in] list List to iterate over
 * @param[in] type Type of each list item
 *
 * This macro provides a simple way to loop over the items in an list_t. It
 * is not safe to call list_remove_item from within the loop.
 */
#define YC_LIST_FOREACH(item, list, type)                             \
  for (item = (type *) (list)->sentinel.next ;      \
       item != (type *) &(list)->sentinel ;                   \
       item = (type *) ((list_item_t *) (item))->next)

/**
 * Loop over a list in reverse.
 *
 * @param[in] item Storage for each item
 * @param[in] list List to iterate over
 * @param[in] type Type of each list item
 *
 * This macro provides a simple way to loop over the items in an list_t. It
 * is not safe to call list_remove_item from within the loop.
 */
#define YC_LIST_FOREACH_REV(item, list, type)                         \
  for (item = (type *) (list)->sentinel.prev ;      \
       item != (type *) &(list)->sentinel ;                   \
       item = (type *) ((list_item_t *) (item))->prev)


/**
 * Loop over a list in a *safe* way
 *
 * @param[in] item Storage for each item
 * @param[in] next Storage for next item
 * @param[in] list List to iterate over
 * @param[in] type Type of each list item
 *
 * This macro provides a simple way to loop over the items in an list_t. It
 * is safe to call list_remove_item(list, item) from within the loop.
 */
#define YC_LIST_FOREACH_SAFE(item, next, list, type)                  \
  for (item = (type *) (list)->sentinel.next,       \
         next = (type *) ((list_item_t *) (item))->next ;\
       item != (type *) &(list)->sentinel ;                   \
       item = next, next = (type *) ((list_item_t *) (item))->next)

/**
 * Loop over a list in a *safe* way
 *
 * @param[in] item Storage for each item
 * @param[in] next Storage for next item
 * @param[in] list List to iterate over
 * @param[in] type Type of each list item
 *
 * This macro provides a simple way to loop over the items in an list_t. If
 * is safe to call list_remove_item(list, item) from within the loop.
 */
#define YC_LIST_FOREACH_SAFE_REV(item, prev, list, type)              \
  for (item = (type *) (list)->sentinel.prev,       \
         prev = (type *) ((list_item_t *) (item))->prev ;\
       item != (type *) &(list)->sentinel ;                   \
       item = prev, prev = (type *) ((list_item_t *) (item))->prev)

static inline bool list_is_empty(list_t* list) {
    return (list->sentinel.next == &(list->sentinel) ? true : false);
}

static inline list_item_t* list_get_first(list_t* list) {
    return list->sentinel.next;
}

static inline list_item_t* list_get_last(list_t* list) {
    return list->sentinel.prev;
}

static inline list_item_t* list_get_begin(list_t* list) {
    return &(list->sentinel);
}

static inline list_item_t* list_get_end(list_t* list) {
    return &(list->sentinel);
}

static inline size_t list_get_size(list_t* list) {
    return list->length;
}

static inline list_item_t *list_remove_item(list_t *list, list_item_t *item) {
    item->prev->next=item->next;
    item->next->prev=item->prev;
    list->length--;
    return (list_item_t *)item->prev;
}

static inline void list_append(list_t *list, list_item_t *item) {
    list_item_t* sentinel = &(list->sentinel);
    item->prev = sentinel->prev;
    sentinel->prev->next = item;
    item->next = sentinel;
    sentinel->prev = item;
    list->length++;
}

static inline void list_prepend(list_t *list, list_item_t *item) {
    list_item_t* sentinel = &(list->sentinel);
    item->next = sentinel->next;
    item->prev = sentinel;
    sentinel->next->prev = item;
    sentinel->next = item;
    list->length++;
}

static inline list_item_t *list_remove_first(list_t *list) {
    volatile list_item_t *item;
    if ( 0 == list->length )
        return (list_item_t *) NULL;

    list->length--;
    item = list->sentinel.next;
    item->next->prev = item->prev;
    list->sentinel.next = item->next;
    return (list_item_t *) item;
}

static inline list_item_t *list_remove_last(list_t *list) {
    volatile list_item_t  *item;
    if ( 0 == list->length )
        return (list_item_t *)NULL;

    list->length--;
    item = list->sentinel.prev;
    item->prev->next = item->next;
    list->sentinel.prev = item->prev;
    return (list_item_t *) item;
}

static inline void list_insert_pos(list_t *list, list_item_t *pos, list_item_t *item) {
    item->next = pos;
    item->prev = pos->prev;
    pos->prev->next = item;
    pos->prev = item;
    list->length++;
}

list_t* list_alloc();
void list_free(list_t* list);
bool list_insert(list_t *list, list_item_t *item, long long idx);
typedef int (*list_item_compare_fn_t)(list_item_t **a, list_item_t **b);
int list_sort(list_t* list, list_item_compare_fn_t compare);
#endif