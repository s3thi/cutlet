# Cutlet Plan (Session Handoff)

## Project snapshot
Cutlet is a dynamic programming language (Python/Ruby/Lua/JS-like) written in C23. Targets Linux and macOS. Build requirements: C23 compiler + POSIX `make`. Dev tooling (analysis scripts, linters) may use standard tools like `ctags`, `cscope`, `python3`, `clang-format`, `clang-tidy`. Libraries are vendored in `vendor/` when possible. See `AGENTS.md` for the full dependency policy.

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
- **Vendor isolation**: Isocline compiled as separate `.o` with upstream-recommended C99 flags. `vendor/.clang-tidy` and `vendor/.clang-format` sentinel files prevent IDE linting/formatting of vendor code.
- **REPL/CLI**: Local in-process REPL as default mode (`cutlet repl`). TCP server (`--listen`, thread-per-client) and TCP client (`--connect`) with isocline for rich line editing and multiline input. `--tokens`, `--ast`, and `--bytecode` debug flags. Shared `print_repl_result()` formatting helper for both local and TCP modes. LSP-style JSON framing with request IDs for TCP mode. nREPL-style multi-frame responses: `say()` sends output frames (`{"type": "output", ...}`) before the terminal result frame (`{"type": "result", ...}`). Client reads frames in a loop. History persistence (`~/.cutlet/history`). `parser_is_complete()` drives continuation prompts and multiline accumulation (both interactive and pipe modes).
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

## Deferred: Example test runner with `.expected` files

The `examples/` directory now contains one `.cutlet` file per language feature (created as part of the understanding tools work below). A future step is to add matching `.expected` files and a test harness that runs `cutlet run` on each example and compares stdout. Integrate into `make test`.

---

## Required process (every step)

1. Write tests first.
2. Run `make test` and `make check` — confirm new tests fail.
3. **Stop and ask the user for confirmation before implementing.**
4. Implement the feature.
5. Run `make test` and `make check` after every code change.
6. Do not remove or modify existing tests without user confirmation.

---

## Next: Codebase understanding tools

Build three analysis scripts that help coding agents (and humans) understand the codebase. All scripts live in `scripts/`, are written in Python 3, and output markdown to stdout. Symbol indexing uses Universal Ctags, call graphs use cscope, and the pipeline tracer uses the cutlet interpreter itself. A `make understand` target runs all of them.

**Dev tool requirements**: `python3`, `ctags` (Universal Ctags), `cscope`. These are standard dev tools available in every package manager (`brew install universal-ctags cscope` / `apt install universal-ctags cscope`).

### Step 1 (completed): `--bytecode` debug flag

Added `--bytecode` REPL flag with `chunk_disassemble_to_string()`. Threaded through REPL, server, JSON protocol, and CLI.

---

### Step 2 (completed): Symbol index script

Created `scripts/symbol_index.py` — uses Universal Ctags JSON output to extract all public symbols from `src/*.h` and produces a markdown reference with Types and Functions tables per header file. Filters out anonymous compiler names (`__anon*`) and include guard macros. Checks for Universal Ctags availability with clear install instructions on error.

**Files created**: `scripts/symbol_index.py`.
**Files touched**: `Makefile` (added `symbol-index` target and help entry).

---

### Step 3 (completed): Call graph script

Created `scripts/call_graph.py` — uses cscope and Universal Ctags to find callers and callees for every public function defined in `src/*.h`, producing a markdown cross-reference. Callers are deduplicated by (file, function) to keep output compact. Builds cscope database in a `try/finally` block that always cleans up temp files. Checks for both cscope and Universal Ctags availability with clear install instructions.

**Files created**: `scripts/call_graph.py`.
**Files touched**: `Makefile` (added `call-graph` target and help entry), `.gitignore` (added cscope temp files).

---

### Step 4 (completed): Example programs in `examples/`

Created 12 `.cutlet` example programs, one per language feature: `arithmetic`, `modulo-power`, `strings`, `booleans`, `nothing`, `comparison`, `variables`, `if-else`, `while-loop`, `break-continue`, `function-call`, `unary`. Each is small, self-contained, uses `say()` for output, and has a comment header naming the feature. All verified with `cutlet run`.

**Files created**: `examples/*.cutlet` (12 files).

---

### Step 5 (completed): Pipeline tracer script

Created `scripts/pipeline_trace.py` — takes a `.cutlet` file and produces a complete markdown trace through every pipeline stage (tokens, AST, bytecode) with source location cross-references. Runs the interpreter with `--tokens --ast --bytecode` in a single invocation, parses the combined output, then uses regex-based grep on `src/parser.c`, `src/compiler.c`, and `src/vm.c` to map keywords, AST node types, and opcodes to their source locations. Tests section groups references by file for readability. Handles parse errors gracefully.

**Files created**: `scripts/pipeline_trace.py`.
**Files touched**: `Makefile` (added `pipeline-trace` target and help entry).

---

### Step 6: `make understand` target and combined output

**Depends on steps 2, 3, and 5.**

Add a `make understand` target that runs all three analysis tools and combines their output.

**What to do:**

1. Add to `Makefile`:
   ```makefile
   understand: symbol-index call-graph pipeline-trace
   ```
   Each sub-target runs its script and prints to stdout. The `understand` target runs all three in sequence.

2. The individual targets should be:
   ```makefile
   symbol-index:
   	@python3 scripts/symbol_index.py

   call-graph:
   	@python3 scripts/call_graph.py

   pipeline-trace: $(BIN)
   	@for f in examples/*.cutlet; do \
   		python3 scripts/pipeline_trace.py "$$f"; \
   		echo ""; \
   	done
   ```
   Note: `pipeline-trace` depends on `$(BIN)` because it runs the interpreter.

3. Output is NOT committed to git. Add a comment in the Makefile noting this. Users/agents run `make understand` to generate fresh analysis. Add `examples/*.cutlet` and `scripts/*.py` to git (they're source, not generated output).

4. Add `cscope.out`, `cscope.in.out`, `cscope.po.out` to `.gitignore` (generated by cscope, cleaned up by the scripts but just in case).

**Files to touch**: `Makefile`, `.gitignore`.

---
End of handoff.
