# Control Flow

**Status:** Done

- **If/else**: `if cond then body [else body] end`. Expression form (returns value). `else if` special case (single `end`). Only taken branch evaluated. No-else returns `nothing`.
- **While loops**: `while cond do body end`. Expression form. Returns last body value, or `nothing` if loop never runs. Uses `OP_LOOP` for backward jumps.
- **Break**: `break [expr]` exits the enclosing loop with an optional value.
- **Continue**: `continue` skips to the next iteration.
- **Multi-line input**: `AST_BLOCK` for newline-separated statements. Evaluator runs children in order, returns last value.
