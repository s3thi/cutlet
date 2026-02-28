# Object System — Mixins and Integration

## Objective

Add mixin composition to the object system and complete integration testing. After this task:

- `object Dog with Speakable, Walkable is ... end` copies methods from mixin objects into Dog's method table at definition time.
- Dog's own methods win on conflict with mixin methods.
- Later mixins overwrite earlier mixins on conflict (left-to-right, last wins).
- Integration tests, CLI tests, and an example program cover the full object system.
- `make test && make check` pass.

## Acceptance criteria

- [ ] `object Dog with Mixin1 is ... end` copies Mixin1's methods into Dog.
- [ ] `object Dog with A, B is ... end` copies A's methods, then B's (B overwrites A on conflict).
- [ ] Dog's own methods overwrite all mixin methods on conflict.
- [ ] Mixin does not modify the source object type (copying is one-way).
- [ ] Runtime error if a mixin name resolves to a non-`VAL_OBJECT_TYPE` value.
- [ ] Runtime error if a mixin name is undefined (standard "undefined variable" error from `OP_GET_GLOBAL`).
- [ ] `init` from a mixin works if the object doesn't define its own.
- [ ] Object's own `init` overwrites a mixin's `init`.
- [ ] `type()` native function handles `VAL_OBJECT_TYPE` and `VAL_INSTANCE`.
- [ ] `str()` on instances returns a readable representation.
- [ ] `keys()` on instances returns data field names (not methods).
- [ ] `len()` on instances returns data field count.
- [ ] CLI integration tests cover object definition, instantiation, method calls, and mixins.
- [ ] Example program `examples/objects.cutlet` and `examples/objects.expected` exist.
- [ ] `make test && make check` pass.
- [ ] `make test-examples` passes.

## Dependencies

- **`objects-compiler-vm`**: Provides `OP_OBJECT_TYPE`, `OP_NEW`, instance field access, method dispatch, and the full object creation pipeline.

## Constraints and non-goals

- **No inheritance.** No `super`, no prototype chains. Mixins are flat method copying.
- **No method resolution order (MRO).** Just left-to-right copying with last-wins semantics.
- **No `instanceof` operator.** Can be added later if needed.
- **No mixin of mixins.** If object A has mixins and object B uses `with A`, B gets A's final method table (including A's mixed-in methods). This works naturally since A's method table already contains its mixin methods.

---

## Design

### Mixin composition

When `object Dog with A, B is fn bark(self) is "woof" end end` executes:

1. Look up `A` and `B` as globals. Both must be `VAL_OBJECT_TYPE`.
2. Copy all methods from A's method table into Dog's method table.
3. Copy all methods from B's method table into Dog's method table (overwrites A's methods on name conflict).
4. Add Dog's own methods (overwrites both A's and B's methods on name conflict).

The compiler pushes mixin types onto the stack before method name-closure pairs. `OP_OBJECT_TYPE` pops them and applies the composition.

### Compiler changes

`compile_object_def()` is modified: if `node->param_count > 0` (mixin names exist), emit `OP_GET_GLOBAL` for each mixin name before the method name-closure pairs. Update the mixin count operand in `OP_OBJECT_TYPE`.

**Stack layout before `OP_OBJECT_TYPE`:**
```
[mixin1_type, mixin2_type, ..., method1_name, method1_closure, ..., methodN_name, methodN_closure]
```

### VM changes

`OP_OBJECT_TYPE` handler is modified:
1. Pop method name-closure pairs (on top of stack).
2. Pop mixin type values (below the method pairs).
3. For each mixin (in forward order, first mixin first): verify it's `VAL_OBJECT_TYPE`, copy its methods into the new type.
4. Add own methods (overwrites mixin methods on conflict).

---

## Implementation steps

### Step 1: Write mixin VM tests

Add mixin tests in `tests/test_vm.c`:

**Basic mixin:**
- `object Greeter is fn greet(self) is "hi" end end\nobject Dog with Greeter is fn bark(self) is "woof" end end\nnew Dog().greet()` → `"hi"`.
- `object Greeter is fn greet(self) is "hi" end end\nobject Dog with Greeter is fn bark(self) is "woof" end end\nnew Dog().bark()` → `"woof"`.

**Own method wins over mixin:**
- `object Base is fn name(self) is "base" end end\nobject Child with Base is fn name(self) is "child" end end\nnew Child().name()` → `"child"`.

**Multiple mixins:**
- `object A is fn a(self) is "a" end end\nobject B is fn b(self) is "b" end end\nobject C with A, B is end\nmy c = new C()\nc.a() ++ c.b()` → `"ab"`.

**Later mixin wins on conflict:**
- `object A is fn x(self) is "from A" end end\nobject B is fn x(self) is "from B" end end\nobject C with A, B is end\nnew C().x()` → `"from B"`.

**Mixin doesn't modify source:**
- `object A is fn x(self) is 1 end end\nobject B with A is fn y(self) is 2 end end\nnew A().x()` → `1` (A still works).
- After defining B with A, `"y" in new A()` → `false` (A does not gain B's methods).

**Init from mixin:**
- `object Initable is fn init(self, n) is self.n = n end end\nobject Foo with Initable is fn get(self) is self.n end end\nnew Foo(42).get()` → `42`.

**Object overrides mixin init:**
- `object Initable is fn init(self, n) is self.n = n end end\nobject Foo with Initable is fn init(self, n) is self.n = n * 2 end fn get(self) is self.n end end\nnew Foo(5).get()` → `10`.

**Mixin of an object that itself has mixins:**
- `object A is fn a(self) is "a" end end\nobject B with A is fn b(self) is "b" end end\nobject C with B is end\nmy c = new C()\nc.a() ++ c.b()` → `"ab"` (B's method table includes A's methods, so C gets both).

**Error cases:**
- `my x = 42\nobject Foo with x is end` → runtime error (mixin is not an object type).

Run `make test` — confirm new tests fail.

**Files touched:** `tests/test_vm.c`

### Step 2: Update compiler for mixins

Modify `compile_object_def()` in `compiler.c`:

If `node->param_count > 0` (mixin names exist), emit `OP_GET_GLOBAL` for each mixin name BEFORE the method name-closure pairs:

```
for each mixin name in node->params:
    add mixin name to constant pool as string
    emit OP_GET_GLOBAL <mixin_name_idx>
```

Then continue with the existing method compilation (push name-closure pairs).

Update the `OP_OBJECT_TYPE` emit to use the actual mixin count: `emit_byte(c, (uint8_t)node->param_count, node->line)` instead of hardcoded 0.

Run `make test && make check`.

**Files touched:** `src/compiler.c`

### Step 3: Implement mixin composition in VM

Modify the `OP_OBJECT_TYPE` handler in `vm.c`:

After reading the 3 operand bytes and before creating the type:

1. **Pop own method pairs** (they're on top of the stack): save `2 * method_count` values into a temporary buffer (pop closure first, then name, for each method — since the last method's closure is on top).

2. **Pop mixin type values** (next on the stack): save `mixin_count` values into a temporary buffer (last mixin is on top, so pop in reverse to get forward order).

3. Create the `ObjObjectType`.

4. **Apply mixins in forward order** (first mixin applied first, later mixins overwrite earlier ones):
   - For each mixin value, verify it's `VAL_OBJECT_TYPE`. If not, free everything and return `vm_runtime_error(vm, "mixin must be an object type")`.
   - Iterate over the mixin's `methods` map entries. For each entry, call `obj_object_type_set_method(new_type, entry->key.string, entry->value)`.
   - Free the mixin value.

5. **Apply own methods** (always last, so they overwrite mixin methods):
   - For each saved name-closure pair, call `obj_object_type_set_method(new_type, name.string, closure)`.
   - Free the name and closure values.

6. Push `make_object_type(new_type)`.

Note: To iterate over an `ObjMap`'s entries, access `map->entries` (a dense array of `MapEntry` structs) and `map->count`. Each `MapEntry` has `.key` and `.value` fields.

Run `make test && make check`. Mixin tests should pass.

**Files touched:** `src/vm.c`

### Step 4: Update native functions for new types

Update native functions and helpers in `vm.c` (and `value.c` if needed):

**`type()` native function (or equivalent):**

Find where value type names are generated (search for `"number"`, `"string"`, `"map"` etc. — likely in a helper function or the `type()` native itself).
- `VAL_OBJECT_TYPE` → return `"object_type"`.
- `VAL_INSTANCE` → return the type name string (e.g., `"Dog"`) accessed via `val.instance->type->name`.

**`str()` native function:**
- `VAL_OBJECT_TYPE` → return `"<object TypeName>"`.
- `VAL_INSTANCE` → return `"<TypeName instance>"`.
- Note: if `str()` uses `value_format()` internally, this may already work from Task 1's `value_format()` implementation. Verify and add handling if needed.

**`keys()` native function:**
- `VAL_INSTANCE` → return keys of the instance's `data` map only (not the type's method table). Follow the same pattern as the `VAL_MAP` handler for `keys()`.

**`len()` native function:**
- `VAL_INSTANCE` → return the count of data fields (`inst->data->count`). Follow the `VAL_MAP` pattern.

**`has_key()` native function:**
- `VAL_INSTANCE` → check data map only (consistent with `keys()` returning only data keys), OR check both data and methods (consistent with `OP_IN`). Choose whichever is consistent with `OP_IN` — if `OP_IN` checks both, `has_key()` should too.

**`say()` native function:**
- Uses `value_format()` internally. Already handled by Task 1's implementation. Verify it prints without crashing.

Add tests for the native function updates in `tests/test_vm.c`:
- `object Foo is end\ntype(new Foo())` → `"Foo"` (or the type name).
- `object Foo is end\ntype(Foo)` → `"object_type"`.
- `object Foo is fn init(self, x) is self.x = x end end\nkeys(new Foo(1))` → `["x"]`.
- `object Foo is fn init(self, x) is self.x = x end end\nlen(new Foo(1))` → `1`.
- `object Foo is fn init(self, x) is self.x = x end end\nstr(new Foo(42))` — verify it returns a string without error.

Run `make test && make check`.

**Files touched:** `src/vm.c`, `tests/test_vm.c`

### Step 5: CLI integration tests

Add end-to-end integration tests in `tests/test_cli.sh`:

- Pipe test: basic object + method call: `echo 'object Foo is fn x(self) is "hi" end end\nsay(new Foo().x())' | cutlet run -` → output contains `hi`.
- Pipe test: object with init: `echo 'object Foo is fn init(self, n) is self.n = n end fn get(self) is self.n end end\nsay(new Foo(42).get())' | cutlet run -` → output contains `42`.
- Pipe test: mixin: `echo 'object A is fn a(self) is "a" end end\nobject B with A is end\nsay(new B().a())' | cutlet run -` → output contains `a`.
- Pipe test: `--ast` output contains `OBJECT_DEF` and `NEW`.
- Pipe test: `--bytecode` output contains `OP_OBJECT_TYPE` and `OP_NEW`.
- Pipe test: error case — `echo 'new 42()' | cutlet run -` produces an error message.

Study the existing tests in `tests/test_cli.sh` for the exact pipe/assertion pattern.

Run `make test && make check`.

**Files touched:** `tests/test_cli.sh`

### Step 6: Example program

Create `examples/objects.cutlet` — a small, readable example demonstrating the object system:

```
# Define a mixin for greeting behavior
object Greeter is
  fn greet(self, name) is
    say("Hello, " ++ name ++ "! I'm a " ++ type(self) ++ ".")
  end
end

# Define a Dog object that uses the Greeter mixin
object Dog with Greeter is
  fn init(self, name, breed) is
    self.name = name
    self.breed = breed
  end

  fn describe(self) is
    say(self.name ++ " is a " ++ self.breed)
  end
end

# Create and use instances
my dog = new Dog("Rex", "German Shepherd")
dog.describe()
dog.greet("World")

# Instances are backed by maps — fields are dynamic
dog.tricks = 3
say(dog.name ++ " knows " ++ str(dog.tricks) ++ " tricks")
```

Adjust the example to use only features that are actually implemented and produce clean output. The exact content depends on what native functions and string operations are available.

Generate `examples/objects.expected` by running:
```
./build/cutlet run examples/objects.cutlet > examples/objects.expected
```

Run `make test && make check && make test-examples`.

**Files touched:** `examples/objects.cutlet`, `examples/objects.expected`

### Step 7: Final verification and edge cases

Add additional edge case tests in `tests/test_vm.c`:

- Empty object with mixin: `object A is fn x(self) is 1 end end\nobject B with A is end\nnew B().x()` → `1`.
- Multiple instances are independent: `object Foo is fn init(self, x) is self.x = x end fn get(self) is self.x end end\nmy a = new Foo(1)\nmy b = new Foo(2)\na.get() + b.get()` → `3`.
- Instance is truthy: `object Foo is end\nif new Foo() then "yes" else "no" end` → `"yes"`.
- Object type is truthy: `object Foo is end\nif Foo then "yes" else "no" end` → `"yes"`.

Run `make test && make check && make test-examples`. All tests pass.

**Post-implementation reminders** (per AGENTS.md):
- Remind user to update `TUTORIAL.md` to cover the object system (object definitions, `new`, `init`, `self`, mixins, dot access on instances).
- The example program `examples/objects.cutlet` serves as the usage example.

**Files touched:** `tests/test_vm.c`

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

- [x] **Step 1**: Write mixin VM tests — 10 tests added in `tests/test_vm.c`. 6 fail as expected (mixin copying not implemented). 4 pass (test own-method behavior that already works). Committed `7f67ebb`.
- [x] **Step 2**: Update compiler for mixins — Modified `compile_object_def()` in `src/compiler.c` to emit `OP_GET_GLOBAL` for each mixin name before method pairs, and replaced hardcoded mixin_count=0 with `node->param_count`. Same 6 mixin tests still fail (VM not updated yet). Committed `92945fe`.
- [x] **Step 3**: Implement mixin composition in VM — Reworked `OP_OBJECT_TYPE` handler in `src/vm.c` to pop method pairs and mixin types into temp buffers, apply mixin methods in forward order, then apply own methods last. All 10 mixin tests pass. Committed `9389893`.
- [x] **Step 4**: Update native functions for new types — Added `type()` native function returning type names ("object_type" for types, user-defined name like "Foo" for instances). Updated `value_type_name()` for VAL_OBJECT_TYPE/VAL_INSTANCE. Extended `keys()`, `len()`, `has_key()` to handle VAL_INSTANCE (has_key checks both data and methods, consistent with OP_IN). str() and say() already worked via value_format(). 17 tests added, all pass.
- [x] **Step 5**: CLI integration tests — Added 6 end-to-end tests in `tests/test_cli.sh`: basic object method call, object with init, mixin composition, --ast shows OBJECT_DEF/NEW, --bytecode shows OP_OBJECT_TYPE/OP_NEW, error case for `new 42()`. All 168 CLI tests pass. Committed `9bb96d9`.
- [ ] **Step 6**: Example program
- [ ] **Step 7**: Final verification and edge cases

---

End of plan.
