/*
 * ptr_array.h - Dynamic pointer array abstraction
 *
 * Provides a type-safe wrapper around void** arrays to handle the
 * multi-level pointer conversion issue in a single place.
 */

#ifndef PTR_ARRAY_H
#define PTR_ARRAY_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Dynamic array of pointers.
 * Uses void** internally to handle pointer conversion in one place.
 */
typedef struct {
    void **items;    /* Array of pointers (owned) */
    size_t count;    /* Number of elements */
    size_t capacity; /* Allocated capacity */
} PtrArray;

/*
 * Initialize a PtrArray with preallocated capacity.
 * Returns true on success, false on allocation failure.
 */
bool ptr_array_init(PtrArray *arr, size_t capacity);

/*
 * Append an element to the array, auto-growing if needed.
 * Returns true on success, false on allocation failure.
 */
bool ptr_array_push(PtrArray *arr, void *ptr);

/*
 * Free the array storage (not the elements themselves).
 * After calling, arr->items is NULL and count/capacity are 0.
 */
void ptr_array_destroy(PtrArray *arr);

/*
 * Extract the raw void** array and reset the PtrArray.
 * Caller takes ownership of the returned pointer.
 * The PtrArray is reset to empty state (items=NULL, count=0, capacity=0).
 */
void **ptr_array_release(PtrArray *arr);

/*
 * Free a raw pointer array previously obtained from ptr_array_release().
 * This is a convenience wrapper around free() that handles the void** cast.
 */
void ptr_array_free_raw(void *ptr);

#endif /* PTR_ARRAY_H */
