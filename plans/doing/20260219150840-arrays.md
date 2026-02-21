# Array + Meta-Operator Implementation Plan

Depends on: user-defined functions (PLAN.md steps 1-9 complete). Assumes `VAL_FUNCTION`, `ObjFunction`, `NativeFn`, stack-based `OP_CALL`, VM call frames, and `OP_GET_LOCAL`/`OP_SET_LOCAL` are all in place.

---

## Design

### Array literals

```cutlet
[]                        # empty array
[1, 2, 3]                # numbers
["a", "b", "c"]          # strings
[1, "two", true, nothing] # mixed types
[1 + 2, 3 * 4]           # expressions as elements
[[1, 2], [3, 4]]         # nested arrays
```

Trailing commas are allowed: `[1, 2, 3,]`.

### Array indexing

```cutlet
my xs = [10, 20, 30, 40, 50]
xs[0]               # => 10
xs[4]               # => 50
xs[-1]              # => 50  (negative wraps from end)
xs[-2]              # => 40
xs[0] = 99          # index assignment
xs                   # => [99, 20, 30, 40, 50]
```

Out-of-bounds indices produce a runtime error.

### Array concatenation with `++`

```cutlet
[1, 2] ++ [3, 4]         # => [1, 2, 3, 4]
[1, 2] ++ [3]             # => [1, 2, 3]
```

Both operands must be arrays. `[1, 2] ++ 3` is an error (use `push()` to append a single element). When neither operand is an array, `++` retains its existing string concatenation behavior.

### `@` meta-operator

`@` lifts an operator or function to work across arrays. It has two forms:

**Prefix `@op expr` — reduction (fold):**

```cutlet
@+ [1, 2, 3, 4, 5]       # => 15       (1+2+3+4+5)
@* [1, 2, 3, 4, 5]       # => 120      (1*2*3*4*5)
@++ ["a", "b", "c"]      # => "abc"
@and [true, true, false]  # => false
@or [false, false, true]  # => true
```

Left fold: `@+ [a, b, c]` = `(a + b) + c`. Empty arrays produce a runtime error. Single-element arrays return that element.

**Prefix `@fn expr` — reduction with custom function:**

```cutlet
fn max(a, b) is if a > b then a else b end end
@max [3, 1, 4, 1, 5]     # => 5
```

**Infix `expr @op expr` — vectorized (element-wise) operation:**

```cutlet
[1, 2, 3] @+ [4, 5, 6]   # => [5, 7, 9]
[1, 2, 3] @* 10           # => [10, 20, 30]  (scalar broadcast)
10 @+ [1, 2, 3]           # => [11, 12, 13]  (scalar broadcast)
[1, 2, 3] @> 2            # => [false, false, true]
```

Mismatched array lengths produce a runtime error. When one operand is a scalar and the other is an array, the scalar is broadcast.

**Infix `expr @fn expr` — vectorized with custom function:**

```cutlet
fn max(a, b) is if a > b then a else b end end
[1, 5, 3] @max [4, 2, 6]  # => [4, 5, 6]
```

### Boolean mask indexing

```cutlet
my xs = [10, 20, 30, 40, 50]
xs[[true, false, true, false, true]]  # => [10, 30, 50]

# Combined with vectorized comparison:
my scores = [85, 92, 67, 74, 95]
scores[scores @>= 70]                 # => [85, 92, 74, 95]
```

When the index expression is an array of booleans, it acts as a mask: elements where the mask is `true` are kept. The mask array must be the same length as the indexed array.

### Built-in array functions

```cutlet
len([1, 2, 3])            # => 3
len([])                   # => 0
push([1, 2], 3)           # => [1, 2, 3]  (returns new array)
pop([1, 2, 3])            # => [1, 2]     (returns new array without last element)
```

`push()` and `pop()` return new arrays. They do not mutate the original. This avoids the need for reference semantics in the initial implementation.

### Value semantics

Arrays use **value semantics with structural sharing** (reference-counted backing store):

- `ObjArray` is a heap-allocated struct with a reference count.
- `value_clone()` for `VAL_ARRAY` increments the refcount (cheap shallow copy).
- `value_free()` for `VAL_ARRAY` decrements the refcount; frees when it reaches 0.
- Mutation operations (`push`, `pop`, index assignment) **copy-on-write**: if refcount > 1, copy the backing array first, then mutate the copy.

This gives predictable value semantics (no spooky action at a distance) while avoiding excessive copying:

```cutlet
my xs = [1, 2, 3]
my ys = xs             # ys shares backing store with xs (refcount 2)
ys[0] = 99             # triggers COW: ys gets its own copy
xs                      # => [1, 2, 3]  (unchanged)
ys                      # => [99, 2, 3]
```

---

## Architecture changes

### New token type: `TOK_META`

The tokenizer recognizes `@` as the start of a meta-operator token. It consumes the `@` plus the following operator or identifier, emitting a single `TOK_META` token.

```
@+       → TOK_META  value="+"
@*       → TOK_META  value="*"
@**      → TOK_META  value="**"
@++      → TOK_META  value="++"
@==      → TOK_META  value="=="
@and     → TOK_META  value="and"
@or      → TOK_META  value="or"
@my_func → TOK_META  value="my_func"
```

Tokenizer logic when `@` is encountered:
1. Consume `@`.
2. If next char is ASCII letter or `_` → scan identifier → emit `TOK_META`.
3. Else if next char is a solo symbol (`+`, `-`, `/`, `%`) → consume one char → emit `TOK_META`.
4. Else if next char is a symbol char → scan operator run (normal multi-char operator rules) → emit `TOK_META`.
5. Else → emit `TOK_ERROR` ("expected operator or identifier after @").

Also add `[` and `]` to the solo symbols list so they are always single-character tokens.

### New AST nodes

Add to `AstNodeType` enum:

| Node | Fields used | Purpose |
|------|------------|---------|
| `AST_ARRAY` | `children`, `child_count` | Array literal `[expr, ...]` |
| `AST_INDEX` | `left` (array), `right` (index) | Index read `expr[expr]` |
| `AST_INDEX_ASSIGN` | `left` (AST_INDEX), `right` (value) | Index write `expr[expr] = expr` |
| `AST_REDUCE` | `value` (op/name), `left` (operand) | Prefix `@op expr` |
| `AST_VECTORIZE` | `value` (op/name), `left`, `right` | Infix `expr @op expr` |

S-expression format:
- `AST_ARRAY`: `[ARRAY [elem1] [elem2] ...]`, `[ARRAY]` for empty.
- `AST_INDEX`: `[INDEX [array] [index]]`.
- `AST_INDEX_ASSIGN`: `[INDEX_ASSIGN [array] [index] [value]]`.
- `AST_REDUCE`: `[REDUCE op [operand]]`.
- `AST_VECTORIZE`: `[VECTORIZE op [left] [right]]`.

### New value type: `VAL_ARRAY` with `ObjArray`

```c
/* Heap-allocated backing store for arrays. Reference-counted. */
typedef struct {
    size_t refcount;   /* Reference count (1 on creation). */
    Value *data;       /* Owned array of Values. */
    size_t count;      /* Number of elements. */
    size_t capacity;   /* Allocated capacity. */
} ObjArray;
```

Value changes (`value.h`):
- Add `VAL_ARRAY` to `ValueType`.
- Add `ObjArray *array` field to the `Value` struct.
- `make_array(ObjArray *arr)` — wraps an ObjArray in a Value (takes ownership).
- `value_format()`: `"[1, 2, 3]"` — comma-separated formatted elements.
- `value_free()`: decrement refcount, free ObjArray + its elements when refcount reaches 0.
- `value_clone()`: increment refcount, copy the pointer (shallow).
- `is_truthy()`: non-empty arrays are truthy, empty arrays are falsy.
- Equality: two arrays are equal if same length and all elements are pairwise equal.

ObjArray utility functions (`value.c`):
- `obj_array_new()` — allocate empty ObjArray with refcount 1.
- `obj_array_push(ObjArray *arr, Value v)` — append, grow if needed.
- `obj_array_clone_deep(ObjArray *src)` — full copy with refcount 1 (for COW).
- `obj_array_ensure_owned(Value *v)` — if refcount > 1, replace with a deep clone (COW helper).

### New opcodes

| Opcode | Operand | Stack effect | Description |
|--------|---------|-------------|-------------|
| `OP_ARRAY` | 1-byte count | pop N, push 1 | Build array from top N stack values |
| `OP_INDEX_GET` | — | pop 2 (array, index), push 1 | Read `array[index]` |
| `OP_INDEX_SET` | — | pop 3 (array, index, value), push 1 | Write `array[index] = value`, push value |
| `OP_REDUCE` | 1-byte op | pop 1 (array), push 1 | Fold array with built-in operator |
| `OP_VECTORIZE` | 1-byte op | pop 2, push 1 | Element-wise operation on arrays |

`OP_REDUCE` and `OP_VECTORIZE` encode the inner operator as a 1-byte operand using the existing `OpCode` enum values (e.g., `OP_ADD`, `OP_MULTIPLY`, `OP_CONCAT`, etc.). This also covers `OP_LESS`, `OP_GREATER`, `OP_EQUAL`, `OP_NOT_EQUAL`, `OP_LESS_EQUAL`, `OP_GREATER_EQUAL`, and logical ops `OP_NOT` (for `@not` prefix only — treated specially).

For `@and` and `@or`: these need short-circuit semantics. `@and` folds left, stopping early if any element is falsy. `@or` folds left, stopping early if any element is truthy. Encoded as `OP_REDUCE` with dedicated op bytes (e.g., reuse placeholder values or add `OP_AND`/`OP_OR` as op-byte-only constants).

For custom functions (`@ident`): the compiler emits loop bytecode instead of `OP_REDUCE`/`OP_VECTORIZE`. No new opcode needed — uses existing `OP_CALL`, `OP_GET_LOCAL`, jump, and array access opcodes.

---

## Implementation steps

Each step follows the required process: tests first, confirm failures, get user confirmation, implement, `make test && make check`.

### Step 1: Tokenizer — `[`, `]` as solo symbols + `TOK_META` for `@`

**Tokenizer** (`tokenizer.h`, `tokenizer.c`):
- Add `TOK_META` to `TokenType` enum.
- Add `[` and `]` to `is_solo_symbol()`.
- Add special handling for `@`: when `@` is the current char, enter the meta-operator scanning path (described in architecture section above). Emit `TOK_META` with the inner operator/identifier as the value.
- Update `token_type_str()` for `TOK_META` → `"META"`.

**Tests** (`tests/test_tokenizer.c`):
- `[1, 2]` → `[`, `1`, `,`, `2`, `]`.
- `[]` → `[`, `]`.
- `@+` → `TOK_META "+"`.
- `@*` → `TOK_META "*"`.
- `@**` → `TOK_META "**"`.
- `@++` → `TOK_META "++"`.
- `@==` → `TOK_META "=="`.
- `@my_func` → `TOK_META "my_func"`.
- `@and` → `TOK_META "and"`.
- `@or` → `TOK_META "or"`.
- `@-` → `TOK_META "-"`.
- `@ ` (space after) → `TOK_ERROR`.
- `@` at end of input → `TOK_ERROR`.

**Files touched**: `src/tokenizer.h`, `src/tokenizer.c`, `tests/test_tokenizer.c`.

### Step 2: `VAL_ARRAY` value type

Add the array value type with `ObjArray` backing store and reference counting.

**ObjArray + Value changes** (`value.h`, `value.c`):
- Add `ObjArray` struct definition (as shown in architecture section).
- Add `VAL_ARRAY` to `ValueType`.
- Add `ObjArray *array` to `Value`.
- Constructors:
  - `ObjArray *obj_array_new(void)` — allocate empty, refcount 1.
  - `void obj_array_push(ObjArray *arr, Value v)` — append value (takes ownership).
  - `ObjArray *obj_array_clone_deep(const ObjArray *src)` — full independent copy.
  - `void obj_array_ensure_owned(Value *v)` — COW: if refcount > 1, deep-clone and replace.
  - `Value make_array(ObjArray *arr)` — wrap ObjArray in Value.
- `value_format()`: `"[1, 2, 3]"` format, `"[]"` for empty. Use `value_format()` on each element, comma-separated.
- `value_free()`: decrement `arr->refcount`. If it reaches 0, `value_free()` each element, then free `data`, then free the ObjArray.
- `value_clone()`: `out->array = src->array; src->array->refcount++`.
- `is_truthy()`: `arr->count > 0`.
- Equality in VM: arrays equal if same length and all elements pairwise equal.

**Tests** (`tests/test_value.c` — new file, or add to existing tests):
- Create an empty ObjArray, wrap in Value, format → `"[]"`.
- Create `[1, 2, 3]`, format → `"[1, 2, 3]"`.
- Create `["a", true, nothing]`, format → `"[a, true, nothing]"`.
- Clone a VAL_ARRAY → refcount is 2. Free clone → refcount is 1. Free original → ObjArray freed.
- `is_truthy([1])` → true. `is_truthy([])` → false.
- Nested array: `[[1, 2], [3]]`, format → `"[[1, 2], [3]]"`.
- `obj_array_ensure_owned` with refcount 1 → no-op.
- `obj_array_ensure_owned` with refcount 2 → deep clone, original refcount drops to 1.

**Files touched**: `src/value.h`, `src/value.c`, tests.

### Step 3: Array literals (parser + compiler + VM)

Parse `[expr, expr, ...]`, compile to `OP_ARRAY`, execute in VM.

**AST** (`parser.h`):
- Add `AST_ARRAY` to `AstNodeType`.

**Parser** (`parser.c`):
- Register `[` as a prefix parse rule.
- `parse_array_literal()`: consume `[`. If next token is `]`, return empty array. Otherwise, parse comma-separated expressions (allowing trailing comma). Consume `]`. Store elements in `children`/`child_count`.
- `ast_format()`: `"[ARRAY [elem1] [elem2] ...]"`.
- `ast_free()`: free each child.
- `parser_is_complete()`: track `[` as a bracket that requires `]` (like `(`).

**Opcode** (`chunk.h`):
- Add `OP_ARRAY` with 1-byte count operand.

**Compiler** (`compiler.c`):
- `compile_array()`: compile each child expression (pushes N values), emit `OP_ARRAY [N]`.

**VM** (`vm.c`):
- `OP_ARRAY`: read count byte. Create new ObjArray. Pop `count` values from stack (in reverse order to preserve element order — first element was pushed first, so it's deepest on the stack). Push the resulting VAL_ARRAY.

**Disassembler** (`chunk.c`):
- Format `OP_ARRAY` with count.

**Tests** (`tests/test_parser.c`, `tests/test_eval.c`):
- Parse `[]` → `[ARRAY]`.
- Parse `[1, 2, 3]` → `[ARRAY [NUMBER 1] [NUMBER 2] [NUMBER 3]]`.
- Parse `[1 + 2, 3 * 4]` → correct nested s-expr.
- Parse `[1, 2,]` → same as `[1, 2]` (trailing comma).
- Eval `[]` → `[]`.
- Eval `[1, 2, 3]` → `[1, 2, 3]`.
- Eval `[1 + 1, 2 + 2]` → `[2, 4]`.
- Eval `[[1], [2, 3]]` → `[[1], [2, 3]]`.
- `parser_is_complete("[1, 2")` → false.
- `parser_is_complete("[1, 2]")` → true.

**Files touched**: `src/parser.h`, `src/parser.c`, `src/chunk.h`, `src/chunk.c`, `src/compiler.c`, `src/vm.c`, tests.

### Step 4: Array indexing (read + write)

Add `expr[expr]` for reading and `expr[expr] = value` for writing.

**AST** (`parser.h`):
- Add `AST_INDEX` and `AST_INDEX_ASSIGN`.

**Parser** (`parser.c`):
- Register `[` as a **postfix** parse rule (infix position, high precedence — same as function calls). After parsing a left-hand expression, if the next token is `[`, parse the index expression, consume `]`, return `AST_INDEX` with `left = array_expr`, `right = index_expr`.
- Distinguish from array literal: `[` in **prefix** position → array literal. `[` in **infix** position (after an expression) → indexing. The Pratt parser already distinguishes prefix from infix based on context.
- Index assignment: extend the `=` handler in `parse_assignment()`. If the LHS of `=` is an `AST_INDEX`, wrap it in `AST_INDEX_ASSIGN` with `left = AST_INDEX node`, `right = value_expr`.
- `ast_format()`: `[INDEX [arr] [idx]]`, `[INDEX_ASSIGN [arr] [idx] [val]]`.

**Opcodes** (`chunk.h`):
- Add `OP_INDEX_GET` (no operand).
- Add `OP_INDEX_SET` (no operand).

**Compiler** (`compiler.c`):
- `compile_index()`: compile array expr (push array), compile index expr (push index), emit `OP_INDEX_GET`.
- `compile_index_assign()`: compile array expr (push array), compile index expr (push index), compile value expr (push value), emit `OP_INDEX_SET`.

**VM** (`vm.c`):
- `OP_INDEX_GET`: pop index, pop array. Validate array is VAL_ARRAY and index is VAL_NUMBER. Handle negative indices (`idx < 0` → `idx + count`). Bounds check. Clone element at index, push result.
- `OP_INDEX_SET`: pop value, pop index, pop array. Validate types. Handle negative indices. Bounds check. Call `obj_array_ensure_owned()` (COW). Set element at index (free old, clone new). Push value as expression result.

**Disassembler** (`chunk.c`):
- Format `OP_INDEX_GET`, `OP_INDEX_SET`.

**Tests**:
- `[10, 20, 30][0]` → 10.
- `[10, 20, 30][2]` → 30.
- `[10, 20, 30][-1]` → 30.
- `[10, 20, 30][-3]` → 10.
- `[10, 20, 30][3]` → error "index out of bounds".
- `[10, 20, 30][-4]` → error "index out of bounds".
- `42[0]` → error "cannot index number".
- `[1, 2][0.5]` → error "index must be an integer".
- `my xs = [10, 20, 30]\nxs[0] = 99\nxs` → `[99, 20, 30]`.
- `my xs = [1, 2]\nmy ys = xs\nys[0] = 99\nxs` → `[1, 2]` (COW: xs unchanged).
- Nested: `[[1, 2], [3, 4]][1][0]` → 3.

**Files touched**: `src/parser.h`, `src/parser.c`, `src/chunk.h`, `src/chunk.c`, `src/compiler.c`, `src/vm.c`, tests.

### Step 5: Array concatenation with `++`

Extend the existing `OP_CONCAT` to support array-array concatenation.

**VM** (`vm.c`):
- In the `OP_CONCAT` handler: before the existing string logic, check if **both** operands are `VAL_ARRAY`. If so, create a new ObjArray containing all elements from the left array followed by all elements from the right array. Push the result.
- If one operand is an array and the other is not → runtime error "cannot concatenate array with \<type\>".
- If neither is an array → existing string concatenation behavior (unchanged).

**Tests**:
- `[1, 2] ++ [3, 4]` → `[1, 2, 3, 4]`.
- `[] ++ [1]` → `[1]`.
- `[1] ++ []` → `[1]`.
- `[] ++ []` → `[]`.
- `[[1]] ++ [[2]]` → `[[1], [2]]`.
- `[1] ++ 2` → error.
- `1 ++ [2]` → error.
- `"a" ++ "b"` → `"ab"` (unchanged).

**Files touched**: `src/vm.c`, tests.

### Step 6: `@` prefix — reduction with built-in operators

Parse `@op expr` as a reduction, compile to `OP_REDUCE`, execute in VM.

**AST** (`parser.h`):
- Add `AST_REDUCE` to `AstNodeType`.

**Parser** (`parser.c`):
- Register `TOK_META` as a prefix parse rule.
- `parse_meta_prefix()`: the `TOK_META` token's value contains the operator/identifier name. Store in `node->value`. Parse the operand expression at high precedence (same as unary minus, precedence 8). Set `node->left = operand`. Return `AST_REDUCE`.
- `ast_format()`: `"[REDUCE + [operand]]"`.

**Opcode** (`chunk.h`):
- Add `OP_REDUCE` with 1-byte operand (the opcode of the operation: `OP_ADD`, `OP_MULTIPLY`, etc.).

**Compiler** (`compiler.c`):
- `compile_reduce()`: check `node->value`. If it's a known operator name, map to opcode:
  - `"+"` → `OP_ADD`, `"-"` → `OP_SUBTRACT`, `"*"` → `OP_MULTIPLY`, `"/"` → `OP_DIVIDE`, `"%"` → `OP_MODULO`, `"**"` → `OP_POWER`, `"++"` → `OP_CONCAT`, `"=="` → `OP_EQUAL`, `"!="` → `OP_NOT_EQUAL`, `"<"` → `OP_LESS`, `">"` → `OP_GREATER`, `"<="` → `OP_LESS_EQUAL`, `">="` → `OP_GREATER_EQUAL`.
  - `"and"` / `"or"` → dedicated op bytes (add `OP_REDUCE_AND` / `OP_REDUCE_OR` as op-byte constants, or reserve values in the opcode enum).
- Compile operand (pushes the array). Emit `OP_REDUCE [op_byte]`.
- If `node->value` is not a known operator → this is a custom function reduction (handled in step 9). For now, emit a compile error.

**VM** (`vm.c`):
- `OP_REDUCE`: read op byte. Pop array from stack. Validate it's `VAL_ARRAY`.
  - If empty → runtime error "cannot reduce empty array".
  - If single element → push clone of that element.
  - Otherwise: `acc = clone(arr[0])`. For `i = 1..count-1`: apply the operation to `acc` and `arr[i]`, free old acc, store result. Push final acc.
  - For `and`/`or`: short-circuit. `@and`: iterate, return first falsy element (or last). `@or`: iterate, return first truthy element (or last).
  - If any intermediate operation produces a VAL_ERROR, stop and return the error.

**Disassembler** (`chunk.c`):
- Format `OP_REDUCE` showing the inner operation name.

**Tests**:
- `@+ [1, 2, 3, 4, 5]` → 15.
- `@* [1, 2, 3, 4, 5]` → 120.
- `@- [10, 3, 2]` → 5 (left fold: (10-3)-2).
- `@++ ["a", "b", "c"]` → `"abc"`.
- `@+ [42]` → 42 (single element).
- `@+ []` → error "cannot reduce empty array".
- `@and [true, true, false]` → false (short-circuit).
- `@and [1, 2, 3]` → 3 (all truthy, return last).
- `@or [false, 0, "hi"]` → `"hi"` (first truthy).
- `@or [false, 0, ""]` → `""` (all falsy, return last).
- `@== [1, 1, 1]` → true (fold: (1==1)==1 → true==1 → false). Note: this is left-fold behavior, may be surprising. Document it.
- Bytecode disassembly shows `OP_REDUCE OP_ADD` etc.

**Files touched**: `src/parser.h`, `src/parser.c`, `src/chunk.h`, `src/chunk.c`, `src/compiler.c`, `src/vm.c`, tests.

### Step 7: `@` infix — vectorization with built-in operators

Parse `expr @op expr` as vectorized operation, compile to `OP_VECTORIZE`, execute in VM.

**AST** (`parser.h`):
- Add `AST_VECTORIZE` to `AstNodeType`.

**Parser** (`parser.c`):
- Register `TOK_META` as an infix parse rule.
- Precedence: determined by the inner operator. The `infix_precedence()` function checks for `TOK_META` tokens and reads the token value to determine precedence (same mapping as normal operators). For identifiers (custom functions), use precedence 6 (same as `+`/`-`).
- `parse_meta_infix()`: store operator/identifier in `node->value`, set `node->left = left_expr`, parse right operand at appropriate precedence (respecting associativity of the inner operator), set `node->right = right_expr`. Return `AST_VECTORIZE`.
- `ast_format()`: `"[VECTORIZE + [left] [right]]"`.

**Opcode** (`chunk.h`):
- Add `OP_VECTORIZE` with 1-byte operand (same op encoding as `OP_REDUCE`).

**Compiler** (`compiler.c`):
- `compile_vectorize()`: check `node->value`. Map to opcode (same table as reduce). Compile left, compile right, emit `OP_VECTORIZE [op_byte]`.
- Custom function vectorization deferred to step 9.

**VM** (`vm.c`):
- `OP_VECTORIZE`: read op byte. Pop right, pop left.
  - **Both arrays**: validate same length. Create new ObjArray. For each index, apply op to `left[i]` and `right[i]`, push result into new array. Push result.
  - **Array + scalar** (either order): create new ObjArray. For each element, apply op to element and scalar. Push result.
  - **Both scalars**: runtime error "@ requires at least one array operand".
  - Mismatched array lengths → runtime error "array length mismatch in @op (left=N, right=M)".
  - If any element operation errors → stop, include element index in error message.

**Disassembler** (`chunk.c`):
- Format `OP_VECTORIZE` showing the inner operation name.

**Tests**:
- `[1, 2, 3] @+ [4, 5, 6]` → `[5, 7, 9]`.
- `[1, 2, 3] @* 10` → `[10, 20, 30]`.
- `10 @- [1, 2, 3]` → `[9, 8, 7]`.
- `[1, 2, 3] @** 2` → `[1, 4, 9]`.
- `[1, 2, 3] @> 2` → `[false, false, true]`.
- `[1, 2] @+ [1, 2, 3]` → error "array length mismatch".
- `1 @+ 2` → error "@ requires at least one array operand".
- `["a", "b"] @++ ["1", "2"]` → `["a1", "b2"]`.
- `[true, false] @and [true, true]` → `[true, false]`.
- Precedence: `[1, 2] @+ [3, 4] @* [5, 6]` → `[1, 2] @+ [15, 24]` → `[16, 26]` (since `@*` has higher precedence than `@+`).

**Files touched**: `src/parser.h`, `src/parser.c`, `src/chunk.h`, `src/chunk.c`, `src/compiler.c`, `src/vm.c`, tests.

### Step 8: Built-in array functions (`len`, `push`, `pop`)

Register `len`, `push`, `pop` as native functions using the `NativeFn` pattern established by the functions work.

**Native functions** (wherever native functions are registered — `runtime.c` or `vm.c`):
- `native_len(argc, args, ctx)`: validate argc == 1 and args[0] is VAL_ARRAY (or VAL_STRING for string length). Return `make_number(arr->count)`. For strings, return `make_number(strlen(str))`.
- `native_push(argc, args, ctx)`: validate argc == 2, args[0] is VAL_ARRAY. Deep clone the array, push args[1] onto the clone, return the new array.
- `native_pop(argc, args, ctx)`: validate argc == 1, args[0] is VAL_ARRAY. If empty → error. Deep clone array, remove last element, return the new array.

These return **new arrays**, not mutating the input. Simple and correct with value semantics.

**Tests**:
- `len([1, 2, 3])` → 3.
- `len([])` → 0.
- `len("hello")` → 5.
- `len(42)` → error "len() requires an array or string".
- `push([1, 2], 3)` → `[1, 2, 3]`.
- `push([], "a")` → `["a"]`.
- `my xs = [1, 2]\npush(xs, 3)\nxs` → `[1, 2]` (xs unchanged — push returns new array).
- `pop([1, 2, 3])` → `[1, 2]`.
- `pop([1])` → `[]`.
- `pop([])` → error "pop() on empty array".
- `len(push([1, 2], 3))` → 3 (composable).

**Files touched**: `src/runtime.c` or wherever native functions are registered, tests.

### Step 9: `@` with custom functions

Allow `@identifier` for user-defined functions. The compiler generates loop bytecode instead of `OP_REDUCE`/`OP_VECTORIZE` opcodes.

**Compiler** (`compiler.c`):

For **custom reduction** (`@my_func xs`, where `my_func` is not a known operator):
- The compiler emits bytecode equivalent to:
  ```
  // Pseudocode for @my_func xs:
  // arr = xs
  // acc = arr[0]
  // i = 1
  // while i < len(arr):
  //   acc = my_func(acc, arr[i])
  //   i = i + 1
  // result = acc
  ```
- Specifically: compile the operand (push array). Emit instructions to: read length, check non-empty, extract first element as accumulator, loop through remaining elements calling the function with acc and current element. The function is loaded via `OP_GET_GLOBAL` (or `OP_GET_LOCAL` if in function scope).

For **custom vectorization** (`xs @my_func ys`):
- The compiler emits bytecode equivalent to:
  ```
  // Pseudocode for xs @my_func ys:
  // left = xs
  // right = ys
  // result = []
  // i = 0
  // while i < len(left):
  //   push my_func(left[i], right[i]) onto result
  //   i = i + 1
  // result
  ```
- Scalar broadcasting: if one operand is not an array, the compiler can emit a simpler loop that reuses the scalar for every iteration.

Implementation note: this step generates more bytecode than the opcode-based approach, but avoids the complexity of the VM needing to call user functions from within opcode handlers. The generated bytecode uses only existing opcodes (`OP_CALL`, `OP_INDEX_GET`, `OP_ARRAY`, jumps, etc.).

If the compiler complexity is too high for inline bytecode generation, an alternative is to implement `reduce(fn, arr)` and `zip_with(fn, xs, ys)` as **native functions** that call user functions by directly manipulating the VM's call stack. This would be explored if the bytecode generation approach proves unwieldy.

**Tests**:
- `fn add(a, b) is a + b end\n@add [1, 2, 3]` → 6.
- `fn max(a, b) is if a > b then a else b end end\n@max [3, 1, 4, 1, 5]` → 5.
- `fn mul(a, b) is a * b end\n[1, 2, 3] @mul [4, 5, 6]` → `[4, 10, 18]`.
- `fn add1(a, b) is a + b end\n[1, 2, 3] @add1 10` → `[11, 12, 13]` (scalar broadcast).
- `@add []` → error "cannot reduce empty array".

**Files touched**: `src/compiler.c`, tests.

### Step 10: Boolean mask indexing

Extend array indexing to support boolean arrays as masks.

**VM** (`vm.c`):
- In `OP_INDEX_GET`: after the existing number-index path, check if the index is a `VAL_ARRAY`. If so, validate it's an array of booleans with the same length as the source array. Create a new ObjArray containing elements where the mask is `true`. Push the result.
- Non-boolean elements in the mask → runtime error "mask array must contain only booleans".
- Length mismatch → runtime error "mask length (N) does not match array length (M)".

**Tests**:
- `[10, 20, 30][[true, false, true]]` → `[10, 30]`.
- `[10, 20, 30][[false, false, false]]` → `[]`.
- `[10, 20, 30][[true, true, true]]` → `[10, 20, 30]`.
- Combine with vectorization: `my xs = [1, 2, 3, 4, 5]\nxs[xs @> 3]` → `[4, 5]`.
- `my xs = [10, 20, 30]\nxs[[true, false]]` → error "mask length mismatch".
- `[1, 2, 3][[1, 0, 1]]` → error "mask must contain only booleans".

**Files touched**: `src/vm.c`, tests.

### Step 11: Integration + REPL + cleanup

End-to-end integration, REPL support, disassembly, and polish.

**REPL** (`src/main.c`):
- Verify multiline array literals work (continuation prompt on `[` without `]`).
- Verify `--bytecode` shows `OP_ARRAY`, `OP_INDEX_GET`, `OP_REDUCE`, `OP_VECTORIZE` correctly.
- Verify `--ast` shows the new node types.

**CLI integration tests** (`tests/test_cli.sh`):
- `cutlet run` with a file that creates, indexes, and reduces arrays.
- Pipe test: `echo "[1, 2, 3]" | cutlet repl` → `[1, 2, 3]`.
- Pipe test: `echo "@+ [1, 2, 3]" | cutlet repl` → `6`.

**Bytecode disassembly** (`src/chunk.c`):
- Verify all new opcodes disassemble correctly.

**Equality in VM** (`vm.c`):
- `[1, 2, 3] == [1, 2, 3]` → true.
- `[1, 2] == [1, 2, 3]` → false.
- `[1, 2] == "hello"` → false (different types).
- `[] == []` → true.

**Files touched**: `src/chunk.c`, `tests/test_cli.sh`, `tests/test_eval.c`, `src/main.c` if needed.

**Post-implementation reminders** (per AGENTS.md):
- Update `TUTORIAL.md` with arrays, indexing, `@` meta-operator, and built-in functions sections.
- Add `examples/arrays.cutlet` example program.
- Add `examples/meta-operator.cutlet` example program.

---

## Deferred

### Array slicing

`xs[1..3]` to extract a sub-array. Now that concatenation uses `++`, `..` is free for use as a range operator. Design after arrays and `@` are stable.

### More array built-ins

`sort()`, `reverse()`, `contains()`, `index_of()`, `join()`, `flat()`. Add as native functions when needed.

### `@` scan (running fold)

Raku's `[\+]` — returns intermediate accumulation results. Could be `@@+` or a separate syntax. Useful but niche.

### Auto-vectorization of bare operators

Making `[1, 2, 3] + 10` automatically vectorize (without `@`). Cleaner syntax but changes existing error behavior and adds implicit magic. Revisit after `@` is well-tested and usage patterns are clear.

### Anonymous functions with `@`

Once anonymous functions land (`fn(a, b) is ... end`), they compose with `@`:
```cutlet
[1, 2, 3] @fn(a, b) is a + b end [4, 5, 6]
```
Should work automatically if the parser handles `fn` as the identifier in `@fn(...)`.

---

## Required process (every step)

1. Write tests first.
2. Run `make test` and `make check` — confirm new tests fail.
3. **Stop and ask the user for confirmation before implementing.**
4. Implement the feature.
5. Run `make test` and `make check` after every code change.
6. Do not remove or modify existing tests without user confirmation.

---
End of plan.
