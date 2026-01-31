# REPL fixes plan (no code yet)

## Goals
- Default `cutlet repl` starts the TCP REPL server (listen mode). There is no local-only eval prompt mode.
- `cutlet repl --connect` is the client that provides the interactive prompt and sends requests to the server.
- Add debug flags `--tokens` and/or `--ast` (can be combined). When enabled on both server and client, the client prints tokenizer and/or AST output **along with** the evaluated result.
- Remove the old AST/token modes and any mode-mismatch behavior. Replace with a single eval path + optional debug output.
- Update tests and `AGENTS.md` to document these debug options for agents.
- Define a minimal, clear protocol for client/server.

## Current state (relevant files)
- CLI + repl entrypoint: `src/main.c`
  - `run_repl()` implements the local stdin/stdout REPL (currently default).
  - `--ast` toggles AST-only output; `--listen` starts server; `--connect` starts client.
- REPL core: `src/repl.c`, `src/repl.h`
  - `repl_format_line()` returns `OK [TYPE value]` (eval output).
  - `repl_format_line_ast()` returns `AST [...]` (AST output).
- REPL server protocol (line-based v0): `src/repl_server.c`, `src/repl_server.h`
- Tests that will need updating:
  - `tests/test_repl.c`
  - `tests/test_runtime.c`
  - `tests/test_repl_server.c`
  - `tests/test_cli.sh`

## Desired CLI behavior
- `cutlet repl` = start server (same as `--listen` with default host/port).
- `cutlet repl --listen [HOST:PORT]` = start server with optional host/port override.
- `cutlet repl --connect [HOST:PORT]` = connect client to server (default host/port if omitted).
- `--tokens` and/or `--ast` are **debug flags** for both server and client.
  - Server only emits debug data when it is started with the corresponding flags.
  - Client prints debug data only when started with the corresponding flags.
  - If client receives debug fields without flags, it **warns once** on stderr and ignores them.

## Proposed protocol (v1, JSON frames)
Rationale: robust, extensible, easy for agents/tools, no line-length issues.

**Framing (LSP-style):**
```
Content-Length: <N>\r\n
\r\n
<JSON bytes>
```
- Server and client both read headers until a blank line, parse `Content-Length`, then read exactly `<N>` bytes.
- All messages are single JSON objects.

**Request schema (from client to server):**
```
{
  "type": "eval",
  "id": <unsigned integer>,
  "expr": "<input line>",
  "want_tokens": true|false,
  "want_ast": true|false
}
```

**Response schema (from server to client):**
```
{
  "type": "result",
  "id": <unsigned integer>,
  "ok": true|false,
  "value": "<evaluated result>",   // only if ok=true
  "error": "<error string>",       // only if ok=false
  "tokens": "<token debug string>",// optional, only if want_tokens && server has --tokens
  "ast": "<ast debug string>"       // optional, only if want_ast && server has --ast
}
```
Notes:
- `value` should be plain eval output (no `OK [...]` wrapper). Use existing `value_format()` for numbers/strings.
- `error` should be the existing error text (e.g., `ERR 1:2 unexpected token` or `ERR division by zero`).
- Tokens and AST fields should be best-effort (see next section).

## Debug output formats
To keep JSON parsing simple, debug fields are **strings** (preformatted by the server):
- Tokens: `TOKENS [TYPE value] [TYPE value] ...`.
  - If tokenization fails, append `ERR line:col message` at the end.
  - Best-effort requirement: include tokens collected up to the error.
- AST: reuse existing `ast_format()` output (`AST [...]`).
  - If parse fails, AST is omitted.

This keeps debug output human-readable and avoids complex JSON arrays. (If you prefer fully structured tokens, update the schema and parser accordingly.)

## Testing strategy (must be followed)
Per `AGENTS.md`:
1) **Before any code changes**, run:
   - `make test`
   - `make check`
   Confirm failures, then **pause and ask for user confirmation** to proceed.
2) **Write/adjust tests first**, then implement code.
3) **After every code change**, re-run `make test` and `make check`.
4) Never delete/disable tests without explicit user confirmation.

## Test updates (tests-first work)
1) `tests/test_repl.c`
   - Update expected outputs to the new eval output (plain values, no `OK [TYPE ...]`).
   - Add tests for debug outputs if new public REPL APIs are introduced (tokens/ast formatting).
   - Add cases for parse errors and eval errors to ensure `error` formatting is stable.

2) `tests/test_runtime.c`
   - Replace `repl_format_line()` / `repl_format_line_ast()` usage with new API (e.g., `repl_eval_line()` or equivalent).
   - Keep the lock-serialization tests; ensure debug generation also uses the eval lock.
   - Update expected result strings (plain values).

3) `tests/test_repl_server.c`
   - Replace the line-based protocol tests with JSON frame tests.
   - Add coverage for:
     - Basic eval response (`ok=true`, `value` set).
     - Parse error (`ok=false`, `error` set).
     - Debug flags: server `--tokens` + client `want_tokens` => `tokens` field present.
     - Debug flags: server `--ast` + client `want_ast` => `ast` field present.
     - Best-effort tokens on tokenizer error.
   - Add minimal helpers for frame send/recv.

4) `tests/test_cli.sh`
   - Remove local stdin REPL expectations (since default is now server).
   - Start server via `cutlet repl` (default) and connect via `cutlet repl --connect` for eval tests.
   - Add debug flag coverage:
     - Start server with `--tokens --ast`, connect with both, verify tokens + AST + value are printed in the expected order.
     - Start server with debug flags, connect without debug flags, verify client warns once on stderr and prints only eval results.
     - Start server without debug flags, connect with debug flags, ensure no debug output appears.

## Implementation plan (after test changes + user confirmation)
1) **Protocol + framing utilities**
   - Add frame read/write helpers (likely in `src/repl_server.c` and `src/main.c`).
   - Implement minimal JSON parse/encode for our schema (strings, booleans, integers).
   - Keep input size limits to avoid unbounded allocations (e.g., cap Content-Length).

2) **REPL core refactor**
   - Replace `repl_format_line()` / `repl_format_line_ast()` with a new API that returns:
     - eval result (value or error)
     - optional tokens string (debug)
     - optional AST string (debug)
   - Ensure this function uses `runtime_eval_lock()` around parse/eval to preserve thread safety.
   - Use tokenizer directly to build tokens string (best-effort) independently from parser errors.
   - Use `parser_parse()` + `ast_format()` for AST output when requested and parse succeeds.

3) **Server changes** (`src/repl_server.c`, `src/repl_server.h`)
   - Replace v0 line protocol with JSON frames (update docs in `src/repl_server.h`).
   - Server stores debug capabilities (`--tokens`, `--ast`) at startup.
   - On each request, emit debug fields only when both server capability **and** request wants them.

4) **Client changes** (`src/main.c`)
   - Parse flags: `--tokens` and `--ast` (order independent).
   - Default `cutlet repl` to server mode; remove or hide local stdin mode.
   - `--connect` uses JSON frames; request includes `want_tokens` / `want_ast`.
   - Client prints response in order:
     1) tokens (if enabled and present)
     2) ast (if enabled and present)
     3) value or error
   - If response includes debug fields but client lacks flags: warn once on stderr, ignore debug fields.

5) **Docs**
   - Update `AGENTS.md` with a short section describing `--tokens` / `--ast` usage for debugging.
   - Update CLI usage text in `src/main.c` comments and `print_usage()` output.

## Open questions / decisions (confirm before coding)
- Confirm token debug string format (recommended: `TOKENS [TYPE value] ...` + `ERR line:col message` on error).
- Confirm that the response should always include `value` or `error` (never both), and that the client prints only one of them.
- Confirm maximum frame size (suggest: reuse 4096 for expr, but allow larger responses).

## Footguns to watch
- Don’t accidentally keep the old local stdin REPL path in `main.c` (default must be server).
- Ensure debug fields are only emitted when both client request and server flag are set.
- Keep warning-once behavior when debug fields appear but client isn’t in debug mode.
- Update all tests that still reference `OK [TYPE ...]` or `AST` mode semantics.

