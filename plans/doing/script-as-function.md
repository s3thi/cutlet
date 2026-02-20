# Script as Implicit Function (Lua Model)

## Objective

Compile top-level script code as an implicit function so that `my` declarations
at the top level create stack-local variables instead of globals. This enables
closures defined at the top level to capture top-level locals via upvalues.

**Done looks like:** In file mode (`cutlet run`), top-level `my` creates locals.
Functions at the top level can capture them as upvalues. The REPL continues to
use globals for top-level `my` (so variables persist between evaluations).
`make test && make check` pass.

---

## Design

### Current state

The `compile()` function creates a `Compiler` with `context = COMPILE_SCRIPT`.
In this context, `compile_decl` emits `OP_DEFINE_GLOBAL`, `compile_ident` emits
`OP_GET_GLOBAL`, and `compile_assign` emits `OP_SET_GLOBAL`. All top-level
variables live in the runtime global hash table.

### After this change

`compile()` accepts a mode parameter:

- **`COMPILE_MODE_FILE`** (for `cutlet run`): uses `COMPILE_FUNCTION` context.
  Top-level `my` becomes `OP_SET_LOCAL`. Variable resolution tries locals first,
  then upvalues (if closures exist), then globals. The top-level chunk is
  compiled as a function body.
- **`COMPILE_MODE_REPL`** (for `cutlet repl`): uses `COMPILE_SCRIPT` context
  (unchanged). Top-level `my` remains `OP_DEFINE_GLOBAL`, preserving REPL
  variable persistence between evaluations.

This matches Lua's behavior: the REPL uses globals for convenience, while
scripts compile as implicit functions with local variables.

### Why the REPL stays global

In Lua's REPL, each input line is compiled and executed as a separate chunk.
Local variables from one line don't carry over to the next. Persisting
variables requires globals. Cutlet's REPL has the same architecture — each
evaluation is an independent `compile()` + `vm_execute()` call. Keeping
`COMPILE_SCRIPT` for the REPL preserves the existing behavior where
`my x = 5` makes `x` available in subsequent lines.

### Globals still accessible

Builtins (`say`, `str`, etc.) are registered as globals in the runtime hash
table. In `COMPILE_FUNCTION` context, `resolve_local` returns -1 for names
not declared locally, and `resolve_upvalue` returns -1 if there's no
enclosing compiler. The compiler falls through to `OP_GET_GLOBAL`, which
finds builtins in the global table. No change needed for builtin access.

---

## Acceptance criteria

- [ ] In file mode: `my x = 10; say(x)` works (x is a local, say is a global)
- [ ] In file mode: `my x = 10; fn get() is x end; say(get())` prints 10
      (top-level x captured as upvalue)
- [ ] In file mode: top-level counter pattern works
      (`my n = 0; fn inc() is n = n + 1; n end; say(inc()); say(inc())` → 1, 2)
- [ ] In REPL mode: `my x = 5` followed by `x` on next line → 5
      (globals persist, unchanged behavior)
- [ ] Builtins (`say`) are accessible from both file and REPL modes
- [ ] All existing example programs produce correct output
- [ ] `make test && make check` pass

---

## Dependencies

- **Requires closures** (`plans/doing/closures.md`). Without closures, there's
  no upvalue mechanism, and top-level functions can't capture top-level locals.
  The primary motivation for this plan is enabling top-level closure capture.

---

## Constraints and non-goals

- **No `global` keyword.** There's no way to explicitly declare a global from
  inside a function. This may be added later if needed.
- **REPL behavior unchanged.** The REPL does not use the Lua model — it keeps
  using globals for top-level variables, matching Lua's own REPL behavior.
- **No persistent REPL stack frame.** A future plan could make the REPL
  persist a top-level stack frame across evaluations, but that's out of scope.

---

## Implementation steps

Each step follows the required process: tests first, confirm failures, get user
confirmation, implement, `make test && make check`.

### Step 1: Add compile mode parameter to compile()

**`src/compiler.h`:**
- Add `CompileMode` enum: `COMPILE_MODE_FILE`, `COMPILE_MODE_REPL`.
- Change `compile()` signature to accept a `CompileMode` parameter.

**`src/compiler.c`:**
- In `compile()`, choose context based on mode:
  - `COMPILE_MODE_FILE` → `COMPILE_FUNCTION` context, `scope_depth = 1`
    (if block scoping is done), reserve slot 0 for the implicit script callee.
  - `COMPILE_MODE_REPL` → `COMPILE_SCRIPT` context (unchanged).
- Update `compile()` to reserve slot 0 when using COMPILE_FUNCTION
  (same as `compile_function` does for the callee slot).

**Update all callers of `compile()`:**
- File execution path (in `main.c` or wherever `cutlet run` calls compile):
  pass `COMPILE_MODE_FILE`.
- REPL evaluation path: pass `COMPILE_MODE_REPL`.
- Test harnesses: choose appropriate mode (most eval tests should use
  `COMPILE_MODE_FILE`; REPL-specific tests use `COMPILE_MODE_REPL`).

**Tests:** All existing tests pass (REPL tests use REPL mode, eval tests
initially use REPL mode for backward compat, then migrated in step 3).

**Files touched:** `src/compiler.h`, `src/compiler.c`, `src/main.c`,
`src/repl.c`, `src/repl_server.c`, tests.

---

### Step 2: File mode compiles top-level as a function

**`src/compiler.c`:**
- When `mode == COMPILE_MODE_FILE`, the top-level compiler uses
  `COMPILE_FUNCTION` context. This means:
  - `compile_decl` takes the COMPILE_FUNCTION path: `my` creates locals.
  - `compile_ident` tries `resolve_local` first.
  - `compile_assign` tries `resolve_local` first.
  - Variable references not found locally fall through to globals (builtins).

**`src/vm.c`:**
- In `vm_execute`, when the top-level chunk was compiled in file mode, the
  script frame's slot 0 is reserved for the script "callee" (same as in
  function frames). Push a dummy value for slot 0.

**Tests:**
- `my x = 10; x` (file mode) → 10 (x is a local).
- `say("hello")` (file mode) → works (say is a global builtin).
- `my x = 10; say(x)` (file mode) → prints 10.

**Files touched:** `src/compiler.c`, `src/vm.c`, tests.

---

### Step 3: Top-level closures capture top-level locals

With closures in place and top-level using COMPILE_FUNCTION context, the
enclosing compiler chain now includes the top-level compiler. Functions
defined at the top level will automatically resolve top-level locals
via `resolve_upvalue`.

**Tests:**
- `my x = 10; fn get() is x end; get()` (file mode) → 10 (captured via
  upvalue).
- `my n = 0; fn inc() is n = n + 1; n end; inc(); inc()` (file mode) → 2
  (mutation through upvalue).
- `my x = 10; fn get() is x end; fn set(v) is x = v end; set(20); get()`
  (file mode) → 20 (shared upvalue).

**Files touched:** tests only (should work automatically from steps 1-2 +
the closures plan).

---

### Step 4: Verify REPL backward compatibility

Ensure the REPL (COMPILE_MODE_REPL) works exactly as before:

**Tests (`tests/test_cli.sh` or REPL-specific tests):**
- REPL: `my x = 5` then `x` → 5 (global persists).
- REPL: `fn double(n) is n * 2 end` then `double(21)` → 42 (function persists
  as global).
- REPL: `my x = 1; x = 2; x` → 2 (reassignment works).

**Files touched:** tests only.

---

### Step 5: Update test harnesses and existing tests

Some existing eval tests may assume `COMPILE_MODE_REPL` behavior (globals).
Update them:

- Tests that evaluate multi-statement programs in a single compile call should
  work in file mode (locals instead of globals — same observable behavior for
  most tests).
- Tests that rely on global persistence across multiple compile calls (REPL
  pattern) should explicitly use `COMPILE_MODE_REPL`.
- Run all example programs (`make test-examples`) to verify they work in file
  mode.

**Files touched:** `tests/test_vm.c`, `tests/test_compiler.c`, other test
files as needed.

---

### Step 6: Update examples and integration tests

Run all `examples/*.cutlet` programs through `cutlet run` and verify output
matches `.expected` files. If any example relies on cross-function global
variable access that no longer works (unlikely — most examples use `say` and
explicit function parameters), update the example.

**Files touched:** examples (only if needed), tests.

---

### Step 7: Final verification

- `make test && make check` passes.
- All example programs produce correct output.
- REPL works interactively (manual test or CLI test).
- File mode correctly uses locals for top-level `my`.

**Post-implementation reminders** (per AGENTS.md):
- Remind user to update `TUTORIAL.md` if the scoping model explanation needs
  updating.

**Files touched:** none (verification only).

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
