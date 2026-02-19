# Anonymous Functions

Syntax: `fn(params) is body end` (no name). The parser already handles `fn` as a prefix — just make the name optional. Anonymous functions are expressions that return a VAL_FUNCTION without binding a global.

## Summary

**What changed**: Made the function name optional in `fn` expressions. When `fn` is immediately followed by `(`, it's an anonymous function. Anonymous functions compile to a bare `OP_CONSTANT` (no `OP_DEFINE_GLOBAL`), leaving the function value on the stack as an expression result. No VM changes were needed.

**Files touched**:
- `src/parser.c` — `parse_fn()` now accepts `fn(` as anonymous; `ast_format_node()` handles NULL name
- `src/compiler.c` — `compile_function()` skips `OP_DEFINE_GLOBAL` when name is NULL
- `tests/test_parser.c` — 14 new tests for anonymous fn parsing, errors, and completeness
- `tests/test_compiler.c` — 3 new tests for anonymous fn bytecode (no DEFINE_GLOBAL, params, in decl)
- `tests/test_vm.c` — 7 new integration tests (assign+call, formatting, say, arity error, expression)
