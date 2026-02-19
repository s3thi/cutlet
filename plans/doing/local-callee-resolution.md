# Fix: Resolve local variables as callees in function calls

## Objective

When a function call `f(x)` appears inside a function body where `f` is a local variable (parameter or `my` declaration), the compiler should emit `OP_GET_LOCAL` to load the callee — not `OP_GET_GLOBAL`. Done means `fn apply(f, x) is f(x) end` works correctly, enabling higher-order function patterns (passing functions as arguments, callbacks, etc.).

## Problem

`compile_call()` in `src/compiler.c` always emits `OP_GET_GLOBAL` to look up the callee by name. It never checks `resolve_local()`, unlike `compile_ident()` which already does local-then-global resolution. This means any call where the callee is a local variable fails at runtime with "unknown variable".

Reproducer:

```cutlet
fn apply(f, x) is f(x) end
fn inc(x) is x + 1 end
apply(inc, 5)
# => ERR unknown variable 'f'
```

## Acceptance criteria

- [ ] `compile_call()` checks `resolve_local()` before falling back to `OP_GET_GLOBAL`, matching the pattern already used by `compile_ident()`
- [ ] Compiler test: call to a parameter inside a function body emits `OP_GET_LOCAL` for the callee
- [ ] Compiler test: call to a non-local inside a function body still emits `OP_GET_GLOBAL`
- [ ] Compiler test: call at top-level (script context) still emits `OP_GET_GLOBAL`
- [ ] VM test: `fn apply(f, x) is f(x) end` with a user-defined function works end-to-end
- [ ] VM test: `fn apply(f, x) is f(x) end` with a built-in function works end-to-end
- [ ] VM test: nested higher-order — a function received as a parameter calls another function received as a parameter
- [ ] VM test: calling a `my`-declared local function variable works
- [ ] `make test && make check` pass with zero failures and zero warnings

## Dependencies

None. This is a self-contained bug fix in the compiler.

## Constraints

- **Parser is not affected.** The `AST_CALL` node already stores the callee name in `node->value`. The fix is purely in the compiler.
- Only `compile_call()` in `src/compiler.c` needs to change. The pattern to follow is the local-then-global resolution already implemented in `compile_ident()` in the same file.
- Do not change how `compile_ident()`, `compile_assign()`, or `compile_decl()` work — they already handle locals correctly.

## Non-goals

- First-class function expressions as callees (e.g., `(fn(x) is x end)(5)`) — that would require the parser to produce an `AST_CALL` node with a child expression instead of a string name. Out of scope for this fix.

## Implementation steps

### Step 1: Write tests

**Compiler tests** (`tests/test_compiler.c`):

1. Add `test_compile_call_local_callee`: compile `fn apply(f, x) is f(x) end`. Extract the body chunk from the `ObjFunction`. Assert the body contains `OP_GET_LOCAL` (not `OP_GET_GLOBAL`) for the callee `f`. The callee `f` is parameter slot 1 (slot 0 is reserved for the function itself).

2. Add `test_compile_call_global_callee_in_function`: compile `fn foo() is bar() end`. Assert the body uses `OP_GET_GLOBAL` for `bar` (since `bar` is not a local).

3. Add `test_compile_call_global_callee_toplevel`: compile `foo()` at top level. Assert it uses `OP_GET_GLOBAL` (existing `test_compile_call_say` already covers this — verify it still passes).

**VM tests** (`tests/test_vm.c`):

4. Add `test_higher_order_apply`: `fn apply(f, x) is f(x) end\nfn inc(x) is x + 1 end\napply(inc, 5)` → expect number `6.0`.

5. Add `test_higher_order_builtin`: `fn apply(f, x) is f(x) end\napply(type, 42)` → expect string `"number"`.

6. Add `test_higher_order_nested`: a function that receives two function args and composes them:
   ```
   fn compose(f, g, x) is f(g(x)) end
   fn double(x) is x * 2 end
   fn inc(x) is x + 1 end
   compose(double, inc, 5)
   ```
   → expect number `12.0` (double(inc(5)) = double(6) = 12).

7. Add `test_local_var_as_callee`: a `my`-declared variable used as a callee:
   ```
   fn foo() is
     my f = fn(x) is x + 10 end
     f(5)
   end
   foo()
   ```
   → expect number `15.0`.

Register all new tests in the `main()` functions of both test files under appropriate sections.

Run `make test && make check` and confirm the new tests fail (the compiler and VM tests for local callees should fail).

### Step 2: Fix `compile_call()` in `src/compiler.c`

In `compile_call()`, replace the unconditional `OP_GET_GLOBAL` callee lookup with local-then-global resolution:

- If `c->context == COMPILE_FUNCTION`, call `resolve_local(c, node->value)`.
  - If the slot is >= 0, emit `OP_GET_LOCAL` with that slot index.
  - Otherwise, fall through to the existing `OP_GET_GLOBAL` path.
- If `c->context == COMPILE_SCRIPT`, use `OP_GET_GLOBAL` as before.

This mirrors the pattern in `compile_ident()`.

Update the comment on `compile_call()` to document that the callee is resolved locally first in function context.

Run `make test && make check` and confirm all tests pass.

---

## Required process

1. Write tests first (Step 1).
2. Run `make test && make check` — confirm new tests fail.
3. **Stop and ask the user for confirmation before implementing.**
4. Implement the fix (Step 2).
5. Run `make test && make check` — confirm everything passes.
