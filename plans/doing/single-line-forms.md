# Single-Line Forms for `if`, `while`, and `fn`

**Status:** Pending

## Objective

Allow `if`, `while`, and `fn` to omit the closing `end` keyword when the body is a single expression on the **same line** as the opening keyword (`then`, `do`, or `is`).

"Done" means all three constructs support single-line forms, all existing tests still pass (backward compatible), and new tests cover the feature exhaustively.

## Design

### Same-line rule

After consuming the keyword (`then` / `do` / `is`) via `advance(p)`, inspect `p->current`:

- **Not a newline** → **single-line mode**. Parse exactly one expression as the body. `end` is accepted but not required.
- **Newline** → **multi-line mode** (existing behavior). Call `skip_newlines(p)`, collect expressions until `end`. `end` is mandatory.

This is unambiguous because the tokenizer emits `TOK_NEWLINE` tokens, and checking for one immediately after the keyword cleanly separates the two modes.

### Syntax examples

```
# Single-line if (no else) — returns nothing when false
if x % 2 == 0 then say("even")

# Single-line if/else
if x % 2 == 0 then "even" else "odd"

# Single-line else-if chain
if x > 0 then "pos" else if x < 0 then "neg" else "zero"

# Single-line while
while x < 10 do x = x + 1

# Single-line fn (named and anonymous)
fn double(x) is x * 2
fn(x) is x ** 2

# end is still accepted in single-line mode (backward compatible)
if true then 1 else 2 end
fn(x) is x * 2 end
```

### What changes

- **Parser only.** The AST nodes produced are identical to the multi-line forms.
- **No compiler or VM changes.** The compiler already handles the same AST shapes.
- Three functions in `src/parser.c` need modification: `parse_if`, `parse_while`, `parse_fn`.

### Dangling else

`else` always binds to the nearest `if`, consistent with most languages:

```
if a then if b then 1 else 2
# else belongs to inner if. Outer if has no else.
# Equivalent to: if a then (if b then 1 else 2 end) end

# To bind else to outer if, use explicit end:
if a then if b then 1 end else 2
```

## Acceptance criteria

- [ ] Single-line `if` without `else` parses and runs correctly (evaluates to `nothing` when false)
- [ ] Single-line `if`/`else` parses and runs correctly
- [ ] Single-line `else if` chains work without `end`
- [ ] Single-line `while` parses and runs correctly
- [ ] Single-line `fn` (named and anonymous) parses and runs correctly
- [ ] Optional `end` is accepted in all single-line forms (backward compatibility)
- [ ] Multi-line forms (with newline after keyword) still require `end` (no regression)
- [ ] All existing tests pass unchanged
- [ ] Nested single-line forms work (e.g., `if a then if b then 1`)
- [ ] Single-line forms work in expression position (e.g., `1 + if true then 2 else 3`)
- [ ] Single-line `fn` works inside function call arguments (e.g., `apply(fn(x) is x + 1, 42)`)
- [ ] Error messages are clear for malformed single-line forms
- [ ] `make test && make check` pass
- [ ] Example `.cutlet` and `.expected` files are added

## Dependencies

None. No other tasks in `plans/doing/` overlap with parser control-flow changes.

## Non-goals

- Postfix conditionals (e.g., `break if condition`) — different feature, out of scope.
- Semicolons or statement separators for multi-statement single-line bodies — out of scope.
- Changes to the compiler or VM.

## Steps

### 1. Write parser tests for single-line `if`

Add tests to `tests/test_parser.c` covering:

- `if true then 1` → `AST [IF [BOOL true] [NUMBER 1]]` (no else, no `end`)
- `if true then 1 else 2` → `AST [IF [BOOL true] [NUMBER 1] [NUMBER 2]]` (no `end`)
- `if false then 1 else if true then 2 else 3` → nested `AST_IF` (else-if chain, no `end`)
- `if true then 1 end` → same AST as multi-line (optional `end` accepted)
- `if true then 1 else 2 end` → same AST as multi-line (optional `end` accepted)
- `if a then if b then 1 else 2` → dangling else binds to inner if
- `if a then if b then 1 end else 2` → explicit `end` makes else bind to outer if
- Expression position: `1 + if true then 2 else 3` → `AST [BINOP + ...]`
- `if true then continue` and `if true then break` (loop-control bodies)
- Verify multi-line form still requires `end` (no regression)

Register all new tests in `tests/test_parser.c` test runner.

### 2. Write parser tests for single-line `while` and `fn`

Add tests to `tests/test_parser.c` covering:

**While:**
- `while true do 1` → `AST [WHILE [BOOL true] [NUMBER 1]]` (no `end`)
- `while true do 1 end` → same AST (optional `end`)
- `while x < 10 do x = x + 1` → assignment in body
- Verify multi-line while still requires `end`

**Fn (named and anonymous):**
- `fn double(x) is x * 2` → `AST [FN double(x) [BINOP * ...]]` (no `end`)
- `fn(x) is x ** 2` → anonymous, no `end`
- `fn foo() is 42 end` → optional `end` accepted
- `my f = fn(x) is x + 1` → in assignment context
- Verify multi-line fn still requires `end`

Register all new tests.

### 3. Write VM integration tests

Add tests to `tests/test_vm.c` covering runtime behavior:

- `if true then 42` evaluates to `42`
- `if false then 42` evaluates to `nothing`
- `if true then 1 else 2` evaluates to `1`; swap condition to test `2`
- `my x = 0\nwhile x < 5 do x = x + 1\nx` → `x` is `5`
- Single-line while with `break`: `while true do break 42` evaluates to `42`
- `fn double(x) is x * 2\ndouble(5)` evaluates to `10`
- `my f = fn(x) is x + 1\nf(10)` evaluates to `11`
- Nested: `if true then if false then 1 else 2` evaluates to `2`

Register all new tests.

### 4. Verify tests fail

Run `make test`. All new tests should fail (the parser currently requires `end`). Confirm failures before implementing.

### 5. Implement single-line `if` in `parse_if()`

In `parse_if` in `src/parser.c`, after consuming the `then` keyword (the `advance(p)` call right after the `then` check), **do not** call `skip_newlines(p)` unconditionally. Instead:

1. Check `p->current.type != TOK_NEWLINE` — if true, enter single-line mode.
2. **Single-line then-body:** Parse one expression via `parse_assignment(p)`. Do not loop for additional expressions.
3. **Single-line else:** If next token is `else` keyword, consume it. If followed by `if`, recursively call `parse_if()` as today. Otherwise parse one expression via `parse_assignment(p)`.
4. **Optional end:** If next token is `end` keyword, consume it. Otherwise, the `if` construct is complete.
5. If `p->current.type == TOK_NEWLINE` after `then`, call `skip_newlines(p)` and fall through to existing multi-line logic (unchanged).

Build the same `AST_IF` node regardless of which path was taken.

### 6. Implement single-line `while` in `parse_while()`

Same pattern as step 5, but in `parse_while` after consuming `do`:

1. Check `p->current.type != TOK_NEWLINE` — single-line mode.
2. Parse one expression as body.
3. If next token is `end`, consume it. Otherwise, the `while` construct is complete.
4. If newline after `do`, fall through to existing multi-line logic.

### 7. Implement single-line `fn` in `parse_fn()`

Same pattern in `parse_fn` after consuming `is`:

1. Check `p->current.type != TOK_NEWLINE` — single-line mode.
2. Parse one expression as body.
3. If next token is `end`, consume it. Otherwise, the `fn` construct is complete.
4. If newline after `is`, fall through to existing multi-line logic.

### 8. Run `make test && make check`

All tests (old and new) must pass. All linter checks must pass. Fix any issues.

### 9. Add example programs

Create `examples/single-line.cutlet` demonstrating the feature with `say()` output. Generate its `.expected` file by running `./build/cutlet run examples/single-line.cutlet > examples/single-line.expected`.

## Constraints

- The tokenizer and compiler are **not modified**. All changes are in `src/parser.c` and test files.
- Existing code with `end` must continue to work identically — this is purely additive syntax sugar.
- When tests fail, do NOT proceed to implementation without user confirmation (per `AGENTS.md`).
