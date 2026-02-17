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

#### Step 3: Compile function definitions

Compile `AST_FUNCTION` into bytecode. Each function body gets its own Chunk.

**Compiler** (`compiler.c`):
- `compile_function()`: create a new Compiler with a fresh Chunk, compile the body into it, emit `OP_RETURN` at the end. Create an `ObjFunction` wrapping the Chunk. Add it as a constant in the *enclosing* Chunk. Emit `OP_CONSTANT` + `OP_DEFINE_GLOBAL` with the function name.
- For now, the function body is compiled but **cannot be called yet** (call convention change is step 4). The function is just stored as a global variable value.

**Tests** (`tests/test_compiler.c` or `tests/test_eval.c`):
- Compile `fn foo() is 42 end` → verify bytecode contains OP_CONSTANT + OP_DEFINE_GLOBAL.
- Eval `fn foo() is 42 end` → verify `foo` is a VAL_FUNCTION in globals.
- `fn foo() is 42 end\nfoo` → verify evaluating the name returns `<fn foo>`.

**Files touched**: `src/compiler.c`, tests.

#### Step 4: Refactor call convention to stack-based

Change how function calls are compiled and dispatched. This is a **refactor step** — existing behavior (calling `say()`) must keep working.

**Built-in registration** (`runtime.c` or `vm.c`):
- At startup, register `say` as a native VAL_FUNCTION in the global environment using `make_native("say", 1, native_say)`.
- Implement `native_say()` matching the `NativeFn` signature.

**Compiler** (`compiler.c`):
- Change `compile_call()`: instead of emitting `OP_CALL [name_idx] [argc]`, emit `OP_GET_GLOBAL` for the function name, then compile args, then emit the new `OP_CALL [argc]` (1-byte argc only).
- Update `OP_CALL` encoding in `chunk.h` comment.

**VM** (`vm.c`):
- Change `OP_CALL` handler: read `argc`, peek at callee at `stack_top[-argc-1]`.
  - If `VAL_FUNCTION` with `native` != NULL: pop args into a temporary array, call native function, pop callee, push result.
  - If `VAL_FUNCTION` with `native` == NULL: error for now ("user function calls not yet supported") — actual dispatch comes in step 5.
  - If not a function: runtime error "cannot call \<type\>".
- Remove `call_builtin()`.

**Disassembler** (`chunk.c`):
- Update `chunk_disassemble` for the new `OP_CALL` encoding (1-byte argc, no name index).

**Tests**:
- All existing `say()` tests pass unchanged.
- `say("hi")` still prints "hi".
- `42()` → error "cannot call number".

**Files touched**: `src/compiler.c`, `src/vm.c`, `src/chunk.c`, `src/chunk.h`, `src/runtime.c` or `src/value.c`, tests.

#### Step 5: VM call frames + call/return

Add call frames to the VM so user-defined functions can actually execute.

**VM** (`vm.h`, `vm.c`):
- Define `CallFrame`:
  ```c
  typedef struct {
      ObjFunction *function;  /* The function being executed. */
      uint8_t *ip;            /* Instruction pointer into function's chunk. */
      Value *slots;           /* Pointer into VM stack: base of this frame's window. */
  } CallFrame;
  ```
- Add `CallFrame frames[FRAMES_MAX]` and `int frame_count` to VM. `FRAMES_MAX` = 64.
- Top-level code runs inside a "script" CallFrame (frame 0) whose function is an `ObjFunction` wrapping the top-level Chunk.
- Refactor `vm_execute()`: `ip`, `chunk` now come from `frames[frame_count-1]`. All `read_byte()`/`read_short()` and constant access go through the current frame.
- **OP_CALL** for user functions: validate arity, push new CallFrame, set `ip` to function's chunk, set `slots` to `stack_top - argc - 1`.
- **OP_RETURN**: pop frame. If `frame_count` reaches 0, return the result (end of program). Otherwise, pop the called function's stack window, push the return value onto the caller's stack, resume caller's frame.

**Tests**:
- `fn foo() is 42 end\nfoo()` → 42.
- `fn greet() is say("hi") end\ngreet()` → prints "hi", returns nothing.
- `fn five() is 2 + 3 end\nsay(five())` → prints 5.
- Existing tests still pass.

**Files touched**: `src/vm.h`, `src/vm.c`, tests.

#### Step 6: Function parameters (OP_GET_LOCAL)

Add local variable reads so function parameters can be accessed.

**Opcodes** (`chunk.h`):
- Add `OP_GET_LOCAL` with 1-byte slot index operand.

**Compiler** (`compiler.c`):
- Add a `Local` struct (name, depth/slot index) and a locals array to the Compiler.
- Track compilation context: "script" vs "function". In function context, parameters are added as locals 0..N-1 before compiling the body.
- `compile_ident()`: in function context, check locals first. If found, emit `OP_GET_LOCAL [slot]`. If not found, fall back to `OP_GET_GLOBAL`.

**VM** (`vm.c`):
- `OP_GET_LOCAL`: read slot index, push clone of `frame->slots[slot]`.

**Disassembler** (`chunk.c`):
- Format `OP_GET_LOCAL` with slot index.

**Tests**:
- `fn identity(x) is x end\nidentity(42)` → 42.
- `fn add(a, b) is a + b end\nadd(1, 2)` → 3.
- `fn shadow(x) is x end\nmy x = 99\nshadow(1)` → 1 (local shadows global).
- `fn readglobal() is x end\nmy x = 99\nreadglobal()` → 99 (falls back to global).

**Files touched**: `src/chunk.h`, `src/chunk.c`, `src/compiler.c`, `src/vm.c`, tests.

#### Step 7: Local variable declarations (OP_SET_LOCAL + `my` in functions)

Allow `my` declarations and `=` assignment inside functions to use stack slots.

**Opcodes** (`chunk.h`):
- Add `OP_SET_LOCAL` with 1-byte slot index operand.

**Compiler** (`compiler.c`):
- `compile_decl()` in function context: add a new local (increment local count), compile the RHS, the value is already at the correct stack slot. Emit `OP_SET_LOCAL` to leave the value as the expression result.
- `compile_assign()` in function context: if the name resolves to a local, emit `OP_SET_LOCAL [slot]`. Otherwise fall back to `OP_SET_GLOBAL`.
- Scope: locals declared in inner blocks (e.g., inside `if`) are valid until the function ends (no block scoping yet — matches the current global model).

**VM** (`vm.c`):
- `OP_SET_LOCAL`: read slot index, free old value at slot, clone TOS into slot (don't pop — value stays as expression result).

**Disassembler** (`chunk.c`):
- Format `OP_SET_LOCAL` with slot index.

**Tests**:
- `fn foo(x) is my y = x + 1\ny end\nfoo(10)` → 11.
- `fn foo() is my a = 1\nmy b = 2\na + b end\nfoo()` → 3.
- `fn foo(x) is x = x + 1\nx end\nfoo(10)` → 11 (reassign parameter).
- Local doesn't leak to global scope.

**Files touched**: `src/chunk.h`, `src/chunk.c`, `src/compiler.c`, `src/vm.c`, tests.

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
