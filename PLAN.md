# Cutlet Plan (Session Handoff)

## Project snapshot
Cutlet is a dynamic programming language (Python/Ruby/Lua/JS-like) written in C23 with no external deps beyond platform libs and POSIX `make`. Targets Linux and macOS.

See `AGENTS.md` for project conventions and instructions that must be followed.

## What exists today (all complete + tested)

- **Tokenizer**: NUMBER, STRING, IDENT, OPERATOR, EOF, ERROR tokens. Solo symbols `( ) + - / % ,` always single-char. `#` line comments.
- **Pratt parser**: Precedence climbing. `or` (prec 1) → `and` (prec 2) → `not` (prec 3, prefix) → comparison (prec 4, non-assoc) → `..` (prec 5, right) → `+ -` (prec 6) → `* / %` (prec 7) → unary minus (prec 8) → `**` (prec 9, right). Parenthesized grouping. `=` assignment and `my` declaration (prec 0, right).
- **AST nodes**: NUMBER, STRING, IDENT, BOOL, NOTHING, BINOP, UNARY, DECL, ASSIGN, BLOCK, IF, CALL, WHILE. S-expr format output.
- **Bytecode compiler + VM**: Single-pass compiler emits bytecode into Chunks. Stack-based VM executes opcodes. Produces VAL_NUMBER, VAL_STRING, VAL_BOOL, VAL_NOTHING, or VAL_ERROR. All arithmetic in double precision. `EvalContext` with write callback enables built-in functions like `say()` to stream output.
- **Modulo operator**: `%` at precedence 7 (same as `*` and `/`), left-associative. Python/Ruby-style semantics: result has the sign of the divisor (`a % b = a - b * floor(a / b)`). Division by zero produces `"modulo by zero"` error.
- **String concatenation**: `..` operator at precedence 5 (between comparison and `+`/`-`), right-associative. Auto-coerces both operands to strings via `value_format()`. `"hello" .. " world"` → `"hello world"`, `"score: " .. 42` → `"score: 42"`. `+` with strings remains an error.
- **While loops**: `while cond do body end` expression. Evaluates body repeatedly while condition is truthy. Returns last body value, or `nothing` if loop never runs. Expression form (can be used in assignment). Uses `OP_LOOP` opcode for backward jumps.
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

## Completed: While loop expression (`while...do...end`)

Added `while cond do body end` as a loop expression. The loop evaluates its body repeatedly while `cond` is truthy. Returns the last value produced by the body, or `nothing` if the loop body never executes. Uses an accumulator-based bytecode pattern with a new `OP_LOOP` backward jump opcode.

**Files touched**: `src/parser.h` (`AST_WHILE`), `src/parser.c` (`parse_while()`, `is_reserved_keyword()` for `while`/`do`, `ast_node_type_str()`, `ast_format_node()`, `parser_is_complete()`), `src/chunk.h` (`OP_LOOP`), `src/chunk.c` (disassembler + `opcode_name()`), `src/compiler.c` (`emit_loop()` helper, `compile_while()`, dispatch), `src/vm.c` (`OP_LOOP` execution), `tests/test_parser.c` (17 tests), `tests/test_vm.c` (7 tests), `tests/test_repl.c` (3 tests), `tests/test_cli.sh` (3 tests).

---

## Next: `break` and `continue` for while loops

**Depends on**: "While loop expression" task above must be completed first.

**Objective**: Add `break` and `continue` keywords that control loop iteration. `break` exits the innermost loop immediately, with an optional value. `continue` skips to the next iteration.

**Syntax**:
```cutlet
# break with value — loop evaluates to 42
while true do
  break 42
end
# => 42

# bare break — loop evaluates to nothing
while true do
  break
end
# => nothing

# continue — skip to next iteration
my i = 0
while i < 10 do
  i = i + 1
  if i % 2 == 0 then continue end
  say(i)
  i
end
# prints: 1 3 5 7 9, evaluates to 9
```

**Semantics**:

`break [expr]`:
- Exits the innermost enclosing while loop.
- If `expr` is provided, the loop evaluates to `expr`. If omitted, the loop evaluates to `nothing`.
- The parser determines whether break has a value by peeking at the next token: if it can start an expression (number, string, non-keyword identifier, `(`, `-`, `not`, `if`, `true`, `false`, `nothing`, `while`), parse the value. If the next token is `end`, `else`, `then`, `do`, `EOF`, `NEWLINE`, or any other non-expression-starting token, treat it as bare break.
- `break` outside a loop is a **compile error** (not a parse error). The parser accepts `break` anywhere syntactically.

`continue`:
- Skips the rest of the current iteration and re-evaluates the loop condition.
- Never takes a value.
- The current iteration's value is discarded — the accumulator becomes `nothing` for this iteration (this means if continue is hit on the last iteration, the loop evaluates to `nothing` since no body value was produced).
- `continue` outside a loop is a **compile error**.

**Bytecode strategy**:

The while loop bytecode layout from the previous task has a `BREAK_TARGET` label after the loop exit's `OP_POP` of the condition. The compiler needs a `LoopContext` to track:
```c
typedef struct {
    size_t loop_start;     /* Bytecode offset of LOOP_START (for continue and OP_LOOP) */
    size_t *break_jumps;   /* Dynamic array of forward-jump offsets to patch to BREAK_TARGET */
    size_t break_count;
    size_t break_capacity;
} LoopContext;
```

The `Compiler` struct gets a `LoopContext *current_loop` pointer (NULL when not inside a loop). `compile_while()` pushes/pops this context.

For `break expr`:
1. Compile `expr` (or emit `OP_NOTHING` for bare break). This pushes 1 value onto the stack.
2. Since the accumulator was already popped before body compilation (the two OP_POPs), and break appears where an expression is expected in the body, the stack depth at this point is exactly D+1 (base + break value). This matches what BREAK_TARGET expects.
3. Emit `OP_JUMP` with a placeholder offset. Record the offset in `current_loop->break_jumps`.
4. After the loop, patch all break jumps to BREAK_TARGET.

For `continue`:
1. Emit `OP_NOTHING` — this becomes the new accumulator for the next iteration (the current iteration's body value is abandoned).
2. Emit `OP_LOOP` backward to `LOOP_START`. At LOOP_START, the stack has the accumulator (`nothing`) at D+1, which is correct.

**AST nodes**:
- `AST_BREAK`: `left` = value expression (or NULL for bare break). No `value` string, no children array.
- `AST_CONTINUE`: Leaf node. No children, no value expression.

**Acceptance criteria**:

- [ ] `AST_BREAK` and `AST_CONTINUE` added to `AstNodeType` enum in `parser.h`.
- [ ] `break` and `continue` added to `is_reserved_keyword()` in `parser.c`.
- [ ] `parse_atom()` handles `break` keyword: consume `break`, peek next token to decide if there's a value (using a `can_start_expression()` helper or inline checks), parse value if present, return `AST_BREAK` node with `left` set to the value node (or NULL).
- [ ] `parse_atom()` handles `continue` keyword: consume `continue`, return leaf `AST_CONTINUE` node.
- [ ] `ast_node_type_str()` handles `AST_BREAK` → `"BREAK"` and `AST_CONTINUE` → `"CONTINUE"`.
- [ ] `ast_format_node()` handles `AST_BREAK` → `[BREAK expr]` or `[BREAK]` and `AST_CONTINUE` → `[CONTINUE]`.
- [ ] `LoopContext` struct added to `compiler.c` with `loop_start`, break jump tracking, and a pointer to the enclosing loop context (for nested loops).
- [ ] `Compiler` struct gets `LoopContext *current_loop` field (initially NULL).
- [ ] `compile_while()` updated to: create a `LoopContext`, set `current_loop`, compile the loop body, then patch all recorded break jumps to `BREAK_TARGET`, and restore the previous `current_loop`.
- [ ] `compile_break()` in `compiler.c`: check `current_loop != NULL` (compile error if outside loop), compile value expression or emit `OP_NOTHING`, emit forward `OP_JUMP`, record jump offset in `current_loop->break_jumps`.
- [ ] `compile_continue()` in `compiler.c`: check `current_loop != NULL` (compile error if outside loop), emit `OP_NOTHING` (new accumulator), emit `OP_LOOP` to `current_loop->loop_start`.
- [ ] `compile_node()` dispatches `AST_BREAK` and `AST_CONTINUE`.
- [ ] Parser tests: `break` alone, `break expr`, `break` with complex expression, `continue`, break/continue as variable names rejected, `break` value detection (bare break before `end`, bare break before newline, break with value).
- [ ] VM/eval tests: break exits loop, break with value, bare break returns nothing, break from nested if inside loop, continue skips iteration, continue in last iteration produces nothing as loop value, nested loops (break/continue affect innermost only), break/continue outside loop produces compile error.
- [ ] REPL tests: break and continue in REPL while loops.
- [ ] CLI integration tests: break and continue in file execution.
- [ ] All existing tests continue to pass.

**Files to touch**:

| File | Changes |
|------|---------|
| `src/parser.h` | Add `AST_BREAK`, `AST_CONTINUE` to enum |
| `src/parser.c` | `parse_atom()` cases for break/continue, `is_reserved_keyword()`, `ast_node_type_str()`, `ast_format_node()` |
| `src/compiler.c` | `LoopContext` struct, `compile_break()`, `compile_continue()`, update `compile_while()` and `Compiler` struct, dispatch in `compile_node()` |
| `tests/test_parser.c` | Parser tests for break/continue |
| `tests/test_vm.c` | VM tests for break/continue |
| `tests/test_repl.c` | REPL tests for break/continue |
| `tests/test_cli.sh` | CLI integration tests for break/continue |

**Non-goals**: `for` loops, labeled break/continue (break from outer loop), `break` as a parse error outside loops.

---
End of handoff.
