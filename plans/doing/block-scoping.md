# Block Scoping

## Objective

Add block-level scoping so that variables declared with `my` inside `if`,
`while`, or other block bodies are local to that block and not visible outside.

**Done looks like:** `my` inside an `if` or `while` body is scoped to that
block. Accessing it outside produces a compile error. Loop bodies correctly
clean up locals between iterations. `make test && make check` pass.

---

## Design

### Current problem

The compiler currently uses a flat `locals[]` array with no scope depth
tracking. A `my` declaration inside a `while` body permanently increments
`local_count` at compile time, but at runtime the stack position of the
local can drift on subsequent iterations because the old local's slot is
never reclaimed. This is a latent stack-misalignment bug.

### Solution

Add `scope_depth` to the Compiler and `depth` to each Local. Block bodies
(`if` then/else, `while` body) are wrapped in `begin_scope()` / `end_scope()`
calls. `end_scope()` emits bytecode to pop locals declared in the ending scope
while preserving the block's result value.

### Scope cleanup for expression-based blocks

Cutlet blocks are expressions — they leave one result on the stack. When a
scope ends, the result sits at TOS above the block's locals. To clean up:

1. `OP_SET_LOCAL [first_departing_slot]` — save result to the first local's
   position.
2. `OP_POP` x N — pop the result copy plus the remaining N-1 locals.
3. The result now occupies the position of the first departing local, which
   is TOS after the pops. `local_count` is reset to `first_departing_slot`.

If a scope declares 0 locals, no cleanup is needed.

### Break and continue

`break` and `continue` must clean up block-scoped locals before jumping.
The `LoopContext` tracks the scope depth at loop entry. Before emitting a
jump, the compiler emits `OP_POP` (or `OP_CLOSE_UPVALUE` if closures exist)
for each local deeper than the loop's scope depth.

---

## Acceptance criteria

- [ ] `my x = 1` inside an `if` body is NOT visible after the `end`
- [ ] `my x = 1` inside a `while` body is NOT visible after the `end`
- [ ] `my` inside a `while` body works correctly across multiple iterations
      (no stack corruption)
- [ ] Shadowing: `my x = 1; if true do my x = 2; x end` evaluates to 2,
      outer `x` is still 1
- [ ] `break` cleans up block-scoped locals before jumping
- [ ] `continue` cleans up block-scoped locals before jumping
- [ ] Nested blocks: scopes inside scopes work correctly
- [ ] If closures exist: `OP_CLOSE_UPVALUE` is emitted for captured locals
      at scope exit instead of `OP_POP`
- [ ] `make test && make check` pass

---

## Dependencies

- None. This can be done before or after closures. If closures exist,
  `end_scope` emits `OP_CLOSE_UPVALUE` for captured locals; if not, `OP_POP`.

---

## Constraints and non-goals

- No new syntax — this is a scoping rule change, not a new keyword.
- Function parameters are at depth 1 (the function body scope) and are not
  affected by block scoping within the function.
- Top-level `my` (COMPILE_SCRIPT context) remains a global variable.

---

## Implementation steps

Each step follows the required process: tests first, confirm failures, get user
confirmation, implement, `make test && make check`.

### Step 1: Add scope_depth to Compiler and depth to Local

**`src/compiler.c`:**

- Add `int scope_depth` to the `Compiler` struct.
  - Initialize to 0 in `compile()` (COMPILE_SCRIPT context).
  - Initialize to 1 in `compile_function` for the body compiler
    (COMPILE_FUNCTION context — the function body is at depth 1).
- Add `int depth` to the `Local` struct.
- In `compile_decl` (COMPILE_FUNCTION path), set the new local's `depth` to
  `c->scope_depth`.
- In `compile_function`, when registering parameters as locals, set their
  `depth` to 1 (the function body scope). Same for the slot-0 callee local.

No behavioral change. All existing tests should pass.

**Tests:** Run full test suite.

**Files touched:** `src/compiler.c`.

---

### Step 2: Add begin_scope() and end_scope() helpers

**`src/compiler.c`:**

- `begin_scope(Compiler *c)`: increment `c->scope_depth`.
- `end_scope(Compiler *c, int line)`:
  1. Count locals at the current depth: scan backwards from `local_count - 1`
     while `locals[i].depth == c->scope_depth`. Call this count `n`.
  2. Compute `first_slot = c->local_count - n` (the slot of the first
     departing local).
  3. If `n > 0`:
     - Emit `OP_SET_LOCAL first_slot` (save TOS result to first local's slot).
     - Emit `OP_POP` x `n` (pop result copy + remaining locals).
     - If closures exist and a local has `is_captured` set, emit
       `OP_CLOSE_UPVALUE` instead of `OP_POP` for that local. (Skip this
       sub-step if closures are not yet implemented.)
  4. Set `c->local_count = first_slot`.
  5. Decrement `c->scope_depth`.

No callers yet. All tests should pass.

**Tests:** Run full test suite (no behavioral change yet).

**Files touched:** `src/compiler.c`.

---

### Step 3: Wrap if then/else bodies in scopes

**`src/compiler.c`, in `compile_if`:**

- Before compiling `node->children[1]` (then-body): call `begin_scope(c)`.
- After compiling `node->children[1]`: call `end_scope(c, line)`.
- Before compiling `node->children[2]` (else-body, if present): call
  `begin_scope(c)`.
- After compiling `node->children[2]`: call `end_scope(c, line)`.

Only add scopes when in `COMPILE_FUNCTION` context (block scoping only
applies inside functions; top-level `my` is still global).

**Tests (`tests/test_vm.c` or `tests/test_compiler.c`):**
- `fn() is if true do my x = 5; x end end()` → 5 (visible inside scope).
- `fn() is if true do my x = 5 end; x end()` → compile error
  "unknown variable 'x'" (not visible outside).
- `fn() is my x = 1; if true do my x = 2; x end end()` → 2 (inner shadows).
- `fn() is my x = 1; if true do my x = 2 end; x end()` → 1 (outer preserved).
- `fn() is if false do my x = 1 else my y = 2; y end end()` → 2
  (else branch has its own scope).

**Files touched:** `src/compiler.c`, tests.

---

### Step 4: Wrap while body in a scope

**`src/compiler.c`, in `compile_while`:**

- Before compiling `node->children[1]` (loop body): call `begin_scope(c)`.
- After compiling `node->children[1]`: call `end_scope(c, line)`.

Only when in `COMPILE_FUNCTION` context.

**Tests:**
- `fn() is my i = 0; while i < 3 do my x = i * 10; i = i + 1 end end()`
  → runs without stack corruption (each iteration cleans up `x`).
- `fn() is while false do my x = 1 end; x end()` → compile error.
- `fn() is my sum = 0; my i = 0; while i < 3 do my x = i + 1; sum = sum + x; i = i + 1 end; sum end()`
  → 6 (1 + 2 + 3).

**Files touched:** `src/compiler.c`, tests.

---

### Step 5: Clean up locals on break and continue

**`src/compiler.c`:**

- Add `int scope_depth` to `LoopContext` to record the scope depth at the
  start of the loop body (after `begin_scope`).
- In `compile_break` and `compile_continue`, before emitting the jump:
  - Count locals from `c->local_count - 1` backwards whose `depth` is
    greater than `c->current_loop->scope_depth`.
  - Emit `OP_POP` for each (or `OP_CLOSE_UPVALUE` if captured).

For `break`: the break value is compiled before the cleanup. The cleanup
pops locals but not the break value. Emit cleanup pops, then the break
value is already on TOS (it was above the locals that were just popped).

Wait — the break value is compiled AFTER the locals exist. The break
value is at TOS, locals are below. Need to use the same save-and-pop
trick: save break value to the first departing local's slot, then pop.

Actually, `break` with a value works differently. The break value is pushed
by compile_break. Then we jump. At BREAK_TARGET, the stack should have just
the loop result. So the break value IS the loop result. But there are locals
on the stack between the accumulator position and the break value.

The simplest approach: in `compile_break`, BEFORE compiling the break value,
emit `OP_POP` for each local that needs cleanup. Then compile the break value.
Then emit the jump. This works because the break value hasn't been computed yet,
so there's nothing to save. The locals are at TOS and can be directly popped.

Same for `continue`: emit `OP_POP` for locals, then `OP_NOTHING`, then the
backward jump.

**Tests:**
- `fn() is while true do my x = 42; break x end end()` → 42.
- `fn() is my i = 0; my result = 0; while i < 5 do my x = i; if x == 3 do break x end; i = i + 1 end end()`
  → 3.
- `fn() is my i = 0; while i < 5 do my x = i; i = i + 1; if x == 2 do continue end; x end end()`
  → continues correctly, locals cleaned up.

**Files touched:** `src/compiler.c`, tests.

---

### Step 6: Nested scopes

Verify that nested blocks (if inside while, while inside if, etc.) work
correctly with multiple scope levels.

**Tests:**
- `fn() is my i = 0; while i < 2 do if true do my x = i end; i = i + 1 end end()`
  → no crash, x is scoped to if body.
- `fn() is if true do my a = 1; if true do my b = 2; a + b end end end()`
  → 3 (inner scope sees outer scope's locals).
- `fn() is if true do my a = 1 end; if true do my b = 2; b end end()`
  → 2 (a is not visible in the second if).
- Three-level nesting: while > if > block with `my` at each level.

**Files touched:** tests only (verification step — may require no source changes
if steps 3-5 handle nesting correctly).

---

### Step 7: Integration with closures (conditional)

**Only if closures (`plans/doing/closures.md`) have been implemented.**

- Add `bool is_captured` to the `Local` struct. Default false.
- In `resolve_upvalue` (from the closures plan): when marking a local as
  captured, set `locals[slot].is_captured = true`.
- In `end_scope`: for locals with `is_captured == true`, emit
  `OP_CLOSE_UPVALUE` instead of `OP_POP`.
- In break/continue cleanup: same — use `OP_CLOSE_UPVALUE` for captured locals.

**If closures are not yet implemented**, skip this step. Document it as deferred
and note that the closures implementation should coordinate with this.

**Tests (if closures exist):**
- Classic closure-in-loop: each iteration creates a closure capturing a
  block-scoped variable. Each closure should see its own value, not the
  final value.

**Files touched:** `src/compiler.c` (if closures exist), tests.

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
