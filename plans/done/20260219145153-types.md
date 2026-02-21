# Value Types

**Status:** Done

- **Numbers**: Double precision floating point.
- **Strings**: Immutable.
- **Booleans**: `true`/`false` keywords -> `AST_BOOL` -> `VAL_BOOL`.
- **Nothing**: `nothing` keyword -> `AST_NOTHING` -> `VAL_NOTHING`. Falsy. `nothing == nothing` is true; ordered comparisons with nothing produce errors.
- **Functions**: First-class `VAL_FUNCTION` values. Truthy. Identity-based equality (pointer comparison).
- **Errors**: `VAL_ERROR` for runtime error propagation.
