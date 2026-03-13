# Objects as first-class expressions

## Objective

Make object definitions behave exactly like function definitions: they are
pure expressions that produce a value, support optional names for
auto-binding, and support anonymous forms. Additionally, `new` should
resolve the type through normal variable lookup (local -> upvalue -> global)
instead of always using `OP_GET_GLOBAL`.

Done when:

- `object Foo is ... end` at the top level auto-binds to `Foo` (global or
  local depending on context) -- same as `fn Foo() is ... end` today.
- `my alias = object Foo is ... end` binds `Foo` *and* `alias` -- same as
  `my alias = fn Foo() is ... end`.
- `object is ... end` (anonymous, no name) is valid and produces an unnamed
  object type value.
- `new` resolves types via local -> upvalue -> global lookup, so
  `my T = object T is ... end; new T()` works inside functions.
- `with` resolves mixin types via local -> upvalue -> global lookup, so
  locally-defined mixins work inside functions.
- `make test && make check` pass.

## Acceptance criteria

- [ ] **Compiler: `compile_object_def` mirrors `compile_function`** -- named
      objects bind as local or global depending on `c->context`; anonymous
      objects just leave a value on the stack with no binding.
- [ ] **Compiler: `compile_new` uses `emit_load_callable` pattern** -- resolves
      the type name as local -> upvalue -> global, not hardcoded `OP_GET_GLOBAL`.
- [ ] **Compiler: mixin resolution in `compile_object_def` uses
      `emit_load_callable` pattern** -- the `with` clause resolves mixin types
      via local -> upvalue -> global, not hardcoded `OP_GET_GLOBAL`.
- [ ] **Parser: `parse_object` accepts anonymous objects** -- `object is ... end`
      (no name after `object`) parses into `AST_OBJECT_DEF` with `value = NULL`.
- [ ] **VM: `OP_OBJECT_TYPE` handles NULL name** -- anonymous object types get a
      generated display name like `"<anonymous object>"` for `typeof()`.
- [ ] **Parser tests cover new cases**: anonymous objects, named objects inside
      functions, `my alias = object ...`.
- [ ] **VM tests cover new cases**: anonymous object instantiation, local-variable
      type lookup in `new`, object defined inside a function, upvalue capture of
      object types.
- [ ] `make test && make check` pass.

## Dependencies

- **`20260309115445-rename-new-to-make`** -- that plan renames `new` -> `make` and
  `AST_NEW` -> `AST_MAKE` etc. This plan uses current names (`new`, `AST_NEW`,
  `compile_new`, `OP_NEW`). If the rename lands first, apply the same changes
  to the renamed symbols (`make`, `AST_MAKE`, `compile_make`, `OP_MAKE`).
  The two plans don't conflict structurally -- one is a rename, this one changes
  compilation logic -- but the executing agent must check which names are current
  before editing.

## Constraints / non-goals

- `typeof()` on anonymous instances should return `"<anonymous object>"`,
  not crash or return garbage.
- Do NOT change the `ObjObjectType` struct -- the `name` field becomes NULL
  for anonymous types, same as `ObjFunction.name` for anonymous functions.
- Do NOT change how methods are compiled inside object bodies -- only change
  how the object type value is bound after `OP_OBJECT_TYPE`.

## Steps

### 1. Write parser tests for anonymous objects

In `tests/test_parser.c`, add tests:

- `"object is fn greet() is 42 end end"` -- anonymous object, parses to
  `AST_OBJECT_DEF` with `value = NULL` and one method child.
- `"my T = object is fn x() is 1 end end"` -- anonymous object assigned to
  variable, parses to `AST_DECL` whose RHS is `AST_OBJECT_DEF` with
  `value = NULL`.
- Verify existing named-object tests still pass (don't touch them).

### 2. Update `parse_object` to accept anonymous objects

In `src/parser.c`, function `parse_object`:

After consuming the `object` keyword, check whether the next token is `is`
(anonymous) or an identifier (named). If the next token is `is`, skip the
name-parsing step and leave `type_name = NULL`. If it's an identifier
followed by `is` or `with`, proceed as today. Otherwise emit a parse error.

Specifically: after `advance(p)` for the `object` keyword, instead of
unconditionally requiring an identifier, add a branch:

- If `token_is_keyword(&p->current, "is")`: set `type_name = NULL`, skip to
  the `is`-consumption step.
- Else: parse identifier as today.

### 3. Write VM tests for expression-like object behavior

In `tests/test_vm.c`, add tests:

- **Anonymous object**: `"my T = object is fn hi() is 42 end end\nnew T()"` --
  evaluates to an instance.
- **Local type lookup in `new`**: A function that defines a local object type
  and instantiates it:
  `"fn make-it() is\n  object Local is\n    fn val() is 99 end\n  end\n  new Local()\nend\nmake-it().val()"`
  -- should return `99`.
- **Alias binding**: `"my X = object Foo is fn id() is 1 end end\nnew X()"` --
  both `Foo` and `X` should work at global scope.
- **Upvalue capture of object type**: A closure that captures a locally-defined
  type and instantiates it later.
- **Local mixin resolution**: A function that defines a mixin locally and uses
  it with `with`:
  `"fn go() is\n  object Greetable is\n    fn hi() is 1 end\n  end\n  object Bot with Greetable is\n  end\n  new Bot().hi()\nend\ngo()"`
  -- should return `1`.
- **typeof() on anonymous instance**: should return `"<anonymous object>"`.

### 4. Update `compile_object_def` to mirror `compile_function`

In `src/compiler.c`, rewrite `compile_object_def` so that after emitting
`OP_OBJECT_TYPE`, the name-binding logic matches `compile_function`:

```
if (node->value) {                     /* named object */
    if (c->context == COMPILE_FUNCTION) {
        /* Register as local (same pattern as compile_function). */
        int slot = c->local_count;
        if (c->local_count >= LOCALS_MAX) { compiler_error(...); return; }
        c->locals[c->local_count++] = (Local){
            .name = node->value,
            .length = strlen(node->value),
            .depth = c->scope_depth,
        };
        emit_bytes(c, OP_GET_LOCAL, slot, line);
    } else {
        /* Script context: global binding. */
        char *name = compiler_strdup(c, node->value);
        int name_idx = chunk_find_or_add_constant(c->chunk, make_string(name));
        emit_bytes(c, OP_DEFINE_GLOBAL, name_idx, line);
    }
}
/* Anonymous objects: no binding, value stays on stack as expression result. */
```

Remove the unconditional `OP_DEFINE_GLOBAL` that currently sits at the end
of `compile_object_def`.

Also update the mixin resolution loop in `compile_object_def`. Currently it
hardcodes `OP_GET_GLOBAL` for each mixin name. Replace each
`emit_bytes(c, OP_GET_GLOBAL, ...)` in the mixin loop with a call to
`emit_load_callable(c, mixin_name, line)` (or inline the same
local -> upvalue -> global pattern). This way `object Child with Base is...end`
works when `Base` is a local variable.

Also update the `OP_OBJECT_TYPE` emission to handle `node->value == NULL`:
when the name is NULL, add `"<anonymous object>"` as the constant for the
name operand. The rest of the opcode emission stays the same.

### 5. Update `compile_new` to use local -> upvalue -> global resolution

In `src/compiler.c`, function `compile_new`:

Replace the hardcoded `OP_GET_GLOBAL` emission with the same
local -> upvalue -> global resolution pattern used by `emit_load_callable`.
Either call `emit_load_callable(c, node->value, line)` directly, or
inline the same logic.

### 6. Update `OP_OBJECT_TYPE` in the VM for NULL names

In `src/vm.c`, the `OP_OBJECT_TYPE` handler reads the name from the constant
pool. When the object is anonymous, the constant will be
`"<anonymous object>"`. The `obj_object_type_new` function receives this
string as the name. Verify that `typeof()` on an anonymous instance returns
`"<anonymous object>"` -- the VM test from step 3 covers this.

No structural changes needed in the VM -- `obj_object_type_new` already takes
a string name parameter. The anonymous name is just a different string.

### 7. Run `make test && make check`

Verify all tests pass. Fix any issues found.

## Progress

- [x] Step 1: Write parser tests for anonymous objects — added 6 tests (3 parsing, 3 completeness)
- [x] Step 2: Update `parse_object` to accept anonymous objects — parser accepts `object is ... end`, ast_format handles NULL name
- [x] Step 3: Write VM tests for expression-like object behavior — added 6 tests: anonymous object, local type lookup, alias binding, upvalue capture, local mixin resolution, typeof on anonymous instance. Used `make` (not `new`) per rename that already landed. Tests fail as expected (segfault on anonymous object).
- [x] Step 4: Update `compile_object_def` to mirror `compile_function` — anonymous objects use `"<anonymous object>"` as display name; mixin resolution uses `emit_load_callable` for local->upvalue->global lookup; named objects bind as local (function context) or global (script context); anonymous objects leave value on stack. Fixes 3 VM tests (anonymous, alias binding, typeof anonymous). Remaining 3 failures need Step 5 (compile_make local lookup) and chained-call parser support.
- [x] Step 5: Update `compile_make` to use local -> upvalue -> global resolution — replaced hardcoded `OP_GET_GLOBAL` with `emit_load_callable`. Fixed `close_upvalues` in vm.c to use `value_clone` instead of raw copy so refcounted types survive upvalue capture. Fixed Step 3 test methods to include `self` parameter; rewrote upvalue capture test to avoid unsupported chained calls (`outer()().val()`). All 620 tests pass.
- [x] Step 6: Verify `OP_OBJECT_TYPE` VM handler for anonymous objects — no code changes needed. The handler at vm.c:1852 reads the type name string from the constant pool (line 1874) and passes it to `obj_object_type_new` (line 1948). Step 4 already emits `"<anonymous object>"` as the constant for anonymous objects (compiler.c:1212). The `test_obj_expr_typeof_anonymous` test confirms `type(make T())` returns `"<anonymous object>"`. All tests pass.
