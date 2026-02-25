# Meta-Operators (`@`) on Maps

## Objective

Extend the `@` meta-operator to work on maps. Prefix `@op map` reduces (folds) over a map's values. Infix `map @op map` vectorizes by key intersection. Infix `map @op scalar` broadcasts the scalar across all map values.

When done:

- `@+ {a: 1, b: 2, c: 3}` → 6 (reduce over values).
- `{a: 1, b: 2} @+ {a: 10, b: 20}` → `{a: 11, b: 22}` (vectorize by key intersection).
- `{a: 1, b: 2} @* 10` → `{a: 10, b: 20}` (scalar broadcast).
- Mismatched keys in map-map vectorization silently produce only the intersection.
- Map-array combinations in `@op` are a runtime error.
- `make test && make check` pass.

## Acceptance criteria

- [ ] `OP_REDUCE` handles `VAL_MAP` operand (fold over values in insertion order).
- [ ] `OP_VECTORIZE` handles map-map operands (key intersection, apply op to matching values).
- [ ] `OP_VECTORIZE` handles map-scalar and scalar-map operands (broadcast).
- [ ] `OP_VECTORIZE` rejects map-array and array-map combinations with a clear error.
- [ ] All existing array `@` behavior is unchanged.
- [ ] VM integration tests pass.
- [ ] `make test && make check` pass.

## Dependencies

- **Arrays plan** (`plans/doing/20260219150840-arrays.md`): must be complete. Provides `OP_REDUCE`, `OP_VECTORIZE`, `TOK_META`, `AST_REDUCE`, `AST_VECTORIZE`, and the `@` meta-operator infrastructure.
- **Maps plan** (`plans/doing/20260223173805-maps.md`): must be complete. Provides `VAL_MAP`, `ObjMap`, `obj_map_new()`, `obj_map_get()`, `obj_map_set()`, `make_map()`, and `value_equal()`.

## Constraints and non-goals

- **No new opcodes.** Only extends the existing `OP_REDUCE` and `OP_VECTORIZE` VM handlers.
- **No new parser/compiler changes.** The parser and compiler already produce `AST_REDUCE`/`AST_VECTORIZE` and emit `OP_REDUCE`/`OP_VECTORIZE` — maps are handled purely in the VM dispatch.
- **No custom-function `@ident` on maps.** Custom function reduction/vectorization (step 9 of the arrays plan) should work on maps automatically if the compiler generates loop bytecode that uses `OP_INDEX_GET`. If not, that's a follow-up.

---

## Design

### Prefix `@op map` — reduction (fold over values)

```cutlet
@+ {math: 92, english: 87, science: 95}    # => 274
@* {a: 2, b: 3, c: 4}                       # => 24
@++ {first: "hello", second: " ", third: "world"}  # => "hello world"
@+ {x: 42}                                  # => 42 (single entry)
@+ {}                                        # => error "cannot reduce empty map"
```

Reduces over the map's values in insertion order. Same rules as array reduction: empty map errors, single-entry map returns that value. `@and` and `@or` short-circuit as they do for arrays.

### Infix `map @op map` — vectorized by key intersection

```cutlet
my us = {math: 92, english: 87, science: 95}
my uk = {maths: 88, english: 91, science: 89}

us @- uk
# => {english: -4, science: 6}
# Only shared keys. Non-shared keys silently dropped.

us @> uk
# => {english: false, science: true}

{a: 1, b: 2} @+ {a: 10, b: 20}   # => {a: 11, b: 22}
{a: 1} @+ {b: 2}                   # => {} (no shared keys)
```

The result map contains only keys present in **both** operand maps. The result preserves the insertion order of the **left** operand (for the intersected keys).

### Infix `map @op scalar` / `scalar @op map` — broadcast

```cutlet
{math: 85, english: 90} @* 1.1
# => {math: 93.5, english: 99}

{math: 85, english: 90} @>= 88
# => {math: false, english: true}

100 @- {a: 10, b: 20}
# => {a: 90, b: 80}
```

The scalar is applied to every value in the map. Key set and order are preserved from the map operand.

### Error: map + array

```cutlet
{a: 1} @+ [1, 2]    # => error "cannot vectorize map with array"
[1, 2] @+ {a: 1}    # => error "cannot vectorize array with map"
```

Maps and arrays cannot be mixed in vectorized operations. Use `values()` or `keys()` to convert first.

---

## Implementation steps

Each step follows the required process: tests first, confirm failures, get user confirmation, implement, `make test && make check`.

### Step 1: Extend `OP_REDUCE` for maps

**VM** (`src/vm.c`):
- In the `OP_REDUCE` handler: after the existing `VAL_ARRAY` path, add a `VAL_MAP` path:
  - Extract values from the map (in insertion order) into a temporary stack-allocated or heap-allocated array of Value pointers.
  - Apply the same reduction logic as for arrays: if empty → error "cannot reduce empty map". If single entry → return that value. Otherwise fold left.
  - For `@and` / `@or`: short-circuit over the values, same as for arrays.
  - If the operand is neither array nor map → runtime error.

**Tests** (`tests/test_eval.c`):
- `@+ {a: 1, b: 2, c: 3}` → 6.
- `@* {x: 2, y: 3, z: 4}` → 24.
- `@- {a: 10, b: 3, c: 2}` → 5 (left fold: (10 - 3) - 2).
- `@++ {first: "hello", second: " world"}` → `"hello world"`.
- `@+ {x: 42}` → 42 (single entry).
- `@+ {}` → error "cannot reduce empty map".
- `@and {a: true, b: true, c: false}` → false.
- `@or {a: false, b: 0, c: "hi"}` → `"hi"`.

**Files touched**: `src/vm.c`, tests.

### Step 2: Extend `OP_VECTORIZE` for maps

**VM** (`src/vm.c`):
- In the `OP_VECTORIZE` handler: after the existing array paths, add map paths. The handler examines the types of the left and right operands:
  - **Both `VAL_MAP`**: iterate over the left map's entries. For each key, check if it exists in the right map using `obj_map_get()`. If found, apply the operation to both values, insert the result into a new ObjMap with that key. Skip keys not in the right map. Push the result.
  - **`VAL_MAP` + non-map-non-array (scalar)**: create a new ObjMap. For each entry in the map, apply the operation to the entry's value and the scalar. Insert the result with the same key. Push the result.
  - **Non-map-non-array (scalar) + `VAL_MAP`**: same, but with operand order preserved (scalar is left operand to the operation).
  - **`VAL_MAP` + `VAL_ARRAY`** or **`VAL_ARRAY` + `VAL_MAP`**: runtime error "cannot vectorize map with array".
  - If any individual element operation produces an error, stop and return that error.

**Tests** (`tests/test_eval.c`):

Map × map:
- `{a: 1, b: 2} @+ {a: 10, b: 20}` → `{a: 11, b: 22}`.
- `{a: 1, b: 2, c: 3} @+ {b: 10, c: 20, d: 30}` → `{b: 12, c: 23}` (key intersection only).
- `{a: 1} @+ {b: 2}` → `{}` (no shared keys).
- `{a: 5, b: 10} @- {a: 1, b: 3}` → `{a: 4, b: 7}`.
- `{a: 1, b: 2} @> {a: 0, b: 3}` → `{a: true, b: false}`.

Map × scalar:
- `{a: 1, b: 2} @* 10` → `{a: 10, b: 20}`.
- `{a: 85, b: 90} @>= 88` → `{a: false, b: true}`.
- `{a: 2, b: 3} @** 2` → `{a: 4, b: 9}`.

Scalar × map:
- `100 @- {a: 10, b: 20}` → `{a: 90, b: 80}`.
- `10 @* {a: 2, b: 3}` → `{a: 20, b: 30}`.

Error cases:
- `{a: 1} @+ [1, 2]` → error containing "cannot vectorize map with array".
- `[1, 2] @+ {a: 1}` → error containing "cannot vectorize array with map".

Existing array behavior unchanged:
- `[1, 2, 3] @+ [4, 5, 6]` → `[5, 7, 9]` (still works).
- `[1, 2, 3] @* 10` → `[10, 20, 30]` (still works).

**Files touched**: `src/vm.c`, tests.

### Step 3: Integration tests + end-to-end

**End-to-end tests** (`tests/test_cli.sh`):
- `echo '@+ {a: 1, b: 2, c: 3}' | cutlet repl` → `6`.
- A script file that combines map `@` with `keys()`/`values()`:
  ```cutlet
  my scores = {math: 92, english: 87, science: 95}
  say(@+ scores)
  say(scores @>= 90)
  ```

**Composability tests** (`tests/test_eval.c`):
- `@+ values({a: 10, b: 20, c: 30})` → 60 (reduce array from `values()`).
- `{a: 85, b: 92} @>= 90` combined with map projection or `keys()` to filter.

**Bytecode disassembly**:
- Verify `--bytecode` shows `OP_REDUCE` and `OP_VECTORIZE` for map expressions (same opcodes as arrays — no new disassembly needed, just visual confirmation).

**Files touched**: tests.

**Post-implementation reminders** (per AGENTS.md):
- Update `TUTORIAL.md` to add a subsection on `@` with maps (reduce, vectorize, broadcast).
- Add `examples/map-operators.cutlet` example program with corresponding `.expected` file.

---

## Required process (every step)

1. Write tests first.
2. Run `make test` and `make check` — confirm new tests fail.
3. **Stop and ask the user for confirmation before implementing.**
4. Implement the feature.
5. Run `make test` and `make check` after every code change.
6. Do not remove or modify existing tests without user confirmation.

## Progress

- [x] Step 1: Extend `OP_REDUCE` for maps — added map-to-values extraction in `OP_REDUCE` handler; maps fold over values in insertion order with distinct "cannot reduce empty map" error; 8 tests added
- [x] Step 2: Extend `OP_VECTORIZE` for maps — added map-map (key intersection), map-scalar broadcast, scalar-map broadcast, and map-array error handling; 14 tests added

---
End of plan.
