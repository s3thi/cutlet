# TCP REPL Plan (handoff)

Goal: Add a threaded TCP REPL transport that reuses repl_format_line() and the tokenizer output format already in place.

Non-negotiables from AGENTS.md / project rules:
- Write tests first.
- Run `make test` after every code change.
- Comment code with guidance/context for future agents.

## Scope
- New TCP server mode: `cutlet repl --listen HOST:PORT`
- Thread-per-client, shared runtime (even though eval is just token formatting today).
- Line-based protocol with request IDs.
- Core REPL logic remains in `repl_format_line()` (transport is separate).

## Protocol (minimal v0)
- Client sends: `<id> <expr>\n`
- Server responds: `-> <id> OK <formatted tokens>\n` or `-> <id> ERR <line:col message>\n`
- Empty/whitespace expr: `-> <id> OK\n`
- One response per request line; order preserved per connection.

Open protocol decisions to lock in before coding:
- ID format: allow any non-whitespace token? restrict to digits? (Tests should match decision.)
- Max line length / input truncation behavior.
- CRLF handling: accept `\r\n` as line ending.
- Error message specificity (prefix is OK, exact text optional).

## Tests to write first
Prefer a new C test (e.g., `tests/test_repl_server.c`) that speaks sockets directly and avoids external tools.
Use timeouts on sockets to avoid hangs.

### Single client / basic protocol
- `1 foo\n` -> `-> 1 OK [IDENT foo]\n`
- `2 "hi"\n` -> `-> 2 OK [STRING hi]\n`
- Error formatting:
  - `3 "unterminated\n` -> prefix `-> 3 ERR 1:1 `
  - `4 10+10\n` -> prefix `-> 4 ERR 1:1 `
- Whitespace expr:
  - `5 \n` -> `-> 5 OK\n`
  - `6     \n` -> `-> 6 OK\n`
- CRLF:
  - `7 foo\r\n` -> `-> 7 OK [IDENT foo]\n`

### Single connection, multiple requests
- Send 3 lines in one connection; assert 3 responses in order.
- Verify the server does not close connection after a single request.

### Multi-client behavior
- Two clients concurrently:
  - Each sends distinct IDs and payloads.
  - Each receives only its own responses.
  - Responses do not cross sockets.

### Disconnect handling
- Client connects then closes immediately (server should not crash).
- Client sends partial line then closes (server should not hang or crash).

### CLI / lifecycle
- `cutlet repl --listen 127.0.0.1:PORT` accepts connections.
- Invalid listen args produce error and non-zero exit.
- Port already in use produces clear error.
- Optional: support `--listen 127.0.0.1:0` (ephemeral port) and surface chosen port for tests.

## Implementation notes (after tests)
- Keep transport code separate from REPL core (`repl_format_line`).
- Thread-per-client with blocking reads per connection.
- Ensure line parsing is robust (handle CRLF).
- Avoid holding locks during blocking IO (for future runtime locking).

## Suggested file layout (flexible)
- `src/repl_server.c` / `src/repl_server.h` for TCP server.
- Minimal additions to `src/main.c` for CLI flag parsing.

## Acceptance criteria
- All new tests pass.
- `make test` passes.
- Stdin REPL behavior unchanged.
- TCP REPL matches protocol expectations above.
