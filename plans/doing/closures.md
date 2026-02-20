# Closures

## Objective

Add closures to Cutlet so that functions can capture and mutate variables from
enclosing function scopes. A closure is a function value bundled with references
to captured variables (upvalues).

**Done looks like:** Nested functions can read and write variables from their
enclosing functions. All user-defined functions are represented as `ObjClosure`
at runtime (those that capture nothing have an empty upvalue array). Native
functions (`say`) remain `ObjFunction`. `make test && make check` pass.

---

## Design

### Syntax

No new syntax. Existing `fn` expressions automatically become closures when they
reference variables from enclosing scopes:

```cutlet
fn make_counter() is
  my count = 0
  fn increment() is
    count = count + 1
    count
  end
end

my counter = make_counter()
say(counter())   # => 1
say(counter())   # => 2
```

### Semantics

- **Capture by reference**: closures share variables with their enclosing scope.
  Mutations in the closure are visible to the encloser, and vice versa.
- **All user-defined functions are closures**: every `fn` expression produces an
  `ObjClosure` wrapping an `ObjFunction`. Functions that capture nothing have an
  empty upvalue array. This keeps the calling convention uniform.
- **Upvalues**: each captured variable is represented by an `ObjUpvalue`. An
  upvalue starts "open" (pointing to a live stack slot) and is "closed" (value
  moved from the stack to a heap-allocated cell inside the upvalue) when the
  stack slot is reclaimed. Multiple closures that capture the same variable
  share the same `ObjUpvalue`.

### Architecture changes

**New types (`src/value.h`, `src/value.c`):**

```c
typedef struct ObjUpvalue {
    size_t refcount;           /* Reference count (1 on creation). */
    Value *location;           /* Points to stack slot (open) or &closed (closed). */
    Value closed;              /* Holds the value after closing. */
    struct ObjUpvalue *next;   /* Intrusive list link for VM's open-upvalue list. */
} ObjUpvalue;

typedef struct {
    size_t refcount;           /* Reference count (1 on creation). */
    ObjFunction *function;     /* The compiled function (shared, refcounted). */
    ObjUpvalue **upvalues;     /* Array of captured upvalue pointers. */
    int upvalue_count;         /* Length of the upvalues array. */
} ObjClosure;
```

- Add `VAL_CLOSURE` to `ValueType`.
- Add `ObjClosure *closure` field to `Value` struct.

**`ObjFunction` addition:**

- Add `int upvalue_count` field to `ObjFunction` (records how many upvalues
  the function expects; set during compilation, used by OP_CLOSURE at runtime).

**New opcodes (`src/chunk.h`):**

| Opcode | Operand(s) | Stack effect | Description |
|--------|-----------|-------------|-------------|
| `OP_CLOSURE` | 1-byte constant index, then N x (1-byte is_local, 1-byte index) | push 1 | Create closure from constant pool ObjFunction |
| `OP_GET_UPVALUE` | 1-byte upvalue index | push 1 | Read captured variable |
| `OP_SET_UPVALUE` | 1-byte upvalue index | (peek TOS) | Write captured variable |
| `OP_CLOSE_UPVALUE` | none | pop 1 | Close the topmost open upvalue at TOS slot |

**Compiler changes (`src/compiler.c`):**

- Add `Compiler *enclosing` field to `Compiler` struct.
- Add upvalue tracking array and count.
- Add `resolve_upvalue()` that walks the enclosing compiler chain.
- `compile_function()` passes `c` as `enclosing` to the child compiler.
- Emit `OP_CLOSURE` + upvalue descriptors instead of `OP_CONSTANT` for functions.
- `compile_ident` / `compile_assign` / `compile_call`: after `resolve_local`
  returns -1, try `resolve_upvalue` before falling back to globals.

**VM changes (`src/vm.h`, `src/vm.c`):**

- `CallFrame.function` becomes `CallFrame.closure` (`ObjClosure *`).
- VM gets `ObjUpvalue *open_upvalues` linked list head.
- Implement all four new opcodes.
- `OP_RETURN` closes upvalues above the returning frame's slots.

### Ownership model (no GC)

Reference counting, following the same pattern as `ObjArray` in the arrays plan:

- `ObjFunction`: add `size_t refcount`. `value_clone` for closures increments
  the contained function's refcount. `obj_closure_free` decrements it.
- `ObjClosure`: `size_t refcount`. `value_clone` for `VAL_CLOSURE` increments
  closure refcount. `value_free` decrements; when 0, free upvalues and function.
- `ObjUpvalue`: `size_t refcount`. ObjClosure holds refs. When closure is freed,
  decrement each upvalue's refcount. When upvalue refcount hits 0, free it.

---

## Acceptance criteria

- [ ] `fn` inside `fn` can read a variable from the outer function
- [ ] Mutation through a closure is visible to the enclosing scope and vice versa
- [ ] Multiple closures sharing the same captured variable share the same `ObjUpvalue`
- [ ] Closures that outlive their enclosing function work correctly (upvalue is closed)
- [ ] Deeply nested capture (3+ levels) works
- [ ] Functions with no captures work as before (trivial closures with 0 upvalues)
- [ ] Named and anonymous functions both produce closures
- [ ] Named functions inside other functions are lexically scoped (local, not global)
- [ ] Native functions (`say`) still work
- [ ] `value_format` for `VAL_CLOSURE` shows `<fn name>` / `<fn>`
- [ ] Bytecode disassembly shows `OP_CLOSURE`, `OP_GET_UPVALUE`, `OP_SET_UPVALUE`
- [ ] `make test && make check` pass

---

## Dependencies

- None. If block-scoping (`plans/doing/block-scoping.md`) is done first,
  `end_scope` should emit `OP_CLOSE_UPVALUE` for captured locals instead of
  `OP_POP`. If block-scoping is not yet done, closures only close upvalues at
  function return.

---

## Constraints and non-goals

- **No garbage collector.** Use reference counting for `ObjClosure`,
  `ObjUpvalue`, and `ObjFunction`.
- **No syntax changes.**
- **Top-level variables remain globals.** Top-level closures work through
  the global table (OP_GET_GLOBAL/OP_SET_GLOBAL) rather than upvalues.

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

### Step 4: Emit OP_CLOSURE in compiler (no captures yet) and lexically scope named functions

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

### Step 5: Implement OP_CLOSURE in VM

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

At this point, `OP_CLOSURE` works but no functions actually capture anything
(upvalue_count is always 0). All existing tests should pass.

**Tests:** Run full test suite to verify OP_CLOSURE + VM integration.

**Files touched:** `src/vm.c`.

---

### Step 6: Add upvalue resolution to compiler

**`src/compiler.c`:**
- Add `Compiler *enclosing` field to `Compiler` struct. Initialize to NULL in
  `compile()` and to `c` in `compile_function`.
- Add upvalue tracking:
  ```c
  typedef struct {
      uint8_t index;
      bool is_local;
  } CompilerUpvalue;

  #define UPVALUES_MAX 256
  ```
  Add `CompilerUpvalue upvalues[UPVALUES_MAX]` and `int upvalue_count` to
  `Compiler`.

- Add `resolve_upvalue(Compiler *c, const char *name)`:
  1. If `c->enclosing == NULL`, return -1.
  2. Try `resolve_local(c->enclosing, name)`. If found at slot `s`:
     - Add upvalue `{index=s, is_local=true}` to `c->upvalues` (dedup first).
     - Return the upvalue index in `c->upvalues`.
  3. Recursively try `resolve_upvalue(c->enclosing, name)`. If found at
     upvalue index `u`:
     - Add `{index=u, is_local=false}` to `c->upvalues` (dedup first).
     - Return the upvalue index.
  4. Return -1 (not found — fall through to global).

- Add `add_upvalue(Compiler *c, uint8_t index, bool is_local)`:
  - Check for existing entry with same (index, is_local). If found, return
    that index.
  - Otherwise add a new entry. Error if `upvalue_count >= UPVALUES_MAX`.
  - Return the new index.

- Update `compile_ident`: after `resolve_local` returns -1 (and before the
  global fallback), call `resolve_upvalue(c, name)`. If found, emit
  `OP_GET_UPVALUE [index]` instead of `OP_GET_GLOBAL`.

- Update `compile_assign`: same pattern — try `resolve_upvalue` before global
  fallback. Emit `OP_SET_UPVALUE [index]`.

- Update `compile_call`: same pattern for the callee resolution — try
  `resolve_upvalue` before global fallback. Emit `OP_GET_UPVALUE [index]`
  for the callee.

- Update `compile_function`: after compiling the body, set
  `fn->upvalue_count = body_compiler.upvalue_count`. After emitting
  `OP_CLOSURE [idx]`, emit each `body_compiler.upvalues[i]` as two bytes:
  `(is_local, index)`.

**Tests (`tests/test_compiler.c`):**
- Compile `fn() is my x = 1; fn() is x end end` → inner function's OP_CLOSURE
  has 1 upvalue descriptor `(is_local=true, index=1)` (assuming x is slot 1).
- Compile a function with no capture → 0 upvalues.
- Compile a 3-level nesting where innermost captures outermost → chained upvalue
  `(is_local=false, ...)`.

**Files touched:** `src/compiler.c`, tests.

---

### Step 7: Implement OP_GET_UPVALUE and OP_SET_UPVALUE in VM

**`src/vm.c`:**
- `OP_GET_UPVALUE`: read 1-byte index. Get `ObjUpvalue *uv =
  frame->closure->upvalues[index]`. Clone `*uv->location` and push.
- `OP_SET_UPVALUE`: read 1-byte index. Peek TOS. Free the old value at
  `*uv->location`. Clone TOS into `*uv->location`. (TOS stays as expression
  result.)

**Tests (`tests/test_vm.c`):**
- `fn outer() is my x = 10; fn inner() is x end; inner() end; outer()` → 10.
- `fn outer() is my x = 10; fn inner() is x = 20 end; inner(); x end; outer()`
  → 20 (mutation visible).
- `fn outer() is my x = 10; fn a() is x end; fn b() is x = 20 end; b(); a() end; outer()`
  → 20 (shared upvalue).

**Files touched:** `src/vm.c`, tests.

---

### Step 8: Close upvalues on function return

**`src/vm.c`:**
- Add `close_upvalues(VM *vm, Value *last)`:
  - Walk `vm->open_upvalues`. For each upvalue whose `location >= last`:
    - Copy `*location` into `closed`.
    - Set `location = &upvalue->closed`.
    - Remove from the open list.

- In `OP_RETURN` handler: before popping the frame's stack window (the
  `while (vm.stack_top > frame->slots)` loop), call
  `close_upvalues(&vm, frame->slots)`.

- Implement `OP_CLOSE_UPVALUE`:
  - Call `close_upvalues(&vm, vm.stack_top - 1)`.
  - Then pop and free the TOS value (same as `OP_POP`).

  (This opcode is used by block scoping — when a block ends, captured locals are
  closed before being popped. For now, the compiler doesn't emit it, but the VM
  handler should be ready.)

**Tests (`tests/test_vm.c`):**
- Closure outliving creator:
  `fn make() is my x = 42; fn get() is x end end; my g = make(); g()` → 42.
- Counter pattern:
  `fn make() is my x = 0; fn inc() is x = x + 1; x end end; my f = make(); f(); f()`
  → 2.
- Two closures sharing a closed upvalue:
  `fn make() is my x = 0; fn get() is x end; fn set(v) is x = v end; ... end`

**Files touched:** `src/vm.c`, tests.

---

### Step 9: Update native function handling and value operations

Ensure the split between `VAL_CLOSURE` (user functions) and `VAL_FUNCTION`
(natives) works everywhere:

- `OP_CALL`: if `callee.type == VAL_CLOSURE` → push call frame (use
  `callee.closure`). If `callee.type == VAL_FUNCTION` → call native (unchanged).
  Any other type → runtime error.
- `value_type_name` for `VAL_CLOSURE` → `"function"`.
- `values_equal` for `VAL_CLOSURE` → identity comparison (same `ObjClosure *`).
- `OP_DEFINE_GLOBAL`: works with any value type (peeks TOS, clones into global
  table). No special handling needed for `VAL_CLOSURE`.
- Search the codebase for all `VAL_FUNCTION` checks and add `VAL_CLOSURE`
  handling where needed (e.g., `value_format`, `is_truthy`, `value_free`,
  `value_clone`).

**Tests:**
- `say("hello")` still works.
- `say(fn() is 42 end)` displays `<fn>`.
- `my f = fn(x) is x + 1 end; f == f` → true.
- All existing CLI tests pass.

**Files touched:** `src/vm.c`, `src/value.c`, tests.

---

### Step 10: Integration tests and example program

Comprehensive end-to-end tests:

- **Counter pattern**: `fn make_counter() is my n = 0; fn() is n = n + 1; n end end; my c = make_counter(); c(); c(); c()` → 3.
- **Adder factory**: `fn make_adder(x) is fn(y) is x + y end end; my add5 = make_adder(5); add5(3)` → 8.
- **Shared capture**: two closures from the same encloser, one reads, one writes.
- **Deep nesting**: 3-level function nesting, innermost captures from outermost.
- **Closure + recursion**: recursive function captured as a closure.
- **Closure + control flow**: closure captures a variable modified by a `while` loop.
- **Closure + parameters**: capture a parameter (not just `my` locals).
- **Error cases**: calling a non-function still errors correctly; arity mismatch on closure still reports function name.

**Example program** (`examples/closures.cutlet`):
```cutlet
fn make_counter() is
  my count = 0
  fn() is
    count = count + 1
    say(count)
  end
end

my counter = make_counter()
counter()
counter()
counter()
```

Generate `examples/closures.expected` by running the example.

Run `make test && make check` for final verification.

**Files touched:** tests, `examples/closures.cutlet`, `examples/closures.expected`.

**Post-implementation reminders** (per AGENTS.md):
- Remind user to update `TUTORIAL.md` with closures section.
- Remind user to review the new example program.

---

## Required process (every step)

1. Write tests first.
2. Run `make test` and `make check` — confirm new tests fail.
3. **Stop and ask the user for confirmation before implementing.**
4. Implement the feature.
5. Run `make test` and `make check` after every code change.
6. Do not remove or modify existing tests without user confirmation.

---
End of plan.
