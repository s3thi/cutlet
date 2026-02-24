# Array Meta-Operator + Mask Indexing Plan

Depends on: basic arrays (arrays-basic plan). Assumes `VAL_ARRAY`, `ObjArray`, `OP_ARRAY`, `OP_INDEX_GET`, `OP_INDEX_SET`, `OP_CONCAT` for arrays, and `len`/`push`/`pop` native functions are all in place.

---

## Design

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

### New AST nodes

Add to `AstNodeType` enum:

| Node | Fields used | Purpose |
|------|------------|---------|
| `AST_REDUCE` | `value` (op/name), `left` (operand) | Prefix `@op expr` |
| `AST_VECTORIZE` | `value` (op/name), `left`, `right` | Infix `expr @op expr` |

S-expression format:
- `AST_REDUCE`: `[REDUCE op [operand]]`.
- `AST_VECTORIZE`: `[VECTORIZE op [left] [right]]`.

### New opcodes

| Opcode | Operand | Stack effect | Description |
|--------|---------|-------------|-------------|
| `OP_REDUCE` | 1-byte op | pop 1 (array), push 1 | Fold array with built-in operator |
| `OP_VECTORIZE` | 1-byte op | pop 2, push 1 | Element-wise operation on arrays |

`OP_REDUCE` and `OP_VECTORIZE` encode the inner operator as a 1-byte operand using the existing `OpCode` enum values (e.g., `OP_ADD`, `OP_MULTIPLY`, `OP_CONCAT`, etc.). This also covers `OP_LESS`, `OP_GREATER`, `OP_EQUAL`, `OP_NOT_EQUAL`, `OP_LESS_EQUAL`, `OP_GREATER_EQUAL`, and logical ops `OP_NOT` (for `@not` prefix only — treated specially).

For `@and` and `@or`: these need short-circuit semantics. `@and` folds left, stopping early if any element is falsy. `@or` folds left, stopping early if any element is truthy. Encoded as `OP_REDUCE` with dedicated op bytes (e.g., reuse placeholder values or add `OP_AND`/`OP_OR` as op-byte-only constants).

For custom functions (`@ident`): the compiler emits loop bytecode instead of `OP_REDUCE`/`OP_VECTORIZE`. No new opcode needed — uses existing `OP_CALL`, `OP_GET_LOCAL`, jump, and array access opcodes.

---

## Implementation steps

Each step follows the required process: tests first, confirm failures, get user confirmation, implement, `make test && make check`.

### Step 1: Tokenizer — `TOK_META` for `@`

**Tokenizer** (`tokenizer.h`, `tokenizer.c`):
- Add `TOK_META` to `TokenType` enum.
- Add special handling for `@`: when `@` is the current char, enter the meta-operator scanning path (described in architecture section above). Emit `TOK_META` with the inner operator/identifier as the value.
- Update `token_type_str()` for `TOK_META` → `"META"`.

**Tests** (`tests/test_tokenizer.c`):
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

### Step 2: `@` prefix — reduction with built-in operators

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
- If `node->value` is not a known operator → this is a custom function reduction (handled in step 4). For now, emit a compile error.

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

### Step 3: `@` infix — vectorization with built-in operators

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
- Custom function vectorization deferred to step 4.

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

### Step 4: `@` with custom functions

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

### Step 5: Boolean mask indexing

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

### Step 6: Integration + REPL + cleanup

End-to-end integration, REPL support, disassembly, and polish.

**REPL** (`src/main.c`):
- Verify `--bytecode` shows `OP_REDUCE`, `OP_VECTORIZE` correctly.
- Verify `--ast` shows the new node types.

**CLI integration tests** (`tests/test_cli.sh`):
- Pipe test: `echo "@+ [1, 2, 3]" | cutlet repl` → `6`.

**Bytecode disassembly** (`src/chunk.c`):
- Verify all new opcodes disassemble correctly.

**Files touched**: `src/chunk.c`, `tests/test_cli.sh`, `tests/test_eval.c`.

**Post-implementation reminders** (per AGENTS.md):
- Update `TUTORIAL.md` with `@` meta-operator and mask indexing sections.
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

## Progress

- [x] Step 1: Tokenizer — `TOK_META` for `@` — added TOK_META enum, read_meta() function, 15 tests covering operators/identifiers/errors, updated one existing test (`@` → `~`)
- [x] Step 2: `@` prefix — reduction with built-in operators — added AST_REDUCE, OP_REDUCE, OP_AND/OP_OR op-bytes, reduce_apply_op helper, short-circuit @and/@or, 8 parser tests + 12 VM tests
- [x] Step 3: `@` infix — vectorization with built-in operators — added AST_VECTORIZE, OP_VECTORIZE, infix TOK_META parsing with inner-operator precedence, scalar broadcasting, 7 parser tests + 10 VM tests
- [x] Step 4: `@` with custom functions — added OP_REDUCE_CALL/OP_VECTORIZE_CALL opcodes, refactored VM dispatch loop into vm_run() for recursive calls, vm_call_value() helper for calling closures/natives, emit_load_callable() compiler helper, 11 VM tests

---
End of plan.
