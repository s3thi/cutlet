# Cutlet Plan (Session Handoff)

## Project snapshot
Cutlet is a dynamic programming language (Python/Ruby/Lua/JS‑like; borrows from Raku/Perl/Tcl) written in C23 with no external deps beyond platform libs and POSIX `make`. It targets Linux, macOS, and Windows.

This plan captures decisions and context from the initial design discussion so a new agent can continue seamlessly.

## Core direction
- Build **one feature at a time** with **exhaustive tests** before moving on.
- Favor end‑to‑end slices: each step should run in a REPL and be testable.
- Architecture should remain **thread‑ready**.

## Current behavior (implemented)
### Tokenizer v0 (current)
Token kinds:
- `NUMBER` — integer only, digits only, e.g. `0`, `42`
  - No negative numbers. `-` is an operator token.
  - Only adjacency error: a number immediately followed by an ident-start (`[A-Za-z_]`).
- `STRING` — double‑quoted, **no escapes yet**, e.g. `"hi"`
  - Unterminated string is an error at the opening quote (newline or EOF ends it).
- `IDENT` — **ASCII-only identifiers**
  - **Start chars**: ASCII letter or underscore (`[A-Za-z_]`).
  - **Continue chars**: ASCII letters, digits, underscore (`[A-Za-z0-9_]`).
  - No symbol‑sandwich behavior. Symbols break identifiers.
  - Examples: `foo`, `_foo`, `foo123`, `my_var`, `__init__`
- `OPERATOR` — one or more **symbol chars** (no whitespace delimiter required)
  - **Symbol char**: anything that is not whitespace, not ASCII letter, not digit, not `_`, and not `"`.
  - Includes: `-`, `+`, `*`, `/`, `@`, `#`, `$`, `%`, `&`, `!`, `(`, `)`, etc.
  - Examples: `+`, `-`, `+-*/`, `@`, `==`, `>=`

Token boundaries:
- Tokens **may be adjacent without whitespace** (Python/Ruby style).
- The **only adjacency error** is `NUMBER` immediately followed by ident‑start:
  - Error: `42foo`, `123_`
  - Valid: `10+10`, `"a"b`, `foo"bar"`, `a+b`, `x==y`

Everything else:
- Whitespace is ignored between tokens.
- Non‑ASCII bytes are treated as symbol chars (not valid identifier starts).

Tokenizer API expectations:
- `tokenizer_next(tok, NULL)` is a **defined failure case** and must return `false`.

### REPL/CLI (current)
- REPL core formats results as `OK [TYPE value] ...` or `ERR line:col message`.
- CLI supports stdin REPL plus TCP `--listen` and `--connect`.

## REPL architecture (kept)
- TCP sockets, thread‑per‑client, single shared runtime image.
- REPL core separated from transport.

## Concurrency plan (later)
- Keep lock ordering discipline (global → namespace → object → IO).
- Phase 1: serialize evaluation with a global write lock.

## Networking protocol (minimal)
Line‑based protocol with request IDs (already implemented).

## Implementation staging (progress)
1) Tokenizer REPL on stdin/stdout. ✅
2) Threaded TCP server using the same REPL core. ✅
3) Global RW locks + serialized eval. ⏳
4) Socket protocol + concurrency tests. ⏳

## Next slice (do this after error‑message pinning)
### Minimal parser v0 (single‑token expressions)
Goal: Introduce a **parser + AST** without changing existing tokenizer output.

**Behavior**
- Parse exactly **one token expression**: `NUMBER`, `STRING`, `IDENT`, or `OPERATOR`.
- Require **EOF** after the single expression; extra tokens are a parse error.
- Tokenizer errors propagate as parse errors (same line/col and message).

**AST representation (v0)**
- `AST_NUMBER`, `AST_STRING`, `AST_IDENT`, `AST_OPERATOR` with value string.
- New API entrypoint in `src/parser.h`:
  - `bool parser_parse_single(const char *input, AstNode **out, ParseError *err)`
  - `void ast_free(AstNode *node)`
- `ParseError` should include `line`, `col`, and `message` (mirrors tokenizer error messages).

**AST printer**
- New function: `char *ast_format(const AstNode *node)`
- Suggested format (simple & stable):
  - `AST [NUMBER 42]`
  - `AST [STRING hi]`
  - `AST [IDENT foo]`
  - `AST [OPERATOR +]`

**CLI / REPL integration**
- Add `cutlet repl --ast` mode that uses parser + AST printer.
- Default `cutlet repl` stays token‑based output.

**Tests (must be written first)**
- `tests/test_parser.c`:
  - Single token success for NUMBER/STRING/IDENT/OPERATOR.
  - Extra token error (e.g., `\"foo bar\"`).
  - Tokenizer error passthrough (`\"unterminated`).
- `tests/test_repl.c` (or new `tests/test_repl_ast.c`):
  - `--ast` output matches exact format.

**Required process**
- Add tests first.
- Run `make test` and `make check` to prove failures.
- Pause for confirmation before implementation.
- Run `make test` and `make check` after every code change.

## Open follow-ups (tracking)
- Pin down tokenizer error wording/positions (see `TOK_ERROR.md`).
- Decide on Windows socket abstraction strategy.
- Define locking and serialization approach for eval.

---
End of handoff.
