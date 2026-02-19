# Bytecode Compiler + VM

**Status:** Done

Single-pass compiler emits bytecode into Chunks. Stack-based VM executes opcodes. Produces VAL_NUMBER, VAL_STRING, VAL_BOOL, VAL_NOTHING, VAL_ERROR, VAL_FUNCTION. All arithmetic in double precision. `EvalContext` with write callback enables built-in functions like `say()` to stream output.
