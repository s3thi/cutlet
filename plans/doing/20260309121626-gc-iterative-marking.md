# GC Iterative Marking (Worklist)

## Objective

Replace the recursive `gc_mark_object` implementation with an iterative worklist (gray stack). After this task, deeply nested object graphs (e.g., a linked list of 100,000 maps) can be marked without overflowing the C call stack.

## Background

The current `gc_mark_object` in `src/gc.c` is recursive: when it marks an `OBJ_CLOSURE`, it calls `gc_mark_object` on the underlying function and each upvalue; when it marks an `OBJ_ARRAY`, it calls `gc_mark_value` on each element, which may call `gc_mark_object` again. Each recursive call consumes C stack space. A deeply nested structure causes a stack overflow with no useful error message.

The standard fix (used by Lua, V8, and most production GCs) is a **gray stack**: instead of recursing into children immediately, push them onto a worklist. A loop drains the worklist until empty. This moves the memory cost from the C stack (limited, ~1-8 MB) to the heap (effectively unlimited).

## Acceptance criteria

- [ ] A `gray_stack` (dynamic array of `Obj*`) exists in `src/gc.c`, grown with `realloc` as needed.
- [ ] `gc_mark_object` sets `is_marked = true` and pushes the object onto the gray stack. It does NOT recurse into children.
- [ ] A new static function `trace_references` (or similar name) drains the gray stack: pops each object and traces its children (pushing any unmarked children onto the gray stack).
- [ ] `gc_collect` calls `gc_mark_roots()` (which calls `gc_mark_object` to populate the gray stack), then calls `trace_references()` to process all gray objects, then calls `gc_sweep()`.
- [ ] `gc_mark_value` continues to call `gc_mark_object` for heap types (which now just pushes to the gray stack).
- [ ] The gray stack is allocated on first use and freed in `gc_free_all`.
- [ ] A test verifies that a deeply nested structure (e.g., 10,000 nested arrays) can be marked without crashing.
- [ ] `make test && make check` pass.
- [ ] `make test-sanitize` passes.
- [ ] `make test-gc-stress` passes.

## Dependencies

None. This is a self-contained refactor of `gc_mark_object` internals.

## Constraints and non-goals

- **No change to marking correctness.** The same objects are marked as before; only the traversal strategy changes from recursive DFS to iterative.
- **No change to the public API.** `gc_mark_object` and `gc_mark_value` keep their existing signatures. Callers (including `gc_mark_roots`) don't change.
- **Gray stack is simple.** A dynamic array of `Obj*` with count/capacity, grown by doubling. No fancy data structure needed.
- **OBJ_STRING remains a leaf.** Strings have no children to trace, so marking a string just sets `is_marked` and does not push to the gray stack.

---

## Implementation steps

### Step 1: Write a test for deep nesting

Add a test to `tests/test_gc.c`:

- **`test_gc_mark_deep_nesting`**: Create a chain of 10,000 nested arrays: each array contains one element which is another array. Store a reference to the outermost array in a global variable (via `runtime_var_define`). Call `gc_collect()`. Verify that all 10,000 arrays survive (walk the chain and check `is_marked` is false — meaning they survived sweep and had their marks cleared). This test would stack-overflow with the current recursive implementation on most systems.

Use `gc_suppress`/`gc_unsuppress` during the chain construction to prevent premature collection before the chain is rooted.

Run `make test` — the new test should crash or fail with the recursive implementation. (If 10,000 levels doesn't overflow the stack on the CI machine, increase to 100,000. The default stack size on macOS is 8 MB; each recursive `gc_mark_object` call uses roughly 64-128 bytes of stack, so ~100,000 levels should reliably overflow.)

### Step 2: Add the gray stack data structure

In `src/gc.c`, add module-level state for the gray stack:

```
static Obj **gray_stack = NULL;
static size_t gray_count = 0;
static size_t gray_capacity = 0;
```

Add a static helper `gray_push(Obj *obj)` that appends to the stack, growing with `realloc` (double capacity, starting at 256) if needed.

Reset the gray stack in `gc_init` (free any existing allocation, set count/capacity to 0).

Free the gray stack in `gc_free_all`.

Run `make test && make check` — existing tests still pass (gray stack exists but isn't used yet).

### Step 3: Convert gc_mark_object to push-only

Rewrite `gc_mark_object` in `src/gc.c`:

```
void gc_mark_object(Obj *obj) {
    if (obj == NULL || obj->is_marked)
        return;
    obj->is_marked = true;

    /* Leaf objects have no children — don't waste gray stack space. */
    if (obj->type == OBJ_STRING)
        return;

    gray_push(obj);
}
```

This replaces the current switch-on-type recursive tracing. The object is marked and pushed to the gray stack for later processing.

`gc_mark_value` is unchanged — it still calls `gc_mark_object`, which now pushes instead of recursing.

Run `make` to verify compilation.

### Step 4: Add trace_references and wire into gc_collect

Add a static function `trace_references` in `src/gc.c` that drains the gray stack:

```
static void trace_references(void) {
    while (gray_count > 0) {
        Obj *obj = gray_stack[--gray_count];

        switch (obj->type) {
        case OBJ_STRING:
            /* Leaf — no children. Should not appear on the gray stack,
             * but handle gracefully. */
            break;

        case OBJ_UPVALUE: {
            ObjUpvalue *uv = (ObjUpvalue *)obj;
            if (uv->location == &uv->closed)
                gc_mark_value(&uv->closed);
            break;
        }

        case OBJ_FUNCTION: {
            ObjFunction *fn = (ObjFunction *)obj;
            if (fn->chunk != NULL) {
                for (size_t i = 0; i < fn->chunk->const_count; i++)
                    gc_mark_value(&fn->chunk->constants[i]);
            }
            break;
        }

        case OBJ_CLOSURE: {
            ObjClosure *cl = (ObjClosure *)obj;
            gc_mark_object((Obj *)cl->function);
            for (int i = 0; i < cl->upvalue_count; i++) {
                if (cl->upvalues[i] != NULL)
                    gc_mark_object((Obj *)cl->upvalues[i]);
            }
            break;
        }

        case OBJ_ARRAY: {
            ObjArray *arr = (ObjArray *)obj;
            for (size_t i = 0; i < arr->count; i++)
                gc_mark_value(&arr->data[i]);
            break;
        }

        case OBJ_MAP: {
            ObjMap *m = (ObjMap *)obj;
            for (size_t i = 0; i < m->count; i++) {
                gc_mark_value(&m->entries[i].key);
                gc_mark_value(&m->entries[i].value);
            }
            break;
        }
        }
    }
}
```

Note: `gc_mark_object` calls inside `trace_references` (e.g., for closure's function) push newly discovered objects onto the gray stack, which the while loop will process in subsequent iterations.

In `gc_collect`, insert `trace_references()` between `gc_mark_roots()` and `gc_sweep()`:

```
gc_mark_roots();
trace_references();
gc_sweep();
```

Run `make test && make check`.

### Step 5: Full verification

- `make test` — all tests pass, including the new deep nesting test.
- `make check` — formatting and lint clean.
- `make test-sanitize` — no leaks, no use-after-free.
- `make test-gc-stress` — no crashes.

Remove the comment in `gc_mark_object`'s doc comment in `src/gc.h` that mentions recursion depth and worklist as a future improvement — it's now implemented.

---

End of plan.
