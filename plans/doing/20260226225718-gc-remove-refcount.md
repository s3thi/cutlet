# GC Task 5: Remove Refcounting, Reference Semantics, and Cleanup

## Objective

Remove the refcounting infrastructure now that mark-and-sweep GC handles all object lifetimes. Switch arrays and maps from value semantics (copy-on-write) to reference semantics (mutations visible through all references). Clean up dead code and simplify `value_clone`/`value_free`.

When done:

- The `refcount` field is removed from all Obj* types.
- `value_clone()` for GC-managed types (function, closure, array, map, string) simply copies the pointer — no refcount bump.
- `value_free()` for GC-managed types is a no-op — no refcount decrement, no freeing. Just nulls out the pointer.
- `obj_closure_free()`, `obj_upvalue_free()`, `obj_function_free()` are removed (or reduced to internal helpers used only by `gc_sweep`).
- `obj_array_ensure_owned()` and `obj_map_ensure_owned()` (COW helpers) are removed.
- Arrays and maps have reference semantics: `my a = [1,2,3]; my b = a; push(b, 4)` — `a` now also contains `[1,2,3,4]`.
- The runtime globals table (`var_table` in `runtime.c`) stores Values without cloning — GC keeps them alive.
- `make test && make check` pass.

## Acceptance criteria

- [ ] `refcount` field removed from `ObjFunction`, `ObjClosure`, `ObjArray`, `ObjMap`, `ObjUpvalue` in `src/value.h`.
- [ ] `value_clone()` for `VAL_FUNCTION`: `out->function = src->function;` (pointer copy, no refcount).
- [ ] `value_clone()` for `VAL_CLOSURE`: `out->closure = src->closure;` (pointer copy).
- [ ] `value_clone()` for `VAL_ARRAY`: `out->array = src->array;` (pointer copy).
- [ ] `value_clone()` for `VAL_MAP`: `out->map = src->map;` (pointer copy).
- [ ] `value_clone()` for `VAL_STRING`: `out->string = src->string;` (pointer copy — already done in task 4).
- [ ] `value_free()` for `VAL_FUNCTION`, `VAL_CLOSURE`, `VAL_ARRAY`, `VAL_MAP`, `VAL_STRING`: no-op (just null the pointer).
- [ ] `obj_closure_free()` removed from public API. Internal content destructor in `gc_sweep` handles freeing.
- [ ] `obj_upvalue_free()` removed from public API.
- [ ] `obj_function_free()` removed from public API (or made static in gc.c).
- [ ] `obj_array_ensure_owned()` removed from `src/value.h` and `src/value.c`.
- [ ] `obj_map_ensure_owned()` removed from `src/value.h` and `src/value.c`.
- [ ] All call sites of `obj_array_ensure_owned` and `obj_map_ensure_owned` (in `src/vm.c`, `OP_INDEX_SET` handler) removed.
- [ ] `runtime_var_define()` and `runtime_var_assign()` store the Value directly (pointer copy via `value_clone`) instead of deep-cloning. `runtime_var_get()` returns a pointer copy.
- [ ] `capture_upvalue()` in `src/vm.c` no longer bumps refcount when reusing an existing upvalue.
- [ ] `obj_closure_new()` no longer bumps `fn->refcount`.
- [ ] OP_CLOSURE handler in `src/vm.c` no longer bumps upvalue refcount when copying from enclosing closure.
- [ ] `close_upvalues()` in `src/vm.c` simply copies the value and redirects the pointer — no refcount changes.
- [ ] Tests updated to reflect reference semantics for arrays and maps.
- [ ] `make test && make check` pass.

## Dependencies

- **gc-infrastructure** (task 1): Obj header.
- **gc-objstring** (task 2): ObjString.
- **gc-mark** (task 3): Mark phase.
- **gc-sweep** (task 4): Sweep phase, string interning.

## Constraints and non-goals

- **Behavioral change: reference semantics.** Arrays and maps are now shared by reference. `my a = [1]; my b = a; push(b, 2)` means `a` is also `[1, 2]`. This is a deliberate language semantics change aligned with the upcoming object system.
- **Existing tests may need updating.** Any test that relies on value semantics for arrays/maps (COW behavior) must be updated to expect reference semantics. The user must confirm before modifying existing tests.
- **No deep-copy function.** If users need to copy an array/map independently, they need an explicit `clone()` function. Adding `clone()` is out of scope for this task but should be noted as a follow-up.
- **`obj_array_clone_deep` and `obj_map_clone_deep` may still be useful** for specific internal operations. Keep them but remove the COW call sites.
- **No Value struct slimming.** The Value struct still has separate pointer fields. Moving to a union or NaN-boxing is a separate optimization task.

---

## Implementation steps

### Step 1: Identify and update tests that assume value semantics

Search the test suite for tests that depend on COW / value semantics for arrays and maps. These tests assert that cloning an array/map produces an independent copy where mutations to one don't affect the other.

Patterns to search for:
- Tests that clone an array, mutate the clone, and check the original is unchanged.
- Tests that push/pop on a "copy" of an array.
- Tests that set keys on a "copy" of a map.

**Do NOT modify tests yet** — just identify them and list them. Present the list to the user for confirmation before making changes.

After user confirmation, update the identified tests to expect reference semantics:
- Mutation through one reference is visible through the other.
- Or, if the test is specifically testing COW behavior, remove or replace it.

### Step 2: Remove refcount from all Obj* types

In `src/value.h`, remove the `size_t refcount;` field from:
- `ObjFunction`
- `ObjClosure`
- `ObjArray`
- `ObjMap`
- `ObjUpvalue`

This will cause compilation failures at every site that reads or writes `refcount`. Fix each one:

**In `src/value.c`**:
- `make_function()`: Remove `fn->refcount = 1;`.
- `obj_upvalue_new()`: Remove `uv->refcount = 1;`.
- `obj_closure_new()`: Remove `cl->refcount = 1;` and `fn->refcount++;` and the error-path `fn->refcount--;`.
- `obj_closure_free()`: Remove all refcount logic. This function becomes unnecessary — remove it from the public API. The GC sweep's content destructor handles cleanup.
- `obj_upvalue_free()`: Remove refcount logic. Remove from public API.
- `obj_function_free()`: Still needed as a content destructor, but remove refcount checks. Make it callable from `gc_sweep`'s `free_object_contents` only.
- `obj_array_new()`: Remove `arr->refcount = 1;`.
- `obj_map_new()`: Remove `m->refcount = 1;`.

Run `make` to find remaining refcount references and fix them.

### Step 3: Simplify value_clone — pointer copies only

In `src/value.c`, rewrite `value_clone()`:

```c
bool value_clone(Value *out, const Value *src) {
    if (!out || !src) return false;
    *out = *src;
    /* For VAL_ERROR, deep-copy the error message (not GC-managed). */
    if (src->type == VAL_ERROR) {
        out->error = strdup(src->error ? src->error : "");
        if (!out->error) return false;
    }
    /* All other types: shallow copy is sufficient.
     * GC-managed pointers (function, closure, array, map, string)
     * are shared references — the GC keeps them alive. */
    return true;
}
```

This replaces the current function which has per-type refcount bumping.

Run `make test && make check`.

### Step 4: Simplify value_free — no-op for GC types

In `src/value.c`, rewrite `value_free()`:

```c
void value_free(Value *v) {
    if (!v) return;
    /* Only VAL_ERROR has non-GC heap data that needs freeing. */
    if (v->type == VAL_ERROR && v->error) {
        free(v->error);
        v->error = NULL;
    }
    /* GC-managed types: null out the pointer. The GC handles freeing.
     * We null the pointer to prevent dangling references in debug builds. */
    v->function = NULL;
    v->closure = NULL;
    v->array = NULL;
    v->map = NULL;
    v->string = NULL;
}
```

Run `make test && make check`.

### Step 5: Remove COW infrastructure and update VM

**Remove COW functions** from `src/value.h` and `src/value.c`:
- Delete `obj_array_ensure_owned()` declaration and definition.
- Delete `obj_map_ensure_owned()` declaration and definition.

**Update `src/vm.c`** — remove COW call sites:
- In the `OP_INDEX_SET` handler, there are calls to `obj_array_ensure_owned(&container)` and `obj_map_ensure_owned(&container)` before mutation. Remove these calls. The array/map is mutated in place, and all references see the change (reference semantics).
- In `native_push()` and `native_pop()`: if they call `obj_array_ensure_owned`, remove those calls.

**Update VM refcount manipulation sites**:
- `capture_upvalue()`: Remove `curr->refcount++;` when reusing an existing upvalue.
- `obj_closure_new()` call sites: No refcount bump needed.
- OP_CLOSURE handler: Remove `cl->upvalues[i]->refcount++` when copying from enclosing closure.
- `close_upvalues()`: Remove any refcount manipulation.

Run `make test && make check`.

### Step 6: Simplify the runtime globals table

In `src/runtime.c`:

**`runtime_var_define()`**: Replace `value_clone(&cloned, value); value_free(&entry->value); entry->value = cloned;` with just `entry->value = *value;` (or `value_clone` which is now a shallow copy). The GC keeps the Value's heap objects alive.

Actually, `value_clone` is still the right call — it handles the `VAL_ERROR` deep-copy case. But since `value_free` is now mostly a no-op for GC types, the old "free old value, clone new value" pattern simplifies to just overwriting. Keep `value_clone` for correctness (error messages still need `strdup`), but the cost is now trivial.

**`runtime_var_get()`**: Still returns a `value_clone` (which is now a shallow copy). This is fine.

**`runtime_var_assign()`**: Same simplification as `runtime_var_define`.

**`var_table_clear()`**: Still calls `value_free` on each value (now mostly a no-op for GC types). The actual object freeing happens in `gc_free_all`.

Run `make test && make check`.

### Step 7: Remove dead code and run full verification

**Remove dead public API**:
- `obj_closure_free()` — remove from `src/value.h` declaration and `src/value.c` definition.
- `obj_upvalue_free()` — same.
- `obj_function_free()` — if only used by GC sweep, make it static in `src/gc.c` or inline into `free_object_contents`.

**Move content destructors**: The type-specific free logic (freeing ObjFunction's name/params/chunk, ObjArray's data, etc.) should now live only in `gc_sweep`'s `free_object_contents()` in `src/gc.c`. Remove duplicate logic from `src/value.c`.

**Update comments**: Remove all references to "refcount", "reference counting", "COW", "copy-on-write" from code comments in `value.h`, `value.c`, `vm.c`, `runtime.c`. Update doc comments to describe GC-based lifetime management and reference semantics.

**Run full test suite**:
- `make test` — all unit tests, CLI tests, example tests pass.
- `make check` — clang-format and clang-tidy pass.
- `make test-sanitize` — no leaks, no use-after-free.
- `make test-gc-stress` — no crashes under aggressive collection.

**Post-implementation reminders** (per AGENTS.md):
- Update `TUTORIAL.md` to document reference semantics for arrays and maps (mutation through one reference is visible through all references).
- Consider adding a `clone()` built-in function so users can explicitly deep-copy arrays/maps when needed.

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

### Step 1: Identify and update tests that assume value semantics ✅

**Completed 2026-02-27.**

Identified and updated all tests, examples, and documentation that assumed
copy-on-write / value semantics for arrays and maps.

**Tests updated (test_vm.c):**
- `test_index_assign_cow` → renamed to `test_index_assign_ref_semantics`, expects mutation visible through other reference
- `test_map_index_set_cow` → renamed to `test_map_index_set_ref_semantics`, expects mutation visible through other reference
- `test_push_no_mutate` → renamed to `test_push_mutates`, expects push to mutate in-place
- `test_pop_no_mutate` → renamed to `test_pop_mutates`, expects pop to mutate in-place
- `test_push_returns_new` → renamed to `test_push_returns_array` (comment update only)
- `test_fn_clone_independence` → removed refcount assertions
- Added `test_push_ref_semantics`: push through one reference visible through another
- Added `test_pop_ref_semantics`: pop through one reference visible through another
- Added `test_map_insert_ref_semantics`: new key insertion visible through other reference

**Tests updated (test_value.c):**
- `test_array_clone_refcount` → renamed to `test_array_clone_shares_pointer`, removed refcount assertions
- `test_ensure_owned_refcount_one` and `test_ensure_owned_refcount_two` → replaced with `test_array_ref_semantics_mutation_visible`
- `test_obj_array_clone_deep` → removed refcount assertion
- `test_map_clone_refcount` → renamed to `test_map_clone_shares_pointer`, removed refcount assertions
- `test_map_ensure_owned_refcount_one` and `test_map_ensure_owned_refcount_two` → replaced with `test_map_ref_semantics_mutation_visible`
- `test_obj_map_clone_deep` → removed refcount assertion
- `test_empty_array_format` → removed refcount assertion
- `test_empty_map_format` → removed refcount assertion

**Examples updated:**
- `examples/arrays.cutlet` + `.expected` — updated to demonstrate reference semantics, push/pop mutate in-place
- `examples/maps.cutlet` + `.expected` — updated to demonstrate reference semantics

**Documentation updated:**
- `TUTORIAL.md` — Sections 12 (Arrays) and 13 (Maps) updated from COW to reference semantics

**Also fixed:** Pre-existing clang-format issue in `src/parser.h`.

**Expected failures (7 VM tests, 2 example tests):** These tests assert reference semantics
behavior but the implementation still uses COW. They will pass once Steps 2-5 are implemented.

### Step 2: Remove refcount from all Obj* types ✅

**Completed 2026-02-27.**

Removed `size_t refcount` field from all five Obj* types and fixed all
compilation errors and behavioral consequences.

**src/value.h changes:**
- Removed `size_t refcount` from ObjFunction, ObjClosure, ObjArray, ObjMap, ObjUpvalue
- Removed declarations: `obj_upvalue_free()`, `obj_closure_free()`, `obj_function_free()`
- Removed declarations: `obj_array_ensure_owned()`, `obj_map_ensure_owned()`
- Updated all comments to reference GC-managed lifetimes instead of refcounting

**src/value.c changes:**
- `make_function()`: Removed `fn->refcount = 1`
- `obj_upvalue_new()`: Removed `uv->refcount = 1`
- `obj_closure_new()`: Removed `cl->refcount = 1`, `fn->refcount++`, error-path `fn->refcount--`
- Removed `obj_closure_free()`, `obj_upvalue_free()`, `obj_function_free()` entirely
- Removed static helpers `obj_array_free()`, `obj_map_free()` (no longer called)
- Removed `obj_array_ensure_owned()`, `obj_map_ensure_owned()` (COW helpers)
- `obj_array_new()`: Removed `arr->refcount = 1`
- `obj_map_new()`: Removed `m->refcount = 1`
- `value_free()`: Rewritten as no-op for GC types (just nulls pointers); only frees VAL_ERROR message
- `value_clone()`: Rewritten as shallow copy; only deep-copies VAL_ERROR message

**src/vm.c changes:**
- `capture_upvalue()`: Removed `curr->refcount++` when reusing existing upvalue
- `vm_execute()`: Removed `.refcount = 1` from stack-allocated script_fn and script_closure
- OP_CLOSURE handler: Removed `cl->upvalues[i]->refcount++` when copying from enclosing closure
- OP_INDEX_SET: Removed `obj_map_ensure_owned()` and `obj_array_ensure_owned()` calls
- `native_push()`: Changed from deep-clone-then-append to mutate-in-place (reference semantics)
- `native_pop()`: Changed from build-new-array to remove-last-in-place (reference semantics)

**src/gc.c changes:**
- Updated comments to reflect that refcounting has been removed

**src/runtime.c changes:**
- Updated comments to reflect GC-managed lifetimes

**Test changes:**
- `test_chunk.c`: Removed all `fn->refcount = 1` initializations; removed refcount assertions;
  removed `obj_function_free()`, `obj_closure_free()`, `obj_upvalue_free()` calls (GC handles cleanup)
- `test_gc.c`: Removed all refcount initializations from test objects
- `test_value.c`: Updated comment (refcount → pointer shared)
- `test_cli.sh`: Updated "array builtins" expected output for reference semantics (pop after push)

**Autonomous decisions:**
- Changed `native_push()` and `native_pop()` to mutate in-place rather than deep-clone.
  This was planned for Step 5 but was needed now because Step 1 tests already expected
  reference semantics. All tests pass with this change.
- Removed the `obj_array_free` and `obj_map_free` static functions entirely (they were
  dead code after value_free stopped calling them). GC sweep handles cleanup.

**All tests pass. Format check passes. Lint warnings are pre-existing (not in modified files).**

---

End of plan.
