# Cutlet Plan (Session Handoff)

## Project snapshot
Cutlet is a dynamic programming language (Python/Ruby/Lua/JS-like) written in C23 with no external deps beyond platform libs and POSIX `make`. Targets Linux and macOS.

See `AGENTS.md` for project conventions and instructions that must be followed.

## What exists today (all complete + tested)

- **Tokenizer**: NUMBER, STRING, IDENT, OPERATOR, EOF, ERROR tokens. Solo symbols `( ) + - / ,` always single-char.
- **Pratt parser**: Precedence climbing. `or` (prec 1) → `and` (prec 2) → `not` (prec 3, prefix) → comparison (prec 4, non-assoc) → `+ -` (prec 5) → `* /` (prec 6) → unary minus (prec 7) → `**` (prec 8, right). Parenthesized grouping. `=` assignment and `my` declaration (prec 0, right).
- **AST nodes**: NUMBER, STRING, IDENT, BOOL, NOTHING, BINOP, UNARY, DECL, ASSIGN, BLOCK, IF, CALL. S-expr format output.
- **Evaluator**: Tree-walking. Produces VAL_NUMBER, VAL_STRING, VAL_BOOL, VAL_NOTHING, or VAL_ERROR. All arithmetic in double precision.
- **Booleans**: `true`/`false` keywords → `AST_BOOL` → `VAL_BOOL`.
- **Nothing**: `nothing` keyword → `AST_NOTHING` → `VAL_NOTHING`. Falsy. `nothing == nothing` is true; ordered comparisons with nothing produce errors.
- **Comparison operators**: `==`, `!=`, `<`, `>`, `<=`, `>=`. Return VAL_BOOL. Non-associative. Mixed-type equality allowed; mixed-type ordering is an error.
- **Logical operators**: `and`, `or` (keyword infix, short-circuit, Python semantics — return operand values), `not` (keyword prefix, returns VAL_BOOL). Truthiness: `false`, `nothing`, `0`, `""`, errors are falsy.
- **Multi-line input**: `TOK_NEWLINE` token, `AST_BLOCK` for newline-separated statements. Evaluator runs children in order, returns last value.
- **If/else expressions**: `if cond then body [else body] end`. Expression form (returns value). `else if` special case (single `end`). Only taken branch evaluated. No-else returns `nothing`.
- **Variables**: `my x = expr` declares, `x = expr` assigns. Linked-list environment with thread-safe get/define/assign.
- **Runtime**: Global pthread rwlock serializes eval.
- **REPL/CLI**: TCP server (thread-per-client), TCP client with isocline for rich line editing and multiline input. `--tokens` and `--ast` debug flags. LSP-style JSON framing with request IDs. History persistence (`~/.cutlet/history`). `parser_is_complete()` drives continuation prompts.
- **Function calls**: `name(arg1, arg2, ...)` syntax parsed as `AST_CALL`. Zero or more comma-separated arguments. Parsed as postfix after identifier.
- **Tests**: Exhaustive C test suites for tokenizer, parser, eval, runtime, REPL client, REPL server, ptr_array. Integration tests in `test_cli.sh`. Sanitizer builds via `make test-sanitize`.

## Key files

| File | Purpose |
|------|---------|
| `src/tokenizer.c/h` | Lexer |
| `src/parser.c/h` | Pratt parser, AST types, `parser_is_complete()` |
| `src/eval.c/h` | Evaluator, Value types |
| `src/runtime.c/h` | Global lock, variable environment |
| `src/main.c` | CLI entry, TCP server/client with isocline |
| `src/repl.c/h` | REPL client |
| `src/repl_server.c/h` | REPL server |
| `src/json.c/h` | JSON protocol framing |
| `src/ptr_array.c/h` | Dynamic pointer array utility |
| `vendor/isocline/` | Isocline library (v1.0.9) for line editing |
| `tests/test_*.c` | Unit tests |
| `tests/test_cli.sh` | Integration tests |
| `TUTORIAL.md` | Language tutorial (learnxinyminutes style) |
| `examples/` | Example programs with `.expected` output files |

---

## Next: Built-in function calls, say(), file execution, tutorial, and examples

### Step 1: Function call parsing ✓

Added `AST_CALL` node type. Parsing in `parse_atom()` after identifier: if next token is `(`, parse comma-separated arguments until `)`. S-expr: `[CALL say [STRING hello]]`. 21 parser tests.

### Step 2: `say()` built-in function

Add `say()` as a built-in function that prints to stdout.

- Behavior: `say(expr)` evaluates `expr`, prints the formatted value to stdout followed by a newline. Returns `nothing`.
- Error handling: `say()` with 0 args or 2+ args produces a runtime error. If the argument evaluates to an error, the error propagates.
- The evaluator needs to handle `AST_CALL` nodes. For now, the only recognized function name is `say`. Unknown function names produce a runtime error.
- `say()` writes to stdout using `printf`/`puts`. In the REPL server context, this means output goes to the server's stdout, not back to the client. This is fine for file execution mode (Step 3). Document this REPL limitation.
- **Tests**: Eval tests for `say("hello")` producing output and returning `nothing`. Error tests for wrong arity and unknown functions.

### Step 3: `cutlet run <file>` — file execution

Add a `run` subcommand to execute a `.cutlet` file directly, without the TCP server.

- Usage: `cutlet run <filename>`
- Reads the entire file into memory, parses it as a block (multi-line), evaluates it directly using the existing evaluator. No TCP server involved.
- The runtime environment is initialized and destroyed around the eval.
- Exit code: 0 on success, 1 on parse/eval error. Errors are printed to stderr.
- The final expression value is NOT printed (unlike the REPL). Output comes only from `say()` calls.
- Update `print_usage()`, update README.
- **Tests**: CLI integration tests in `test_cli.sh` — write a temp `.cutlet` file, run it with `cutlet run`, verify stdout output.

### Step 4: Write `TUTORIAL.md`

Write a language tutorial in the style of [learnxinyminutes.com](https://learnxinyminutes.com/go/). The tutorial is a single `.cutlet` file embedded in markdown with extensive inline comments (using `#` once we have comments, or just markdown prose around code blocks).

Since Cutlet doesn't have comments yet, the tutorial will use markdown prose with code blocks showing REPL sessions and file examples. Cover all working features:

- Numbers, strings, booleans, nothing
- Arithmetic, exponentiation, unary minus
- String values
- Comparison operators
- Logical operators (and, or, not) with short-circuit and truthiness rules
- Variables (my, assignment)
- If/else expressions
- Multi-line blocks
- `say()` for output
- Running files with `cutlet run`
- The REPL

### Step 5: Example programs in `examples/`

Create example `.cutlet` programs that exercise the language features and produce output via `say()`. Each example has a matching `.expected` file containing the expected stdout.

Examples:
- `examples/hello.cutlet` + `examples/hello.expected` — Hello World with `say()`
- `examples/arithmetic.cutlet` + `examples/arithmetic.expected` — arithmetic and variables
- `examples/fizzbuzz.cutlet` + `examples/fizzbuzz.expected` — if/else logic (simplified, since we don't have loops yet)
- `examples/logic.cutlet` + `examples/logic.expected` — logical operators and truthiness

### Step 6: Test that runs all examples

Add a test (in `tests/test_examples.sh` or as part of `test_cli.sh`) that:

1. Finds all `examples/*.cutlet` files.
2. For each, runs `cutlet run <file>` and captures stdout.
3. Compares stdout against the matching `examples/*.expected` file.
4. Reports pass/fail per example.
5. Integrated into `make test` so it runs with the rest of the suite.

### Step 7: Update `AGENTS.md`

Add a reminder to `AGENTS.md` that says:
- When a new language feature is added, remind the user to update `TUTORIAL.md` and add/update examples in `examples/`.
- The agent should NOT do the update itself — just remind the user.

---

## Required process (every step)

1. Write tests first.
2. Run `make test` and `make check` — confirm new tests fail.
3. **Stop and ask the user for confirmation before implementing.**
4. Implement the feature.
5. Run `make test` and `make check` after every code change.
6. Do not remove or modify existing tests without user confirmation.

---
End of handoff.
