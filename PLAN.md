# Cutlet Plan (Session Handoff)

## Project snapshot
Cutlet is a dynamic programming language (Python/Ruby/Lua/JS-like) written in C23 with no external deps beyond platform libs and POSIX `make`. Targets Linux and macOS.

See `AGENTS.md` for project conventions and instructions that must be followed.

## What exists today (all complete + tested)

- **Tokenizer**: NUMBER, STRING, IDENT, OPERATOR, EOF, ERROR tokens. Solo symbols `( ) + - / ,` always single-char. Adjacent tokens without whitespace allowed.
- **Pratt parser**: Precedence climbing. Binary `+ -` (prec 1, left), `* /` (prec 2, left), `**` (prec 4, right). Unary minus (prec 3, prefix). Parenthesized grouping. `=` assignment and `my` declaration (prec 0, right).
- **AST nodes**: NUMBER, STRING, IDENT, BOOL, BINOP, UNARY, DECL, ASSIGN. S-expr format output.
- **Evaluator**: Walks AST, produces VAL_NUMBER, VAL_STRING, VAL_BOOL, or VAL_ERROR. All arithmetic in double precision. Division always float. Integers format without decimal.
- **Booleans**: `true` and `false` are keywords that produce `AST_BOOL` nodes and evaluate to `VAL_BOOL`. Cannot be used as variable names. REPL output: `OK [BOOL true]` / `OK [BOOL false]`.
- **Runtime**: Global pthread rwlock serializes eval. Linked-list variable environment with thread-safe get/define/assign.
- **REPL/CLI**: TCP server (thread-per-client), TCP client. `--tokens` and `--ast` debug flags. LSP-style JSON framing with request IDs.
- **Tests**: Exhaustive C test suites for tokenizer, parser, eval, runtime, REPL client, REPL server (protocol + concurrency). Integration tests in `test_cli.sh`. Sanitizer builds via `make test-sanitize`.

## Key files

| File | Purpose |
|------|---------|
| `src/tokenizer.c/h` | Lexer |
| `src/parser.c/h` | Pratt parser, AST types |
| `src/eval.c/h` | Evaluator, Value types (VAL_NUMBER, VAL_STRING, VAL_BOOL, VAL_ERROR) |
| `src/runtime.c/h` | Global lock, variable environment |
| `src/main.c` | CLI entry, TCP server/client |
| `src/repl.c/h` | REPL client |
| `src/repl_server.c/h` | REPL server |
| `src/json.c/h` | JSON protocol framing |
| `tests/test_*.c` | Unit tests |
| `tests/test_cli.sh` | Integration tests |

## Next slice: Booleans, comparison operators, and logical operators

### Goal
Add boolean values, comparison operators, and logical operators to the language.

### Step 1: Boolean literals and VAL_BOOL ✅

Implemented. `true`/`false` produce `AST_BOOL` nodes, evaluate to `VAL_BOOL`, and are rejected as assignment/declaration targets. REPL outputs `OK [BOOL true]` / `OK [BOOL false]`. 18 tests added across parser, eval, and REPL test suites.

### Step 2: Comparison operators ✅

Add `==`, `!=`, `<`, `>`, `<=`, `>=`. All return VAL_BOOL.

**Tokenizer**: `==`, `!=`, `<=`, `>=` are two-char operators. The tokenizer currently groups symbol runs, so `==` should already tokenize as a single OPERATOR token. Verify this works and add tests. `<` and `>` are single-char — they need to be added to the solo-symbol list or handled correctly as operators.

**Parser**: New precedence level below `+ -`. Suggested: prec 0.5 (or renumber). Comparison operators should be **non-associative** (i.e., `1 < 2 < 3` is a parse error, not silently wrong). This is a deliberate design choice — chained comparisons can be added later.

**Evaluator**:
- Number == Number: exact double comparison.
- String == String: strcmp.
- Mixed types: `==` returns false, `!=` returns true, ordered comparisons (`< > <= >=`) on mixed types are errors.
- Bool == Bool: supported. Ordered comparisons on bools are errors.

**Tests to write first**:
- `1 == 1` → true, `1 == 2` → false
- `"a" == "a"` → true, `"a" == "b"` → false
- `1 != 2` → true, `1 != 1` → false
- `1 < 2` → true, `2 < 1` → false, `1 < 1` → false
- `1 > 2` → false, `2 > 1` → true
- `1 <= 1` → true, `1 <= 2` → true, `2 <= 1` → false
- `1 >= 1` → true, `2 >= 1` → true, `1 >= 2` → false
- `"a" < "b"` → true (lexicographic)
- `1 == "1"` → false (mixed types)
- `1 < "1"` → error (ordered comparison on mixed types)
- `true == true` → true, `true == false` → false
- `true < false` → error
- `1 < 2 < 3` → parse error (non-associative)
- Precedence: `1 + 2 == 3` → true (comparison binds looser than arithmetic)
- AST output for comparison expressions

### Step 3: Logical operators (next)

Add `and`, `or`, `not`. These are keyword-based (not `&&`, `||`, `!`).

**Tokenizer**: `and`, `or`, `not` tokenize as TOK_IDENT. The parser must recognize them as keywords.

**Parser**:
- `not` is a unary prefix operator. Precedence: below comparisons, above `and`/`or`. Or bind tighter than comparisons — agent should follow Python's model where `not` binds looser than comparisons: `not 1 < 2` means `not (1 < 2)`.
- `or` has lowest precedence (below `and`).
- `and` has precedence above `or` but below `not`.
- Suggested precedence order (low to high): assignment → `or` → `and` → `not` → comparison → `+ -` → `* /` → unary minus → `**`.

**Evaluator**:
- `and` and `or` are **short-circuit**: `false and expr` does not evaluate `expr`.
- Truthiness: `false` is falsy, `true` is truthy, `0` is falsy, all other numbers are truthy, all strings are truthy, errors are falsy.
- `and` returns the first falsy operand or the last operand. `or` returns the first truthy operand or the last operand. (Python semantics — they return operand values, not necessarily bools.)
- `not` always returns a VAL_BOOL.

**Tests to write first**:
- `true and true` → true, `true and false` → false
- `false and true` → false (short-circuit: returns false without evaluating right)
- `true or false` → true, `false or true` → true
- `false or false` → false
- `not true` → false, `not false` → true
- `not 0` → true, `not 1` → false, `not ""` — decide truthiness of empty string
- `1 and 2` → 2 (returns last truthy operand)
- `0 and 2` → 0 (returns first falsy operand)
- `0 or 2` → 2 (returns first truthy operand)
- `0 or false` → false (returns last falsy operand)
- Precedence: `true or true and false` → true (`and` binds tighter)
- Precedence: `not true and false` → `false` (if Python model: `(not true) and false`)
- `not 1 < 2` → false (`not (1 < 2)` in Python model)
- Short-circuit with side effects: `false and (my x = 1)` — `x` should not be defined
- AST output for logical expressions
- `and`, `or`, `not` should not be assignable: `and = 1` → error

### Required process (every step)

1. Write tests first.
2. Run `make test` and `make check` — confirm new tests fail.
3. **Stop and ask the user for confirmation before implementing.**
4. Implement the feature.
5. Run `make test` and `make check` after every code change.
6. Do not remove or modify existing tests without user confirmation.

---
End of handoff.
