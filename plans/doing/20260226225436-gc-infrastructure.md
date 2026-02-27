# GC Task 1: Obj Header, Object Tracking List, and GC Allocation Infrastructure

## Objective

Every heap-allocated object (`ObjFunction`, `ObjClosure`, `ObjArray`, `ObjMap`, `ObjUpvalue`) has a common `Obj` base header containing a type tag, a mark bit, and an intrusive linked-list pointer. All object allocations go through a centralized `gc_alloc()` that prepends each new object to a global object list. A `gc_free_all()` walks the list to free every tracked object at shutdown.

Refcounting continues to manage lifetimes — this task only adds the **structural foundation** for mark-and-sweep. No marking, sweeping, or collection behavior is introduced yet.

When done:

- A new `Obj` base struct exists with `type`, `is_marked`, and `next` fields.
- An `ObjType` enum identifies each heap object kind.
- All five `Obj*` types embed `Obj` as their first field.
- A global `GC` state struct holds the head of the object list and allocation counters.
- `gc_alloc()` allocates an object, initializes the `Obj` header, and links it into the list.
- `gc_free_object()` type-dispatches to the appropriate destructor and unlinks from the list.
- `gc_free_all()` walks the entire list and frees every object (used at shutdown).
- `gc_collect()` exists as a no-op stub (implemented in task 3).
- The GC is initialized in `runtime_init()` and torn down in `runtime_destroy()`.
- `make test && make check` pass, including `make test-sanitize` (no leaks, no use-after-free).

## Acceptance criteria

- [ ] `ObjType` enum in `src/gc.h` with entries: `OBJ_FUNCTION`, `OBJ_CLOSURE`, `OBJ_ARRAY`, `OBJ_MAP`, `OBJ_UPVALUE`.
- [ ] `Obj` struct in `src/gc.h` with fields: `ObjType type`, `bool is_marked`, `struct Obj *next`.
- [ ] `GC` struct in `src/gc.h` holding: `Obj *objects` (list head), `size_t bytes_allocated`, `size_t next_gc`.
- [ ] `gc_init()`, `gc_alloc()`, `gc_free_object()`, `gc_free_all()`, `gc_collect()` declared in `src/gc.h`.
- [ ] `src/gc.c` implements all functions; `gc_collect()` is a no-op stub.
- [ ] `ObjFunction` in `src/value.h` has `Obj obj` as its first field (replacing standalone `refcount`; `refcount` moves after `obj`).
- [ ] `ObjClosure` in `src/value.h` has `Obj obj` as its first field (same pattern).
- [ ] `ObjArray` in `src/value.h` has `Obj obj` as its first field.
- [ ] `ObjMap` in `src/value.h` has `Obj obj` as its first field.
- [ ] `ObjUpvalue` in `src/value.h` has `Obj obj` as its first field.
- [ ] Every `calloc(1, sizeof(Obj*))` allocation site in `src/value.c` and `src/compiler.c` is replaced with `gc_alloc()`.
- [ ] Existing `obj_*_free()` functions call `gc_unlink()` to remove the object from the GC list before freeing.
- [ ] `gc_init()` is called from `runtime_init()` in `src/runtime.c`.
- [ ] `gc_free_all()` is called from `runtime_destroy()` in `src/runtime.c`.
- [ ] New test file `tests/test_gc.c` with unit tests for `gc_alloc`, `gc_free_object`, `gc_free_all`.
- [ ] `Makefile` updated to compile `src/gc.c` and `tests/test_gc.c`.
- [ ] `make test && make check` pass.

## Dependencies

None. This is the first GC task.

## Constraints and non-goals

- **No marking or sweeping.** `gc_collect()` is a no-op. Mark-and-sweep is task 3+4.
- **No ObjString yet.** Strings remain as bare `char*`. ObjString is task 2.
- **Refcounting stays.** All existing refcount logic remains functional. Removal is task 5.
- **No behavioral changes.** Every existing test passes without modification to test expectations.
- **Thread safety.** The GC state is module-level static (like `var_table` in `runtime.c`). Access is serialized by the existing `eval_lock`. No additional synchronization needed.
- **Keep `refcount` field.** The `Obj` header is added *alongside* the existing `refcount` field, not replacing it. The struct layout becomes `{ Obj obj; size_t refcount; ... }`.

---

## Implementation steps

### Step 1: Write tests for GC infrastructure

Create `tests/test_gc.c` with unit tests:

- **`test_gc_init`**: Call `gc_init()`, verify the object list head is NULL, `bytes_allocated` is 0.
- **`test_gc_alloc_single`**: Allocate one object via `gc_alloc(OBJ_ARRAY, sizeof(ObjArray))`, verify it's non-NULL, its `obj.type == OBJ_ARRAY`, `obj.is_marked == false`, and it appears in the object list.
- **`test_gc_alloc_multiple`**: Allocate 3 objects of different types, verify all appear in the object list (walk the list and count).
- **`test_gc_free_object`**: Allocate an object, call `gc_free_object()`, verify the object list is empty.
- **`test_gc_free_all`**: Allocate several objects, call `gc_free_all()`, verify the list is empty and `bytes_allocated` is 0.

Add the test binary to the Makefile following the existing pattern (see `test_value`, `test_vm`, etc.). Include `src/gc.c` in both the test binary and the main binary's object list.

Run `make test && make check` — tests should fail (gc.h/gc.c don't exist yet).

### Step 2: Implement gc.h and gc.c

**`src/gc.h`**:

- Include guard `CUTLET_GC_H`.
- `ObjType` enum: `OBJ_FUNCTION`, `OBJ_CLOSURE`, `OBJ_ARRAY`, `OBJ_MAP`, `OBJ_UPVALUE`.
- `Obj` struct: `ObjType type; bool is_marked; struct Obj *next;`
- Function declarations: `gc_init()`, `gc_alloc(ObjType type, size_t size)` returns `void*`, `gc_free_object(Obj *obj)`, `gc_unlink(Obj *obj)`, `gc_free_all()`, `gc_collect()`.
- A macro `OBJ_TYPE(obj)` that casts a pointer and reads `((Obj*)(obj))->type`.
- For testing: expose the GC state via `gc_get_objects()` (returns the list head) and `gc_get_bytes_allocated()`.

**`src/gc.c`**:

- Module-level static `GC gc;`.
- `gc_init()`: zero out `gc`, set `next_gc` to a reasonable initial threshold (e.g., `1024 * 1024`).
- `gc_alloc(type, size)`: `calloc(1, size)`, set `obj->type = type`, `obj->is_marked = false`, prepend to `gc.objects`, increment `gc.bytes_allocated += size`. Return the pointer.
- `gc_unlink(obj)`: Walk `gc.objects` list, find `obj`, unlink it (adjust prev->next or list head), decrement `gc.bytes_allocated` by the object's size. Note: the size isn't stored in the Obj header, so either store it there or compute it from `obj->type` using a type-to-size lookup. The simpler approach is to add a `size_t alloc_size` field to `Obj` that records the allocation size.
- `gc_free_object(obj)`: Call `gc_unlink(obj)` then `free(obj)`.
- `gc_free_all()`: Walk `gc.objects`, for each object call a type-specific content destructor (free internal allocations like `ObjFunction.name`, `ObjArray.data`, etc.) then `free(obj)`. Reset `gc.objects = NULL`, `gc.bytes_allocated = 0`.
- `gc_collect()`: No-op stub.

**Design note on `gc_free_all()`**: At shutdown, `gc_free_all()` needs to free object *contents* (not just the object struct). Add a static helper `free_object_contents(Obj *obj)` that switches on `obj->type`:
  - `OBJ_FUNCTION`: Free `name`, `params` array + strings, `chunk` (via `chunk_free` + `free`). Do NOT free the ObjFunction struct itself (the loop handles that).
  - `OBJ_CLOSURE`: Free upvalue pointer array (but NOT the upvalues themselves — they're separate objects on the list).
  - `OBJ_ARRAY`: Free each element via `value_free`, then free `data` array.
  - `OBJ_MAP`: Free each entry's key and value via `value_free`, then free `entries` array.
  - `OBJ_UPVALUE`: If closed, free the closed value via `value_free`.

This duplicates some logic from existing `obj_*_free` functions, which is acceptable during the transition. Once refcounting is removed (task 5), the `obj_*_free` functions will be replaced entirely by `gc_free_all()`'s content destructor.

Run `make test && make check`.

### Step 3: Embed Obj header in all five heap object types

In `src/value.h`, add `#include "gc.h"` and update each struct. The `Obj obj` field must be the **first** member so that casting `(Obj*)ptr` works:

- `ObjFunction`: `{ Obj obj; size_t refcount; char *name; int arity; ... }`
- `ObjArray`: `{ Obj obj; size_t refcount; Value *data; size_t count; size_t capacity; }`
- `ObjUpvalue`: `{ Obj obj; size_t refcount; Value *location; Value closed; struct ObjUpvalue *next; }`
- `ObjClosure`: `{ Obj obj; size_t refcount; ObjFunction *function; ObjUpvalue **upvalues; int upvalue_count; }`
- `ObjMap`: `{ Obj obj; size_t refcount; MapEntry *entries; size_t count; size_t capacity; }`

Update the struct-level doc comments to mention the Obj header.

Note: `ObjUpvalue` already has a `next` field for the VM's open-upvalue linked list. This is **distinct** from `Obj.next` which is for the GC object list. Both coexist.

Run `make` to verify compilation. Tests may not pass yet until allocations are updated.

### Step 4: Route all allocations through gc_alloc()

Replace every `calloc(1, sizeof(ObjXxx))` for heap objects with `gc_alloc()`:

**In `src/value.c`**:

- `make_native()`: Replace `calloc(1, sizeof(ObjFunction))` with `gc_alloc(OBJ_FUNCTION, sizeof(ObjFunction))`.
- `obj_upvalue_new()`: Replace `calloc(1, sizeof(ObjUpvalue))` with `gc_alloc(OBJ_UPVALUE, sizeof(ObjUpvalue))`.
- `obj_closure_new()`: Replace `calloc(1, sizeof(ObjClosure))` with `gc_alloc(OBJ_CLOSURE, sizeof(ObjClosure))`. The upvalue pointer array stays as regular `calloc`.
- `obj_array_new()`: Replace `calloc(1, sizeof(ObjArray))` with `gc_alloc(OBJ_ARRAY, sizeof(ObjArray))`.
- `obj_map_new()`: Replace `calloc(1, sizeof(ObjMap))` with `gc_alloc(OBJ_MAP, sizeof(ObjMap))`.

`obj_array_clone_deep()` and `obj_map_clone_deep()` call `obj_array_new()`/`obj_map_new()` internally, so they automatically go through `gc_alloc`.

**In `src/compiler.c`**:

- `compile_function()`: Replace `calloc(1, sizeof(ObjFunction))` with `gc_alloc(OBJ_FUNCTION, sizeof(ObjFunction))`.

After each `gc_alloc()` call, the existing field initialization (`refcount = 1`, etc.) remains. `gc_alloc` uses `calloc` internally so all fields are zero-initialized.

Add `#include "gc.h"` to `src/value.c` and `src/compiler.c`.

Run `make test && make check`.

### Step 5: Update free functions to unlink from GC list

Before each `free(ptr)` in the existing destructor functions, call `gc_unlink((Obj*)ptr)`:

- `obj_function_free()` in `src/value.c`: Add `gc_unlink((Obj*)fn);` before `free(fn);`.
- `obj_closure_free()` in `src/value.c`: Add `gc_unlink((Obj*)cl);` before `free(cl);`.
- `obj_upvalue_free()` in `src/value.c`: Add `gc_unlink((Obj*)uv);` before `free(uv);`.
- `obj_array_free()` (static, in `src/value.c`): Add `gc_unlink((Obj*)arr);` before `free(arr);`.
- `obj_map_free()` (static, in `src/value.c`): Add `gc_unlink((Obj*)m);` before `free(m);`.

Run `make test && make check`.

### Step 6: Initialize and tear down GC in runtime lifecycle

In `src/runtime.c`:

- Add `#include "gc.h"`.
- In `runtime_init_impl()`: Call `gc_init()` after the rwlock is initialized.
- In `runtime_destroy()`: After `var_table_clear()` (which frees globals and may free objects via `gc_unlink`), call `gc_free_all()` to sweep any remaining objects still on the GC list. This handles objects that were not freed by refcounting (e.g., future reference cycles).

**Handle the stack-allocated script objects in `vm_execute()`**: The `script_fn` and `script_closure` in `vm_execute()` (in `src/vm.c`) are stack-allocated — they are NOT on the GC object list and must NOT be. They don't go through `gc_alloc()` and have no `Obj` header to initialize. However, since they are declared as `ObjFunction` and `ObjClosure` which now have an `Obj` field, their `obj` field will be zero-initialized (stack variable). This is fine as long as `gc_unlink` is never called on them. Verify that the existing code doesn't call `obj_function_free` or `obj_closure_free` on these stack-allocated objects (it doesn't — they're stack-allocated precisely to avoid heap management).

Run `make test && make check`.

### Step 7: Update Makefile and run full verification

- Add `src/gc.c` to `SRCS` / object file lists in the Makefile.
- Add `tests/test_gc.c` to the test targets, following the pattern of existing test targets.
- Ensure `src/gc.h` is in the dependency list for files that include it.

Run the full test suite:
- `make test` — all unit tests, CLI tests, example tests pass.
- `make check` — clang-format and clang-tidy pass.
- `make test-sanitize` — ASan/LSan/UBSan detect no leaks or errors.

---

## Required process (every step)

1. Write tests first.
2. Run `make test` and `make check` — confirm new tests fail.
3. **Stop and ask the user for confirmation before implementing.**
4. Implement the feature.
5. Run `make test` and `make check` after every code change.
6. Do not remove or modify existing tests without user confirmation.

---

## Progress

- [x] **Step 1**: Write tests for GC infrastructure — `tests/test_gc.c` created with 5 tests (test_gc_init, test_gc_alloc_single, test_gc_alloc_multiple, test_gc_free_object, test_gc_free_all). All pass.
- [x] **Step 2**: Implement gc.h and gc.c — `src/gc.h` with ObjType enum, Obj struct, GC struct, function declarations, OBJ_TYPE macro, and test accessors. `src/gc.c` with gc_init, gc_alloc, gc_unlink, gc_free_object, gc_free_all (with no-op free_object_contents stub until Obj header is embedded), gc_collect no-op stub, and test accessors. Makefile updated with gc.c in LIB_SRCS and test_gc build/run targets (including sanitizer builds).
- [x] **Step 3**: Embed Obj header in all five heap object types — Added `Obj obj` as first field of ObjFunction, ObjArray, ObjUpvalue, ObjClosure, ObjMap in `src/value.h`. Activated `free_object_contents()` in `src/gc.c` with type-dispatched content destructors. Fixed pre-existing implicit-widening lint warning in GC_INITIAL_THRESHOLD.
- [x] **Step 4**: Route all allocations through gc_alloc() — Replaced 6 `calloc(1, sizeof(ObjXxx))` calls with `gc_alloc()` in `src/value.c` (make_native, obj_upvalue_new, obj_closure_new, obj_array_new, obj_map_new) and `src/compiler.c` (compile_function). Added `#include "gc.h"` to both files. Updated Makefile to link `gc.c` into test_chunk, test_compiler, test_vm, and test_value targets (regular + sanitizer builds). All tests pass.
- [x] **Step 5**: Update free functions to unlink from GC list — Added `gc_unlink()` calls before every `free()` of a gc_alloc'd object. In `src/value.c`: obj_upvalue_free, obj_closure_new (error path), obj_closure_free, obj_function_free, obj_array_clone_deep (error path), obj_array_free, obj_map_clone_deep (error path), obj_map_free (8 sites). In `src/compiler.c`: compile_function error paths (3 sites). Verified stack-allocated script_fn/script_closure in vm_execute() are never passed to these free functions. All tests pass.
- [x] **Step 6**: Initialize and tear down GC in runtime lifecycle — Added `#include "gc.h"` to `src/runtime.c`. Call `gc_init()` in `runtime_init_impl()` after rwlock init. Call `gc_free_all()` in `runtime_destroy()` after `var_table_clear()`. Verified stack-allocated `script_fn`/`script_closure` in `vm_execute()` are safe (never passed to free functions, zero-initialized Obj header).
- [ ] **Step 7**: Update Makefile and run full verification

---

End of plan.
