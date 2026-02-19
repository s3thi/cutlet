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

- [x] `compile_call()` checks `resolve_local()` before falling back to `OP_GET_GLOBAL`, matching the pattern already used by `compile_ident()`
- [x] Compiler test: call to a parameter inside a function body emits `OP_GET_LOCAL` for the callee
- [x] Compiler test: call to a non-local inside a function body still emits `OP_GET_GLOBAL`
- [x] Compiler test: call at top-level (script context) still emits `OP_GET_GLOBAL`
- [x] VM test: `fn apply(f, x) is f(x) end` with a user-defined function works end-to-end
- [x] VM test: `fn apply(f, x) is f(x) end` with a built-in function works end-to-end
- [x] VM test: nested higher-order — a function received as a parameter calls another function received as a parameter
- [x] VM test: calling a `my`-declared local function variable works
- [x] `make test && make check` pass with zero failures and zero warnings

## Summary

**What changed:** `compile_call()` in `src/compiler.c` now checks `resolve_local()` before falling back to `OP_GET_GLOBAL` when in function context, mirroring the pattern already used by `compile_ident()`. This enables higher-order function patterns — passing functions as arguments, callbacks, and calling `my`-declared local function variables.

**Files touched:**
- `src/compiler.c` — Added local-then-global callee resolution in `compile_call()`
- `tests/test_compiler.c` — Added `test_compile_call_local_callee` and `test_compile_call_global_callee_in_function`
- `tests/test_vm.c` — Added `test_higher_order_apply`, `test_higher_order_builtin`, `test_higher_order_nested`, `test_local_var_as_callee`
