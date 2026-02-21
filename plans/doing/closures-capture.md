# Closures: Upvalue Capture

## Objective

Add upvalue capture to closures so that nested functions can read and write
variables from enclosing function scopes. This completes the closures feature.

**Done looks like:** Nested functions can read and write variables from their
enclosing functions. Closures that outlive their enclosing function work
correctly (upvalues are closed). `make test && make check` pass.

---

## Prerequisites

- `closures-infrastructure.md` must be completed first. That plan establishes
  `ObjClosure`, `ObjUpvalue`, `VAL_CLOSURE`, the four new opcodes, the
  `CallFrame.closure` refactor, and `OP_CLOSURE` emission with 0 upvalues.

---

## Design

See `plans/doing/closures.md` for the full design. This plan implements:

- **Compiler upvalue resolution**: walking the enclosing compiler chain to find
  captured variables and emitting upvalue descriptors with `OP_CLOSURE`.
- **`OP_GET_UPVALUE` / `OP_SET_UPVALUE`**: reading and writing captured variables
  through upvalue indirection in the VM.
- **Closing upvalues**: moving values from the stack to heap when the enclosing
  function returns, so closures that outlive their creator still work.

### Semantics recap

- **Capture by reference**: closures share variables with their enclosing scope.
  Mutations in the closure are visible to the encloser, and vice versa.
- **Upvalues**: each captured variable is represented by an `ObjUpvalue`. An
  upvalue starts "open" (pointing to a live stack slot) and is "closed" (value
  moved to a heap cell inside the upvalue) when the stack slot is reclaimed.
  Multiple closures that capture the same variable share the same `ObjUpvalue`.

---

## Acceptance criteria

- [ ] `fn` inside `fn` can read a variable from the outer function
- [ ] Mutation through a closure is visible to the enclosing scope and vice versa
- [ ] Multiple closures sharing the same captured variable share the same `ObjUpvalue`
- [ ] Closures that outlive their enclosing function work correctly (upvalue is closed)
- [ ] Deeply nested capture (3+ levels) works
- [ ] Closure + parameters: capture a parameter (not just `my` locals)
- [ ] Closure + recursion: recursive function captured as a closure
- [ ] Closure + control flow: closure captures a variable modified by a `while` loop
- [ ] Error cases: calling a non-function still errors correctly; arity mismatch on closure still reports function name
- [ ] Bytecode disassembly shows upvalue descriptors in `OP_CLOSURE`
- [ ] `make test && make check` pass

---

## Constraints and non-goals

- **No garbage collector.** Use reference counting (already in place from the
  infrastructure plan).
- **No syntax changes.**
- **Top-level variables remain globals.** Top-level closures work through
  the global table (OP_GET_GLOBAL/OP_SET_GLOBAL) rather than upvalues.

---

## Implementation steps

Each step follows the required process: tests first, confirm failures, get user
confirmation, implement, `make test && make check`.

### Step 1: Add upvalue resolution to compiler

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

### Step 2: Implement OP_GET_UPVALUE and OP_SET_UPVALUE in VM

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

### Step 3: Close upvalues on function return

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

### Step 4: Integration tests and example program

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

## Progress

- [x] Step 1: Add upvalue resolution to compiler — added `enclosing` pointer, `CompilerUpvalue` array, `resolve_upvalue`/`add_upvalue`, updated `compile_ident`/`compile_assign`/`compile_call` for upvalue resolution, emit upvalue descriptors after `OP_CLOSURE`, 5 new compiler tests
- [x] Step 2: Implement OP_GET_UPVALUE and OP_SET_UPVALUE in VM — added dispatch handlers for reading/writing captured variables through upvalue indirection, 3 new VM tests (read, mutate, shared upvalue)
- [x] Step 3: Close upvalues on function return — added close_upvalues() to VM, called from OP_RETURN and OP_CLOSE_UPVALUE handler, 3 new tests (outlive creator, counter pattern, shared closed upvalue)

---
End of plan.
