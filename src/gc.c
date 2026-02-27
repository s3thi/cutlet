/*
 * gc.c - Garbage collection infrastructure for Cutlet
 *
 * Implements the global GC state, object allocation/tracking,
 * and shutdown cleanup. The GC maintains an intrusive linked list
 * of all heap-allocated objects, enabling future mark-and-sweep
 * collection.
 *
 * gc_collect() marks all reachable objects, sweeps (frees) unreachable
 * ones, and updates the allocation threshold for the next cycle.
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
#include "runtime.h"
#include "value.h"
#include "vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Initial GC threshold: 1 MiB before first collection attempt. */
#define GC_INITIAL_THRESHOLD ((size_t)1024 * 1024)

/* Module-level GC state. Access serialized by eval_lock in runtime. */
static GC gc;

/* Pointer to the currently executing VM, or NULL if no evaluation is
 * in progress. Set by gc_set_vm() at vm_execute() entry/exit.
 * Used by gc_mark_roots() to walk the VM's stack and call frames. */
static VM *gc_vm;

/* ---- String intern table ----
 *
 * Open-addressing hash table of ObjString* pointers (weak references).
 * Deduplicates strings so that identical content always maps to the
 * same ObjString instance. This enables O(1) string equality via
 * pointer comparison.
 *
 * Entries are weak references: the intern table does NOT keep strings
 * alive. gc_sweep() removes dead (unmarked) entries before freeing
 * the ObjString. A tombstone sentinel marks deleted slots so that
 * probe chains remain intact across removals. */

#define INTERN_INIT_CAP 64
#define INTERN_MAX_LOAD 0.75

/* Tombstone sentinel: marks a deleted slot in the intern table.
 * Must be a unique address that can never be a valid ObjString*.
 * We use the address of a static char as the sentinel value. */
static char intern_tombstone_storage;
#define INTERN_TOMBSTONE ((ObjString *)&intern_tombstone_storage)

static ObjString **intern_table = NULL;
static size_t intern_cap = 0;
static size_t intern_count = 0; /* live entries (excludes tombstones) */
/* intern_used tracks slots that are non-NULL (live + tombstones).
 * This is the count used for load factor calculation, because
 * tombstones still occupy probe chain positions. */
static size_t intern_used = 0;

/*
 * intern_find - look up a string in the intern table by content.
 *
 * Uses open addressing with linear probing. Skips tombstones during
 * the probe walk (they don't terminate the chain). Returns the
 * matching ObjString* if found, NULL otherwise.
 *
 * Parameters:
 *   chars  - the character data to look up
 *   length - number of bytes in chars
 *   hash   - precomputed FNV-1a hash of chars
 */
static ObjString *intern_find(const char *chars, size_t length, uint32_t hash) {
    if (intern_cap == 0)
        return NULL;

    size_t index = hash & (intern_cap - 1);
    for (;;) {
        ObjString *entry = intern_table[index];
        if (entry == NULL) {
            /* Empty slot: end of probe chain — string not found. */
            return NULL;
        }
        if (entry != INTERN_TOMBSTONE && entry->hash == hash && entry->length == length &&
            memcmp(entry->chars, chars, length) == 0) {
            /* Match: same hash, same length, same content. */
            return entry;
        }
        /* Slot is occupied by a different string or a tombstone — continue probing. */
        index = (index + 1) & (intern_cap - 1);
    }
}

/*
 * intern_grow - resize the intern table when load factor is exceeded.
 *
 * Allocates a new table with double the capacity, re-inserts all live
 * entries (skipping tombstones), then frees the old table. Tombstones
 * are discarded during regrow, which reclaims their slots.
 */
static void intern_grow(void) {
    size_t new_cap = intern_cap == 0 ? INTERN_INIT_CAP : intern_cap * 2;
    ObjString **new_table = (ObjString **)calloc(new_cap, sizeof(ObjString *));
    if (!new_table)
        return; /* OOM — keep the old table and hope for the best. */

    /* Re-insert all live entries into the new table. */
    for (size_t i = 0; i < intern_cap; i++) {
        ObjString *entry = intern_table[i];
        if (entry == NULL || entry == INTERN_TOMBSTONE)
            continue;
        size_t index = entry->hash & (new_cap - 1);
        while (new_table[index] != NULL)
            index = (index + 1) & (new_cap - 1);
        new_table[index] = entry;
    }

    free((void *)intern_table);
    intern_table = new_table;
    intern_cap = new_cap;
    /* After regrow, used == count because tombstones are discarded. */
    intern_used = intern_count;
}

/*
 * intern_add - insert an ObjString into the intern table.
 *
 * Grows the table if the load factor (based on used slots, including
 * tombstones) exceeds INTERN_MAX_LOAD. Finds a free slot via linear
 * probing. Reuses tombstone slots when encountered during the probe.
 */
static void intern_add(ObjString *str) {
    /* Grow before inserting if load factor is too high. */
    if (intern_cap == 0 || (double)(intern_used + 1) / (double)intern_cap > INTERN_MAX_LOAD) {
        intern_grow();
    }

    size_t index = str->hash & (intern_cap - 1);
    for (;;) {
        ObjString *entry = intern_table[index];
        if (entry == NULL) {
            /* Empty slot — insert here. Both count and used increase. */
            intern_table[index] = str;
            intern_count++;
            intern_used++;
            return;
        }
        if (entry == INTERN_TOMBSTONE) {
            /* Tombstone slot — reuse it. count increases, used stays
             * the same (tombstone was already counted as used). */
            intern_table[index] = str;
            intern_count++;
            return;
        }
        /* Slot occupied by a live entry — continue probing. */
        index = (index + 1) & (intern_cap - 1);
    }
}

/*
 * intern_remove - remove an ObjString from the intern table.
 *
 * Replaces the string's slot with a tombstone so that probe chains
 * for other entries that hash to nearby slots remain intact.
 * Called by gc_sweep() before freeing a dead OBJ_STRING.
 */
static void intern_remove(ObjString *str) {
    if (intern_cap == 0)
        return;

    size_t index = str->hash & (intern_cap - 1);
    for (;;) {
        ObjString *entry = intern_table[index];
        if (entry == NULL) {
            /* End of probe chain — string not in table (shouldn't happen
             * for properly managed strings, but safe to handle). */
            return;
        }
        if (entry == str) {
            /* Found the exact pointer — replace with tombstone. */
            intern_table[index] = INTERN_TOMBSTONE;
            intern_count--;
            /* intern_used stays the same: tombstone still occupies the slot. */
            return;
        }
        index = (index + 1) & (intern_cap - 1);
    }
}

void gc_init(void) {
    gc.objects = NULL;
    gc.bytes_allocated = 0;
    gc.next_gc = GC_INITIAL_THRESHOLD;
    gc.sweeping = false;

    /* Reset the string intern table. Free any existing table from a
     * previous cycle (e.g. if gc_init is called again in tests). */
    free((void *)intern_table);
    intern_table = NULL;
    intern_cap = 0;
    intern_count = 0;
    intern_used = 0;
}

void *gc_alloc(ObjType type, size_t size) {
    /* In GC_STRESS mode, trigger a collection on every allocation.
     * This is a development-time check that helps catch missing GC
     * roots — if an object isn't properly rooted, a stress-mode
     * collection during allocation will free it prematurely. */
#ifdef GC_STRESS
    gc_collect();
#endif

    /* Automatic GC triggering: when the heap has grown past the
     * threshold, run a collection to reclaim unreachable objects.
     * The threshold is adjusted after each collection in gc_collect(). */
    if (gc.bytes_allocated > gc.next_gc) {
        gc_collect();
    }

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

    /* During sweep, the sweep loop relinks the object list inline.
     * External gc_unlink calls (e.g. from value_free triggered by
     * free_object_contents) must be suppressed to avoid corrupting
     * the list and to prevent O(n^2) behavior. */
    if (gc.sweeping)
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
    case OBJ_STRING: {
        ObjString *s = (ObjString *)obj;
        /* Free the owned character buffer. The ObjString struct itself
         * is freed by the caller (gc_free_all walk). */
        free(s->chars);
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

    /* Free the intern table array and reset state.
     * All ObjStrings have already been freed above, so the table
     * only holds dangling pointers at this point — just free the array. */
    free((void *)intern_table);
    intern_table = NULL;
    intern_cap = 0;
    intern_count = 0;
    intern_used = 0;
}

/*
 * gc_mark_value - mark the heap object inside a Value (if any).
 *
 * Dispatches on the Value's type to extract the embedded Obj pointer
 * and call gc_mark_object. Scalar types (number, bool, nothing, error)
 * contain no heap objects and are no-ops.
 */
void gc_mark_value(Value *v) {
    switch (v->type) {
    case VAL_FUNCTION:
        gc_mark_object((Obj *)v->function);
        break;
    case VAL_CLOSURE:
        gc_mark_object((Obj *)v->closure);
        break;
    case VAL_ARRAY:
        gc_mark_object((Obj *)v->array);
        break;
    case VAL_MAP:
        gc_mark_object((Obj *)v->map);
        break;
    case VAL_STRING:
        gc_mark_object((Obj *)v->string);
        break;
    case VAL_NUMBER:
    case VAL_BOOL:
    case VAL_NOTHING:
    case VAL_ERROR:
        /* Scalar/non-heap types — nothing to mark. */
        break;
    }
}

/*
 * gc_mark_object - mark a heap object and recursively trace its children.
 *
 * If obj is NULL or already marked, returns immediately (cycle safety).
 * Sets is_marked = true, then dispatches on obj->type to trace any
 * child objects (Values, embedded pointers) that are reachable from
 * this object.
 *
 * Note on recursion depth: deeply nested closures/arrays could cause
 * deep recursion. For typical program depths this is fine. An explicit
 * worklist (gray stack) could be added later if stack overflow becomes
 * a concern.
 */
void gc_mark_object(Obj *obj) {
    if (obj == NULL || obj->is_marked)
        return;

    obj->is_marked = true;

    switch (obj->type) {
    case OBJ_STRING:
        /* Leaf object — no children to trace. */
        break;

    case OBJ_UPVALUE: {
        ObjUpvalue *uv = (ObjUpvalue *)obj;
        /* If the upvalue is closed (location points to the inline
         * closed field), the closed value may reference heap objects. */
        if (uv->location == &uv->closed)
            gc_mark_value(&uv->closed);
        break;
    }

    case OBJ_FUNCTION: {
        ObjFunction *fn = (ObjFunction *)obj;
        /* Native functions have chunk == NULL — nothing to trace.
         * User-defined functions own a chunk whose constant pool
         * may contain ObjStrings and ObjFunctions. */
        if (fn->chunk != NULL) {
            for (size_t i = 0; i < fn->chunk->const_count; i++)
                gc_mark_value(&fn->chunk->constants[i]);
        }
        break;
    }

    case OBJ_CLOSURE: {
        ObjClosure *cl = (ObjClosure *)obj;
        /* Mark the underlying function. */
        gc_mark_object((Obj *)cl->function);
        /* Mark each captured upvalue. */
        for (int i = 0; i < cl->upvalue_count; i++) {
            if (cl->upvalues[i] != NULL)
                gc_mark_object((Obj *)cl->upvalues[i]);
        }
        break;
    }

    case OBJ_ARRAY: {
        ObjArray *arr = (ObjArray *)obj;
        /* Mark each element value in the array's data. */
        for (size_t i = 0; i < arr->count; i++)
            gc_mark_value(&arr->data[i]);
        break;
    }

    case OBJ_MAP: {
        ObjMap *m = (ObjMap *)obj;
        /* Mark each key-value pair in the map's entries. */
        for (size_t i = 0; i < m->count; i++) {
            gc_mark_value(&m->entries[i].key);
            gc_mark_value(&m->entries[i].value);
        }
        break;
    }
    }
}

void gc_set_vm(VM *vm) { gc_vm = vm; }

/*
 * gc_mark_roots - walk all root sets and mark every directly
 * reachable object.
 *
 * Root sets:
 *   1. VM value stack (stack[0] through stack_top - 1).
 *   2. Call frame closures (frames[0..frame_count-1].closure).
 *   3. Open upvalue linked list (vm->open_upvalues).
 *   4. Global variable table (via runtime_mark_globals — Step 4).
 *
 * If no VM is active (gc_vm == NULL), only globals are marked.
 * The VM's stack-allocated script_fn and script_closure (frame 0)
 * are safe to mark: gc_mark_object sets is_marked on them but
 * sweep (task 4) only walks the GC object list, so non-list
 * objects are never freed.
 */
void gc_mark_roots(void) {
    if (gc_vm != NULL) {
        /* Mark every Value on the VM value stack. */
        for (Value *slot = gc_vm->stack; slot < gc_vm->stack_top; slot++)
            gc_mark_value(slot);

        /* Mark the closure in each active call frame.
         * This includes frame 0's stack-allocated script_closure. */
        for (int i = 0; i < gc_vm->frame_count; i++)
            gc_mark_object((Obj *)gc_vm->frames[i].closure);

        /* Mark every open upvalue on the linked list. */
        for (ObjUpvalue *uv = gc_vm->open_upvalues; uv != NULL; uv = uv->next)
            gc_mark_object((Obj *)uv);
    }

    /* Mark all Values in the global variable table. Globals persist
     * across evaluations, so this must run even when gc_vm is NULL. */
    runtime_mark_globals();
}

/*
 * gc_collect - run a full mark-and-sweep garbage collection cycle.
 *
 * 1. Marks all reachable objects via gc_mark_roots().
 * 2. Sweeps the object list via gc_sweep(), freeing unmarked objects
 *    and clearing is_marked on survivors.
 * 3. Updates the next_gc threshold based on surviving bytes: at least
 *    1024 bytes, otherwise double the current allocation.
 *
 * When GC_LOG is defined, prints collection stats to stderr.
 */
void gc_collect(void) {
#ifdef GC_LOG
    size_t bytes_before = gc.bytes_allocated;

    /* Count objects before sweep for logging. */
    size_t obj_count_before = 0;
    for (Obj *o = gc.objects; o != NULL; o = o->next)
        obj_count_before++;
#endif

    /* Mark all objects reachable from roots. */
    gc_mark_roots();

    /* Sweep: free unreachable objects, clear marks on survivors. */
    gc_sweep();

    /* Update the collection threshold based on surviving bytes.
     * Minimum threshold is 1024 bytes to avoid thrashing on small heaps. */
    gc.next_gc = gc.bytes_allocated < 1024 ? 1024 : gc.bytes_allocated * 2;

#ifdef GC_LOG
    size_t obj_count_after = 0;
    for (Obj *o = gc.objects; o != NULL; o = o->next)
        obj_count_after++;

    size_t objects_freed = obj_count_before - obj_count_after;
    size_t bytes_freed = bytes_before - gc.bytes_allocated;
    fprintf(stderr, "-- gc collected %zu objects, %zu bytes. %zu remaining.\n", objects_freed,
            bytes_freed, gc.bytes_allocated);
#endif
}

/* ---- Sweep phase ---- */

/*
 * gc_sweep - walk the object list and free unmarked objects.
 *
 * Iterates the gc.objects linked list with a prev pointer for O(1)
 * relinking. For each object:
 *   - If marked: clear the mark (ready for the next cycle), advance
 *     prev to this object, and move to next.
 *   - If unmarked: unlink from the list (prev->next = obj->next, or
 *     gc.objects = obj->next if it's the head), free internal contents
 *     via free_object_contents(), decrement bytes_allocated, and free
 *     the object struct. prev stays the same.
 *
 * The gc.sweeping flag is set to true for the duration so that
 * gc_unlink() (which may be called indirectly from value_free inside
 * free_object_contents) becomes a no-op — the sweep loop already
 * handles relinking, so an external gc_unlink would corrupt the list.
 */
void gc_sweep(void) {
    gc.sweeping = true;

    Obj *prev = NULL;
    Obj *obj = gc.objects;

    while (obj) {
        if (obj->is_marked) {
            /* Object is reachable — clear the mark for the next
             * collection cycle and advance. */
            obj->is_marked = false;
            prev = obj;
            obj = obj->next;
        } else {
            /* Object is unreachable — unlink and free it. */
            Obj *unreached = obj;
            obj = obj->next;

            /* Relink around the freed object. */
            if (prev != NULL) {
                prev->next = obj;
            } else {
                /* unreached was the list head. */
                gc.objects = obj;
            }

            /* Decrement bytes_allocated by the object's recorded size. */
            if (gc.bytes_allocated >= unreached->alloc_size)
                gc.bytes_allocated -= unreached->alloc_size;
            else
                gc.bytes_allocated = 0;

            /* If the object is an interned string, remove it from the
             * intern table BEFORE freeing. This prevents the table from
             * holding dangling pointers to freed ObjStrings. */
            if (unreached->type == OBJ_STRING)
                intern_remove((ObjString *)unreached);

            /* Free type-specific internal allocations (name, data,
             * entries, etc.) then the object struct itself. */
            free_object_contents(unreached);
            free(unreached);
        }
    }

    gc.sweeping = false;
}

/* ---- String intern public API ---- */

/*
 * gc_intern_find - public wrapper around intern_find().
 *
 * Called by obj_string_new() and obj_string_take() in value.c to check
 * whether a string with the given content already exists in the intern
 * table. Returns the existing ObjString* if found, NULL otherwise.
 */
ObjString *gc_intern_find(const char *chars, size_t length, uint32_t hash) {
    return intern_find(chars, length, hash);
}

/*
 * gc_intern_add - public wrapper around intern_add().
 *
 * Called by obj_string_new() and obj_string_take() in value.c after
 * allocating a new ObjString to register it in the intern table.
 */
void gc_intern_add(ObjString *str) { intern_add(str); }

/* ---- Test/inspection accessors ---- */

Obj *gc_get_objects(void) { return gc.objects; }

size_t gc_get_bytes_allocated(void) { return gc.bytes_allocated; }

size_t gc_get_next_gc(void) { return gc.next_gc; }

void gc_set_next_gc(size_t threshold) { gc.next_gc = threshold; }
