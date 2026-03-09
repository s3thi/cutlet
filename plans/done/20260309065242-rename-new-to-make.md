# Rename `new` keyword to `make`

## Objective

Replace the `new` keyword with `make` throughout the language surface and all
internal C symbols. This is a pure rename — no semantic changes to object
instantiation. Done when `make test && make check` pass, all user-facing syntax
uses `make`, all internal identifiers say "make" instead of "new", and
documentation/examples are updated.

## Acceptance criteria

- [ ] `make` is the reserved keyword; `new` is no longer reserved.
- [ ] `make Foo(args)` parses, compiles, and runs identically to the old `new Foo(args)`.
- [ ] Using `new` as a keyword produces a parse error (it's just a regular identifier now).
- [ ] All internal C symbols renamed: `AST_NEW` → `AST_MAKE`, `OP_NEW` → `OP_MAKE`,
      `parse_new()` → `parse_make()`, `compile_new()` → `compile_make()`.
- [ ] Error messages say `make` (e.g. `"can only use 'make' with object types"`).
- [ ] All existing tests updated and passing.
- [ ] `examples/objects.cutlet` updated.
- [ ] `TUTORIAL.md` updated.
- [ ] `make test && make check` pass.

## Dependencies

None. No overlap with `plans/doing/20260219145148-quality-fuzzing.md`.

## Constraints / non-goals

- No semantic changes — only rename.
- `is_initializer` field in `CallFrame` keeps its name (it describes init(),
  not the keyword).
- Do NOT rename `obj_instance_new()` or `obj_object_type_new()` — those are C
  allocation helpers, not related to the keyword.
- Do NOT rename local variables like `new_frame` — those are C idioms.
- Do NOT touch `.expected` files unless their content changes (it shouldn't).

## Steps

### 1. Rename the AST node type

In `src/parser.h`, rename `AST_NEW` to `AST_MAKE` in the `AstNodeType` enum.
Update its comment to say `make expression: make Name(args)`.

### 2. Rename the opcode

In `src/chunk.h`, rename `OP_NEW` to `OP_MAKE` in the `OpCode` enum.
Update its comment to say `make` instead of `new`.
Update the comment at the top of `chunk.h` that mentions `OP_NEW`.

In `src/chunk.c`, update `opcode_name()` to return `"OP_MAKE"` for the renamed
opcode. Update the disassembly case and its comment in `chunk_disassemble_instruction()`
(the `OP_NEW` case that prints `opcode_name(OP_NEW)`).

### 3. Rename parser functions and update keyword recognition

In `src/parser.c`:
- In `is_reserved_keyword()`, change `"new"` to `"make"`.
- In `parse_primary()`, change the `token_is_keyword(&t, "new")` check to
  `token_is_keyword(&t, "make")`.
- Rename the forward declaration `parse_new` → `parse_make`.
- Rename the function definition `parse_new` → `parse_make`.
- Inside `parse_make()`, update the doc-comment and any user-facing error messages
  that mention "new" to say "make".
- In `ast_format()`, update the `AST_NEW` case to `AST_MAKE`.
- In `ast_name()` or wherever `AST_NEW` maps to a string, update to `AST_MAKE`.
  (Search for the string `"NEW"` in parser.c.)

### 4. Rename compiler function

In `src/compiler.c`:
- Rename `compile_new()` → `compile_make()`.
- Update its doc-comment to say `make` instead of `new`.
- In `compile_node()`, change the `case AST_NEW:` to `case AST_MAKE:` and
  the call from `compile_new(c, node)` to `compile_make(c, node)`.
- Change `OP_NEW` → `OP_MAKE` in the `emit_bytes()` call.

### 5. Update the VM

In `src/vm.c`:
- Change `case OP_NEW:` to `case OP_MAKE:`.
- Update the error message `"can only use 'new' with object types"` to
  `"can only use 'make' with object types"`.
- Update comments inside the `OP_MAKE` handler that mention "new" to say "make"
  (but keep `new_frame` variable names unchanged).

In `src/vm.h`:
- Update the comment on `is_initializer` that mentions `OP_NEW` to say `OP_MAKE`.

### 6. Update parser tests

In `tests/test_parser.c`:
- Change all Cutlet source strings from `"new ..."` to `"make ..."`.
- Change AST expectation strings from `"[NEW ..."` to `"[MAKE ..."`.
- Update test names and description strings (e.g. `"new with no args"` →
  `"make with no args"`).
- Update the reserved-keyword test: `"my new = 1"` should now succeed (or be
  removed); add `"my make = 1"` as the reserved-keyword failure test.

### 7. Update VM tests

In `tests/test_vm.c`:
- Change all Cutlet source strings from `"new ..."` to `"make ..."`.
- Update test description strings (e.g. `"new on number"` → `"make on number"`).
- Rename test functions: `test_obj_new_number_error` → `test_obj_make_number_error`,
  `test_obj_new_string_error` → `test_obj_make_string_error`.
- Update any assertion messages that mention "new".

### 8. Update examples and documentation

In `examples/objects.cutlet`:
- Replace all occurrences of `new ` with `make ` in Cutlet source lines.
- Update the comment `# Create an instance with new` → `# Create an instance with make`.

In `TUTORIAL.md`:
- Replace all occurrences of the `new` keyword in code examples with `make`.
- Update prose that mentions `new` (e.g. "`new` takes a type name..." →
  "`make` takes a type name...").
- Update the reserved-keywords list: remove `new`, add `make`.

### 9. Run `make test && make check`

Verify everything passes. Fix any remaining references.

## Summary

Renamed the `new` keyword to `make` throughout the entire codebase. All changes
are purely mechanical renames with no semantic changes.

**Files changed:**
- `src/parser.h` — `AST_NEW` → `AST_MAKE` in enum
- `src/parser.c` — `parse_new()` → `parse_make()`, keyword `"new"` → `"make"`, `AST_NEW` → `AST_MAKE`, `"NEW"` → `"MAKE"` in string representations
- `src/chunk.h` — `OP_NEW` → `OP_MAKE` in enum
- `src/chunk.c` — `OP_NEW` → `OP_MAKE` in `opcode_name()` and disassembler
- `src/compiler.c` — `compile_new()` → `compile_make()`, `AST_NEW` → `AST_MAKE`, `OP_NEW` → `OP_MAKE`
- `src/vm.c` — `OP_NEW` → `OP_MAKE`, error message `'new'` → `'make'`
- `src/vm.h` — comment update `OP_NEW` → `OP_MAKE`
- `tests/test_parser.c` — all `new` → `make` in Cutlet source strings and test names
- `tests/test_vm.c` — all `new` → `make` in Cutlet source strings, test names; also renamed `fn make()` → `fn create()` in closure tests (since `make` is now reserved)
- `tests/test_cli.sh` — all `new` → `make` in Cutlet source strings and expected output
- `examples/objects.cutlet` — all `new` → `make`
- `TUTORIAL.md` — all `new` keyword references → `make`

## Progress

- [x] Step 1: Rename AST_NEW → AST_MAKE in parser.h
- [x] Step 2: Rename OP_NEW → OP_MAKE in chunk.h/chunk.c
- [x] Step 3: Rename parse_new → parse_make, update keyword recognition in parser.c
- [x] Step 4: Rename compile_new → compile_make in compiler.c
- [x] Step 5: Update OP_MAKE handler and error message in vm.c/vm.h
- [x] Step 6: Update parser tests
- [x] Step 7: Update VM tests (including renaming `fn make()` → `fn create()` in closure tests)
- [x] Step 8: Update examples/objects.cutlet, TUTORIAL.md, tests/test_cli.sh
- [x] Step 9: All tests pass, lint clean (pre-existing issues only)
