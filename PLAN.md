# Cutlet Plan (Session Handoff)

## Project snapshot
Cutlet is a dynamic programming language (Python/Ruby/Lua/JS-like) written in C23 with no external deps beyond platform libs and POSIX `make`. Targets Linux and macOS.

See `AGENTS.md` for project conventions and instructions that must be followed.

## What exists today (all complete + tested)

- **Tokenizer**: NUMBER, STRING, IDENT, OPERATOR, EOF, ERROR tokens. Solo symbols `( ) + - / ,` always single-char.
- **Pratt parser**: Precedence climbing. `or` (prec 1) → `and` (prec 2) → `not` (prec 3, prefix) → comparison (prec 4, non-assoc) → `+ -` (prec 5) → `* /` (prec 6) → unary minus (prec 7) → `**` (prec 8, right). Parenthesized grouping. `=` assignment and `my` declaration (prec 0, right).
- **AST nodes**: NUMBER, STRING, IDENT, BOOL, NOTHING, BINOP, UNARY, DECL, ASSIGN, BLOCK, IF. S-expr format output.
- **Evaluator**: Tree-walking. Produces VAL_NUMBER, VAL_STRING, VAL_BOOL, VAL_NOTHING, or VAL_ERROR. All arithmetic in double precision.
- **Booleans**: `true`/`false` keywords → `AST_BOOL` → `VAL_BOOL`.
- **Nothing**: `nothing` keyword → `AST_NOTHING` → `VAL_NOTHING`. Falsy. `nothing == nothing` is true; `nothing == anything_else` is false. Ordered comparisons with nothing produce errors.
- **Comparison operators**: `==`, `!=`, `<`, `>`, `<=`, `>=`. All return VAL_BOOL. Non-associative (no chaining). Mixed-type equality allowed; mixed-type ordering is an error.
- **Logical operators**: `and`, `or` (keyword infix, short-circuit, Python semantics — return operand values), `not` (keyword prefix, returns VAL_BOOL). Truthiness: `false`, `nothing`, `0`, `""`, errors are falsy.
- **Multi-line input**: `TOK_NEWLINE` token, `AST_BLOCK` for multiple statements, newlines as separators.
- **If/else expressions**: `if cond then body [else body] end`. Expression form (returns value). `else if` special case (single `end`). Only taken branch evaluated.
- **Runtime**: Global pthread rwlock serializes eval. Linked-list variable environment with thread-safe get/define/assign.
- **REPL/CLI**: TCP server (thread-per-client), TCP client with isocline for rich line editing and multiline input. `--tokens` and `--ast` debug flags. LSP-style JSON framing with request IDs.
- **Isocline integration**: REPL client uses isocline for interactive input with line editing, history persistence (`~/.cutlet/history`), and multiline expression accumulation. Uses `parser_is_complete()` to detect when to show continuation prompt (`"    ... "`).
- **Tests**: Exhaustive C test suites for tokenizer, parser, eval, runtime, REPL client, REPL server. Integration tests in `test_cli.sh`. Sanitizer builds via `make test-sanitize`.

## Key files

| File | Purpose |
|------|---------|
| `src/tokenizer.c/h` | Lexer |
| `src/parser.c/h` | Pratt parser, AST types, `parser_is_complete()` |
| `src/eval.c/h` | Evaluator, Value types |
| `src/runtime.c/h` | Global lock, variable environment |
| `src/main.c` | CLI entry, TCP server/client with isocline |
| `src/repl.c/h` | REPL client |
| `src/repl_server.c/h` | REPL server |
| `src/json.c/h` | JSON protocol framing |
| `vendor/isocline/` | Isocline library (v1.0.9) for line editing |
| `tests/test_*.c` | Unit tests |
| `tests/test_cli.sh` | Integration tests |

## Completed: Multi-line input and if/else expressions ✓

### Step 1: Nothing literal ✓
Added `nothing` keyword → `AST_NOTHING` → `VAL_NOTHING`. Falsy. `nothing == nothing` is true. Ordered comparisons with nothing produce errors.

### Step 2: Multi-line input ✓
Added `TOK_NEWLINE` token. Parser creates `AST_BLOCK` for newline-separated statements. Evaluator runs children in order, returns last value.

### Step 3: If/else expressions ✓
Syntax: `if cond then body [else body] end`. Expression form (returns value). `else if` requires single `end`. Only taken branch evaluated. No-else returns `nothing`.

---

## Completed: Isocline integration for multiline REPL input ✓

### What was implemented

- **Vendored isocline v1.0.9** in `vendor/isocline/` with original directory structure
- **Added `parser_is_complete()` API** to detect incomplete expressions (unclosed if/end, unmatched parens, unterminated strings, trailing operators)
- **Replaced `fgets()` with isocline** in the REPL client for interactive input
- **Multiline expression accumulation**: Lines are accumulated until `parser_is_complete()` returns true
- **Continuation prompt**: `"    ... "` shown for incomplete expressions
- **History persistence**: Saved to `~/.cutlet/history` (directory created automatically)
- **Pipe mode preserved**: Non-interactive input (pipes, files) still works line-by-line

### Example session

```
cutlet> my x = if true then
    ...   "hello"
    ... else
    ...   "goodbye"
    ... end
hello
cutlet> x
hello
```

---

## Required process (every step)

1. Write tests first.
2. Run `make test` and `make check` — confirm new tests fail.
3. **Stop and ask the user for confirmation before implementing.**
4. Implement the feature.
5. Run `make test` and `make check` after every code change.
6. Do not remove or modify existing tests without user confirmation.

---
End of handoff.
