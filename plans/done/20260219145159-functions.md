# User-Defined Functions

**Status:** Done (all 9 steps complete + tested)

## Design

Syntax: `fn name(params) is body end`. Expression form (returns the function as a value). Named form binds the function to a global variable. Lexical scope. Functions are first-class values. Recursion works because the named function is bound globally before the body executes at call time.

## Architecture changes

- `VAL_FUNCTION` value type with `ObjFunction` struct (name, arity, params, chunk, native pointer).
- Stack-based call convention: callee on stack, then arguments, then `OP_CALL [argc]`.
- VM call frames: `CallFrame` struct tracks function, IP, and stack window. Call stack as array (`FRAMES_MAX` = 64).
- Local variables: `OP_GET_LOCAL`/`OP_SET_LOCAL` opcodes. Compiler resolves params and `my` declarations to stack slots inside functions.

## Steps completed

1. AST_FUNCTION node + parser
2. VAL_FUNCTION value type
3. Compile function definitions
4. Refactor call convention to stack-based
5. VM call frames + call/return
6. Function parameters (OP_GET_LOCAL)
7. Local variable declarations (OP_SET_LOCAL + `my` in functions)
8. Recursion + error handling (test coverage only, no code changes needed)
9. Integration + REPL + cleanup (recursive disassembly, CLI tests)
