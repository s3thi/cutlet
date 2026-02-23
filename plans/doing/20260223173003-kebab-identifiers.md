# Kebab-case identifiers

## Objective

Allow dashes (`-`) inside identifier names, following Raku's disambiguation
rules.  When done, users can write `my compute-sum = fn(list-of-nums) ...`
and the tokenizer produces a single `TOK_IDENT` for each dashed name.  The
rest of the pipeline (parser, compiler, VM, runtime) already treats
identifiers as opaque strings, so no changes are needed outside the
tokenizer and its tests.

## Design (Raku rules)

A `-` is part of an identifier **only** when:

1. It appears immediately after identifier-continue characters (no space
   before).
2. The character immediately after the `-` is an ASCII letter (not a digit,
   not `_`, not another `-`, not whitespace/EOF).

Consequences:

| Input          | Tokens                                  |
|----------------|-----------------------------------------|
| `foo-bar`      | `IDENT("foo-bar")`                      |
| `foo-bar-baz`  | `IDENT("foo-bar-baz")`                  |
| `foo - bar`    | `IDENT("foo")  OP("-")  IDENT("bar")`   |
| `foo -bar`     | `IDENT("foo")  OP("-")  IDENT("bar")`   |
| `foo- bar`     | `IDENT("foo")  OP("-")  IDENT("bar")`   |
| `foo-3`        | `IDENT("foo")  OP("-")  NUMBER("3")`    |
| `foo-_bar`     | `IDENT("foo")  OP("-")  IDENT("_bar")`  |
| `foo--bar`     | `IDENT("foo")  OP("-")  OP("-")  IDENT("bar")` |
| `-foo`         | `OP("-")  IDENT("foo")`                 |
| `not-empty`    | `IDENT("not-empty")` (not keyword+minus)|
| `if-thing`     | `IDENT("if-thing")` (not keyword+minus) |

`my-var` and `my_var` are **distinct** identifiers.

Apostrophes are **not** supported (only dashes).

## Acceptance criteria

- [ ] `make test && make check` pass
- [ ] Tokenizer emits a single `TOK_IDENT` for `foo-bar`, `foo-bar-baz`,
      `not-empty`, `if-thing`, etc.
- [ ] Tokenizer emits separate tokens for `foo - bar`, `foo-3`, `foo--bar`,
      `foo-_bar`, `-foo`, `foo-`
- [ ] Variables with dashed names can be declared, assigned, and read
- [ ] Functions with dashed names can be defined, called, and passed around
- [ ] Function parameters with dashed names work correctly
- [ ] Closures can capture dashed-name variables
- [ ] Dashed identifiers and underscored identifiers are distinct
      (`my-x` and `my_x` are different variables)

## Dependencies

None.  This feature touches only the tokenizer and tests; no overlap with
the plans currently in `doing/` (fuzzing, arrays, return-keyword).

## Constraints / non-goals

- No changes to parser, compiler, VM, or runtime — identifiers are already
  opaque strings throughout the pipeline.
- No apostrophe support (can be added later with the same mechanism).
- No canonicalization of `-` and `_`.
- This is a breaking change: `not-empty` (previously `not` minus `empty`)
  now parses as a single identifier.  Per AGENTS.md, backward compatibility
  is not a concern.

---

## Steps

### Step 1 — Tokenizer unit tests

Add tests to `tests/test_tokenizer.c`.  Place them near the existing
identifier tests (after `test_ident_with_underscores`).

Tests to add (each is a separate test function):

| Test name                            | Input           | Expected tokens                                            |
|--------------------------------------|-----------------|------------------------------------------------------------|
| `test_ident_with_dash`               | `foo-bar`       | `IDENT("foo-bar")`                                         |
| `test_ident_with_multiple_dashes`    | `foo-bar-baz`   | `IDENT("foo-bar-baz")`                                     |
| `test_ident_dash_not_before_digit`   | `foo-3`         | `IDENT("foo")  OP("-")  NUMBER("3")`                       |
| `test_ident_dash_not_before_underscore` | `foo-_bar`   | `IDENT("foo")  OP("-")  IDENT("_bar")`                     |
| `test_ident_double_dash`             | `foo--bar`      | `IDENT("foo")  OP("-")  OP("-")  IDENT("bar")`             |
| `test_ident_dash_with_spaces`        | `foo - bar`     | `IDENT("foo")  OP("-")  IDENT("bar")`                      |
| `test_ident_leading_dash`            | `-foo`          | `OP("-")  IDENT("foo")`                                    |
| `test_ident_trailing_dash`           | `foo-`          | `IDENT("foo")  OP("-")`                                    |
| `test_ident_keyword_prefix_dash`     | `not-empty`     | `IDENT("not-empty")`                                       |
| `test_ident_keyword_if_dash`         | `if-thing`      | `IDENT("if-thing")`                                        |
| `test_ident_left_space_dash`         | `foo -bar`      | `IDENT("foo")  OP("-")  IDENT("bar")`                      |

Register each test in the test suite's `main()` function.

### Step 2 — Integration tests (parser + compiler + VM)

Add integration tests to the appropriate VM-level test file that exercises
dashed identifiers end-to-end.  Use the existing `eval_and_check` /
`eval_and_compare` helpers (or whatever the VM-level test harness provides).

Tests to add:

1. **Variable declaration & read**: `my compute-sum = 42; compute-sum`
   → result is `42`
2. **Variable assignment**: `my x-val = 1; x-val = 2; x-val` → `2`
3. **Distinct from underscore**: `my x-y = 10; my x_y = 20; x-y` → `10`
4. **Function definition & call**: `fn add-one(n) n + 1 end; add-one(5)`
   → `6`
5. **Dashed parameter names**: `fn f(my-param) my-param * 2 end; f(3)` → `6`
6. **Closure capture of dashed var**: `my outer-val = 99; my f = fn() outer-val end; f()` → `99`
7. **Dashed name in expression with subtraction**:
   `my foo-bar = 10; foo-bar - 3` → `7`
   (Tests that `foo-bar` is one ident and `- 3` is subtraction due to space.)

### Step 3 — Run tests, confirm failures

Run `make test && make check`.  Every new test from Steps 1–2 should fail
(the tokenizer does not yet recognise dashes in identifiers).  Confirm the
failures, then **pause for user confirmation** before proceeding to
implementation.

### Step 4 — Implement: modify `read_ident()` in `src/tokenizer.c`

In the `read_ident()` function, change the identifier-continue loop to also
consume a `-` when it is immediately followed by an ASCII letter.

Current logic (pseudocode):

```
consume ident-start
while is_ident_continue(current): consume
```

New logic:

```
consume ident-start
loop:
    while is_ident_continue(current): consume
    if current == '-' and next char is ASCII letter:
        consume '-'
        continue loop            // next iteration will consume the letter
    break
```

This keeps the change minimal and localised.  The `-` is only consumed
when the look-ahead confirms an ASCII letter follows, so all disambiguation
rules are enforced at the tokenizer level.

Also update the file-level comment at the top of `tokenizer.c` (the line
describing `IDENT`) to reflect the new pattern:

```
IDENT: [A-Za-z_][A-Za-z0-9_]*(-[A-Za-z][A-Za-z0-9_]*)*
```

And update the comment above `read_ident()` or add a comment near the
new code explaining the Raku-style dash rule.

### Step 5 — Run tests, confirm everything passes

Run `make test && make check`.  All tests (old and new) must pass.  If any
fail, fix the implementation before proceeding.

### Step 6 — Remind user about docs and example

Per the language-feature checklist in AGENTS.md, remind the user to:

- Update `TUTORIAL.md` to cover kebab-case identifiers.
- Add an example program in `examples/` (e.g., `examples/kebab_idents.cutlet`
  with a corresponding `.expected` file).
