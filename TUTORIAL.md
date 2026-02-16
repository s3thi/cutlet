# Learn Cutlet in Y Minutes

Cutlet is a dynamic programming language written in C. It aims to be a small
"glue language" in the spirit of Python, Ruby, Lua, and JavaScript.

```cutlet
# This is a comment. Comments start with # and go to end of line.

# ============================================================
# 1. Numbers and arithmetic
# ============================================================

# Numbers are 64-bit floating point (doubles).
42          # => 42
0           # => 0

# Arithmetic works how you'd expect.
1 + 2       # => 3
10 - 3      # => 7
2 * 3       # => 6
7 / 2       # => 3.5

# Modulo with % (Python-style: result has the sign of the divisor).
10 % 3      # => 1
-7 % 3      # => 2   (not -1 like C)
7 % -3      # => -2  (not 1 like C)

# Exponentiation with ** (right-associative).
2 ** 10     # => 1024
2 ** 3 ** 2 # => 512 (2 ** (3 ** 2) = 2 ** 9)

# Unary minus.
-3          # => -3
-(-3)       # => 3

# Standard precedence: ** > unary - > * / % > + -
1 + 2 * 3   # => 7    (not 9)
(1 + 2) * 3 # => 9    (parentheses override)
-2 ** 2     # => -4   (-(2 ** 2), not (-2) ** 2)

# Division and modulo by zero are runtime errors.
# 1 / 0     # => ERR division by zero
# 1 % 0     # => ERR modulo by zero

# ============================================================
# 2. Strings
# ============================================================

# Strings are double-quoted.
"hello"         # => hello
"hello world"   # => hello world
""              # => (empty string)

# No escape sequences yet. What you type is what you get.
# Strings can contain any characters except newlines and double quotes.

# Concatenation with .. (two dots).
"hello" .. " world"     # => hello world
"a" .. "b" .. "c"       # => abc

# .. auto-coerces any value to a string.
"score: " .. 42         # => score: 42
"alive: " .. true       # => alive: true
"value: " .. nothing    # => value: nothing

# + does NOT work on strings — it's only for numbers.
# "a" + "b"             # => ERR arithmetic requires numbers

# .. binds looser than + but tighter than comparison.
1 + 2 .. 3 + 4          # => 37 (same as (1+2) .. (3+4))

# .. is right-associative (like Lua).
"a" .. "b" .. "c"       # => abc (same as "a" .. ("b" .. "c"))

# ============================================================
# 3. Booleans and nothing
# ============================================================

true        # => true
false       # => false
nothing     # => nothing

# `nothing` is Cutlet's null/nil value.

# ============================================================
# 4. Comparison operators
# ============================================================

# Equality works across all types.
1 == 1          # => true
1 == 2          # => false
"a" == "a"      # => true
1 == "1"        # => false (different types are never equal)
nothing == nothing  # => true

1 != 2          # => true

# Ordering works for numbers and strings (but not across types).
1 < 2           # => true
"a" < "b"       # => true
2 > 1           # => true
1 <= 1          # => true
2 >= 1          # => true

# Comparisons are non-associative: 1 < 2 < 3 is a syntax error.
# Use `and` to chain: 1 < 2 and 2 < 3

# ============================================================
# 5. Logical operators
# ============================================================

# `and`, `or`, `not` — keywords, not symbols.
true and true       # => true
true and false      # => false
false or true       # => true
not true            # => false
not false           # => true

# Truthiness: false, nothing, 0, and "" are falsy.
# Everything else is truthy.
not 0               # => true
not ""              # => true
not nothing         # => true
not 1               # => false
not "hi"            # => false

# Short-circuit evaluation (Python semantics):
# `and` returns the first falsy operand, or the last operand.
# `or` returns the first truthy operand, or the last operand.
1 and 2             # => 2
0 and 2             # => 0
0 or 2              # => 2
false or 0          # => 0

# Precedence: not > and > or
true or true and false  # => true  (true or (true and false))
not true and false      # => false ((not true) and false)

# `not` binds looser than comparisons (like Python).
not 1 < 2           # => false (not (1 < 2))

# ============================================================
# 6. Variables
# ============================================================

# Declare with `my`, assign with `=`.
my x = 10
x               # => 10

# Reassign:
x = 20
x               # => 20

# Declarations can chain (right-associative).
my a = my b = 5
a               # => 5
b               # => 5

# Using an undeclared variable is a runtime error.
# y              # => ERR undefined variable 'y'

# ============================================================
# 7. If/else expressions
# ============================================================

# if/else is an expression — it returns a value.
if true then 1 else 2 end    # => 1
if false then 1 else 2 end   # => 2

# The else branch is optional. Without it, false returns nothing.
if false then 1 end           # => nothing
if true then 42 end           # => 42

# Multi-line bodies:
if 1 < 2 then
  my x = 10
  x + 5        # last expression is the return value
else
  0
end             # => 15

# else if (no extra `end` needed):
my score = 85
if score >= 90 then
  "A"
else if score >= 80 then
  "B"
else
  "C"
end             # => "B"

# Use if/else in assignments:
my result = if 1 < 2 then "yes" else "no" end
result          # => yes

# ============================================================
# 8. While loops
# ============================================================

# while...do...end is a loop expression.
my i = 0
while i < 5 do
  i = i + 1
end                 # => 5 (last body value)

# A loop that never runs returns nothing.
while false do 42 end   # => nothing

# Single-line form:
my j = 10
while j > 0 do j = j - 1 end   # => 0

# Multi-expression body:
my k = 0
while k < 3 do
  say(k)
  k = k + 1
end
# prints: 0, 1, 2
# => 3

# while is an expression — you can use it in assignments.
my n = 0
my total = while n < 4 do
  n = n + 1
end
total               # => 4

# Nested loops work as expected.
my outer = 0
my count = 0
while outer < 3 do
  my inner = 0
  while inner < 2 do
    count = count + 1
    inner = inner + 1
  end
  outer = outer + 1
end
count               # => 6

# ============================================================
# 9. say() for output
# ============================================================

# say() prints a value followed by a newline. Returns nothing.
say("hello")    # prints: hello
say(42)         # prints: 42
say(1 + 2)      # prints: 3
say(true)       # prints: true
say(nothing)    # prints: nothing

# ============================================================
# 10. Running Cutlet programs
# ============================================================

# Save code to a file (e.g., hello.cutlet):
#
#   say("Hello, world!")
#   my x = 10
#   my y = 20
#   say(x + y)
#
# Run it:
#   cutlet run hello.cutlet
#
# Output:
#   Hello, world!
#   30
#
# In file mode, only say() produces output.
# The final expression value is NOT printed (unlike the REPL).

# ============================================================
# 11. The REPL
# ============================================================

# Start an interactive REPL:
#   cutlet repl
#
# The REPL evaluates each expression and prints the result.
# Multi-line input is supported — the prompt changes to ... when
# your input is incomplete (unclosed if/end, parentheses, etc.).
#
# You can also pipe input:
#   echo "1 + 2" | cutlet repl
#
# Debug flags:
#   cutlet repl --tokens         # show tokenizer output
#   cutlet repl --ast            # show AST output
#   cutlet repl --tokens --ast   # show both
#
# For networked REPL (TCP server/client):
#   cutlet repl --listen              # start a TCP REPL server
#   cutlet repl --connect             # connect to a running server
#   cutlet repl --listen :9000        # listen on custom port
#   cutlet repl --connect :9000       # connect to custom port
```
