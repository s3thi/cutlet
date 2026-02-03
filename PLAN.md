# Cutlet Plan (Session Handoff)

## Project snapshot
Cutlet is a dynamic programming language (Python/Ruby/Lua/JS-like) written in C23 with no external deps beyond platform libs and POSIX `make`. Targets Linux and macOS.

See `AGENTS.md` for project conventions and instructions that must be followed.

## What exists today (all complete + tested)

- **Tokenizer**: NUMBER, STRING, IDENT, OPERATOR, EOF, ERROR tokens. Solo symbols `( ) + - / ,` always single-char.
- **Pratt parser**: Precedence climbing. `or` (prec 1) → `and` (prec 2) → `not` (prec 3, prefix) → comparison (prec 4, non-assoc) → `+ -` (prec 5) → `* /` (prec 6) → unary minus (prec 7) → `**` (prec 8, right). Parenthesized grouping. `=` assignment and `my` declaration (prec 0, right).
- **AST nodes**: NUMBER, STRING, IDENT, BOOL, NOTHING, BINOP, UNARY, DECL, ASSIGN. S-expr format output.
- **Evaluator**: Tree-walking. Produces VAL_NUMBER, VAL_STRING, VAL_BOOL, VAL_NOTHING, or VAL_ERROR. All arithmetic in double precision.
- **Booleans**: `true`/`false` keywords → `AST_BOOL` → `VAL_BOOL`.
- **Nothing**: `nothing` keyword → `AST_NOTHING` → `VAL_NOTHING`. Falsy. `nothing == nothing` is true; `nothing == anything_else` is false. Ordered comparisons with nothing produce errors.
- **Comparison operators**: `==`, `!=`, `<`, `>`, `<=`, `>=`. All return VAL_BOOL. Non-associative (no chaining). Mixed-type equality allowed; mixed-type ordering is an error.
- **Logical operators**: `and`, `or` (keyword infix, short-circuit, Python semantics — return operand values), `not` (keyword prefix, returns VAL_BOOL). Truthiness: `false`, `nothing`, `0`, `""`, errors are falsy.
- **Runtime**: Global pthread rwlock serializes eval. Linked-list variable environment with thread-safe get/define/assign.
- **REPL/CLI**: TCP server (thread-per-client), TCP client. `--tokens` and `--ast` debug flags. LSP-style JSON framing with request IDs.
- **Tests**: Exhaustive C test suites for tokenizer, parser, eval, runtime, REPL client, REPL server. Integration tests in `test_cli.sh`. Sanitizer builds via `make test-sanitize`.

## Key files

| File | Purpose |
|------|---------|
| `src/tokenizer.c/h` | Lexer |
| `src/parser.c/h` | Pratt parser, AST types |
| `src/eval.c/h` | Evaluator, Value types |
| `src/runtime.c/h` | Global lock, variable environment |
| `src/main.c` | CLI entry, TCP server/client |
| `src/repl.c/h` | REPL client |
| `src/repl_server.c/h` | REPL server |
| `src/json.c/h` | JSON protocol framing |
| `tests/test_*.c` | Unit tests |
| `tests/test_cli.sh` | Integration tests |

## Next slice: Multi-line input and if/else expressions

### Goal

Add newline-separated multi-statement input and if/else expressions. After this slice, the following should work:

```
my counter = 10
my winner = if counter > 8 then
  "me"
else
  "you"
end
```

### Step 1: Nothing literal and VAL_NOTHING ✓ COMPLETE

Added `nothing` keyword, `AST_NOTHING` node type, and `VAL_NOTHING` value type. Nothing is needed because else-less `if` expressions must evaluate to something when the condition is false.

**Implementation summary**:
- `nothing` tokenizes as `TOK_IDENT`, parser recognizes it as keyword
- `AST_NOTHING` in parser.h, `VAL_NOTHING` in eval.h
- `nothing` is falsy in `is_truthy()`
- `nothing == nothing` → true; `nothing == anything_else` → false
- Ordered comparisons with nothing produce error: "cannot compare nothing with <type>"
- REPL output format: `OK nothing`
- AST output: `AST [NOTHING nothing]`

### Step 2: Multi-line input (newlines as statement separators) ✓ COMPLETE

**Implementation summary**:
- **Tokenizer**: Added `TOK_NEWLINE` token type. `\n`, `\r`, and `\r\n` each emit a single `TOK_NEWLINE` token. Spaces and tabs remain whitespace (skipped).
- **Parser**: Added `AST_BLOCK` node type with `children` array and `child_count`. `parser_parse()` collects newline-separated expressions. Single expressions are unwrapped (no block). Leading/trailing/consecutive newlines are skipped.
- **Evaluator**: `AST_BLOCK` evaluates each child in order, returns value of last. Variables from earlier statements visible in later ones (no new scope).
- **AST format**: `AST [BLOCK [child1] [child2] ...]`

**Tests added**:
- Tokenizer: 9 new tests for TOK_NEWLINE (LF, CR, CRLF, multiple, leading, trailing, etc.)
- Parser: 11 new tests for AST_BLOCK (two/three statements, single unwrap, blank lines, declarations, etc.)
- Eval: 8 new tests for block evaluation (returns last, decl-then-use, reassign, comparisons, etc.)
- Updated 4 existing tokenizer tests that assumed newlines were whitespace

### Step 3: If/else expressions

If/else is an expression (it evaluates to a value). Syntax: `if condition then body else body end`. The `else` clause is optional; if omitted and condition is false, the expression evaluates to nothing.

**Keywords**: `if`, `then`, `else`, `end` are all reserved (cannot be used as variable names).

**Parser**:
- Add `AST_IF` node type with three children: condition, then-body, else-body (nullable).
- Parse in `parse_atom()` when current token is the `if` keyword:
  1. Consume `if`.
  2. Parse condition expression (stop before `then`).
  3. Expect and consume `then`.
  4. Parse body as a block of newline-separated expressions (stop at `else` or `end`).
  5. If `else`, consume it and parse else-body as a block (stop at `end`).
  6. Expect and consume `end`.
- If no else clause, else-body child is NULL.

**Evaluator**:
- Evaluate condition.
- If truthy → evaluate then-body, return its value.
- If falsy → evaluate else-body if present, otherwise return nothing.
- Only evaluate the taken branch (like short-circuit).

**AST format**: `(if cond then-body else-body)` or `(if cond then-body)` when no else.

**Tests to write first**:
- `if true then 1 else 2 end` → 1
- `if false then 1 else 2 end` → 2
- `if false then 1 end` → nothing
- `if 1 < 2 then "yes" else "no" end` → "yes"
- `my x = if true then 42 else 0 end` then `x` → 42
- Multi-line bodies: `"if true then\n  my x = 1\n  x + 1\nelse\n  0\nend"` → 2
- Nested: `if true then if false then 1 else 2 end else 3 end` → 2
- Missing `then` → parse error
- Missing `end` → parse error
- `if = 1` → error, `my then = 1` → error (reserved keywords)
- AST output for if/else expressions
- Side effects only in taken branch: `"if false then\n  my x = 1\nend\nx"` → error (x not defined)

### Required process (every step)

1. Write tests first.
2. Run `make test` and `make check` — confirm new tests fail.
3. **Stop and ask the user for confirmation before implementing.**
4. Implement the feature.
5. Run `make test` and `make check` after every code change.
6. Do not remove or modify existing tests without user confirmation.

---
End of handoff.
