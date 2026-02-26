# Learn Cutlet in Y Minutes

Cutlet is a dynamic programming language written in C, designed primarily for
writing better shell scripts. It replaces Bash for anything beyond trivial
one-liners, combining the expressiveness of Python, Ruby, Lua, and JavaScript
with first-class support for running subprocesses, building pipelines, and
scripting your system.

Comments start with `#` and go to the end of the line.

## 1. Numbers and arithmetic

Numbers are 64-bit floating point (doubles).

```cutlet
42          # => 42
0           # => 0
3.14        # => 3.14
0.5         # => 0.5
```

Decimal literals need at least one digit before the dot. `.5` is NOT a valid number (the dot starts an operator). `5.` is NOT a decimal — it's the integer 5 followed by a dot operator.

Arithmetic works how you'd expect.

```cutlet
1 + 2       # => 3
10 - 3      # => 7
2 * 3       # => 6
7 / 2       # => 3.5
0.5 + 0.5   # => 1
3.14 * 2    # => 6.28
```

Modulo with `%` uses Python-style semantics: the result has the sign of the divisor.

```cutlet
10 % 3      # => 1
-7 % 3      # => 2   (not -1 like C)
7 % -3      # => -2  (not 1 like C)
```

Exponentiation with `**` is right-associative.

```cutlet
2 ** 10     # => 1024
2 ** 3 ** 2 # => 512 (2 ** (3 ** 2) = 2 ** 9)
```

Unary minus.

```cutlet
-3          # => -3
-(-3)       # => 3
```

Standard precedence: `**` > unary `-` > `*` `/` `%` > `+` `-`.

```cutlet
1 + 2 * 3   # => 7    (not 9)
(1 + 2) * 3 # => 9    (parentheses override)
-2 ** 2     # => -4   (-(2 ** 2), not (-2) ** 2)
```

Division and modulo by zero are runtime errors.

```cutlet
# 1 / 0     # => ERR division by zero
# 1 % 0     # => ERR modulo by zero
```

## 2. Strings

Strings are double-quoted. No escape sequences yet — what you type is what you get. Strings can contain any characters except newlines and double quotes.

```cutlet
"hello"         # => hello
"hello world"   # => hello world
""              # => (empty string)
```

Concatenation with `++` (two plusses). Both sides must be strings.

```cutlet
"hello" ++ " world"     # => hello world
"a" ++ "b" ++ "c"       # => abc
```

`++` is strict — non-string operands are a runtime error.

```cutlet
# "score: " ++ 42       # => ERR ++ requires strings, got string and number
```

Use `str()` to explicitly convert any value to a string.

```cutlet
"score: " ++ str(42)         # => score: 42
"alive: " ++ str(true)       # => alive: true
"value: " ++ str(nothing)    # => value: nothing
str(3.14)                    # => 3.14
str("hi")                    # => hi (identity — already a string)
```

`+` does NOT work on strings — it's only for numbers.

```cutlet
# "a" + "b"             # => ERR arithmetic requires numbers
```

`++` binds looser than `+` but tighter than comparison.

```cutlet
str(1 + 2) ++ str(3 + 4)    # => 37 (+ binds tighter than ++)
```

`++` is right-associative (like Lua).

```cutlet
"a" ++ "b" ++ "c"       # => abc (same as "a" ++ ("b" ++ "c"))
```

## 3. Booleans and nothing

```cutlet
true        # => true
false       # => false
nothing     # => nothing
```

`nothing` is Cutlet's null/nil value.

## 4. Comparison operators

Equality works across all types.

```cutlet
1 == 1          # => true
1 == 2          # => false
"a" == "a"      # => true
1 == "1"        # => false (different types are never equal)
nothing == nothing  # => true

1 != 2          # => true
```

Ordering works for numbers and strings (but not across types).

```cutlet
1 < 2           # => true
"a" < "b"       # => true
2 > 1           # => true
1 <= 1          # => true
2 >= 1          # => true
```

Comparisons are non-associative: `1 < 2 < 3` is a syntax error. Use `and` to chain: `1 < 2 and 2 < 3`.

## 5. Logical operators

`and`, `or`, `not` are keywords, not symbols.

```cutlet
true and true       # => true
true and false      # => false
false or true       # => true
not true            # => false
not false           # => true
```

Truthiness: `false`, `nothing`, `0`, and `""` are falsy. Everything else is truthy.

```cutlet
not 0               # => true
not ""              # => true
not nothing         # => true
not 1               # => false
not "hi"            # => false
```

Short-circuit evaluation uses Python semantics: `and` returns the first falsy operand (or the last operand), `or` returns the first truthy operand (or the last operand).

```cutlet
1 and 2             # => 2
0 and 2             # => 0
0 or 2              # => 2
false or 0          # => 0
```

Precedence: `not` > `and` > `or`.

```cutlet
true or true and false  # => true  (true or (true and false))
not true and false      # => false ((not true) and false)
```

`not` binds looser than comparisons (like Python).

```cutlet
not 1 < 2           # => false (not (1 < 2))
```

## 6. Variables

Declare with `my`, assign with `=`.

```cutlet
my x = 10
x               # => 10
```

Reassign:

```cutlet
x = 20
x               # => 20
```

Declarations can chain (right-associative).

```cutlet
my a = my b = 5
a               # => 5
b               # => 5
```

Kebab-case identifiers — dashes are allowed inside names. A dash is part of the name when it's immediately followed by a letter.

```cutlet
my my-var = 42
my-var          # => 42

my compute-sum = 100
compute-sum     # => 100
```

Kebab and underscore names are distinct variables.

```cutlet
my x-y = 10
my x_y = 20
x-y             # => 10
x_y             # => 20
```

Subtraction still works — just add spaces around the minus sign.

```cutlet
my foo-bar = 10
foo-bar - 3     # => 7 (foo-bar is one variable, `- 3` is subtraction)
```

These are NOT kebab identifiers (dash not followed by a letter):
- `foo-3` — variable `foo`, minus, number 3
- `foo--bar` — variable `foo`, minus, minus, variable `bar`
- `foo - bar` — variable `foo`, minus, variable `bar`

Kebab-case works for function names and parameters too.

```cutlet
fn add-one(n) is n + 1
say(add-one(5))         # prints: 6

fn greet-user(user-name) is
  "hello " ++ user-name
end
say(greet-user("alice")) # prints: hello alice
```

Using an undeclared variable is a runtime error.

```cutlet
# y              # => ERR undefined variable 'y'
```

## 7. If/else expressions

`if`/`else` is an expression — it returns a value.

```cutlet
if true then 1 else 2 end    # => 1
if false then 1 else 2 end   # => 2
```

The else branch is optional. Without it, a false condition returns `nothing`.

```cutlet
if false then 1 end           # => nothing
if true then 42 end           # => 42
```

Single-line forms: when the body is on the same line as `then`, you can omit `end`.

```cutlet
if true then 42               # => 42
if true then 1 else 2         # => 1
if false then 1               # => nothing
```

Single-line else-if chains:

```cutlet
my x = 5
if x > 0 then "pos" else if x < 0 then "neg" else "zero"  # => pos
```

Multi-line bodies (with newline after `then`) require `end`:

```cutlet
if 1 < 2 then
  my x = 10
  x + 5        # last expression is the return value
else
  0
end             # => 15
```

Else-if (no extra `end` needed):

```cutlet
my score = 85
if score >= 90 then
  "A"
else if score >= 80 then
  "B"
else
  "C"
end             # => "B"
```

Use if/else in assignments:

```cutlet
my result = if 1 < 2 then "yes" else "no"
result          # => yes
```

## 8. While loops

`while`...`do`...`end` is a loop expression.

```cutlet
my i = 0
while i < 5 do
  i = i + 1
end                 # => 5 (last body value)
```

A loop that never runs returns `nothing`.

```cutlet
while false do 42 end   # => nothing
```

Single-line form: when the body is on the same line as `do`, you can omit `end`.

```cutlet
my j = 10
while j > 0 do j = j - 1       # => 0
```

Multi-expression body:

```cutlet
my k = 0
while k < 3 do
  say(k)
  k = k + 1
end
# prints: 0, 1, 2
# => 3
```

`while` is an expression — you can use it in assignments.

```cutlet
my n = 0
my total = while n < 4 do
  n = n + 1
end
total               # => 4
```

Nested loops work as expected.

```cutlet
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
```

`break` exits the loop immediately.

```cutlet
my m = 0
while m < 100 do
  m = m + 1
  if m == 5 then break
end
m                   # => 5
```

`break` with a value — the loop evaluates to that value.

```cutlet
my found = while true do
  break "done"
end
found               # => done
```

Bare `break` (no value) — the loop evaluates to `nothing`.

```cutlet
while true do break         # => nothing
```

`continue` skips the rest of the current iteration.

```cutlet
my p = 0
while p < 6 do
  p = p + 1
  if p % 2 == 0 then continue
  say(p)
end
# prints: 1, 3, 5
```

In nested loops, `break` and `continue` affect the innermost loop only.

```cutlet
my q = 0
while q < 3 do
  q = q + 1
  my r = 0
  while r < 3 do
    r = r + 1
    if r == 2 then continue
    say(str(q) ++ "-" ++ str(r))
  end
end
# prints: 1-1, 1-3, 2-1, 2-3, 3-1, 3-3
```

`break` and `continue` outside a loop are compile errors.

```cutlet
# break        # => ERR 'break' outside of loop
# continue     # => ERR 'continue' outside of loop
```

## 9. User-defined functions

Define a function with `fn name(params) is body end`.

```cutlet
fn greet(name) is
  say("hello " ++ name)
end

greet("world")      # prints: hello world
```

Functions are expressions — the last expression in the body is the return value.

```cutlet
fn double(x) is
  x * 2
end
say(double(21))     # prints: 42
```

Single-line form: when the body is on the same line as `is`, you can omit `end`.

```cutlet
fn triple(x) is x * 3
say(triple(7))      # prints: 21
```

Zero-parameter functions:

```cutlet
fn meaning_of_life() is 42
say(meaning_of_life())  # prints: 42
```

Multi-line body with local variables:

```cutlet
fn sum_of_squares(a, b) is
  my a2 = a ** 2
  my b2 = b ** 2
  a2 + b2
end
say(sum_of_squares(3, 4))  # prints: 25
```

Recursion works — functions are bound globally before the body runs.

```cutlet
fn factorial(n) is
  if n <= 1 then 1 else n * factorial(n - 1)
end
say(factorial(5))       # prints: 120

fn fib(n) is
  if n <= 1 then n else fib(n - 1) + fib(n - 2)
end
say(fib(10))            # prints: 55
```

Functions are first-class values.

```cutlet
my f = fn square(x) is x ** 2
say(f)                  # prints: <fn square>
say(f(7))               # prints: 49
```

`fn ... is ...` is an expression, so the function definition itself returns the function value. In the REPL: `fn add(a, b) is a + b` evaluates to `<fn add>`.

Calling with wrong arity is a runtime error.

```cutlet
# greet()             # => ERR 'greet' expects 1 argument, got 0
# greet("a", "b")     # => ERR 'greet' expects 1 argument, got 2
```

Calling a non-function is a runtime error.

```cutlet
# my x = 42
# x()                 # => ERR cannot call value of type number
```

## 9b. Anonymous functions

Anonymous functions have no name — just `fn(params) is body end`. Single-line form works here too (omit `end`).

```cutlet
my double = fn(x) is x * 2
say(double(21))         # prints: 42
```

They work exactly like named functions, but don't bind a global.

```cutlet
my add = fn(a, b) is a + b
say(add(3, 4))          # prints: 7
```

Zero-parameter anonymous function:

```cutlet
my greeting = fn() is "hello world"
say(greeting())         # prints: hello world
```

Multi-line body works the same way:

```cutlet
my sum_sq = fn(a, b) is
  my a2 = a ** 2
  my b2 = b ** 2
  a2 + b2
end
say(sum_sq(3, 4))       # prints: 25
```

Anonymous functions are expressions — they evaluate to a function value.

```cutlet
fn(x) is x + 1         # => <fn>
```

You can reassign a variable to different anonymous functions.

```cutlet
my op = fn(x) is x + 10
say(op(5))              # prints: 15
op = fn(x) is x * 10
say(op(5))              # prints: 50
```

## 9c. Block scoping

Inside a function, `my` inside an `if` or `while` body is scoped to that block — it's not visible after the `end`.

```cutlet
fn scoping_demo() is
  my x = 1
  if true then
    my x = 99        # shadows the outer x inside this block
    say(x)           # prints: 99
  end
  x                  # => 1 (outer x is unchanged)
end
say(scoping_demo())  # prints: 1
```

Variables declared in a while body are cleaned up each iteration.

```cutlet
fn loop_scoping() is
  my total = 0
  my i = 0
  while i < 3 do
    my x = i + 1     # x is fresh each iteration
    total = total + x
    i = i + 1
  end
  total               # => 6 (1 + 2 + 3)
end
say(loop_scoping())   # prints: 6
```

Else branches have their own scope too.

```cutlet
fn else_scope() is
  if false then
    my a = 1
  else
    my b = 2
    b                 # => 2 (b is visible here)
  end
  # a and b are both out of scope here
end
```

`break` and `continue` properly clean up block-scoped variables.

```cutlet
fn break_cleanup() is
  while true do
    my x = 42
    break x           # x is cleaned up before the jump
  end                 # => 42
end
say(break_cleanup())  # prints: 42
```

Nested blocks work as expected — inner scopes can see outer locals.

```cutlet
fn nested_scopes() is
  if true then
    my a = 10
    if true then
      my b = 20
      a + b           # => 30 (inner sees outer)
    end
  end
end
say(nested_scopes())  # prints: 30
```

At the top level (outside functions), `my` always creates a global variable. Block scoping only applies inside functions.

## 9d. Early return

By default, a function returns the value of its last expression. Use `return` when you need to exit a function early.

Guard clause — return early for special cases:

```cutlet
fn abs(x) is
  if x < 0 then return -x end
  x
end
say(abs(-7))            # prints: 7
say(abs(3))             # prints: 3
```

Bare `return` (no expression) returns `nothing`.

```cutlet
fn maybe_greet(name) is
  if name == "" then return end
  say("hello " ++ name)
end
maybe_greet("")         # does nothing, returns nothing
maybe_greet("world")    # prints: hello world
```

Return from inside a loop exits the function, not just the loop.

```cutlet
fn find_first_multiple(factor, limit) is
  my i = 1
  while i <= limit do
    if i % factor == 0 then return i end
    i = i + 1
  end
  nothing
end
say(find_first_multiple(3, 10))  # prints: 3
```

`return` outside a function is a compile error. `return` is a reserved keyword — you can't use it as a variable name.

```cutlet
# return 42             # => ERR 'return' outside of function
# my return = 5         # => ERR parse error
```

```cutlet
# ============================================================
# 10. Higher-order functions
# ============================================================

# Functions are first-class values, so you can pass them as arguments.
# This enables higher-order function patterns like apply, map, compose.

# Pass a function as an argument:
fn apply(f, x) is f(x)
fn inc(x) is x + 1
say(apply(inc, 5))      # prints: 6

# Works with built-in functions too:
apply(say, "hello")     # prints: hello

# Compose two functions:
fn compose(f, g, x) is f(g(x))
fn double(x) is x * 2
say(compose(double, inc, 5))   # prints: 12 (double(inc(5)))

# Anonymous functions work as arguments (single-line, no `end`):
say(apply(fn(x) is x ** 2, 4))   # prints: 16

# Store a function in a local variable and call it:
fn run_twice(f, x) is
  my result = f(x)
  f(result)
end
say(run_twice(inc, 5))   # prints: 7 (inc(inc(5)))

# A simple "each" pattern using a counter loop:
fn times(n, f) is
  my i = 0
  while i < n do
    f(i)
    i = i + 1
  end
end
times(3, fn(i) is say("iteration " ++ str(i)))
# prints: iteration 0, iteration 1, iteration 2

# ============================================================
# 11. Closures
# ============================================================

# A closure is a function that captures variables from its enclosing
# scope. When an inner function references a variable from an outer
# function, it "closes over" that variable.

# Basic capture — inner function reads from outer scope:
fn make_greeter(name) is
  fn() is "hello " ++ name end
end
my greet = make_greeter("world")
say(greet())            # prints: hello world

# Closures capture by reference — mutations are shared:
fn make_counter() is
  my count = 0
  fn() is
    count = count + 1
    count
  end
end
my counter = make_counter()
say(counter())          # prints: 1
say(counter())          # prints: 2
say(counter())          # prints: 3

# Closures outlive their enclosing function. When make_counter()
# returns, `count` is moved from the stack to the heap so the
# closure can keep using it.

# Factory pattern — each call creates independent state:
my c1 = make_counter()
my c2 = make_counter()
c1()
c1()
say(c1())               # prints: 3
say(c2())               # prints: 1 (c2 has its own count)

# Adder factory — capture a parameter:
fn make_adder(x) is
  fn(y) is x + y end
end
my add5 = make_adder(5)
my add10 = make_adder(10)
say(add5(3))            # prints: 8
say(add10(3))           # prints: 13

# Two closures sharing the same captured variable:
fn make_cell() is
  my value = 0
  fn get() is value end
  fn set(v) is value = v end
end

# Deep nesting — a closure can capture from any enclosing scope,
# not just the immediately enclosing one:
fn outer() is
  my x = 100
  fn middle() is
    fn inner() is x end
    inner()
  end
  middle()
end
say(outer())            # prints: 100

# Closures work with all the features you'd expect:
# - Capture parameters (not just `my` locals)
# - Use inside while loops
# - Pass as arguments to higher-order functions

# ============================================================
# 12. Arrays
# ============================================================

# Arrays are ordered, indexable collections of values.
# They use value semantics with copy-on-write for efficiency.

# Array literals use square brackets.
[1, 2, 3]              # => [1, 2, 3]
[]                      # => [] (empty array)
[1, "two", true]        # => [1, two, true] (mixed types)
[[1, 2], [3, 4]]        # => [[1, 2], [3, 4]] (nested)

# Trailing commas are allowed.
[1, 2, 3,]             # => [1, 2, 3]

# Multiline arrays work — newlines inside brackets are ignored.
my colors = [
  "red",
  "green",
  "blue",
]
colors                  # => [red, green, blue]

# Elements can be any expression.
[1 + 1, 2 * 3, 4 ** 2] # => [2, 6, 16]

# Indexing is zero-based with square brackets.
my xs = [10, 20, 30, 40, 50]
xs[0]                   # => 10
xs[4]                   # => 50

# Negative indices wrap from the end.
xs[-1]                  # => 50
xs[-2]                  # => 40

# Out-of-bounds is a runtime error.
# xs[5]                # => ERR index out of bounds
# xs[-6]               # => ERR index out of bounds

# Index assignment.
xs[0] = 99
xs                      # => [99, 20, 30, 40, 50]

# Copy-on-write: assigning an array shares the backing store.
# Mutation triggers a copy, so the original is never changed.
my ys = xs
ys[0] = 1
ys                      # => [1, 20, 30, 40, 50]
xs                      # => [99, 20, 30, 40, 50] (unchanged)

# Nested indexing chains.
my grid = [[1, 2], [3, 4]]
grid[1][0]              # => 3

# Concatenation with ++ (both sides must be arrays).
[1, 2] ++ [3, 4]       # => [1, 2, 3, 4]
[] ++ [1]               # => [1]

# ++ still works for strings when neither side is an array.
"a" ++ "b"              # => ab

# Mixing arrays with non-arrays in ++ is an error.
# [1] ++ 2             # => ERR cannot concatenate array with number

# len() returns the number of elements (also works on strings).
len([1, 2, 3])          # => 3
len([])                 # => 0
len("hello")            # => 5

# push() returns a new array with an element appended.
push([1, 2], 3)         # => [1, 2, 3]
push([], "a")           # => [a]

# pop() returns a new array without the last element.
pop([1, 2, 3])          # => [1, 2]
pop([1])                # => []
# pop([])              # => ERR pop() on empty array

# push() and pop() do NOT mutate — they return new arrays.
my nums = [1, 2]
push(nums, 3)
nums                    # => [1, 2] (unchanged)

# Equality is structural and recursive.
[1, 2, 3] == [1, 2, 3]     # => true
[1, 2] == [1, 2, 3]        # => false
[] == []                    # => true
[[1]] == [[1]]              # => true

# Truthiness: non-empty arrays are truthy, empty arrays are falsy.
not []                  # => true
not [1]                 # => false

# Building an array in a loop.
my squares = []
my i = 1
while i <= 5 do
  squares = push(squares, i ** 2)
  i = i + 1
end
squares                 # => [1, 4, 9, 16, 25]

# ============================================================
# 13. Maps (dictionaries)
# ============================================================

# Maps are key-value collections. They use value semantics with
# copy-on-write, just like arrays.

# Map literals use curly braces. Bare identifiers become string keys.
{name: "alice", age: 30}   # => {name: alice, age: 30}
{}                          # => {} (empty map)

# Trailing commas are allowed.
{a: 1, b: 2,}             # => {a: 1, b: 2}

# Multiline maps work — newlines inside braces are ignored.
my person = {
  name: "alice",
  age: 30,
  role: "admin",
}
person                      # => {name: alice, age: 30, role: admin}

# Values can be any expression.
{x: 1 + 2, y: 3 * 4}      # => {x: 3, y: 12}

# Computed keys: use [expr] to use any expression as a key.
{[1 + 2]: "three"}         # => {3: three}
{[true]: "yes", [false]: "no"}  # => {true: yes, false: no}

# To use a variable as a key, you must use computed syntax:
my key = "color"
{[key]: "blue"}             # => {color: blue}
# Without brackets, `key` is treated as the string "key":
{key: "blue"}               # => {key: blue}

# Duplicate keys: last value wins.
{a: 1, a: 2}               # => {a: 2}

# Valid key types: strings, numbers, booleans, nothing.
# Functions are not valid keys (runtime error).

# Indexing with square brackets.
my m = {name: "alice", age: 30}
m["name"]                   # => alice
m["age"]                    # => 30

# Missing keys return nothing (not an error).
m["email"]                  # => nothing

# Use has_key() to distinguish "key absent" from "key present with
# value nothing":
my m2 = {a: nothing}
m2["a"]                     # => nothing
has_key(m2, "a")            # => true  (key exists)
has_key(m2, "b")            # => false (key absent)

# Index assignment. COW: the original is never changed.
my m3 = {x: 10}
my m4 = m3
m4["x"] = 99
m4["y"] = 20
m4                          # => {x: 99, y: 20}
m3                          # => {x: 10} (unchanged)

# Map projection: index with an array of keys to select a sub-map.
my data = {name: "alice", age: 30, email: "a@b.com", role: "admin"}
data[["name", "email"]]     # => {name: alice, email: a@b.com}

# Missing projection keys are silently skipped.
data[["name", "zzz"]]       # => {name: alice}

# Empty projection.
data[[]]                     # => {}

# Merge with ++ (right side wins on key conflicts).
{a: 1, b: 2} ++ {b: 3, c: 4}   # => {a: 1, b: 3, c: 4}
{} ++ {a: 1}                    # => {a: 1}
{a: 1} ++ {}                    # => {a: 1}

# Mixing maps with non-maps in ++ is an error.
# {a: 1} ++ 2            # => ERR cannot concatenate map with number

# Built-in functions.
keys({name: "alice", age: 30})     # => [name, age]  (insertion order)
values({name: "alice", age: 30})   # => [alice, 30]   (insertion order)
has_key({a: 1}, "a")               # => true
has_key({a: 1}, "b")               # => false
len({a: 1, b: 2})                  # => 2
len({})                             # => 0

# keys() and values() return arrays, so you can use them in loops.

# Equality is structural. Key order does not matter.
{a: 1, b: 2} == {b: 2, a: 1}  # => true
{a: 1} == {a: 1, b: 2}        # => false
{} == {}                        # => true
{a: 1} == "hello"               # => false (different types)

# Truthiness: non-empty maps are truthy, empty maps are falsy.
not {}                     # => true
not {a: 1}                 # => false

# Building a map in a loop.
my squares = {}
my i = 1
while i <= 5 do
  squares[str(i)] = i ** 2
  i = i + 1
end
squares                     # => {1: 1, 2: 4, 3: 9, 4: 16, 5: 25}

# ============================================================
# 14. Membership testing with `in`
# ============================================================

# `in` tests whether a value is a member of a collection.
# It works with maps (key lookup), arrays (element search),
# and strings (substring search).

# Map: checks if a key exists.
"name" in {name: "alice", age: 30}    # => true
"email" in {name: "alice"}             # => false

# Works with any key type (computed keys).
1 in {[1]: "one", [2]: "two"}         # => true
true in {[true]: "yes"}               # => true

# Key exists even when the value is nothing.
"a" in {a: nothing}                    # => true

# Array: checks if an element is present (linear scan, equality-based).
42 in [1, 2, 42]                       # => true
99 in [1, 2, 3]                        # => false
"b" in ["a", "b", "c"]                # => true
1 in []                                # => false

# String: checks for a substring.
"lo" in "hello"                        # => true
"xyz" in "hello"                       # => false
"" in "hello"                          # => true  (empty string is always found)
"hello" in "hello"                     # => true  (string contains itself)

# For strings, the left operand must also be a string.
# 42 in "hello"                       # => ERR in requires a string left operand

# `not in` — syntactic sugar for `not (x in y)`.
10 not in [1, 2, 3]                    # => true
1 not in [1, 2, 3]                     # => false
"z" not in {a: 1}                      # => true
"xyz" not in "hello"                   # => true

# `not in` is identical to writing `not ... in ...` explicitly:
not 10 in [1, 2, 3]                    # => true (same as `10 not in [1, 2, 3]`)

# Precedence: `in` is at comparison level (like ==, <, >).
# Arithmetic binds tighter, logical operators bind looser.
1 + 1 in [2, 3]                        # => true  ((1+1) in [2,3])
true and 1 in [1, 2]                   # => true  (true and (1 in [1,2]))
not 5 in [1, 2, 3]                     # => true  (not (5 in [1,2,3]))

# `in` is non-associative (like other comparisons).
# 1 in [1] in [[1]]                   # => ERR parse error

# Composability: `in` works with any expression that produces
# a map, array, or string.
"a" in keys({a: 1, b: 2})             # => true

# Combining with `and`/`or`:
my xs = [1, 2, 3]
5 not in xs and 1 in xs                # => true

# Using `in` with `has_key()`:
# `in` is the idiomatic way for membership testing.
# `has_key()` is still useful in higher-order contexts
# (e.g., passing as a callback), but `in` is preferred for
# direct checks.

# Invalid right operand types are runtime errors.
# 1 in 42                             # => ERR cannot use 'in' with number
# 1 in true                           # => ERR cannot use 'in' with boolean

# ============================================================
# 15. The @ meta-operator
# ============================================================

# The @ meta-operator lifts operators and functions to work across
# arrays. It has two forms: prefix (reduction/fold) and infix
# (element-wise vectorization).

# --- Prefix @op: reduction (fold) ---

# @op array folds an operator across the array from left to right.
@+ [1, 2, 3, 4, 5]     # => 15   (1+2+3+4+5)
@* [1, 2, 3, 4, 5]     # => 120  (1*2*3*4*5)
@- [10, 3, 2]           # => 5    ((10-3)-2)
@++ ["a", "b", "c"]     # => abc

# Single-element arrays return that element.
@+ [42]                 # => 42

# Empty arrays are a runtime error.
# @+ []                # => ERR cannot reduce empty array

# @and and @or fold with short-circuit semantics.
@and [true, true, false]    # => false (stops at first falsy)
@and [1, 2, 3]              # => 3     (all truthy, returns last)
@or [false, 0, "hi"]        # => hi    (stops at first truthy)
@or [false, 0, ""]          # => ""    (all falsy, returns last)

# --- Infix @op: vectorization (element-wise) ---

# expr @op expr applies the operator to matching elements.
[1, 2, 3] @+ [4, 5, 6]     # => [5, 7, 9]
[1, 2, 3] @* [4, 5, 6]     # => [4, 10, 18]
["a", "b"] @++ ["1", "2"]   # => [a1, b2]

# Scalar broadcast: when one operand is a scalar, it's used
# for every element.
[1, 2, 3] @* 10             # => [10, 20, 30]
10 @- [1, 2, 3]             # => [9, 8, 7]
[1, 2, 3] @** 2             # => [1, 4, 9]

# Vectorized comparison produces boolean arrays.
[1, 2, 3] @> 2              # => [false, false, true]
[10, 20, 30] @>= 20         # => [false, true, true]

# Mismatched array lengths are a runtime error.
# [1, 2] @+ [1, 2, 3]     # => ERR array length mismatch

# Both scalars are also an error (use the regular operator instead).
# 1 @+ 2                  # => ERR @ requires at least one array operand

# Precedence follows the inner operator: @* binds tighter than @+.
[1, 2] @+ [3, 4] @* [5, 6]  # => [16, 26]  ([1,2] @+ [15,24])

# --- @fn: custom function reduction and vectorization ---

# @identifier works with user-defined functions too.
# For reduction, the function must take two arguments.
fn max(a, b) is if a > b then a else b end end
@max [3, 1, 4, 1, 5]       # => 5

fn add(a, b) is a + b end
@add [1, 2, 3]              # => 6

# For vectorization, the function is called on matching pairs.
fn mul(a, b) is a * b end
[1, 2, 3] @mul [4, 5, 6]   # => [4, 10, 18]

# Scalar broadcast works with custom functions too.
fn add1(a, b) is a + b end
[1, 2, 3] @add1 10          # => [11, 12, 13]

# --- Boolean mask indexing ---

# When you index an array with a boolean array, it acts as a mask:
# elements where the mask is true are kept.
my xs = [10, 20, 30, 40, 50]
xs[[true, false, true, false, true]]  # => [10, 30, 50]

# All false => empty array.
[1, 2, 3][[false, false, false]]      # => []

# The mask must be the same length as the array.
# [1, 2, 3][[true, false]]   # => ERR mask length mismatch

# The mask must contain only booleans.
# [1, 2, 3][[1, 0, 1]]      # => ERR mask must contain only booleans

# --- Combining @ with mask indexing ---

# This is where @ really shines. Vectorized comparison produces a
# boolean array, which you can use directly as a mask.
my scores = [85, 92, 67, 74, 95]
scores[scores @>= 70]               # => [85, 92, 74, 95]

my data = [1, 2, 3, 4, 5, 6, 7, 8]
data[data @> 3]                      # => [4, 5, 6, 7, 8]

# --- @ on maps ---

# The @ meta-operator works on maps too. Prefix @op reduces (folds)
# over the map's values in insertion order.
@+ {math: 92, english: 87, science: 95}   # => 274
@* {a: 2, b: 3, c: 4}                      # => 24
@++ {first: "hello", second: " world"}     # => hello world

# Single-entry maps return that value. Empty maps are a runtime error.
@+ {x: 42}                # => 42
# @+ {}                   # => ERR cannot reduce empty map

# @and and @or short-circuit over map values, same as arrays.
@and {a: true, b: true, c: false}   # => false
@or {a: false, b: 0, c: "hi"}       # => hi

# Infix @op on two maps: vectorizes by key intersection.
# Only shared keys appear in the result. Non-shared keys are dropped.
{a: 1, b: 2} @+ {a: 10, b: 20}     # => {a: 11, b: 22}
{a: 1, b: 2, c: 3} @+ {b: 10, c: 20, d: 30}  # => {b: 12, c: 23}
{a: 1} @+ {b: 2}                    # => {} (no shared keys)

# Scalar broadcast: when one operand is a scalar, it applies to
# every value in the map.
{a: 1, b: 2} @* 10                  # => {a: 10, b: 20}
100 @- {a: 10, b: 20}               # => {a: 90, b: 80}
{math: 85, english: 90} @>= 88      # => {math: false, english: true}

# Maps and arrays cannot be mixed in vectorized operations.
# {a: 1} @+ [1, 2]       # => ERR cannot vectorize map with array
# [1, 2] @+ {a: 1}       # => ERR cannot vectorize array with map
# Use values() or keys() to convert first.

# --- @: (zip arrays into a map) ---

# @: takes two arrays — keys on the left, values on the right — and
# produces a map. It's the idiomatic way to construct a map from arrays.
["a", "b", "c"] @: [1, 2, 3]        # => {a: 1, b: 2, c: 3}

# Works with any valid key type (strings, numbers, booleans, nothing).
[1, 2] @: ["one", "two"]            # => {1: one, 2: two}
[true, false] @: ["yes", "no"]      # => {true: yes, false: no}

# Empty arrays produce an empty map.
[] @: []                              # => {}

# Duplicate keys: last occurrence wins (same as map literal behavior).
["a", "b", "a"] @: [1, 2, 3]        # => {a: 3, b: 2}

# Compose with other @op operations.
my names = ["alice", "bob"]
my scores = [85, 92]
names @: (scores @* 1.1)             # => {alice: 93.5, bob: 101.2}

# Map inversion using keys() and values().
my m = {x: 1, y: 2}
values(m) @: keys(m)                 # => {1: x, 2: y}

# Round-trip: decompose a map and zip it back.
my m = {a: 1, b: 2}
keys(m) @: values(m)                 # => {a: 1, b: 2}

# Mismatched array lengths are a runtime error.
# ["a"] @: [1, 2]                   # => ERR array length mismatch in @:
# Non-array operands are a runtime error.
# "a" @: [1]                        # => ERR @: requires two arrays
# Invalid key types (e.g. functions) are a runtime error.
# @: cannot be used as a prefix (reduction) operator.
# @: [1, 2, 3]                      # => ERR ':' cannot be used as a reduction

# --- Composability ---

# Since keys(), values() return arrays and @ works on both arrays
# and maps, you can mix freely.
my scores = {math: 92, english: 87, science: 95}
@+ values(scores)                    # => 274 (reduce the values array)

# Vectorize then reduce: total of element-wise product.
@+ ({a: 2, b: 3} @* {a: 10, b: 20}) # => 80

# ============================================================
# 16. say() for output
# ============================================================

# say() prints a value followed by a newline. Returns nothing.
# say() auto-formats any value type (unlike ++, which requires strings).
say("hello")    # prints: hello
say(42)         # prints: 42
say(1 + 2)      # prints: 3
say(true)       # prints: true
say(nothing)    # prints: nothing

# str() converts any value to a string. Use it with ++ for mixed-type output.
say("result: " ++ str(42))   # prints: result: 42

# ============================================================
# 17. Running Cutlet programs
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
# 18. The REPL
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
