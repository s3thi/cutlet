# Remove Implicit Type Coercion from `++`

## Objective

The `++` operator currently auto-coerces any value type to a string via
`value_format()`. After this task, `++` requires **both operands to be
strings** (or, in the future, both arrays — see `plans/doing/arrays.md`
step 5). Any other combination produces a runtime error. A new `str()`
built-in provides explicit conversion for cases where coercion was
previously relied on.

## Done looks like

- `"hello" ++ " world"` works (string ++ string).
- `"x" ++ 42` produces a runtime error like `"++ requires strings, got string and number"`.
- `str(42)` returns `"42"`, `str(true)` returns `"true"`, etc.
- `say()` continues to auto-format any value (unchanged).
- `make test && make check` pass.

## Acceptance criteria

- [x] `str()` native built-in registered, converts any value to string via `value_format()`
- [x] `OP_CONCAT` in VM rejects non-string operands with a clear error message
- [x] Tests for `str()`: number, bool, nothing, string (identity), error, function
- [x] Coercion tests converted: tests that previously asserted successful coercion now assert runtime errors
- [x] New tests added: `str(x) ++ str(y)` patterns work correctly
- [x] `test_concat_precedence` updated to use string operands (tests precedence, not coercion)
- [x] `examples/strings.cutlet` and `examples/strings.expected` updated to use `str()` for non-string values
- [x] `make test && make check` pass

## Dependencies

None. The `plans/doing/arrays.md` task (step 5) will later extend `++`
to also accept array operands, but that task can proceed independently —
it just needs to add its array-array branch to `OP_CONCAT` alongside the
existing string-string check.

## Constraints / non-goals

- `say()` keeps auto-formatting. Only `++` becomes strict.
- Do not change `value_format()` itself — it is still used by `say()`,
  error reporting, and debug output.
- Do not add any other conversion builtins (`num()`, `bool()`, etc.) — out
  of scope.

---

## Steps

### Step 1: Add `str()` native built-in — tests first

**Write tests** in `tests/test_vm.c`:

- `str(42)` → `"42"` (integer)
- `str(3.5)` → `"3.5"` (float)
- `str(true)` → `"true"`
- `str(false)` → `"false"`
- `str(nothing)` → `"nothing"`
- `str("hello")` → `"hello"` (identity — already a string)
- `str(42) ++ str(true)` → `"42true"` (compose with `++`)

Add the test functions and register them in the test runner.

Run `make test && make check` — confirm the new tests **fail** (unknown
function `str`). Stop and get user confirmation before implementing.

**Implement** in `src/vm.c`:

- Add `native_str()` next to `native_say()`. It takes 1 argument, calls
  `value_format(&args[0])`, and returns a `make_string()` wrapping the
  result.
- In `register_builtins()`, register `str` the same way `say` is
  registered: `make_native("str", 1, native_str)` →
  `runtime_var_define("str", &str_fn)`.

Run `make test && make check`.

### Step 2: Make `OP_CONCAT` reject non-string operands — tests first

**Modify existing tests** in `tests/test_vm.c`:

The following tests currently assert successful coercion. Change them to
assert a **runtime error** instead (use `assert_vm_error` or equivalent):

| Test function | Input | Old assertion | New assertion |
|---|---|---|---|
| `test_concat_str_num` | `"x" ++ 42` | string `"x42"` | runtime error |
| `test_concat_num_str` | `42 ++ "x"` | string `"42x"` | runtime error |
| `test_concat_bool_str` | `true ++ "!"` | string `"true!"` | runtime error |
| `test_concat_nothing_str` | `nothing ++ "x"` | string `"nothingx"` | runtime error |
| `test_concat_num_num` | `1 ++ 2` | string `"12"` | runtime error |
| `test_concat_float_str` | `(7 / 2) ++ "x"` | string `"3.5x"` | runtime error |
| `test_fn_concat_coercion` | `"val: " ++ true` | string `"val: true"` | runtime error |

Also update `test_concat_precedence`: change input from `1 + 2 ++ 3 + 4`
to `str(1 + 2) ++ str(3 + 4)` and keep the expected result `"37"`. This
still tests that `+` binds tighter than `++`.

**Add new passing tests**:

- `str("x") ++ str(42)` → `"x42"` (explicit conversion works)
- `str(true) ++ "!"` → `"true!"`
- `str(nothing) ++ "x"` → `"nothingx"`

Run `make test && make check` — confirm coercion tests now **pass
unexpectedly** (they still coerce) and the new tests pass. Stop and get
user confirmation.

**Implement** in `src/vm.c`:

In the `OP_CONCAT` handler, **before** calling `value_format()`, check
that both `a` and `b` are `VAL_STRING`. If either is not, produce a
runtime error:

```
"++ requires strings, got <type_a> and <type_b>"
```

Use the `value_type_name()` helper already in `vm.c` for the type names.

Remove the `value_format()` calls from `OP_CONCAT` — instead, directly
access `a.string` and `b.string` since both are guaranteed strings.

Run `make test && make check`.

### Step 3: Update examples

**Update** `examples/strings.cutlet`:

```cutlet
# Feature: strings, concatenation, and str()
# Exercises: STRING, ++ operator, str() for explicit conversion

say("hello" ++ " " ++ "world")
say("score: " ++ str(42))
say("alive: " ++ str(true))
say("value: " ++ str(nothing))
```

The expected output (`examples/strings.expected`) does **not** change —
the output is the same, just the source now uses explicit `str()`.

Update the comment at the top of the example to mention `str()`.

Run `make test && make check` (includes `make test-examples`).

---

## Required process (every step)

1. Write tests first.
2. Run `make test && make check` — confirm new tests fail.
3. **Stop and ask the user for confirmation before implementing.**
4. Implement the feature.
5. Run `make test && make check` after every code change.
6. Do not remove or modify existing tests without user confirmation.

---

## Summary

Removed implicit type coercion from `++`. The operator now requires both operands to be strings; any other combination produces a runtime error like `"++ requires strings, got string and number"`. Added `str()` built-in for explicit conversion via `value_format()`. `say()` continues to auto-format any value.

**Files changed:**
- `src/vm.c` — added `native_str()`, registered `str` built-in, replaced `OP_CONCAT` auto-coercion with strict type check
- `tests/test_vm.c` — added 7 `str()` tests, converted 7 coercion tests to error tests, added 3 explicit `str() ++ str()` tests, updated `test_concat_precedence` and `test_fn_concat_coercion`, fixed `test_nested_continue_affects_inner` to use `str()`
- `tests/test_repl.c` — converted 3 REPL coercion tests to error tests
- `tests/test_cli.sh` — converted coercion CLI test to error test, added `str()` CLI test
- `examples/anonymous-functions.cutlet` — use `str()` for number concat
- `examples/function-call.cutlet` — use `str()` for number concat
- `examples/functions.cutlet` — use `str()` for number concat
- `examples/higher-order.cutlet` — use `str()` for number concat
- `examples/single-line.cutlet` — use `str()` for number concat
- `examples/strings.cutlet` — use `str()` for non-string concat
- `examples/while-loop.cutlet` — use `str()` for number concat
