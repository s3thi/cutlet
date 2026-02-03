/*
 * ptr_array.c - Dynamic pointer array implementation
 */

#include "ptr_array.h"
#include <stdlib.h>

bool ptr_array_init(PtrArray *arr, size_t capacity) {
    if (!arr)
        return false;

    if (capacity == 0) {
        arr->items = NULL;
        arr->count = 0;
        arr->capacity = 0;
        return true;
    }

    arr->items = (void **)malloc(capacity * sizeof(void *));
    if (!arr->items)
        return false;

    arr->count = 0;
    arr->capacity = capacity;
    return true;
}

bool ptr_array_push(PtrArray *arr, void *ptr) {
    if (!arr)
        return false;

    /* Grow array if needed */
    if (arr->count >= arr->capacity) {
        size_t new_capacity = arr->capacity == 0 ? 4 : arr->capacity * 2;
        void **new_items = (void **)realloc((void *)arr->items, new_capacity * sizeof(void *));
        if (!new_items)
            return false;
        arr->items = new_items;
        arr->capacity = new_capacity;
    }

    arr->items[arr->count++] = ptr;
    return true;
}

void ptr_array_destroy(PtrArray *arr) {
    if (!arr)
        return;

    free((void *)arr->items);
    arr->items = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

void **ptr_array_release(PtrArray *arr) {
    if (!arr)
        return NULL;

    void **items = arr->items;
    arr->items = NULL;
    arr->count = 0;
    arr->capacity = 0;
    return items;
}

void ptr_array_free_raw(void *ptr) { free(ptr); }
