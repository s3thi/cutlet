# Object System — Value Types and Parser

## Objective

Add the value-layer types (`ObjObjectType`, `ObjInstance`, `VAL_OBJECT_TYPE`, `VAL_INSTANCE`) and parser infrastructure (`AST_OBJECT_DEF`, `AST_NEW`) for the object system. After this task:

- `object Name is fn method(self) is body end end` parses into an `AST_OBJECT_DEF` node.
- `object Name with Mixin1, Mixin2 is fn method(self) is body end end` parses with mixin names captured.
- `new Name(args)` parses into an `AST_NEW` node.
- `ObjObjectType` and `ObjInstance` structs exist with full lifecycle support (create, free, clone, format, equal).
- `make test && make check` pass.

## Acceptance criteria

- [ ] `VAL_OBJECT_TYPE` and `VAL_INSTANCE` added to `ValueType` enum in `value.h`.
- [ ] `ObjObjectType` struct: `name` (`char *`), `methods` (`ObjMap *`), `refcount`. Constructor, free, format (`<object Name>`), equal (pointer equality).
- [ ] `ObjInstance` struct: `type` (`ObjObjectType *`), `data` (`ObjMap *`), `refcount`. Constructor, free, format (`<Name instance>`), equal (pointer equality).
- [ ] `make_object_type()` and `make_instance()` constructors wrap structs into `Value`.
- [ ] `value_free`, `value_clone`, `value_format`, `value_equal`, `is_truthy` handle both new types.
- [ ] `obj_object_type_set_method()` and `obj_object_type_get_method()` accessors exist.
- [ ] `object`, `new`, `with` recognized as keywords.
- [ ] `AST_OBJECT_DEF` and `AST_NEW` added to `AstNodeType` enum in `parser.h`.
- [ ] Object definition parsing: empty body, one method, multiple methods, with mixins.
- [ ] `new` expression parsing: no args, with args, expression args.
- [ ] Parse errors for non-identifier name, non-`fn` in body.
- [ ] `parser_is_complete()` returns false for incomplete `object...end` blocks.
- [ ] AST formatting: `AST_OBJECT_DEF` formats as `[OBJECT_DEF Name ...]`, `AST_NEW` formats as `[NEW Name ...]`.
- [ ] `make test && make check` pass.

## Dependencies

None. This task is purely additive — it adds new types, enum values, and parser branches without modifying existing functionality. The dot-access plan is NOT required for this task (test method bodies use simple expressions that don't require dot access).

## Constraints and non-goals

- **No compilation or VM execution.** This task only covers value types and the parser. Compilation and VM are in the next task (`objects-compiler-vm`).
- **`self` is not a keyword.** It's a regular identifier used as a parameter name convention. No special compiler or parser treatment.
- **Method bodies in parser tests must use simple expressions** (string literals, arithmetic, identifiers) rather than dot access, since the dot-access plan may not be implemented yet.

---

## Design

### AST_OBJECT_DEF field usage

| Field | Usage |
|-------|-------|
| `value` | Type name (e.g., `"Dog"`) — heap-allocated |
| `params` / `param_count` | Mixin names (e.g., `["Speakable", "Walkable"]`) — heap-allocated array of heap-allocated strings. NULL/0 if no mixins. |
| `children` / `child_count` | Method definitions (each is `AST_FUNCTION`) |
| `left` | Unused (NULL) |
| `right` | Unused (NULL) |
| `line` | Source line of `object` keyword |

### AST_NEW field usage

| Field | Usage |
|-------|-------|
| `value` | Type name (e.g., `"Dog"`) — heap-allocated |
| `children` / `child_count` | Argument expressions |
| `left` | Unused (NULL) |
| `right` | Unused (NULL) |
| `line` | Source line of `new` keyword |

### AST formatting examples

- `[OBJECT_DEF Foo]` — empty object, no mixins, no methods
- `[OBJECT_DEF Foo [FN greet(self) [STRING hi]]]` — one method
- `[OBJECT_DEF Foo [FN a(self) [NUMBER 1]] [FN b(self, x) [IDENT x]]]` — multiple methods
- `[OBJECT_DEF Foo with Bar]` — one mixin, no methods
- `[OBJECT_DEF Foo with A, B, C [FN x(self) [NUMBER 1]]]` — mixins + methods
- `[NEW Foo]` — no args
- `[NEW Foo [NUMBER 1]]` — one arg
- `[NEW Foo [NUMBER 1] [STRING x]]` — multiple args

---

## Implementation steps

### Step 1: Write parser tests

Add parser tests in `tests/test_parser.c`. Use the existing test pattern (`TEST()` macro, `assert_ast()` or similar helper). For method bodies, use only simple expressions (string literals, numbers, identifiers).

**Object definition tests:**

- `object Foo is end` → `[OBJECT_DEF Foo]`
- `object Foo is fn greet(self) is "hi" end end` → `[OBJECT_DEF Foo [FN greet(self) [STRING hi]]]`
- `object Foo is fn a(self) is 1 end fn b(self, x) is x end end` → `[OBJECT_DEF Foo [FN a(self) [NUMBER 1]] [FN b(self, x) [IDENT x]]]`
- `object Foo with Bar is end` → `[OBJECT_DEF Foo with Bar]`
- `object Foo with A, B, C is fn x(self) is 1 end end` → `[OBJECT_DEF Foo with A, B, C [FN x(self) [NUMBER 1]]]`

**New expression tests:**

- `new Foo()` → `[NEW Foo]`
- `new Foo(1)` → `[NEW Foo [NUMBER 1]]`
- `new Foo(1, "x")` → `[NEW Foo [NUMBER 1] [STRING x]]`
- `new Foo(1 + 2)` → `[NEW Foo [BINOP + [NUMBER 1] [NUMBER 2]]]`

**Error tests:**

- `object 42 is end` → parse error (non-identifier name)
- `object Foo is 42 end` → parse error (non-fn in body)
- `object Foo` at EOF → `parser_is_complete()` returns false

Run `make test` — new tests will fail (AST types don't exist yet). This is expected.

**Files touched:** `tests/test_parser.c`

### Step 2: Add type scaffolding

Add the minimum declarations so that tests compile and fail with assertion errors (not compilation errors).

**`value.h`:**

- Add `VAL_OBJECT_TYPE` and `VAL_INSTANCE` to the `ValueType` enum.
- Add `ObjObjectType` struct definition:
  - `size_t refcount`
  - `char *name` — heap-allocated type name
  - `ObjMap *methods` — owned method table mapping string names to closure values
- Add `ObjInstance` struct definition:
  - `size_t refcount`
  - `ObjObjectType *type` — reference to the object type (refcount-managed)
  - `ObjMap *data` — owned instance data map
- Add fields to the `Value` struct: `ObjObjectType *object_type;` and `ObjInstance *instance;`.
- Add constructor declarations: `Value make_object_type(ObjObjectType *t);`, `Value make_instance(ObjInstance *inst);`.
- Add allocator declarations: `ObjObjectType *obj_object_type_new(const char *name);`, `ObjInstance *obj_instance_new(ObjObjectType *type);`.
- Add method table accessor declarations: `void obj_object_type_set_method(ObjObjectType *type, const char *name, Value method);`, `Value *obj_object_type_get_method(const ObjObjectType *type, const char *name);`.

**`parser.h`:**

- Add `AST_OBJECT_DEF` and `AST_NEW` to the `AstNodeType` enum.

**`parser.c`:**

- Add `"object"`, `"new"`, and `"with"` to `is_keyword()`. Note: `is_keyword()` is used by the parser to decide whether a `TOK_IDENT` can start a new expression or is a keyword terminator. Study which keywords are in `is_keyword()` vs `is_reserved_keyword()` and add to both as appropriate. `object` and `new` can start expressions (like `fn`, `if`, `while`). `with` is only meaningful inside `object...end` and does not start an expression — it may or may not need to be in the keyword list depending on whether the parser needs to distinguish it from identifiers in other contexts. Study how `is` is handled for guidance.

Run `make test` — tests should now compile but assertions fail.

**Files touched:** `src/value.h`, `src/parser.h`, `src/parser.c`

### Step 3: Implement ObjObjectType

Full implementation in `value.c`:

- `obj_object_type_new(const char *name)`: `malloc` an `ObjObjectType`, set `refcount = 1`, `name = strdup(name)`, `methods = obj_map_new()`.
- `obj_object_type_set_method(type, name, method)`: Build a `Value` key from a `strdup` of the name string via `make_string()`, call `obj_map_set(type->methods, &key, &method)`, free the key.
- `obj_object_type_get_method(type, name)`: Build a `Value` key from a `strdup` of the name string via `make_string()`, call `obj_map_get(type->methods, &key)`, free the key, return the result pointer (or NULL).
- `make_object_type(ObjObjectType *t)`: Return a `Value` with `type = VAL_OBJECT_TYPE`, `object_type = t`. Zero-initialize all other payload fields.
- In `value_free()`: add `case VAL_OBJECT_TYPE` — decrement refcount; if 0, free `name` with `free()`, free the methods map (study how `VAL_MAP` frees its `ObjMap` — iterate entries, free each key and value, then free the map struct), free the `ObjObjectType` struct.
- In `value_clone()`: add `case VAL_OBJECT_TYPE` — increment `refcount`, copy struct pointer.
- In `value_format()`: add `case VAL_OBJECT_TYPE` — return heap-allocated string in the format `<object Name>`.
- In `value_equal()`: add `case VAL_OBJECT_TYPE` — pointer equality (`a->object_type == b->object_type`).
- In `is_truthy()`: add `case VAL_OBJECT_TYPE` — always truthy.

Run `make test && make check`.

**Files touched:** `src/value.c`

### Step 4: Implement ObjInstance

Full implementation in `value.c`:

- `obj_instance_new(ObjObjectType *type)`: `malloc` an `ObjInstance`, set `refcount = 1`, set `type = type` and increment `type->refcount` (the instance holds a reference), set `data = obj_map_new()`.
- `make_instance(ObjInstance *inst)`: Return a `Value` with `type = VAL_INSTANCE`, `instance = inst`. Zero-initialize all other payload fields.
- In `value_free()`: add `case VAL_INSTANCE` — decrement refcount; if 0, free the `data` map (same pattern as `VAL_MAP` free), then handle the type reference: decrement `type->refcount`, and if it reaches 0, free the type using the same logic as `VAL_OBJECT_TYPE` free (or call a helper). Finally free the `ObjInstance` struct.
- In `value_clone()`: add `case VAL_INSTANCE` — increment `refcount`, copy struct pointer.
- In `value_format()`: add `case VAL_INSTANCE` — return heap-allocated string in the format `<Name instance>` where Name comes from `inst->type->name`.
- In `value_equal()`: add `case VAL_INSTANCE` — pointer equality (`a->instance == b->instance`). Two different instances of the same type are NOT equal.
- In `is_truthy()`: add `case VAL_INSTANCE` — always truthy.

Run `make test && make check`.

**Files touched:** `src/value.c`

### Step 5: Parse object definitions

Add object definition parsing in `parser.c`. The `object` keyword should be handled in `parse_atom()` (or the equivalent entry point for keyword-initiated expressions — study how `fn`, `if`, and `while` are handled).

**Parsing logic:**

1. Recognize `object` keyword (current token is `TOK_IDENT` with value `"object"`). Save the token for line info. Advance.
2. Expect `TOK_IDENT` for the type name (must not be a reserved keyword — use `is_reserved_keyword()` to check). Save a heap-allocated copy of the name. Advance.
3. Check for `with` keyword (current token is `TOK_IDENT "with"`):
   - If present: advance past `with`. Parse comma-separated `TOK_IDENT` mixin names. Each mixin name must be a non-reserved-keyword identifier. Stop when the next token is `TOK_IDENT "is"`. Store names in a `PtrArray`, then transfer to the node's `params`/`param_count`.
4. Expect `is` keyword (`TOK_IDENT "is"`). Advance.
5. Call `skip_newlines()`.
6. Parse method definitions in a loop while current token is `TOK_IDENT "fn"`:
   - Call `parse_fn(p)` to parse the function definition (returns `AST_FUNCTION` node).
   - Verify the function is named (`node->value != NULL`). If anonymous, emit parse error: `"object methods must be named"`.
   - Append to a children `PtrArray`.
   - Call `skip_newlines()` between methods.
7. If current token is not the `end` keyword, emit parse error: `"expected 'fn' or 'end' in object body"`.
8. Expect and consume `end` keyword. Advance.
9. Build `AST_OBJECT_DEF` node using the field layout in the Design section.

**AST formatting** (in `ast_format_node()`):

Add a formatting branch for `AST_OBJECT_DEF`:
- Start with `[OBJECT_DEF Name`.
- If `param_count > 0`, append ` with ` then comma-separated mixin names (e.g., ` with A, B, C`).
- For each child (method), append ` ` + `ast_format_node(child)`.
- Append `]`.

Add `ast_node_type_str()` case: `AST_OBJECT_DEF` → `"OBJECT_DEF"`.

Run `make test && make check`.

**Files touched:** `src/parser.c`

### Step 6: Parse `new` expressions

Add `new` expression parsing in `parser.c`. Handle `new` in `parse_atom()` when the current token is `TOK_IDENT "new"`.

**Parsing logic:**

1. Recognize `new` keyword. Save the token for line info. Advance.
2. Expect `TOK_IDENT` for the type name. Save a heap-allocated copy. Advance.
3. Expect `(` operator. Advance.
4. Parse comma-separated argument expressions until `)`. Reuse the same argument-parsing pattern used by function calls in `parse_atom()` for `AST_CALL` nodes: in a loop, parse an expression with `parse_expr()`, check for `,` or `)`.
5. Expect and consume `)`. Advance.
6. Build `AST_NEW` node using the field layout in the Design section.

**AST formatting** (in `ast_format_node()`):

Add a formatting branch for `AST_NEW`. Follow the `AST_CALL` formatting pattern:
- Start with `[NEW Name`.
- For each child (argument), append ` ` + `ast_format_node(child)`.
- Append `]`.

Add `ast_node_type_str()` case: `AST_NEW` → `"NEW"`.

Run `make test && make check`. All parser tests should now pass.

**Files touched:** `src/parser.c`

### Step 7: REPL completeness + memory cleanup + final verification

**REPL completeness** (`parser_is_complete()` in `parser.c`):

Add detection for incomplete `object...end` blocks. Study how `if...end` and `while...end` blocks handle this — when the parser hits EOF before `end`, it produces an error message that `parser_is_complete()` recognizes as "incomplete input." The `object` parser should produce a similar error when it hits EOF before `end` (or before `is`), and `parser_is_complete()` should match that message.

**Memory cleanup:**

- Verify that `ast_free()` in `parser.c` properly frees `AST_OBJECT_DEF` nodes. The existing `ast_free()` generically frees `value`, `params`, `children`, `left`, and `right`. Check whether it handles the new node types correctly. If `ast_free()` has type-specific branches, add the new types.
- Verify `AST_NEW` is also freed correctly (same reasoning — it uses `value` and `children`).

**Final verification:**

- Run `make test && make check`. All tests pass.
- Verify no memory leaks by checking if the project uses sanitizers (look at the Makefile for ASan flags).

**Files touched:** `src/parser.c`

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

## Progress

### Step 1: Write parser tests — DONE (2026-02-28)

Added 19 parser tests in `tests/test_parser.c`:

**Object definition success tests (5):**
- `test_object_def_empty` — `object Foo is end`
- `test_object_def_one_method` — one `fn` method
- `test_object_def_multiple_methods` — two methods with varying params
- `test_object_def_one_mixin` — `object Foo with Bar is end`
- `test_object_def_multiple_mixins` — `with A, B, C` plus method

**Object definition error tests (2):**
- `test_object_def_non_ident_name` — `object 42 is end` fails
- `test_object_def_non_fn_body` — `object Foo is 42 end` fails

**Object definition completeness tests (5):**
- `test_object_def_incomplete_eof` — `object Foo` incomplete
- `test_object_def_incomplete_no_end` — `object Foo is` incomplete
- `test_object_def_incomplete_no_outer_end` — method but no outer `end` incomplete
- `test_object_def_complete` — complete object is complete
- `test_object_def_complete_with_method` — complete object with method is complete

**New expression success tests (4):**
- `test_new_no_args` — `new Foo()`
- `test_new_one_arg` — `new Foo(1)`
- `test_new_multi_args` — `new Foo(1, "x")`
- `test_new_expr_arg` — `new Foo(1 + 2)`

**New expression error tests (1):**
- `test_new_missing_name` — `new 42()` fails

**Reserved keyword tests (2):**
- `test_object_keyword_reserved` — `my object = 1` fails
- `test_new_keyword_reserved` — `my new = 1` fails

All 19 tests fail as expected (14 failures from `make test`; some already pass because `new` without a recognized keyword just fails parsing naturally). Commit: `0058b31`.

### Step 2: Add type scaffolding — DONE (2026-02-28)

**`value.h` changes:**
- Added `VAL_OBJECT_TYPE` and `VAL_INSTANCE` to `ValueType` enum.
- Added forward declarations for `ObjObjectType` and `ObjInstance`.
- Added `ObjObjectType` struct: `refcount`, `name`, `methods` (ObjMap*).
- Added `ObjInstance` struct: `refcount`, `type` (ObjObjectType*), `data` (ObjMap*).
- Added `object_type` and `instance` fields to `Value` struct.
- Added constructor declarations: `make_object_type()`, `make_instance()`.
- Added allocator declarations: `obj_object_type_new()`, `obj_instance_new()`.
- Added accessor declarations: `obj_object_type_set_method()`, `obj_object_type_get_method()`.

**`parser.h` changes:**
- Added `AST_OBJECT_DEF` and `AST_NEW` to `AstNodeType` enum.

**`parser.c` changes:**
- Added `object`, `new`, `with` to `is_reserved_keyword()`.
- Added `AST_OBJECT_DEF` → `"OBJECT_DEF"` and `AST_NEW` → `"NEW"` to `ast_node_type_str()`.

**`gc.c` changes:**
- Added `VAL_OBJECT_TYPE` and `VAL_INSTANCE` cases to `gc_mark_value()` to eliminate -Wswitch warnings and correctly mark the GC-managed ObjMap pointers embedded in the refcount-managed structs.

**Decisions made:**
- `object` and `new` are NOT added to break/return value-exclusion lists (they start expressions, like `fn`/`if`/`while`).
- `with` is added to `is_reserved_keyword()` following the pattern of `is` (contextual delimiter, cannot be used as variable name).
- GC marking added proactively for the embedded ObjMap pointers in ObjObjectType.methods and ObjInstance.data.

**Test results:** 341/353 pass. 12 parser tests fail with assertion errors (not compilation errors). All non-parser test suites pass (tokenizer, value, chunk, compiler, VM, GC). Format check passes. Commit: `a74665c`.

### Step 3: Implement ObjObjectType — DONE (2026-02-28)

Implemented all ObjObjectType functionality in `src/value.c`:

**Allocator and accessors:**
- `obj_object_type_new(name)`: malloc + refcount=1 + strdup(name) + obj_map_new() for methods. Error handling for partial allocation failure.
- `obj_object_type_set_method(type, name, method)`: builds string Value key via make_string(strdup(name)), delegates to obj_map_set, frees the key.
- `obj_object_type_get_method(type, name)`: builds string Value key, delegates to obj_map_get, frees key, returns result pointer or NULL.

**Value constructor:**
- `make_object_type(t)`: returns Value with type=VAL_OBJECT_TYPE, object_type=t.

**Lifecycle functions:**
- `value_free`: VAL_OBJECT_TYPE decrements refcount; if 0, frees name, methods (via free_owned_map helper), and struct.
- `value_clone`: VAL_OBJECT_TYPE increments refcount (shared ownership).
- `value_format`: VAL_OBJECT_TYPE returns `<object Name>`.
- `value_equal`: VAL_OBJECT_TYPE uses pointer equality.
- `is_truthy`: VAL_OBJECT_TYPE always truthy.

**Helper added:**
- `free_owned_map(ObjMap *m)`: frees entries (key+value), entries array, then gc_free_object on the map. Reusable for ObjInstance in Step 4.

**Safety:**
- Null out `object_type` and `instance` pointers in value_free for all types.

**Decisions made:**
- Used `gc_free_object()` to properly unlink the ObjMap from the GC's object list before freeing, since the ObjMap is GC-allocated but lifetime-managed by the refcount-managed ObjObjectType.

**Test results:** 341/353 pass (same 12 parser failures from Steps 5-7). All value, compiler, VM, GC, tokenizer, chunk tests pass. Format check passes. Commit: `11004b3`.

### Step 4: Implement ObjInstance — DONE (2026-02-28)

Implemented all ObjInstance functionality in `src/value.c`:

**Allocator and constructor:**
- `obj_instance_new(type)`: malloc + refcount=1 + type refcount bump + obj_map_new() for data. Null check on type, cleanup on allocation failure (undo refcount bump).
- `make_instance(inst)`: returns Value with type=VAL_INSTANCE, instance=inst.

**Lifecycle functions:**
- `value_free`: VAL_INSTANCE decrements refcount; if 0, frees data map via free_owned_map, decrements type->refcount (frees type via free_object_type if last ref), frees the ObjInstance struct.
- `value_clone`: VAL_INSTANCE increments refcount (shared ownership).
- `value_format`: VAL_INSTANCE returns `<Name instance>` where Name is inst->type->name.
- `value_equal`: VAL_INSTANCE uses pointer equality (two different instances are never equal).
- `is_truthy`: VAL_INSTANCE always truthy.

**Helper extracted:**
- `free_object_type(ObjObjectType *t)`: freed from the inlined code in VAL_OBJECT_TYPE's value_free path. Now shared by both VAL_OBJECT_TYPE and VAL_INSTANCE (when instance's type ref is the last one).

**Test results:** 341/353 pass (same 12 parser failures from Steps 5-7). All value, compiler, VM, GC, tokenizer, chunk tests pass. Format check passes. Commit: `c39cf5e`.

### Step 5: Parse object definitions — DONE (2026-02-28)

Implemented object definition parsing in `src/parser.c`:

**parse_object() function:**
- Handles `object Name [with Mixin1, Mixin2, ...] is fn... end` syntax.
- Type name must be a non-reserved-keyword identifier.
- Optional `with` clause parses comma-separated mixin names (each validated as non-reserved identifier).
- Expects `is` keyword, then parses method definitions in a loop via `parse_fn()`.
- Each method is verified to be named (anonymous functions rejected with "object methods must be named").
- Expects closing `end` keyword.
- Builds AST_OBJECT_DEF node: value=type name, params=mixin names, children=methods.

**parse_atom() integration:**
- Added `object` keyword handler dispatching to `parse_object()`, following the same pattern as `fn`/`if`/`while`.

**AST formatting:**
- Added AST_OBJECT_DEF branch in `ast_format_node()`:
  - `[OBJECT_DEF Name]` for empty object
  - `[OBJECT_DEF Name with A, B]` for mixins
  - `[OBJECT_DEF Name [FN ...]]` for methods
  - Combined form for mixins + methods

**parser_is_complete():**
- Added detection for `"expected 'fn' or 'end' in object body"` error message so that incomplete object bodies at EOF are recognized as incomplete input for REPL continuation.
- The existing `"expected 'is'"` check already covers `object Foo` at EOF.

**Decisions made:**
- Added `parser_is_complete()` support in this step rather than deferring to Step 7, since the completeness tests were written in Step 1 and naturally need the parsing to exist to function.

**Test results:** 349/353 pass. All 13 object definition tests pass (5 success, 2 error, 5 completeness, 1 reserved keyword). Remaining 4 failures are all `new` expression tests (Step 6). Format check passes. Commit: `d0da907`.

### Step 6: Parse `new` expressions — DONE (2026-02-28)

Implemented `new` expression parsing in `src/parser.c`:

**parse_new() function:**
- Handles `new TypeName(arg1, arg2, ...)` syntax.
- Recognizes `new` keyword in `parse_atom()`, dispatches to `parse_new()`.
- Type name must be a non-reserved-keyword identifier.
- Expects `(`, parses comma-separated argument expressions using the same pattern as `AST_CALL` in `parse_atom()`, expects `)`.
- Builds `AST_NEW` node: `value`=type name, `children`=argument expressions, `left`/`right`=NULL.

**AST formatting:**
- Added `AST_NEW` branch in `ast_format_node()`:
  - Formats as `[NEW TypeName]` for zero args.
  - Formats as `[NEW TypeName [NUMBER 1] [STRING x]]` for multiple args.
  - Follows the `AST_CALL` formatting pattern exactly.

**Memory cleanup:**
- `AST_NEW` uses `value` and `children`, both of which are handled by the generic `ast_free()` logic — no type-specific branch needed.

**Test results:** 353/353 parser tests pass. All 7 new-expression tests pass (4 success, 1 error, 2 reserved keyword). All test suites pass with 0 failures. Format check passes. Commit: `5e9310f`.

### Step 7: REPL completeness + memory cleanup + final verification — DONE (2026-02-28)

**REPL completeness:** Already handled in Step 5. `parser_is_complete()` correctly detects:
- `"expected 'is'"` — covers `object Foo` at EOF.
- `"expected 'fn' or 'end' in object body"` — covers `object Foo is` at EOF.
- `"expected 'end'"` — covers unterminated blocks with methods.
All 5 completeness tests pass.

**Memory cleanup:** Verified. `ast_free()` is generic — it unconditionally frees `value`, `params`/`param_count`, `children`/`child_count`, `left`, and `right`. Both `AST_OBJECT_DEF` (uses `value`, `params`, `children`) and `AST_NEW` (uses `value`, `children`) are properly freed with no type-specific branches needed.

**Sanitizer verification:** Ran `make test-sanitize` (ASan+UBSan+LSan). All tests pass. Only pre-existing leaks in `test_chunk.c` (unrelated to this plan — `test_chunk.c` was not modified).

**Tutorial update:** Added Section 19 "Objects and types (planned)" to `TUTORIAL.md` documenting:
- `object Name is ... end` syntax with methods.
- `with` clause for mixins.
- `new TypeName(args)` syntax.
- Reserved keyword status of `object`, `new`, `with`.
- REPL continuation support for incomplete object blocks.
- Note that these are parser-only features; runtime support is a future task.

**Example file:** Skipped per plan instructions (interpreter cannot run `object`/`new` yet).

**Final test results:** `make test` — all tests pass (353/353 parser, 29/29 examples, all suites). `make check` — same pre-existing lint warnings as main branch (no new warnings introduced).

**All 7 steps complete.** Plan ready for `scripts/plan-done`.
