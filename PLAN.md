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
- **Example programs**: 12 `.cutlet` files in `examples/`, one per language feature: arithmetic, modulo-power, strings, booleans, nothing, comparison, variables, if-else, while-loop, break-continue, function-call, unary. Small, self-contained, use `say()` for output. Serve as documentation, pipeline tracer input, and lightweight feature tests.
- **Codebase understanding tools**: Three Python analysis scripts in `scripts/` help orient agents and humans. `make understand` runs all three. Requires `python3`, Universal Ctags (`ctags`), and `cscope`.
  - `scripts/symbol_index.py` (`make symbol-index`) — uses Universal Ctags JSON output to extract all public symbols from `src/*.h`, producing markdown with Types and Functions tables per header.
  - `scripts/call_graph.py` (`make call-graph`) — uses cscope and Universal Ctags to find callers and callees for every public function in `src/*.h`, producing a markdown cross-reference.
  - `scripts/pipeline_trace.py` (`make pipeline-trace`) — traces a `.cutlet` file through every pipeline stage (tokens, AST, bytecode) with source location cross-references. Dynamically extracts keywords from `parser.c`. Validates output format parsing and source location coverage with fail-fast errors.

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
| `examples/*.cutlet` | Example programs, one per language feature |
| `scripts/symbol_index.py` | Public symbol index via Universal Ctags |
| `scripts/call_graph.py` | Caller/callee cross-reference via cscope |
| `scripts/pipeline_trace.py` | Pipeline tracer (tokens → AST → bytecode → source locations) |

---

## Next feature: User-defined functions

### Design

Syntax: `fn name(params) is body end`. Expression form (returns the function as a value). Named form binds the function to a global variable. Anonymous form (`fn(params) is body end`) deferred to a follow-up.

Lexical scope. Functions are first-class values. Recursion works because the named function is bound globally before the body executes at call time.

```cutlet
fn greet(name) is
  say("hello " .. name)
end

greet("world")       # prints: hello world

fn factorial(n) is
  if n <= 1 then 1
  else n * factorial(n - 1)
  end
end

say(factorial(5))    # prints: 120
```

### Architecture changes

Currently all variables are global (linked-list environment in `runtime.c`). Function calls use name-based dispatch (`OP_CALL [name_idx] [argc]` → `call_builtin()`). The VM has no call frames.

This feature requires:
- **New value type**: `VAL_FUNCTION` holding name, arity, parameter names, compiled Chunk, and optional native function pointer (for built-ins like `say`).
- **Stack-based call convention**: Callee value on stack, then arguments, then `OP_CALL [argc]`. Replaces name-based dispatch.
- **VM call frames**: `CallFrame` struct tracks function, instruction pointer, and stack window. Call stack as an array.
- **Local variables**: `OP_GET_LOCAL`/`OP_SET_LOCAL` opcodes. Compiler resolves params and `my` declarations to stack slots inside functions. Globals unchanged outside functions.

### Implementation steps

Each step follows the required process: tests first → confirm failures → implement → `make test && make check`.

#### Step 1: AST_FUNCTION node + parser ✅

Added `AST_FUNCTION` node type and `fn name(params) is body end` parsing. `params`/`param_count` fields on `AstNode`. `fn`/`is` reserved keywords. `parse_fn()` follows `parse_while` pattern. `ast_format` outputs `[FN name(a, b) body]`. `parser_is_complete` handles `fn` as opener needing `end`. 20 new tests (8 success, 5 error, 2 reserved keyword, 5 is_complete). Files: `src/parser.h`, `src/parser.c`, `tests/test_parser.c`.

#### Step 2: VAL_FUNCTION value type ✅

Added `VAL_FUNCTION` to the value system with `ObjFunction` struct (name, arity, params, chunk, native pointer). `NativeFn` typedef for built-in function pointers. `Value` changed from anonymous `typedef struct` to `struct Value` (needed for `NativeFn` forward reference). `Chunk` changed to named struct for forward declaration in `value.h`. Constructors: `make_function()` (takes ownership), `make_native()` (allocates ObjFunction for built-ins). `value_format()`: `"<fn name>"` / `"<fn>"`. `value_free()`: deep-frees ObjFunction. `value_clone()`: deep-copies ObjFunction including chunk bytecode and constants. `is_truthy()`: functions are truthy. `values_equal()` in `vm.c`: identity-based (pointer comparison). 8 new tests in `test_vm.c`. Files: `src/value.h`, `src/value.c`, `src/chunk.h`, `src/vm.c`, `tests/test_vm.c`.

#### Step 3: Compile function definitions ✅

Added `compile_function()` to the compiler. Creates a new Compiler with a fresh Chunk for the function body, compiles the body into it, emits `OP_RETURN`. Wraps the Chunk in an `ObjFunction` (with name, arity, deep-copied params). Adds it as a `VAL_FUNCTION` constant in the enclosing Chunk. Emits `OP_CONSTANT` + `OP_DEFINE_GLOBAL` to bind the function as a global variable. Functions cannot be called yet (Step 4). 6 new tests (3 compiler, 3 VM). Files: `src/compiler.c`, `tests/test_compiler.c`, `tests/test_vm.c`.

#### Step 4: Refactor call convention to stack-based ✅

Refactored function call dispatch from name-based (`OP_CALL [name_idx] [argc]`) to stack-based (`OP_GET_GLOBAL` pushes callee, `OP_CALL [argc]` dispatches from stack). `say` is registered as a native `VAL_FUNCTION` in globals at the start of each `vm_execute()` via `register_builtins()`. Compiler emits `OP_GET_GLOBAL` for function name before args, then `OP_CALL` with 1-byte argc only. VM `OP_CALL` handler peeks callee from stack, checks type and arity, calls native or errors for user functions. Removed `call_builtin()`. Added `native_say()` matching `NativeFn` signature, `value_type_name()` helper. Disassembler updated for new 2-byte `OP_CALL` encoding. Added NOLINT for pre-existing struct Value padding warning. 2 new VM tests (call number/bool errors), updated compiler test + CLI test. Files: `src/vm.c`, `src/compiler.c`, `src/chunk.c`, `src/chunk.h`, `src/value.h`, `tests/test_vm.c`, `tests/test_compiler.c`, `tests/test_cli.sh`.

#### Step 5: VM call frames + call/return ✅

Added `CallFrame` struct (function, ip, slots) and call frame stack (`frames[FRAMES_MAX]`, `frame_count`) to VM. `FRAMES_MAX` = 64. Top-level code runs inside a "script" CallFrame (frame 0) wrapping the top-level Chunk in a stack-allocated `ObjFunction`. Refactored `vm_execute()`: `read_byte()`/`read_short()` take a `CallFrame*`; all constant/chunk access goes through `frame->function->chunk`. `OP_CALL` for user functions pushes a new CallFrame. `OP_RETURN` pops the frame, discards the called function's stack window, and pushes the return value for the caller; frame_count==0 ends the program. 3 new VM tests. Files: `src/vm.h`, `src/vm.c`, `tests/test_vm.c`.

#### Step 6: Function parameters (OP_GET_LOCAL) ✅

Added `OP_GET_LOCAL` opcode with 1-byte slot index operand. Compiler now tracks `CompileContext` (script vs function) and a `Local` struct array. In function context, slot 0 is reserved for the callee, parameters occupy slots 1..arity. `compile_ident()` resolves locals first via `resolve_local()`, falling back to `OP_GET_GLOBAL`. VM handler clones `frame->slots[slot]` onto the stack. Disassembler formats as `OP_GET_LOCAL slot=N`. 4 VM tests + 3 compiler tests. Files: `src/chunk.h`, `src/chunk.c`, `src/compiler.c`, `src/vm.c`, `tests/test_vm.c`, `tests/test_compiler.c`.

#### Step 7: Local variable declarations (OP_SET_LOCAL + `my` in functions) ✅

Added `OP_SET_LOCAL` opcode with 1-byte slot index operand. `compile_decl()` in function context compiles the RHS (value lands at the next local slot), registers the local, then emits `OP_GET_LOCAL` to push a clone as the expression result (so `compile_block`'s OP_POP doesn't destroy the local). `compile_assign()` in function context resolves locals first via `resolve_local()`, emitting `OP_SET_LOCAL` instead of `OP_SET_GLOBAL`. VM `OP_SET_LOCAL` handler: peek TOS, clone into slot (freeing old value), TOS stays as expression result. Disassembler formats as `OP_SET_LOCAL slot=N`. Locals declared in inner blocks are valid until function end (no block scoping yet). 4 VM tests + 3 compiler tests. Files: `src/chunk.h`, `src/chunk.c`, `src/compiler.c`, `src/vm.c`, `tests/test_vm.c`, `tests/test_compiler.c`.

#### Step 8: Recursion + error handling

Test recursion (should work automatically) and add missing error handling.

**Recursion** (no code changes expected — OP_GET_GLOBAL finds the function at call time):
- Test: `fn factorial(n) is if n <= 1 then 1 else n * factorial(n - 1) end end\nfactorial(5)` → 120.
- Test: `fn fib(n) is if n < 2 then n else fib(n - 1) + fib(n - 2) end end\nfib(10)` → 55.
- Test: mutual recursion (fn even/odd calling each other).

**Error handling**:
- Wrong arity: `fn foo(a, b) is a end\nfoo(1)` → error "'foo' expects 2 arguments, got 1".
- Call stack overflow: deeply recursive function → error "stack overflow" (not a segfault).
- Verify all errors include line numbers.

**Files touched**: `src/vm.c` (error messages), tests.

#### Step 9: Integration + REPL + cleanup

End-to-end integration, REPL multi-line support, and disassembly cleanup.

**REPL** (`src/main.c`):
- Verify multi-line function definitions work in the local REPL (continuation prompt on `fn ... is` without `end`).
- Verify `--bytecode` flag shows the function's inner Chunk disassembly.

**CLI integration tests** (`tests/test_cli.sh`):
- `cutlet run` with a file that defines and calls functions.
- `echo "fn f() is 42 end\nsay(f())" | cutlet repl` → prints 42.

**Bytecode disassembly** (`src/chunk.c`):
- When disassembling a Chunk that contains a VAL_FUNCTION constant, recursively disassemble the function's Chunk too.

**Tests**: integration tests covering the full pipeline.

**Files touched**: `src/chunk.c`, `tests/test_cli.sh`, `tests/test_eval.c`, `src/main.c` if needed.

**Post-implementation reminders** (per AGENTS.md):
- Update `TUTORIAL.md` with a functions section.
- Add `examples/functions.cutlet` example program.

---

## Deferred: Anonymous functions

After named functions land, add anonymous function syntax: `fn(params) is body end` (no name). The parser already handles `fn` as a prefix — just make the name optional. Anonymous functions are expressions that return a VAL_FUNCTION without binding a global.

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
End of handoff.
