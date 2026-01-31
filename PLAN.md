# Cutlet Plan (Session Handoff)

## Project snapshot
Cutlet is a dynamic programming language (Python/Ruby/Lua/JS‑like; borrows from Raku/Perl/Tcl) written in C23 with no external deps beyond platform libs and POSIX `make`. It targets Linux, macOS, and Windows.

This plan captures decisions and context from the initial design discussion so a new agent can continue seamlessly.

## Core direction
- Build **one feature at a time** with **exhaustive tests** before moving on.
- Favor end‑to‑end slices: each step should run in a REPL and be testable.
- Architecture should remain **thread‑ready**.

## Current behavior (implemented)
### Tokenizer v0.1
- NUMBER (digits only), STRING (double‑quoted, no escapes), IDENT (ASCII only), OPERATOR (symbol runs).
- Solo symbols (`(`, `)`, `+`, `-`, `/`, `,`) always emitted as single-char tokens; other symbols group.
- Tokens may be adjacent without whitespace; only adjacency error is NUMBER followed by ident‑start.
- Whitespace ignored; non‑ASCII bytes are treated as symbols.

### Parser v1 (Pratt expression parser)
- Full expression parsing with precedence climbing (Pratt parser).
- Binary operators: `+`, `-` (prec 1, left), `*`, `/` (prec 2, left), `**` (prec 4, right).
- Unary minus (prec 3, prefix).
- Parenthesized grouping.
- AST kinds: NUMBER/STRING/IDENT/BINOP/UNARY.
- Format: nested S-expr `AST [BINOP + [NUMBER 1] [NUMBER 2]]`.

### Evaluator v1
- Evaluates AST trees to values (VAL_NUMBER, VAL_STRING, VAL_ERROR).
- All arithmetic in double precision; division always float.
- Integers format without decimal (`8`), floats with minimal decimals (`8.4`).
- Unknown identifiers and division by zero produce errors.

### REPL/CLI
- `repl` token mode: parse → eval → `OK [TYPE value]` or `ERR message`.
- `repl --ast` AST mode: parse → format AST tree.
- TCP server is thread‑per‑client with a shared runtime image.

### Error‑message pinning (tests)
- Tokenizer, parser, and REPL errors are pinned with exact strings/positions in tests.
- Server‑side errors are partially pinned (prefix checks) and can be tightened later.

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
3) Minimal parser + AST + `--ast` REPL mode. ✅
4) Global RW locks + serialized eval. ✅ (tests and hooks added; lock hierarchy comments in runtime)
5) Socket protocol + concurrency tests. ⏳ (deferred; see guardrail)
6) Expression evaluation (precedence/parentheses). ✅
7) Variables + assignment. ✅

## Guardrail while deferring protocol tests
- Do **not** modify TCP server/transport code while implementing language features.
- Keep wire format, REPL formatting, and AST printing behavior unchanged.
- Revisit protocol tests after expressions + variables land, or earlier if server code needs changes.

## Open follow-ups (tracking)
- Tighten server‑side error pinning to exact strings/positions (optional).

## Deferred slice: Socket protocol + concurrency tests (Phase 2)
Goal: lock in the wire protocol behavior and prove correct behavior under multi‑client concurrency.

### Scope
- Tests must define the **line‑based request/response format with request IDs** as currently implemented.
- The server must correctly handle multiple requests per connection, partial reads, and error responses with IDs.
- Concurrency: multiple clients can issue requests in parallel while evaluation remains serialized by the global lock.

### Tests (write first, exhaustive)
- Protocol framing (IDs, ordering).
- Partial read buffering (newline‑terminated requests only).
- Error wire format pinned to IDs.
- Multi‑client concurrency with no cross‑contamination; reuse lock hooks to assert no overlapping eval.

### Implementation notes
- Keep transport logic centralized; route through `repl_format_line()` / `repl_format_line_ast()`.
- Add test‑only server hooks if needed (guarded by test macro).

### Required process
1) Add tests first.
2) Run `make test` and `make check` to prove failures.
3) Pause for confirmation before implementing.
4) Implement protocol fixes / server buffering / test hooks.
5) Run `make test` and `make check` after every code change.

## Next slice: Expression evaluation (Phase 2a)
Goal: turn the parser into a real expression parser with evaluation.

### Scope
- Arithmetic operators: `+ - * /` with standard precedence and left‑associativity.
- Exponentiation: `**` with higher precedence than `* /` and **right‑associativity**.
- Unary minus for numeric literals/expressions (binds tighter than `* /` but looser than `**` if `-2 ** 2` is parsed as `-(2 ** 2)`).
- Parentheses for grouping.
- Literals: NUMBER and STRING.
- Identifiers allowed as expressions but **error on unknown variable** (until variables land).
- Preserve existing AST printing output for the old single‑token paths where possible.

### Tests (write first, exhaustive)
- Parser precedence: `1 + 2 * 3` parses as `1 + (2 * 3)`.
- Parentheses: `(1 + 2) * 3`.
- Exponent precedence/associativity: `2 ** 3 ** 2` parses as `2 ** (3 ** 2)`.
- Unary minus: `-3 * 2` and `-(1 + 2)` parse/evaluate correctly.
- Unary minus vs exponent: `-2 ** 2` should parse as `-(2 ** 2)` (unless explicitly decided otherwise and tests updated).
- Evaluation results for numeric expressions.
- Error pinning for syntax errors and unknown identifier.

### Implementation notes
- Add expression grammar (Pratt or precedence climbing).
- Introduce a value type in runtime/eval (number/string).
- Keep evaluation entrypoints behind the global write lock already in place.

### Required process
1) Add tests first.
2) Run `make test` and `make check` to prove failures.
3) Pause for confirmation before implementing.
4) Implement parser + evaluator.
5) Run `make test` and `make check` after every code change.

## Completed: Variables + assignment (Phase 2b)
Goal: add persistent state across REPL lines and sockets.

### Scope (delivered)
- Assignment syntax (`name = expr`) and identifier lookup.
- Declaration syntax (`my name = expr`) with right-associative chains.
- Environment lives in the shared runtime image; respects global write lock.
- AST output includes `DECL`/`ASSIGN` nodes with pinned error messages.

---
End of handoff.
