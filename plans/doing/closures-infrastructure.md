# Closures: Infrastructure

## Objective

Lay the groundwork for closures by introducing `ObjClosure`, `ObjUpvalue`, and
`VAL_CLOSURE` types, adding the new opcodes, refactoring the VM to use closures
for all user-defined functions, and emitting `OP_CLOSURE` from the compiler.

**Done looks like:** All user-defined functions are represented as `ObjClosure`
at runtime (with 0 upvalues — no captures yet). Native functions (`say`) remain
`ObjFunction`. Named functions inside other functions are lexically scoped.
`make test && make check` pass.

---

## Design

See `plans/doing/closures.md` for the full design (types, semantics, ownership
model). This plan implements the structural pieces without upvalue capture.

### Key types

```c
typedef struct ObjUpvalue {
    size_t refcount;
    Value *location;           /* Points to stack slot (open) or &closed (closed). */
    Value closed;
    struct ObjUpvalue *next;   /* VM's open-upvalue linked list. */
} ObjUpvalue;

typedef struct {
    size_t refcount;
    ObjFunction *function;
    ObjUpvalue **upvalues;
    int upvalue_count;
} ObjClosure;
```

### New opcodes

| Opcode | Operand(s) | Stack effect | Description |
|--------|-----------|-------------|-------------|
| `OP_CLOSURE` | 1-byte constant index, then N x (1-byte is_local, 1-byte index) | push 1 | Create closure from constant pool ObjFunction |
| `OP_GET_UPVALUE` | 1-byte upvalue index | push 1 | Read captured variable |
| `OP_SET_UPVALUE` | 1-byte upvalue index | (peek TOS) | Write captured variable |
| `OP_CLOSE_UPVALUE` | none | pop 1 | Close the topmost open upvalue at TOS slot |

### Ownership model (no GC)

Reference counting, same pattern as `ObjArray`:

- `ObjFunction`: add `size_t refcount`. `value_clone` for closures increments
  the contained function's refcount. `obj_closure_free` decrements it.
- `ObjClosure`: `size_t refcount`. `value_clone` for `VAL_CLOSURE` increments
  closure refcount. `value_free` decrements; when 0, free upvalues and function.
- `ObjUpvalue`: `size_t refcount`. ObjClosure holds refs. When closure is freed,
  decrement each upvalue's refcount. When upvalue refcount hits 0, free it.

---

## Acceptance criteria

- [ ] `ObjClosure` and `ObjUpvalue` types exist with correct refcounting
- [ ] `VAL_CLOSURE` works in `value_format`, `value_clone`, `value_free`, `is_truthy`
- [ ] All four new opcodes are in the `OpCode` enum and disassemble correctly
- [ ] `CallFrame` uses `ObjClosure *closure` instead of `ObjFunction *function`
- [ ] Compiler emits `OP_CLOSURE` (with 0 upvalue descriptors) for all `fn` expressions
- [ ] `OP_CLOSURE` in the VM creates an `ObjClosure` from the constant pool `ObjFunction`
- [ ] `OP_CALL` dispatches on `VAL_CLOSURE` (user functions) vs `VAL_FUNCTION` (natives)
- [ ] Named functions inside other functions are lexically scoped (local, not global)
- [ ] Functions with no captures work as before (trivial closures with 0 upvalues)
- [ ] Native functions (`say`) still work
- [ ] `value_format` for `VAL_CLOSURE` shows `<fn name>` / `<fn>`
- [ ] `make test && make check` pass

---

## Dependencies

- None. This plan is a prerequisite for `closures-capture.md`.

---

## Constraints and non-goals

- **No upvalue capture yet.** All closures have 0 upvalues. Capture is in
  `closures-capture.md`.
- **No garbage collector.** Use reference counting.
- **No syntax changes.**
- **Top-level variables remain globals.**

---

## Implementation steps

Each step follows the required process: tests first, confirm failures, get user
confirmation, implement, `make test && make check`.

### Step 1: Add ObjUpvalue, ObjClosure types and VAL_CLOSURE

**`src/value.h`:**
- Add `ObjUpvalue` struct (refcount, location, closed, next).
- Add `ObjClosure` struct (refcount, function, upvalues, upvalue_count).
- Add `VAL_CLOSURE` to `ValueType` enum.
- Add `ObjClosure *closure` field to `Value` struct.
- Add `int upvalue_count` field to `ObjFunction`.
- Add `size_t refcount` field to `ObjFunction`.

**`src/value.c`:**
- `obj_upvalue_new(Value *slot)` — allocate with refcount 1, location = slot.
- `obj_closure_new(ObjFunction *fn, int upvalue_count)` — allocate closure +
  upvalue pointer array (all NULL initially). Increments `fn->refcount`.
- `make_closure(ObjClosure *cl)` — construct a `VAL_CLOSURE` Value.
- `value_clone` for `VAL_CLOSURE`: increment `closure->refcount` (shallow clone,
  shared upvalues).
- `value_free` for `VAL_CLOSURE`: decrement `closure->refcount`; when 0, free
  each upvalue (decrement its refcount), free the function (decrement its
  refcount), free the upvalue array, free the struct.
- `value_format` for `VAL_CLOSURE`: delegate to `closure->function->name`.
- `is_truthy` for `VAL_CLOSURE`: true.
- Update `make_function` to set `fn->refcount = 1` and `fn->upvalue_count = 0`.
- Update existing `value_clone` for `VAL_FUNCTION` (natives) to increment
  `function->refcount` instead of deep-copying (simplification enabled by
  refcounting).
- Update existing `value_free` for `VAL_FUNCTION`: decrement `function->refcount`;
  only free when 0.

**Tests:**
- Create an ObjClosure wrapping a simple ObjFunction, verify `value_format`.
- Clone a `VAL_CLOSURE`, verify refcount is 2. Free clone, verify refcount is 1.
- Create an ObjUpvalue pointing to a stack value, verify `location` works.
- `is_truthy` for closure returns true.

**Files touched:** `src/value.h`, `src/value.c`, tests.

---

### Step 2: Add new opcodes and update disassembler

**`src/chunk.h`:**
- Add `OP_CLOSURE`, `OP_GET_UPVALUE`, `OP_SET_UPVALUE`, `OP_CLOSE_UPVALUE` to
  `OpCode` enum.

**`src/chunk.c`:**
- `opcode_name()`: return names for all four opcodes.
- `chunk_disassemble` / `chunk_disassemble_to_string`: handle variable-length
  `OP_CLOSURE` instruction. After the constant index byte, read
  `fn->upvalue_count` pairs of (is_local, index) and display each. Handle
  `OP_GET_UPVALUE` and `OP_SET_UPVALUE` as 1-byte operand instructions.
  Handle `OP_CLOSE_UPVALUE` as a simple instruction.

**Tests:**
- Disassemble a chunk containing OP_GET_UPVALUE, verify output format.
- Disassemble OP_CLOSE_UPVALUE, verify output.

**Files touched:** `src/chunk.h`, `src/chunk.c`, tests.

---

### Step 3: Change CallFrame to use ObjClosure and refactor VM

**`src/vm.h`:**
- Change `CallFrame` field from `ObjFunction *function` to
  `ObjClosure *closure`.
- Add `ObjUpvalue *open_upvalues` to `VM` struct (initially NULL).

**`src/vm.c`:**
- Every `frame->function` becomes `frame->closure->function`.
- In `vm_execute`, wrap the top-level `script_fn` in a stack-allocated
  `ObjClosure` with 0 upvalues (no heap allocation needed for the script
  frame's closure — it lives on the C stack alongside `script_fn`).
- In `vm_runtime_error`: update `frame->function->chunk` to
  `frame->closure->function->chunk`.

This is a pure refactor. No behavioral change. All existing tests must pass.

**Tests:** Run the full existing test suite. No new tests needed — this step
validates that the refactor is correct.

**Files touched:** `src/vm.h`, `src/vm.c`.

---

### Step 4: Emit OP_CLOSURE in compiler and lexically scope named functions

**`src/compiler.c`:**
- In `compile_function`: replace the `emit_constant(c, fn_val, line)` call
  (which emits `OP_CONSTANT`) with:
  1. Add the ObjFunction to the constant pool (get the index).
  2. Emit `OP_CLOSURE [constant_index]`.
  3. Emit 0 upvalue descriptor pairs (no captures yet).
- Set `fn->upvalue_count = 0`.
- Do NOT change `make_function` to `make_closure` here — OP_CLOSURE in the VM
  (next step) will create the closure from the ObjFunction constant.
- **Lexically scope named functions:** Currently, named functions always emit
  `OP_DEFINE_GLOBAL` for their name. Change the named-function binding to follow
  the same pattern as `compile_decl`: when `c->context == COMPILE_FUNCTION`,
  register the function name as a local (add to `c->locals`, emit
  `OP_GET_LOCAL` to push a clone as the expression result) instead of emitting
  `OP_DEFINE_GLOBAL`. At the top level (`COMPILE_SCRIPT`), keep the existing
  `OP_DEFINE_GLOBAL` behavior.

**Tests:** Existing function tests should pass. Update bytecode tests to expect
`OP_CLOSURE` instead of `OP_CONSTANT` for function definitions. Add tests
verifying that a named function inside another function is local (not visible
as a global).

**Files touched:** `src/compiler.c`, tests.

---

### Step 5: Implement OP_CLOSURE in VM and update native function handling

**`src/vm.c`:**
- Add the `OP_CLOSURE` case:
  1. Read constant index. Get `ObjFunction *fn` from the constants pool.
  2. Create `ObjClosure *cl = obj_closure_new(fn, fn->upvalue_count)`.
  3. For each upvalue descriptor (is_local, index):
     - If `is_local`: capture from `frame->slots[index]` — call
       `capture_upvalue(vm, &frame->slots[index])`.
     - If not `is_local`: copy `frame->closure->upvalues[index]`.
  4. Push `make_closure(cl)` onto the stack.

- Add helper `capture_upvalue(VM *vm, Value *slot)`:
  - Walk `vm->open_upvalues` looking for one whose `location == slot`.
  - If found, increment refcount and return it.
  - If not found, create a new `ObjUpvalue`, insert into the linked list
    (sorted by slot address descending for efficient closing), return it.

- Ensure the split between `VAL_CLOSURE` (user functions) and `VAL_FUNCTION`
  (natives) works everywhere:
  - `OP_CALL`: if `callee.type == VAL_CLOSURE` → push call frame (use
    `callee.closure`). If `callee.type == VAL_FUNCTION` → call native
    (unchanged). Any other type → runtime error.
  - `value_type_name` for `VAL_CLOSURE` → `"function"`.
  - `values_equal` for `VAL_CLOSURE` → identity comparison (same `ObjClosure *`).
  - Search the codebase for all `VAL_FUNCTION` checks and add `VAL_CLOSURE`
    handling where needed (e.g., `value_format`, `is_truthy`, `value_free`,
    `value_clone`).

At this point, `OP_CLOSURE` works but no functions actually capture anything
(upvalue_count is always 0). All existing tests should pass.

**Tests:**
- `say("hello")` still works.
- `say(fn() is 42 end)` displays `<fn>`.
- All existing CLI tests pass.

**Files touched:** `src/vm.c`, `src/value.c`, tests.

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

- [x] Step 1: Add ObjUpvalue, ObjClosure types and VAL_CLOSURE — added types, constructors, refcounting, value_format/clone/free/truthy support, and 7 unit tests
- [x] Step 2: Add new opcodes and update disassembler — added OP_CLOSURE, OP_GET_UPVALUE, OP_SET_UPVALUE, OP_CLOSE_UPVALUE to enum, opcode_name(), and disassembler with 6 unit tests

---
End of plan.
