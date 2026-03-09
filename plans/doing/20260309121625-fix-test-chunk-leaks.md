# Fix Pre-existing Sanitizer Leaks in test_chunk.c

## Objective

Eliminate the 903-byte / 10-allocation memory leak reported by LSan in `tests/test_chunk.c`. After this task, `make test-sanitize` reports zero leaks for `test_chunk`.

## Background

Two disassemble tests (`test_disassemble_recursive_function` and `test_disassemble_recursive_anonymous_function`) allocate `ObjFunction` objects with raw `malloc` instead of `gc_alloc`. Since these objects are not on the GC object list, `gc_free_all()` at the end of `main` never frees them. Their inner `Chunk` objects (also `malloc`'d and stored as `fn->chunk`) leak transitively.

The other disassemble/closure tests in the file already use `gc_alloc` correctly and are cleaned up by `gc_free_all`.

## Acceptance criteria

- [ ] `test_disassemble_recursive_function` allocates its `ObjFunction` via `gc_alloc(OBJ_FUNCTION, sizeof(ObjFunction))` instead of `malloc`.
- [ ] `test_disassemble_recursive_anonymous_function` allocates its `ObjFunction` via `gc_alloc(OBJ_FUNCTION, sizeof(ObjFunction))` instead of `malloc`.
- [ ] Inner `Chunk` objects in both tests remain allocated with `malloc` (they are freed by `free_object_contents` via `gc_free_all` through `fn->chunk`).
- [ ] Both tests still initialize `fn->upvalue_count = 0` explicitly, matching the pattern in other closure tests in this file.
- [ ] `make test` passes.
- [ ] `make check` passes.
- [ ] `make test-sanitize` reports zero leaks for `test_chunk`.

## Dependencies

None.

## Constraints and non-goals

- **Minimal change.** Only touch the two leaking test functions. Don't refactor other tests.
- **No behavioral change.** The tests verify the same disassembly output as before.
- **Inner Chunks stay as `malloc`.** The `Chunk *inner = malloc(sizeof(Chunk))` pattern is correct because `free_object_contents` for `OBJ_FUNCTION` calls `chunk_free(fn->chunk); free(fn->chunk);`, which properly frees the `malloc`'d Chunk when `gc_free_all` processes the ObjFunction.

---

## Implementation steps

### Step 1: Confirm the leak baseline

Run `make test-sanitize` and confirm the leak exists in `test_chunk`. Note the exact byte count and allocation count in the LSan output. This establishes the baseline.

### Step 2: Fix both leaking tests

In `tests/test_chunk.c`, in `test_disassemble_recursive_function`:

- Change `ObjFunction *fn = malloc(sizeof(ObjFunction));` to `ObjFunction *fn = gc_alloc(OBJ_FUNCTION, sizeof(ObjFunction));`.
- Add `fn->upvalue_count = 0;` after the other field assignments, for explicitness (matching the pattern in `test_disassemble_closure_no_upvalues` and similar tests).
- Leave `Chunk *inner = malloc(sizeof(Chunk));` as-is.

Apply the same two changes to `test_disassemble_recursive_anonymous_function`.

Run `make test && make check`.

### Step 3: Full verification

- `make test` — all tests pass.
- `make check` — formatting and lint clean.
- `make test-sanitize` — zero leaks for `test_chunk`.
- `make test-gc-stress` — no crashes.

---

End of plan.
