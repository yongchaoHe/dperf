#include <stdlib.h>
#include <stdbool.h>

#include "list.h"

list_t* list_alloc() {
    list_t* list = (list_t*) malloc(sizeof(list_t));
    list->sentinel.next = &list->sentinel;
    list->sentinel.prev = &list->sentinel;
    list->length = 0;
    return list;
}

void list_free(list_t* list) {
    if (list) {
        list_item_t* item = list->sentinel.next;
        list_item_t* next;
        while(item != &list->sentinel) {
            next = item->next;
            free(item);
            item = next;
        }
        free(list);
    }
}

bool list_insert(list_t *list, list_item_t *item, long long idx) {
    int i;
    list_item_t *ptr, *next;

    if ( idx >= (long long) list->length )
        return false;

    if ( 0 == idx ) {
        list_prepend(list, item);
    } else {
        ptr = list->sentinel.next;
        for ( i = 0; i < idx-1; i++ )
            ptr = ptr->next;
        next = ptr->next;
        item->next = next;
        item->prev = ptr;
        next->prev = item;
        ptr->next = item;
    }

    list->length++;
    return true;
}

int list_sort(list_t* list, list_item_compare_fn_t compare) {
    list_item_t* item;
    list_item_t** items;
    size_t i, index=0;

    if (0 == list->length)
        return 0;
    items = (list_item_t**)malloc(sizeof(list_item_t*) * list->length);

    if (NULL == items)
        return -1;

    while(NULL != (item = list_remove_first(list)))
        items[index++] = item;

    qsort(items, index, sizeof(list_item_t*), (int(*)(const void*, const void*))compare);
    for (i=0; i<index; i++)
        list_append(list, items[i]);
    
    free(items);
    return 0;
}