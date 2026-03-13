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
 * gc_collect() marks all reachable objects, sweeps (frees) unreachable
 * ones, and updates the allocation threshold for the next cycle.
 *
 * The GC is the sole lifetime management mechanism for all heap
 * objects. There is no reference counting.
 */

#ifndef CUTLET_GC_H
#define CUTLET_GC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
 * sweeping:        True while gc_sweep() is running. When true, gc_unlink()
 *                  is a no-op — sweep relinks the list inline, so external
 *                  gc_unlink calls (e.g. from value_free via
 *                  free_object_contents) must be suppressed to avoid O(n^2)
 *                  behavior and double-free issues.
 */
typedef struct {
    Obj *objects;
    size_t bytes_allocated;
    size_t next_gc;
    bool sweeping;
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
 *
 * No-op when the GC is sweeping or tearing down (gc_free_all) to avoid
 * double-free on objects still in the GC list.
 */
void gc_free_object(Obj *obj);

/*
 * Returns true when the GC is in sweep or teardown mode (gc_sweep or
 * gc_free_all is running). Used by value_free helpers to skip freeing
 * GC-managed sub-objects that will be handled by the sweep/teardown loop.
 */
bool gc_is_sweeping(void);

/*
 * Free all objects on the GC list. For each object, frees internal
 * contents (type-specific: name, data, entries, etc.) then frees
 * the object struct. Resets the list to empty and bytes_allocated to 0.
 * Used at shutdown.
 */
void gc_free_all(void);

/*
 * Run a full mark-and-sweep garbage collection cycle:
 *   1. gc_mark_roots() marks all reachable objects.
 *   2. gc_sweep() frees unmarked objects, clears marks on survivors.
 *   3. Updates next_gc threshold based on surviving bytes.
 *
 * When GC_LOG is defined, prints collection stats to stderr.
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
 * Implemented in gc-sweep task step 2.
 */
void gc_sweep(void);

/* ---- Temporary GC root pinning ---- */

/*
 * Maximum number of temporarily pinned objects. VM opcodes push
 * objects here when they hold references in local C variables
 * (not on the VM stack) and need them to survive a GC triggered
 * by a subsequent gc_alloc. Must be large enough for the deepest
 * nesting of pinned objects in any single opcode.
 */
#define GC_TEMP_ROOTS_MAX 16

/*
 * Pin an object as a temporary GC root. While pinned, gc_mark_roots()
 * will mark it and trace its children, preventing collection even if
 * the object is not reachable from the VM stack or globals.
 *
 * Must be paired with gc_unpin() in reverse order.
 */
void gc_pin(Obj *obj);

/*
 * Unpin the most recently pinned temporary root.
 */
void gc_unpin(void);

/* Pop n entries from the temporary root stack (calls gc_unpin n times). */
void gc_unpin_n(int n);

/*
 * Pin the GC-managed heap object(s) inside a Value as temporary GC roots.
 * Returns the number of entries pushed onto the pin stack.
 *
 * For most types, this is 0 (scalars) or 1 (string, function, closure,
 * array, map). For VAL_OBJECT_TYPE and VAL_INSTANCE, it may push 1-2
 * entries (methods map, data map) since their sub-objects are GC-managed.
 *
 * Callers must call gc_unpin() exactly the returned number of times.
 */
int gc_pin_value(struct Value *v);

/* ---- Compiler root tracking ---- */

/* Forward declaration of Chunk (defined in chunk.h). */
struct Chunk;

/*
 * Maximum depth of nested compiler chunks that can be tracked as GC
 * roots. Each nested function definition pushes one chunk. Matches
 * the practical nesting limit of the compiler.
 */
#define GC_COMPILER_ROOTS_MAX 64

/*
 * Push a Chunk onto the compiler roots stack. While on the stack,
 * gc_mark_roots() will mark all Values in the chunk's constant pool,
 * preventing GC from collecting ObjStrings and other objects that are
 * being compiled but not yet reachable from the VM stack or globals.
 *
 * Must be paired with gc_pop_compiler_root() when compilation of
 * that chunk is complete.
 */
void gc_push_compiler_root(struct Chunk *chunk);

/*
 * Pop the most recently pushed compiler root chunk. Must be called
 * after compilation of the corresponding chunk is complete, in
 * reverse order of gc_push_compiler_root() calls.
 */
void gc_pop_compiler_root(void);

/* ---- String intern table ---- */

/* Forward declaration of ObjString (defined in value.h). */
struct ObjString;

/*
 * Look up a string in the intern table by content.
 * Returns the existing ObjString* if a match (same hash, length,
 * and chars) is found, NULL otherwise. Used by obj_string_new()
 * and obj_string_take() to avoid creating duplicate ObjStrings.
 */
struct ObjString *gc_intern_find(const char *chars, size_t length, uint32_t hash);

/*
 * Add an ObjString to the intern table. Must be called after
 * allocating a new ObjString that is not yet in the table.
 * The table grows automatically when the load factor is exceeded.
 */
void gc_intern_add(struct ObjString *str);

/* ---- Test/inspection accessors ---- */

/* Return the head of the global object list (for test inspection). */
Obj *gc_get_objects(void);

/* Return the current bytes_allocated counter (for test inspection). */
size_t gc_get_bytes_allocated(void);

/* Return the current next_gc threshold (for test inspection). */
size_t gc_get_next_gc(void);

/* Set the next_gc threshold (for testing automatic GC triggering). */
void gc_set_next_gc(size_t threshold);

/*
 * Suppress / unsuppress GC collection. While suppressed, gc_collect()
 * is a no-op (including GC_STRESS-triggered collections). This is used
 * by unit tests that create GC-tracked objects outside a VM context
 * where no root set exists to protect objects from sweep.
 *
 * Must be paired: gc_suppress() / gc_unsuppress(). Nesting is NOT
 * supported — a single gc_unsuppress() fully re-enables collection.
 */
void gc_suppress(void);
void gc_unsuppress(void);

#endif /* CUTLET_GC_H */
