# Unify Block Delimiters: `do...end` for Control Flow, `is...end` for Definitions

## Objective

Replace the `then` keyword with `do` in conditionals so that all control flow
uses `do...end` and all definitions use `is...end`. Remove `then` as a reserved
keyword entirely.

**Before:**

```cutlet
if x > 5 then say("big") end      # conditionals use then...end
while i < 10 do i = i + 1 end     # loops use do...end
fn double(x) is x * 2 end         # functions use is...end
```

**After:**

```cutlet
if x > 5 do say("big") end        # conditionals use do...end
while i < 10 do i = i + 1 end     # loops use do...end (unchanged)
fn double(x) is x * 2 end         # functions use is...end (unchanged)
```

**Done looks like:** `make test && make check` pass, all examples produce
correct output, `then` is no longer a keyword, and every `if` conditional in
the codebase uses `do`.

---

## Design Rationale

| Delimiter | Semantic role | Constructs (current) | Constructs (future) |
|-----------|--------------|---------------------|---------------------|
| `do...end` | "execute this code" (control flow) | `if`, `while` | `for`, `match` |
| `is...end` | "this is defined as" (definitions) | `fn` | `class`, `enum`, `module` |

The `then` keyword is removed entirely — it becomes a plain identifier that
users can use as a variable or function name.

---

## Acceptance Criteria

- [ ] `if cond do body end` parses and compiles correctly (multi-line and single-line)
- [ ] `if cond do body else body end` works
- [ ] `else if cond do body end` chains work (with single `end`)
- [ ] Single-line form: `if cond do expr` (no `end`) works
- [ ] `then` is NOT a reserved keyword — `my then = 42` is valid
- [ ] `while cond do body end` still works (unchanged)
- [ ] `fn name(params) is body end` still works (unchanged)
- [ ] REPL multi-line detection works for incomplete `if` expressions
- [ ] All existing tests pass after updating `then` → `do`
- [ ] All example programs produce correct output
- [ ] `make test && make check` pass

---

## Dependencies

- None. The `arrays` plan in `plans/doing/arrays.md` references `then` in
  its examples, so this plan updates those examples as part of step 7.

---

## Constraints and Non-Goals

- **No behavioral changes.** The AST, bytecode, and runtime behavior are
  identical. Only the keyword that opens an `if` body changes.
- **No new features.** This is purely a syntax migration.
- **Don't touch `while` or `fn` parsing.** They already use `do` and `is`
  respectively.
- **Don't touch vendor/ files.** The word "then" in vendor code is unrelated.

---

## Implementation Steps

### Step 1: Write new tests for `do`-based conditionals

Add tests that exercise the new `if...do...end` syntax. These tests will fail
before the parser is updated.

**In `tests/test_parser.c`:**

- Add a test that parses `if true do 1 end` and expects the same AST as
  today's `if true then 1 end` (`[IF [BOOL true] [NUMBER 1]]`).
- Add a test that parses `if true do 1 else 2 end` → correct AST.
- Add a test that `my then = 42` parses successfully (variable named `then`).

**In `tests/test_eval.c` (or `tests/test_vm.c`):**

- Add an eval test: `if true do 1 else 2 end` → `1`.
- Add an eval test: `if false do 1 else 2 end` → `2`.

Run `make test` — confirm these new tests fail.

**Files touched:** `tests/test_parser.c`, `tests/test_vm.c` (or whichever
file has eval tests).

---

### Step 2: Update parser source code

**In `src/parser.c`:**

1. **`is_reserved_keyword` function:** Remove the
   `token_is_keyword(t, "then")` check. `then` is no longer reserved.

2. **`parse_if` function:** Change every `token_is_keyword(&..., "then")`
   call to `token_is_keyword(&..., "do")`. There are two:
   - The guard that checks for missing condition
     (`token_is_keyword(&p->current, "then")` → `"do"`)
   - The expectation after the condition
     (`token_is_keyword(&p->current, "then")` → `"do"`)

3. **Error messages in `parse_if`:** Update all error strings:
   - `"expected 'then' after condition"` → `"expected 'do' after condition"`
   - `"expected expression in 'then' body"` → `"expected expression in 'if' body"`
     (Use `'if'` instead of `'do'` since `while` bodies already say `'do' body`
     and distinguishing them aids debugging.)

4. **`expression_stops_at_keyword` helper** (the lookahead in `parse_decl`
   around the `!token_is_keyword(&next, "then")` check): Remove the
   `!token_is_keyword(&next, "then")` clause. The `!token_is_keyword(&next, "do")`
   clause already covers it.

5. **`parser_is_complete` function:** The `strstr(msg, "expected 'then'")`
   check becomes dead code since the error message no longer contains `"then"`.
   Remove that block. The existing `strstr(msg, "expected 'do'")` check
   already handles both `while` and `if` incompleteness. Update the comment
   that says `"expected 'then'" at EOF` accordingly.

6. **Comments:** Update any comments in `parse_if` that reference `then`
   (e.g., `"Parse condition - stops at 'then' keyword"` → `"stops at 'do'"`).

Run `make test` — the new tests from step 1 should now pass. Some existing
tests will fail because they still use `then` in their input strings.

**Files touched:** `src/parser.c`

---

### Step 3: Update C test files

In every C test file, replace `then` with `do` in cutlet source strings that
are parsed or evaluated. Be careful to only change cutlet syntax strings, not
C code or English prose.

Affected files (grep for `then` in test input strings):

- `tests/test_parser.c` (~85 occurrences)
- `tests/test_vm.c` (~40 occurrences)
- `tests/test_compiler.c` (~5 occurrences)
- `tests/test_chunk.c` (~1 occurrence)
- `tests/test_cli.sh` (~69 occurrences)
- `tests/test_repl.c` (~3 occurrences)
- `tests/test_repl_server.c` (~5 occurrences)
- `tests/test_tokenizer.c` (~3 occurrences — only in comments/strings, the
  tokenizer doesn't special-case `then`)
- `tests/test_examples.sh` (~3 occurrences — check if these are cutlet code
  or English)

**Strategy:** In each file, search for the pattern ` then ` and `then\n`
within string literals. Replace with ` do ` and `do\n` respectively. Also
handle `"then"` when it appears as an expected keyword/token value. Be careful
not to change:
- English comments ("then" in prose)
- The word "then" in `test_json.c` (unrelated JSON test data)

Run `make test && make check` after updating each file to catch mistakes
early.

**Files touched:** All test files listed above.

---

### Step 4: Update example `.cutlet` files

Replace `then` with `do` in all example programs:

- `examples/if-else.cutlet`
- `examples/functions.cutlet`
- `examples/break-continue.cutlet`
- `examples/single-line.cutlet`

Then regenerate the corresponding `.expected` files by running each example
through the interpreter:

```
./build/cutlet run examples/if-else.cutlet > examples/if-else.expected
./build/cutlet run examples/functions.cutlet > examples/functions.expected
./build/cutlet run examples/break-continue.cutlet > examples/break-continue.expected
./build/cutlet run examples/single-line.cutlet > examples/single-line.expected
```

Diff each `.expected` file against its previous version — the output should be
**identical** (since this is a syntax-only change, not a behavior change). If
any output differs, investigate.

Run `make test-examples` to confirm all examples pass.

**Files touched:** `examples/*.cutlet`, `examples/*.expected`

---

### Step 5: Update `src/parser.h` comment

The `AST_IF` comment in the `AstNodeType` enum in `src/parser.h` says:
`/* if expression: if cond then body [else body] end */`

Change to:
`/* if expression: if cond do body [else body] end */`

**Files touched:** `src/parser.h`

---

### Step 6: Update TUTORIAL.md

Search `TUTORIAL.md` for all occurrences of `then` in cutlet code blocks and
syntax descriptions. Replace with `do`. Update any prose that explains the
`then` keyword (e.g., "use `then` to open the body of an `if`" → "use `do`").

**Files touched:** `TUTORIAL.md`

---

### Step 7: Update `plans/doing/arrays.md` and other references

The arrays plan contains examples like:
```
fn max(a, b) is if a > b then a else b end end
```

Replace `then` with `do` in all cutlet code examples within `plans/doing/arrays.md`.

Also check `AGENTS.md` for any cutlet syntax examples containing `then` and
update them.

**Files touched:** `plans/doing/arrays.md`, `AGENTS.md` (if applicable)

---

### Step 8: Final verification

Run the full test suite and linter:

```
make test && make check
```

Verify:
- All parser tests pass
- All eval/VM tests pass
- All compiler tests pass
- All CLI tests pass
- All example tests pass
- All REPL tests pass
- Linter and formatter are clean

Also manually verify in the REPL that multi-line `if` works:
```
cutlet repl
> if true do
...   1
... end
1
```

**Files touched:** None (verification only).

---

## Required Process (every step)

1. Write tests first.
2. Run `make test` and `make check` — confirm new tests fail.
3. **Stop and ask the user for confirmation before implementing.**
4. Implement the feature.
5. Run `make test` and `make check` after every code change.
6. Do not remove or modify existing tests without user confirmation.
