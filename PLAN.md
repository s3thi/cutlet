# Cutlet Plan (Session Handoff)

## Project snapshot
Cutlet is a dynamic programming language (Python/Ruby/Lua/JS‑like; borrows from Raku/Perl/Tcl) written in C23 with no external deps beyond platform libs and POSIX `make`. It is optimized for REPL‑driven programming and excels at text parsing, filesystem navigation, IPC, job control, and quick UI scripting. Targets Linux, macOS, Windows.

This plan captures decisions and context from the initial design discussion so a new agent can continue seamlessly.

## Core direction
- Build **one feature at a time** with **exhaustive tests** before moving on.
- Favor end‑to‑end slices: each step should run in a REPL and be testable.
- Early architecture should be **thread‑ready** even if full parallel eval comes later.

## Very first feature (minimal slice)
### Minimal tokenizer v0
Token kinds:
- `NUMBER` — integer only, digits only, e.g. `0`, `42`
  - No negative numbers. `-` is an operator or symbol char.
  - Numbers must be followed by whitespace or EOF.
- `STRING` — double‑quoted, **no escapes yet**, e.g. `"hi"`
  - Strings must be followed by whitespace or EOF.
- `IDENT` — **ASCII-only identifiers** with symbol sandwiching
  - **Start chars**: ASCII letter only (`a-z`, `A-Z`).
  - **Continue chars**: ASCII letters and digits freely.
  - **Symbol sandwich**: when a symbol char is encountered during identifier scanning:
    - The previous char must have been an ASCII letter (not a digit).
    - The entire run of symbol chars is consumed.
    - The next char must be an ASCII letter (not digit, not whitespace, not EOF).
    - If either check fails, it is an error.
  - Examples: `hello+world`, `my_var_name`, `kebab-case`, `a-b-c`, `hello_-_world`
  - Errors: `a1_b` (symbol after digit), `ab_12` (symbol before digit), `foo-` (trailing symbol)
- `OPERATOR` — symbol chars delimited by whitespace (or start/end of input)
  - **Symbol char**: anything that is not whitespace, not ASCII letter, not digit, not `"`.
  - Includes: `_`, `-`, `+`, `*`, `/`, `@`, `#`, `$`, `%`, `&`, `!`, etc.
  - A run of one or more symbol chars preceded by whitespace/SOI and followed by whitespace/EOI.
  - Examples: `+`, `-`, `+-*/`, `@`, `_`

Token boundaries:
- **Tokens must be separated by whitespace or EOF.** Adjacent tokens without whitespace are **errors**.
  - Examples: `"a"foo` is an error; `42foo` is an error; `10+10` is an error.
- Start-of-input (SOI) and end-of-input (EOI) count as whitespace for operator delimiting.

Everything else:
- whitespace is ignored between tokens
- Non-ASCII bytes are treated as symbol chars (not valid identifier starts)

### Tokenizer API expectations (v0)
- `tokenizer_next(tok, NULL)` is a **defined failure case** and must return `false`.

### First observable behavior
A REPL that reads a line, tokenizes it, and prints a one‑line list of tokens or a precise error with position.

## REPL architecture decision (important)
We want **multiple clients** to connect to a **single running REPL** (Clojure/Common Lisp style).

**Decision:** Use **TCP sockets** (not UNIX) and **thread‑per‑client** from the start.
- Clients run in parallel.
- Runtime state is **shared globally** (single image), not isolated per client.
- **No enqueueing** of lines; each client thread reads and evaluates directly.

**Why still split REPL core vs transport?**
Even if it's one binary, keep a separation of concerns:
- REPL core = parse/eval/print on strings and result/err
- Transport = stdin/stdout or TCP socket
Benefits: easier tests, reusable engine, simpler debugging, multiple frontends.
Implementation can still be a single executable with flags:
- `cutlet repl` (stdin/stdout)
- `cutlet repl --listen 127.0.0.1:5555`

## Concurrency and locking plan (thread‑ready)
### Lock ordering (to avoid deadlocks)
Always acquire in this order:
1) Global runtime lock
2) Namespace / module lock
3) Object lock
4) IO lock

Never acquire in reverse order.

### Phase 1 (safe, simple)
- **Threaded TCP server** is in place.
- **Evaluation is serialized** with a **global write lock** to ensure correctness.
- All shared structures protected with real RW locks, even if used coarsely.

### Phase 2 (parallel eval later)
- Parse without locks.
- Acquire **read** locks for read‑only eval.
- Upgrade to **write** only for mutations.
- Introduce per‑object / per‑collection locks to reduce contention.

### Initial lock map
- **Global environment / bindings**: RW lock
  - Reads = shared
  - Defines/sets = exclusive
- **Symbol / intern table**: RW lock
  - Lookup = shared
  - Intern new symbol = exclusive
- **Type registry / method tables**: RW lock
  - Lookup = shared
  - Define new type/method = exclusive
- **Module cache / loader state**: RW lock
- **GC / allocator**: start with **single global mutex** or thread‑safe allocator
- **IO / job control / child registry**: separate IO lock

### Rules of thumb
- Never hold write locks while doing blocking IO.
- Keep lock scope minimal; release before calling external processes.
- All public runtime entry points must acquire locks (even if callers "should" have already).

## Networking protocol (minimal)
Line‑based protocol per client (no enqueueing):
- Client sends a line.
- Server responds with a line (or multiline with a clear terminator).
- Include a request id so responses can be matched safely when clients are async.

Example:
```
<id> <expr>\n
-> <id> OK <result>\n
-> <id> ERR <message>\n
```

## Implementation staging (suggested)
1) **Tokenizer REPL** on stdin/stdout.
2) **Threaded TCP server** that plugs into the same REPL core.
3) **Global RW locks** + serialized eval (global write lock).
4) Tests for tokenizer + socket protocol + concurrency correctness.

## Open follow-ups for next session
- Token output format chosen:
  - Success: `OK [IDENT foo] [NUMBER 42] [STRING hi] [OPERATOR +]`
  - Error: `ERR 1:5 message`
  - Blank/whitespace-only lines should return `OK` with an empty list.
- Define error messages (positioning, line/col vs index).
- Decide on Windows socket abstraction strategy early.
- Decide on request/response framing and error handling.

---
End of handoff.
