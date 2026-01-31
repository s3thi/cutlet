# Cutlet Plan (Session Handoff)

## Project snapshot
Cutlet is a dynamic programming language (Python/Ruby/Lua/JS‑like; borrows from Raku/Perl/Tcl) written in C23 with no external deps beyond platform libs and POSIX `make`. It targets Linux, macOS, and Windows.

This plan captures decisions and context from the initial design discussion so a new agent can continue seamlessly.

## Core direction
- Build **one feature at a time** with **exhaustive tests** before moving on.
- Favor end‑to‑end slices: each step should run in a REPL and be testable.
- Architecture should remain **thread‑ready**.

## Current behavior (implemented)
### Tokenizer v0
- NUMBER (digits only), STRING (double‑quoted, no escapes), IDENT (ASCII only), OPERATOR (symbol runs).
- Tokens may be adjacent without whitespace; only adjacency error is NUMBER followed by ident‑start.
- Whitespace ignored; non‑ASCII bytes are treated as symbols.

### Parser v0
- Single‑token expressions only; operators are parse errors; extra tokens after first are errors.
- AST kinds: NUMBER/STRING/IDENT/OPERATOR with stable `AST [TYPE value]` format.

### REPL/CLI
- `repl` token mode and `repl --ast` AST mode.
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
4) Global RW locks + serialized eval. ✅
5) Socket protocol + concurrency tests. ⏳

## Next slice: Global RW lock + serialized eval (Phase 1)
Goal: serialize evaluation in the shared runtime so concurrent REPL clients do not evaluate in parallel.

### Design
- Introduce a minimal runtime module with a global `pthread_rwlock_t` (write‑lock only for now).
- All evaluation entrypoints take the **write** lock for the full duration of processing:
  - `repl_format_line()`
  - `repl_format_line_ast()`
  - TCP server request handling should flow through these functions (no bypass).
- Keep API thread‑ready: lock API should be reusable for future read locks.
- Add clear comments on lock ordering and future expansion.
- Future‑proofing: treat the global lock as the **top** of a lock hierarchy so we can later add
  namespace/object locks and read‑locks without reworking call sites.

### Tests (write first)
- New test that **proves serialization** across threads:
  - Add test‑only hooks in runtime (e.g., `runtime_test_on_lock_enter/exit`) guarded by a compile‑time test macro.
  - Spawn 2+ threads calling `repl_format_line()`/`repl_format_line_ast()`; hooks assert no overlap (e.g., atomic in‑critical counter).
  - Ensure test fails without locking and passes with locking.
- Add a REPL server test if feasible to validate multi‑client requests remain correct under concurrency.

### Required process
1) Add tests first.
2) Run `make test` and `make check` to prove failures.
3) Pause for confirmation before implementing.
4) Implement runtime lock + integration.
5) Run `make test` and `make check` after every code change.

## Open follow-ups (tracking)
- Tighten server‑side error pinning to exact strings/positions (optional).
- Decide on Windows socket abstraction strategy.
- Define Windows‑friendly RW‑lock abstraction (pthread vs platform shim).

---
End of handoff.
