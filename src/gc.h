/*
 * gc.h - Garbage collection infrastructure for Cutlet
 *
 * Provides the Obj base header, object type tags, a global object
 * tracking list, and allocation/deallocation primitives. Every
 * heap-allocated object (ObjFunction, ObjClosure, ObjArray, ObjMap,
 * ObjUpvalue, ObjString) will eventually embed an Obj as its first field so
 * that the GC can walk all live objects via a single linked list.
 *
 * This is the structural foundation for mark-and-sweep GC.
 * gc_collect() marks all reachable objects and clears marks afterward.
 * Sweep (actual freeing of unreachable objects) is implemented in
 * a later GC task.
 *
 * Refcounting remains the primary lifetime management mechanism
 * during the transition period. The GC list exists alongside it.
 */

#ifndef CUTLET_GC_H
#define CUTLET_GC_H

#include <stdbool.h>
#include <stddef.h>

/* Object type tags for GC-tracked heap objects. */
typedef enum {
    OBJ_FUNCTION,
    OBJ_CLOSURE,
    OBJ_ARRAY,
    OBJ_MAP,
    OBJ_UPVALUE,
    OBJ_STRING,
} ObjType;

/*
 * Obj - common header for all GC-tracked heap objects.
 *
 * Must be the first field of every heap object struct so that
 * casting (Obj*)ptr works correctly.
 *
 * type:       Identifies the concrete object type (OBJ_FUNCTION, etc.).
 * is_marked:  Used by the mark phase of mark-and-sweep GC.
 * next:       Intrusive linked list pointer for the global object list.
 * alloc_size: Records the allocation size for bytes_allocated tracking.
 */
typedef struct Obj {
    ObjType type;
    bool is_marked;
    struct Obj *next;
    size_t alloc_size;
} Obj;

/*
 * GC - global garbage collector state.
 *
 * objects:         Head of the intrusive linked list of all GC-tracked objects.
 * bytes_allocated: Running total of memory allocated through gc_alloc().
 * next_gc:         Threshold (in bytes) at which gc_collect() triggers.
 */
typedef struct {
    Obj *objects;
    size_t bytes_allocated;
    size_t next_gc;
} GC;

/* Cast any Obj-embedded pointer to read its type tag. */
#define OBJ_TYPE(obj) (((Obj *)(obj))->type)

/*
 * Initialize the global GC state. Must be called before any gc_alloc().
 * Sets objects list to NULL, bytes_allocated to 0, and next_gc to an
 * initial threshold.
 */
void gc_init(void);

/*
 * Allocate a GC-tracked object of the given type and size.
 *
 * Uses calloc(1, size) so all fields are zero-initialized.
 * Initializes the Obj header (type, is_marked=false, alloc_size=size)
 * and prepends the object to the global object list.
 *
 * Returns a pointer to the allocated memory, or NULL on failure.
 */
void *gc_alloc(ObjType type, size_t size);

/*
 * Unlink an object from the global GC object list without freeing it.
 * Decrements bytes_allocated by obj->alloc_size.
 * Safe to call if obj is not in the list (no-op in that case).
 */
void gc_unlink(Obj *obj);

/*
 * Free a single GC-tracked object: unlink it from the list, then free.
 * Does NOT free internal contents (name, data arrays, etc.) — only
 * the object struct itself. Callers should free contents first if needed.
 */
void gc_free_object(Obj *obj);

/*
 * Free all objects on the GC list. For each object, frees internal
 * contents (type-specific: name, data, entries, etc.) then frees
 * the object struct. Resets the list to empty and bytes_allocated to 0.
 * Used at shutdown.
 */
void gc_free_all(void);

/*
 * Run a garbage collection cycle. Marks all reachable objects via
 * gc_mark_roots(), then clears all is_marked flags. No objects are
 * freed yet (sweep is task 4).
 */
void gc_collect(void);

/* ---- Mark phase ---- */

/* Forward declaration of Value (defined in value.h). Needed here so
 * gc_mark_value can accept a Value pointer without requiring value.h. */
struct Value;

/*
 * Mark a single heap object as reachable and recursively trace its
 * children. Safe to call with NULL (no-op). If the object is already
 * marked, returns immediately to prevent infinite loops on cycles.
 *
 * Tracing rules by type:
 *   OBJ_STRING:   leaf — no children.
 *   OBJ_UPVALUE:  if closed (location == &closed), marks the closed Value.
 *   OBJ_FUNCTION: iterates chunk->constants and marks each Value.
 *   OBJ_CLOSURE:  marks the underlying function and each upvalue.
 *   OBJ_ARRAY:    marks each element Value.
 *   OBJ_MAP:      marks each key and value in entries.
 */
void gc_mark_object(Obj *obj);

/*
 * Mark the heap object (if any) inside a Value and recursively trace
 * its children. Values that don't contain heap objects (VAL_NUMBER,
 * VAL_BOOL, VAL_NOTHING, VAL_ERROR) are no-ops.
 */
void gc_mark_value(struct Value *v);

/* ---- Root marking ---- */

/* Forward declaration of VM (defined in vm.h). Avoids circular includes
 * since vm.h already includes value.h -> gc-adjacent types. */
struct VM;

/*
 * Store a pointer to the currently executing VM so that gc_mark_roots()
 * can walk the VM's stack, call frames, and open upvalues. Called at the
 * start of vm_execute() (with &vm) and at the end (with NULL).
 */
void gc_set_vm(struct VM *vm);

/*
 * Walk all root sets and mark every directly reachable object:
 *   - VM value stack (if a VM is active)
 *   - Call frame closures (if a VM is active)
 *   - Open upvalue list (if a VM is active)
 *   - Global variable table (always — globals persist across evaluations)
 *
 * Called by gc_collect() at the start of each collection cycle.
 */
void gc_mark_roots(void);

/* ---- Sweep phase ---- */

/*
 * Sweep the object list: free every unmarked object, clear is_marked
 * on survivors. Called by gc_collect() after gc_mark_roots().
 *
 * During sweep, gc_unlink() is suppressed (objects are relinked
 * inline rather than via gc_unlink's O(n) list walk).
 *
 * Stub: currently a no-op. Implemented in gc-sweep task step 2.
 */
void gc_sweep(void);

/* ---- Test/inspection accessors ---- */

/* Return the head of the global object list (for test inspection). */
Obj *gc_get_objects(void);

/* Return the current bytes_allocated counter (for test inspection). */
size_t gc_get_bytes_allocated(void);

/* Return the current next_gc threshold (for test inspection). */
size_t gc_get_next_gc(void);

/* Set the next_gc threshold (for testing automatic GC triggering). */
void gc_set_next_gc(size_t threshold);

#endif /* CUTLET_GC_H */
