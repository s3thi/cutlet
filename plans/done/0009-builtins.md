# Built-in Functions

**Status:** Done

`say(expr)` — prints formatted value + newline via `EvalContext` write callback. Returns `nothing`. Wrong arity and unknown function names produce runtime errors. Registered as a native `VAL_FUNCTION` in globals at VM startup.
