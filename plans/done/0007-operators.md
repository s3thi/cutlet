# Operators

**Status:** Done

- **Arithmetic**: `+`, `-`, `*`, `/`, `%`, `**`. Modulo uses Python/Ruby-style semantics (result has sign of divisor). Division/modulo by zero produce runtime errors.
- **String concatenation**: `..` at precedence 5, right-associative. Auto-coerces both operands to strings. `+` with strings remains an error.
- **Comparison**: `==`, `!=`, `<`, `>`, `<=`, `>=`. Return VAL_BOOL. Non-associative. Mixed-type equality allowed; mixed-type ordering is an error.
- **Logical**: `and`, `or` (keyword infix, short-circuit, Python semantics — return operand values), `not` (keyword prefix, returns VAL_BOOL). Truthiness: `false`, `nothing`, `0`, `""`, errors are falsy.
- **Unary**: `-` (negation).
