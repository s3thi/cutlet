/*
 * gc.c - Garbage collection infrastructure for Cutlet
 *
 * Implements the global GC state, object allocation/tracking,
 * and shutdown cleanup. The GC maintains an intrusive linked list
 * of all heap-allocated objects, enabling future mark-and-sweep
 * collection.
 *
 * gc_collect() is currently a no-op — marking and sweeping will
 * be implemented in later GC tasks.
 *
 * Design notes:
 * - Module-level static GC state (like var_table in runtime.c).
 * - gc_alloc() uses calloc for zero-initialization.
 * - gc_free_all() frees object contents then the struct itself.
 * - free_object_contents() is the type-specific content destructor.
 *   It duplicates some logic from obj_*_free() in value.c, which
 *   is acceptable during the transition. Once refcounting is removed
 *   (GC task 5), obj_*_free() will be replaced entirely.
 */

#include "gc.h"
#include <stdlib.h>

/* chunk.h and value.h will be needed once free_object_contents()
 * is activated in Step 3 (Obj header embedded in all struct types). */

/* Initial GC threshold: 1 MiB before first collection attempt. */
#define GC_INITIAL_THRESHOLD (1024 * 1024)

/* Module-level GC state. Access serialized by eval_lock in runtime. */
static GC gc;

void gc_init(void) {
    gc.objects = NULL;
    gc.bytes_allocated = 0;
    gc.next_gc = GC_INITIAL_THRESHOLD;
}

void *gc_alloc(ObjType type, size_t size) {
    void *ptr = calloc(1, size);
    if (!ptr)
        return NULL;

    /* Initialize the Obj header at the start of the allocation. */
    Obj *obj = (Obj *)ptr;
    obj->type = type;
    obj->is_marked = false;
    obj->alloc_size = size;

    /* Prepend to the global object list. */
    obj->next = gc.objects;
    gc.objects = obj;

    gc.bytes_allocated += size;
    return ptr;
}

void gc_unlink(Obj *obj) {
    if (!obj)
        return;

    /* Walk the list to find and unlink obj. */
    Obj **prev = &gc.objects;
    while (*prev) {
        if (*prev == obj) {
            *prev = obj->next;
            obj->next = NULL;
            if (gc.bytes_allocated >= obj->alloc_size)
                gc.bytes_allocated -= obj->alloc_size;
            else
                gc.bytes_allocated = 0;
            return;
        }
        prev = &(*prev)->next;
    }
    /* obj was not in the list — no-op. */
}

void gc_free_object(Obj *obj) {
    if (!obj)
        return;
    gc_unlink(obj);
    free(obj);
}

/*
 * Free the internal contents of an object based on its type.
 * Does NOT free the object struct itself — the caller handles that.
 *
 * IMPORTANT: This function casts (Obj*) to the concrete type (e.g.,
 * ObjArray*). This only works correctly once the Obj header is embedded
 * as the first field of each concrete struct (Step 3). Until then,
 * this is a no-op stub — the casts would produce wrong field offsets.
 *
 * The full implementation is commented out below and will be activated
 * in Step 3 when the Obj header is embedded in all five struct types.
 */
static void free_object_contents(Obj *obj) {
    /*
     * TODO(Step 3): Activate once Obj is embedded as first field.
     * Until then, gc_free_all() only frees the object structs, not
     * their internal contents (name, data, entries, etc.). This is
     * acceptable because:
     * - The GC tests allocate raw objects that have no content to free.
     * - Real objects are still freed via the existing refcount-based
     *   obj_*_free() functions during normal execution.
     */
    (void)obj; /* Suppress unused parameter warning. */

    /* Planned implementation for reference (activated in Step 3):
     *
     * switch (obj->type) {
     * case OBJ_FUNCTION: {
     *     ObjFunction *fn = (ObjFunction *)obj;
     *     free(fn->name);
     *     if (fn->params) {
     *         for (int i = 0; i < fn->arity; i++)
     *             free(fn->params[i]);
     *         free((void *)fn->params);
     *     }
     *     if (fn->chunk) {
     *         chunk_free(fn->chunk);
     *         free(fn->chunk);
     *     }
     *     break;
     * }
     * case OBJ_CLOSURE: {
     *     ObjClosure *cl = (ObjClosure *)obj;
     *     free((void *)cl->upvalues);
     *     break;
     * }
     * case OBJ_ARRAY: {
     *     ObjArray *arr = (ObjArray *)obj;
     *     for (size_t i = 0; i < arr->count; i++)
     *         value_free(&arr->data[i]);
     *     free(arr->data);
     *     break;
     * }
     * case OBJ_MAP: {
     *     ObjMap *m = (ObjMap *)obj;
     *     for (size_t i = 0; i < m->count; i++) {
     *         value_free(&m->entries[i].key);
     *         value_free(&m->entries[i].value);
     *     }
     *     free(m->entries);
     *     break;
     * }
     * case OBJ_UPVALUE: {
     *     ObjUpvalue *uv = (ObjUpvalue *)obj;
     *     if (uv->location == &uv->closed)
     *         value_free(&uv->closed);
     *     break;
     * }
     * }
     */
}

void gc_free_all(void) {
    Obj *obj = gc.objects;
    while (obj) {
        Obj *next = obj->next;
        free_object_contents(obj);
        free(obj);
        obj = next;
    }
    gc.objects = NULL;
    gc.bytes_allocated = 0;
}

void gc_collect(void) { /* No-op stub. Mark-and-sweep will be implemented in a later task. */ }

/* ---- Test/inspection accessors ---- */

Obj *gc_get_objects(void) { return gc.objects; }

size_t gc_get_bytes_allocated(void) { return gc.bytes_allocated; }
