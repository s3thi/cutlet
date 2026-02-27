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
#include "chunk.h"
#include "value.h"
#include <stdlib.h>

/* Initial GC threshold: 1 MiB before first collection attempt. */
#define GC_INITIAL_THRESHOLD ((size_t)1024 * 1024)

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
 * Casts (Obj*) to the concrete type (e.g., ObjArray*). This works
 * because the Obj header is the first field of every concrete struct,
 * so the cast produces correct field offsets.
 *
 * This duplicates some logic from the existing obj_*_free() functions
 * in value.c. Both coexist during the transition: refcount-based
 * obj_*_free() handles normal lifetime, while free_object_contents()
 * handles gc_free_all() at shutdown. Once refcounting is removed
 * (GC task 5), obj_*_free() will be replaced entirely.
 */
static void free_object_contents(Obj *obj) {
    switch (obj->type) {
    case OBJ_FUNCTION: {
        ObjFunction *fn = (ObjFunction *)obj;
        free(fn->name);
        if (fn->params) {
            for (int i = 0; i < fn->arity; i++)
                free(fn->params[i]);
            free((void *)fn->params);
        }
        if (fn->chunk) {
            chunk_free(fn->chunk);
            free(fn->chunk);
        }
        /* Do NOT free the ObjFunction struct itself — the caller does that. */
        break;
    }
    case OBJ_CLOSURE: {
        ObjClosure *cl = (ObjClosure *)obj;
        /* Free the upvalue pointer array, but NOT the upvalues themselves.
         * Upvalues are separate objects on the GC list and will be freed
         * when their own turn comes in the gc_free_all() walk. */
        free((void *)cl->upvalues);
        break;
    }
    case OBJ_ARRAY: {
        ObjArray *arr = (ObjArray *)obj;
        for (size_t i = 0; i < arr->count; i++)
            value_free(&arr->data[i]);
        free(arr->data);
        break;
    }
    case OBJ_MAP: {
        ObjMap *m = (ObjMap *)obj;
        for (size_t i = 0; i < m->count; i++) {
            value_free(&m->entries[i].key);
            value_free(&m->entries[i].value);
        }
        free(m->entries);
        break;
    }
    case OBJ_UPVALUE: {
        ObjUpvalue *uv = (ObjUpvalue *)obj;
        /* If the upvalue is closed (location points to &closed),
         * free the captured value. Open upvalues don't own their
         * location's value — it belongs to the stack. */
        if (uv->location == &uv->closed)
            value_free(&uv->closed);
        break;
    }
    }
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
