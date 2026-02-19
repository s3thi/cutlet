# Decimal Number Literals

## Objective

Support decimal number literals like `0.5`, `3.14`, `100.001` in the tokenizer. When done, expressions like `0.5 + 0.5` evaluate to `1` and `say(0.5)` prints `0.5`.

## Background

The tokenizer's `read_number()` (in `src/tokenizer.c`, lines 255-292) currently only consumes ASCII digits. A decimal like `0.5` is split into three tokens: `NUMBER "0"`, `OPERATOR "."`, `NUMBER "5"`, which causes a parse error. The compiler already uses `strtod()` and the VM already uses `double`, so no downstream changes are needed.

## Constraints

- **Only the `digit.digit` form** — at least one digit must precede the dot. `.5` remains invalid (the dot starts an operator).
- **No scientific notation** — `1e10` and `1.5e-3` are deferred to a future task.
- **`..` operator must not break** — `5..10` must still tokenize as `NUMBER "5"`, `OPERATOR ".."`, `NUMBER "10"`. The decimal-consuming logic must look ahead: only consume the `.` if the character after it is a digit (not another `.`).

## Dependencies

None. No dependency on other tasks in `plans/doing/`.

## Non-goals

- Leading-dot decimals (`.5`)
- Trailing-dot decimals (`5.`)
- Scientific notation (`1e10`)
- Changes to the parser, compiler, or VM

## Acceptance criteria

- [ ] `0.5` tokenizes as a single `TOK_NUMBER` with value `"0.5"`
- [ ] `3.14` tokenizes as a single `TOK_NUMBER` with value `"3.14"`
- [ ] `100.001` tokenizes as a single `TOK_NUMBER` with value `"100.001"`
- [ ] `5..10` still tokenizes as `NUMBER "5"`, `OPERATOR ".."`, `NUMBER "10"`
- [ ] `5.` (dot at end of input or followed by non-digit) tokenizes as `NUMBER "5"` then `OPERATOR "."`
- [ ] `5.a` produces an error (number followed by identifier character)
- [ ] `0.5 + 0.5` evaluates to `1`
- [ ] `say(0.5)` prints `0.5`
- [ ] `make test` passes
- [ ] `make check` passes

## Steps

### Step 1: Write tokenizer tests for decimal literals

Add tests to `tests/test_tokenizer.c`:

- `"0.5"` → single `TOK_NUMBER` token with value `"0.5"`.
- `"3.14"` → single `TOK_NUMBER` token with value `"3.14"`.
- `"100.001"` → single `TOK_NUMBER` token with value `"100.001"`.
- `"0.0"` → single `TOK_NUMBER` token with value `"0.0"`.
- `"5..10"` → `NUMBER "5"`, `OPERATOR ".."`, `NUMBER "10"` (no regression).
- `"5."` → `NUMBER "5"`, `OPERATOR "."` (trailing dot is not part of the number).
- `"5.a"` → `TOK_ERROR` (the dot starts a decimal part, but `a` is an ident char after the number — error).

Wait: `5.a` — what should happen? The `.` is followed by `a`, which is not a digit. So `read_number()` should NOT consume the `.` (it only consumes `.` when the next char is a digit). That means `5.a` tokenizes as `NUMBER "5"` followed by `OPERATOR "."` followed by `IDENT "a"`. This is fine — no error needed. Adjust: remove the `5.a` error test case; instead test that `5.a` tokenizes as three tokens.

Corrected test cases for `5.a`:
- `"5.a"` → `NUMBER "5"`, `OPERATOR "."`, `IDENT "a"`.

Also add:
- `"1.2.3"` → `NUMBER "1.2"`, `OPERATOR "."`, `NUMBER "3"` (only the first dot is consumed as part of the number).

Run `make test` — confirm the new tests fail.

**Files touched**: `tests/test_tokenizer.c`.

### Step 2: Write eval/VM integration tests for decimal literals

Add tests to `tests/test_vm.c` (or `tests/test_eval.c`, whichever has the expression evaluation tests):

- `"0.5"` evaluates to `0.5`.
- `"0.5 + 0.5"` evaluates to `1`.
- `"3.14 * 2"` evaluates to `6.28`.
- `"1.0 == 1"` evaluates to `true`.
- `"0.1 + 0.2"` evaluates to `0.30000000000000004` (or whatever `%g` formats it as — verify the expected output matches `value_format` behavior).

Run `make test` — confirm these also fail.

**Files touched**: `tests/test_vm.c` or `tests/test_eval.c`.

### Step 3: Implement decimal support in `read_number()`

Modify `read_number()` in `src/tokenizer.c` (line 263, after the integer-digit loop):

After consuming the initial run of digits, add:

```c
/* Consume optional decimal part: '.' followed by one or more digits.
 * Do NOT consume if the dot is followed by another dot (that's the
 * '..' concat operator) or by a non-digit character. */
if (tok->pos < tok->input_len && tok->input[tok->pos] == '.' &&
    tok->pos + 1 < tok->input_len && is_ascii_digit(tok->input[tok->pos + 1])) {
    tok->pos++;  /* consume '.' */
    tok->col++;
    while (tok->pos < tok->input_len && is_ascii_digit(tok->input[tok->pos])) {
        tok->pos++;
        tok->col++;
    }
}
```

This goes between the integer-digit loop (line 263) and the `value_len` calculation (line 265). The existing ident-start adjacency check (line 268) naturally handles the error case for something like `1.5x`.

Run `make test && make check` — all tests should pass.

**Files touched**: `src/tokenizer.c`.

### Step 4: Verify no regressions in CLI tests

Run `make test && make check` one final time to verify everything passes, including existing CLI tests in `tests/test_cli.sh`.

**Files touched**: none (verification only).

## Required process (every step)

1. Write tests first.
2. Run `make test` and `make check` — confirm new tests fail.
3. **Stop and ask the user for confirmation before implementing.**
4. Implement the feature.
5. Run `make test` and `make check` after every code change.
6. Do not remove or modify existing tests without user confirmation.
