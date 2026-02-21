# REPL and CLI

**Status:** Done

- **Local REPL**: Default mode (`cutlet repl`). Isocline for rich line editing and multiline input. History persistence (`~/.cutlet/history`). `parser_is_complete()` drives continuation prompts.
- **TCP mode**: Server (`--listen`, thread-per-client) and client (`--connect`). LSP-style JSON framing with request IDs. nREPL-style multi-frame responses: `say()` sends output frames before the terminal result frame.
- **File execution**: `cutlet run <file>` reads and evaluates a `.cutlet` file. Output via `say()` only (final expression not printed). Exit code 0 on success, 1 on error.
- **Debug flags**: `--tokens`, `--ast`, `--bytecode` show pipeline stages before the result.
- **Shared formatting**: `print_repl_result()` helper for both local and TCP modes.
