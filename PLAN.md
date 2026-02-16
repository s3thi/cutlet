# Cutlet Plan (Session Handoff)

## Project snapshot
Cutlet is a dynamic programming language (Python/Ruby/Lua/JS-like) written in C23 with no external deps beyond platform libs and POSIX `make`. Targets Linux and macOS.

See `AGENTS.md` for project conventions and instructions that must be followed.

## What exists today (all complete + tested)

- **Tokenizer**: NUMBER, STRING, IDENT, OPERATOR, EOF, ERROR tokens. Solo symbols `( ) + - / % ,` always single-char. `#` line comments.
- **Pratt parser**: Precedence climbing. `or` (prec 1) → `and` (prec 2) → `not` (prec 3, prefix) → comparison (prec 4, non-assoc) → `..` (prec 5, right) → `+ -` (prec 6) → `* / %` (prec 7) → unary minus (prec 8) → `**` (prec 9, right). Parenthesized grouping. `=` assignment and `my` declaration (prec 0, right).
- **AST nodes**: NUMBER, STRING, IDENT, BOOL, NOTHING, BINOP, UNARY, DECL, ASSIGN, BLOCK, IF, CALL, WHILE, BREAK, CONTINUE. S-expr format output.
- **Bytecode compiler + VM**: Single-pass compiler emits bytecode into Chunks. Stack-based VM executes opcodes. Produces VAL_NUMBER, VAL_STRING, VAL_BOOL, VAL_NOTHING, or VAL_ERROR. All arithmetic in double precision. `EvalContext` with write callback enables built-in functions like `say()` to stream output.
- **Modulo operator**: `%` at precedence 7 (same as `*` and `/`), left-associative. Python/Ruby-style semantics: result has the sign of the divisor (`a % b = a - b * floor(a / b)`). Division by zero produces `"modulo by zero"` error.
- **String concatenation**: `..` operator at precedence 5 (between comparison and `+`/`-`), right-associative. Auto-coerces both operands to strings via `value_format()`. `"hello" .. " world"` → `"hello world"`, `"score: " .. 42` → `"score: 42"`. `+` with strings remains an error.
- **While loops**: `while cond do body end` expression. Evaluates body repeatedly while condition is truthy. Returns last body value, or `nothing` if loop never runs. Expression form (can be used in assignment). Uses `OP_LOOP` opcode for backward jumps. Supports `break [expr]` to exit the loop (with optional value) and `continue` to skip to the next iteration.
- **Built-in functions**: `say(expr)` — prints formatted value + newline via `EvalContext` write callback. Returns `nothing`. Wrong arity and unknown function names produce runtime errors.
- **Booleans**: `true`/`false` keywords → `AST_BOOL` → `VAL_BOOL`.
- **Nothing**: `nothing` keyword → `AST_NOTHING` → `VAL_NOTHING`. Falsy. `nothing == nothing` is true; ordered comparisons with nothing produce errors.
- **Comparison operators**: `==`, `!=`, `<`, `>`, `<=`, `>=`. Return VAL_BOOL. Non-associative. Mixed-type equality allowed; mixed-type ordering is an error.
- **Logical operators**: `and`, `or` (keyword infix, short-circuit, Python semantics — return operand values), `not` (keyword prefix, returns VAL_BOOL). Truthiness: `false`, `nothing`, `0`, `""`, errors are falsy.
- **Multi-line input**: `TOK_NEWLINE` token, `AST_BLOCK` for newline-separated statements. Evaluator runs children in order, returns last value.
- **If/else expressions**: `if cond then body [else body] end`. Expression form (returns value). `else if` special case (single `end`). Only taken branch evaluated. No-else returns `nothing`.
- **Variables**: `my x = expr` declares, `x = expr` assigns. Linked-list environment with thread-safe get/define/assign.
- **Runtime**: Global pthread rwlock serializes eval.
- **REPL/CLI**: Local in-process REPL as default mode (`cutlet repl`). TCP server (`--listen`, thread-per-client) and TCP client (`--connect`) with isocline for rich line editing and multiline input. `--tokens` and `--ast` debug flags. Shared `print_repl_result()` formatting helper for both local and TCP modes. LSP-style JSON framing with request IDs for TCP mode. nREPL-style multi-frame responses: `say()` sends output frames (`{"type": "output", ...}`) before the terminal result frame (`{"type": "result", ...}`). Client reads frames in a loop. History persistence (`~/.cutlet/history`). `parser_is_complete()` drives continuation prompts and multiline accumulation (both interactive and pipe modes).
- **File execution**: `cutlet run <file>` reads and evaluates a `.cutlet` file. Output via `say()` only (final expression not printed). Exit code 0 on success, 1 on error.
- **Comments**: `#` to end of line.
- **Function calls**: `name(arg1, arg2, ...)` syntax parsed as `AST_CALL`. Zero or more comma-separated arguments. Parsed as postfix after identifier.
- **Tests**: Exhaustive C test suites for tokenizer, parser, eval, runtime, REPL client, REPL server, ptr_array, JSON. Integration tests in `test_cli.sh`. Sanitizer builds via `make test-sanitize`. All REPL tests use `repl_eval_line()` (no legacy wrappers).
- **Documentation**: `TUTORIAL.md` — learnxinyminutes-style tutorial covering all features. `AGENTS.md` — includes language feature checklist reminding agents to prompt users to update tutorial and examples.

## Key files

| File | Purpose |
|------|---------|
| `src/tokenizer.c/h` | Lexer |
| `src/parser.c/h` | Pratt parser, AST types, `parser_is_complete()` |
| `src/compiler.c/h` | Single-pass bytecode compiler (AST → Chunk) |
| `src/chunk.c/h` | Bytecode chunk: opcodes, constants pool, disassembler |
| `src/vm.c/h` | Stack-based bytecode VM |
| `src/value.c/h` | Value types (`VAL_NUMBER`, `VAL_STRING`, etc.), `EvalContext` |
| `src/runtime.c/h` | Global lock, variable environment |
| `src/main.c` | CLI entry, local REPL, TCP server/client with isocline, `run` subcommand |
| `src/repl.c/h` | REPL core (`repl_eval_line()`) |
| `src/repl_server.c/h` | REPL server |
| `src/json.c/h` | JSON protocol framing (result + output frames) |
| `src/ptr_array.c/h` | Dynamic pointer array utility |
| `vendor/isocline/` | Isocline library (v1.0.9) for line editing |
| `tests/test_*.c` | Unit tests |
| `tests/test_cli.sh` | Integration tests |
| `TUTORIAL.md` | Language tutorial (learnxinyminutes style) |

---

## Deferred: Example programs and example test runner

Create example `.cutlet` programs in `examples/` with matching `.expected` files, and a test that runs `cutlet run` on each and compares stdout. Integrate into `make test`. Deferred until the language has more features (e.g. loops).

---

## Required process (every step)

1. Write tests first.
2. Run `make test` and `make check` — confirm new tests fail.
3. **Stop and ask the user for confirmation before implementing.**
4. Implement the feature.
5. Run `make test` and `make check` after every code change.
6. Do not remove or modify existing tests without user confirmation.

---

## Completed: String concatenation with `..` operator

Added `..` binary operator for string concatenation with auto-coercion. Right-associative at precedence 5 (between comparison and `+`/`-`). Both operands coerced to strings via `value_format()`. `+` with strings remains an error.

**Files touched**: `src/parser.c` (precedence renumbering 5→6, 6→7, 7→8, 8→9; `..` at prec 5; `is_right_assoc`; unary minus prec 7→8), `src/chunk.h` (`OP_CONCAT`), `src/chunk.c` (disassembler), `src/compiler.c` (`".."` → `OP_CONCAT`), `src/vm.c` (`OP_CONCAT` implementation), `tests/test_tokenizer.c` (3 tests), `tests/test_parser.c` (6 tests), `tests/test_vm.c` (14 tests), `tests/test_repl.c` (5 tests), `tests/test_cli.sh` (3 tests).

---

## Completed: Isolate vendor code from project build flags and code-quality tools

Isocline is now compiled as a separate `.o` with upstream-recommended C99 flags (`ISOCLINE_BUILD_CFLAGS` and `ISOCLINE_SANITIZE_BUILD_CFLAGS`) instead of the project's C23 flags. `ISOCLINE_CFLAGS` renamed to `ISOCLINE_INCLUDES`. `vendor/.clang-tidy` and `vendor/.clang-format` sentinel files prevent IDE linting/formatting of vendor code. Added `-Wno-shorten-64-to-32` to suppress an upstream narrowing bug in `term.c:1036` on 64-bit macOS.

**Files touched**: `Makefile` (renamed `ISOCLINE_CFLAGS` → `ISOCLINE_INCLUDES`, added `ISOCLINE_BUILD_CFLAGS`/`ISOCLINE_SANITIZE_BUILD_CFLAGS`, separate isocline `.o` compile rules, updated `$(BIN)` and `$(SANITIZE_BIN)` to link `.o`), `vendor/.clang-tidy` (new), `vendor/.clang-format` (new).

---

## Completed: While loop expression (`while...do...end`)

Added `while cond do body end` as a loop expression. The loop evaluates its body repeatedly while `cond` is truthy. Returns the last value produced by the body, or `nothing` if the loop body never executes. Uses an accumulator-based bytecode pattern with a new `OP_LOOP` backward jump opcode.

**Files touched**: `src/parser.h` (`AST_WHILE`), `src/parser.c` (`parse_while()`, `is_reserved_keyword()` for `while`/`do`, `ast_node_type_str()`, `ast_format_node()`, `parser_is_complete()`), `src/chunk.h` (`OP_LOOP`), `src/chunk.c` (disassembler + `opcode_name()`), `src/compiler.c` (`emit_loop()` helper, `compile_while()`, dispatch), `src/vm.c` (`OP_LOOP` execution), `tests/test_parser.c` (17 tests), `tests/test_vm.c` (7 tests), `tests/test_repl.c` (3 tests), `tests/test_cli.sh` (3 tests).

---

## Completed: `break` and `continue` for while loops

Added `break` and `continue` keywords that control loop iteration. `break` exits the innermost loop immediately, with an optional value (`break expr` or bare `break` → nothing). `continue` skips to the next iteration, setting the accumulator to nothing. Both produce compile errors outside loops. The parser peeks at the next token to determine whether `break` has a value expression.

**Files touched**: `src/parser.h` (`AST_BREAK`, `AST_CONTINUE` in enum), `src/parser.c` (`parse_atom()` for break/continue, `is_reserved_keyword()`, `ast_node_type_str()`, `ast_format_node()`), `src/compiler.c` (`LoopContext` struct, `compile_break()`, `compile_continue()`, updated `compile_while()` with break jump patching, `Compiler` struct gets `current_loop` field, `compile_node()` dispatch), `tests/test_parser.c` (15 tests), `tests/test_vm.c` (11 tests), `tests/test_repl.c` (3 tests), `tests/test_cli.sh` (6 tests).

---
End of handoff.
