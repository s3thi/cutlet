# Object System — Compiler and VM

## Objective

Add compiler and VM support for object definitions and instance creation. After this task:

- `object Name is fn method(self, ...) is body end end` compiles and executes, creating a `VAL_OBJECT_TYPE` stored as a global.
- `new Name(args)` creates a `VAL_INSTANCE`, calls `init` if defined, and returns the instance.
- Dot access on instances works: `instance.field` reads data or methods, `instance.field = value` writes data.
- Method calls on instances work: `instance.method(args)` looks up the method and calls it with the instance as `self`.
- `make test && make check` pass.

## Acceptance criteria

- [ ] `OP_OBJECT_TYPE` opcode: creates `ObjObjectType` from name-closure pairs on the stack.
- [ ] `OP_NEW` opcode: creates `ObjInstance`, calls `init` if defined, returns the instance.
- [ ] `CallFrame` has an `is_initializer` field; `OP_RETURN` returns the instance (not init's return value) for initializer frames.
- [ ] `compile_object_def()` emits method closures + `OP_OBJECT_TYPE` + `OP_DEFINE_GLOBAL`.
- [ ] `compile_new()` emits `OP_GET_GLOBAL` + args + `OP_NEW`.
- [ ] `OP_INDEX_GET` on `VAL_INSTANCE`: checks data map first, then type's method table, then returns `nothing`.
- [ ] `OP_INDEX_SET` on `VAL_INSTANCE`: writes to the instance data map.
- [ ] `OP_IN` on `VAL_INSTANCE`: checks both data map and method table.
- [ ] Instance creation works (empty data map + type reference).
- [ ] `init` is called automatically by `new` with correct arity.
- [ ] `init`'s return value is discarded; `new` always returns the instance.
- [ ] Methods can access and modify `self` fields via dot access.
- [ ] Methods can call other methods on `self`.
- [ ] Missing field access returns `nothing`.
- [ ] Dot assignment creates new fields on the instance.
- [ ] `new` on a non-type value produces a runtime error.
- [ ] Arity mismatch with `init` produces a runtime error.
- [ ] `new Name(args)` with no `init` and args > 0 produces a runtime error.
- [ ] `make test && make check` pass.

## Dependencies

- **`objects-types-parser`**: Provides `VAL_OBJECT_TYPE`, `VAL_INSTANCE`, `ObjObjectType`, `ObjInstance`, `AST_OBJECT_DEF`, `AST_NEW`, and the parser.
- **`dot-access`**: Provides dot access syntax (`.` operator), `AST_METHOD_CALL`, `OP_DUP`, `OP_SWAP`, and method call compilation/execution. Required for `instance.field` and `instance.method()` to work.

Both dependencies must be completed before this task begins.

## Constraints and non-goals

- **No mixins in this task.** `OP_OBJECT_TYPE` accepts a mixin count operand (always 0 in this task) for forward compatibility. Mixin composition is implemented in `objects-mixins-integration`.
- **No `type()` builtin update.** Deferred to the mixins/integration task.
- **`self` is not special.** It's a regular parameter name. Methods must declare it explicitly: `fn method(self, args) is ... end`.

---

## Design

### OP_OBJECT_TYPE

**Encoding:** 1 opcode byte + 3 operand bytes.

| Byte | Meaning |
|------|---------|
| `OP_OBJECT_TYPE` | Opcode |
| name constant index | Index into constant pool for the type name string |
| method count | Number of method name-closure pairs on the stack |
| mixin count | Number of mixin type values on the stack (0 in this task) |

**Stack before:** `[...mixin_types..., name1, closure1, name2, closure2, ...]` (last pair on top)

**Stack after:** `[...object_type]`

**Behavior:**
1. Pop `2 * method_count` values from the stack (name-closure pairs, last pair first).
2. Pop `mixin_count` values (mixin types — unused in this task).
3. Create `ObjObjectType` with the name from the constant pool.
4. Add each method to the type's method table via `obj_object_type_set_method()`.
5. Push the resulting `VAL_OBJECT_TYPE`.

### OP_NEW

**Encoding:** 1 opcode byte + 1 operand byte.

| Byte | Meaning |
|------|---------|
| `OP_NEW` | Opcode |
| argc | Number of explicit arguments (not counting self) |

**Stack before:** `[type, arg1, arg2, ..., argN]` (type is below args)

**Stack after:** `[instance]`

**Behavior:**
1. Read the type from `stack_top[-(int)argc - 1]`. Verify it's `VAL_OBJECT_TYPE`.
2. Create `ObjInstance` via `obj_instance_new(type)`.
3. Look up `init` in the type's method table via `obj_object_type_get_method(type, "init")`.
4. **If init exists:**
   - Save the args, pop everything (type + args) from the stack.
   - Push the init closure (callee, slots[0]).
   - Push the instance (self, slots[1]).
   - Push the saved args (slots[2..]).
   - Verify arity: `init_fn->arity` must equal `argc + 1` (self + explicit args).
   - Push a new `CallFrame` with `is_initializer = true`.
   - Execution continues inside init; when init returns, `OP_RETURN` handles the rest.
5. **If init doesn't exist and argc > 0:** Runtime error.
6. **If init doesn't exist and argc == 0:** Pop the type, push the instance.

### OP_RETURN modification

When `frame->is_initializer` is true:
- Instead of pushing the normal return value, push `frame->slots[1]` (the instance, which was passed as `self`).
- Clone `frame->slots[1]` before collapsing the frame, since the memory may be reused.
- Free the normal return value.

### Instance field access

**`OP_INDEX_GET` on `VAL_INSTANCE`:**
1. Check instance's `data` map for the key.
2. If not found, and key is `VAL_STRING`, check `type->methods` via `obj_object_type_get_method()`.
3. If not found in either, return `nothing`.

**`OP_INDEX_SET` on `VAL_INSTANCE`:**
- Always write to instance's `data` map (never to the method table).

**`OP_IN` on `VAL_INSTANCE`:**
- Check both `data` map and (for string keys) `type->methods`.

### Compilation

**`compile_object_def()`:**
```
For each method in node->children:
    OP_CONSTANT <method_name_string>
    OP_CLOSURE <method_function> [upvalue descriptors]
OP_OBJECT_TYPE <name_idx> <method_count> <mixin_count=0>
OP_DEFINE_GLOBAL <name_idx>
```

**`compile_new()`:**
```
OP_GET_GLOBAL <type_name_idx>
compile(arg1)
compile(arg2)
...
OP_NEW <argc>
```

---

## Implementation steps

### Step 1: Write VM tests

Add tests in `tests/test_vm.c` using the existing test pattern. These tests exercise the full pipeline: parse → compile → execute.

**Object definition:**
- `object Foo is fn greet(self) is "hello" end end\nnew Foo().greet()` → `"hello"` (verifies object definition + instance + method call work end-to-end).

**Instance creation (no init):**
- `object Foo is end\nmy f = new Foo()\nf` — creates an instance without error (check the result is not nothing/error; or check `value_format` output if possible).
- `object Foo is fn greet(self) is "hello" end end\nnew Foo().greet()` → `"hello"`.

**Instance creation with init:**
- `object Foo is fn init(self, x) is self.x = x end end\nmy f = new Foo(42)\nf.x` → `42`.
- `object Foo is fn init(self, a, b) is self.a = a\nself.b = b end end\nmy f = new Foo(1, 2)\nf.a + f.b` → `3`.

**Method calls:**
- `object Foo is fn init(self, x) is self.x = x end fn get(self) is self.x end end\nmy f = new Foo(99)\nf.get()` → `99`.
- `object Foo is fn init(self, x) is self.x = x end fn add(self, n) is self.x + n end end\nnew Foo(10).add(5)` → `15`.
- `object Counter is fn init(self, n) is self.n = n end fn inc(self) is self.n = self.n + 1 end fn get(self) is self.n end end\nmy c = new Counter(0)\nc.inc()\nc.inc()\nc.inc()\nc.get()` → `3` (method with side effects, called multiple times).

**Method calling another method on self:**
- `object Foo is fn init(self, x) is self.x = x end fn double(self) is self.x * 2 end fn quad(self) is self.double() * 2 end end\nnew Foo(5).quad()` → `20`.

**Dot access on instances:**
- `object Foo is fn init(self, x) is self.x = x end end\nmy f = new Foo(1)\nf.x = 99\nf.x` → `99` (field assignment overwrites).
- `object Foo is end\nmy f = new Foo()\nf.name = "test"\nf.name` → `"test"` (setting field on object without init).
- `object Foo is end\nmy f = new Foo()\nf.missing` → `nothing`.

**Bracket access on instances:**
- `object Foo is fn init(self, x) is self.x = x end end\nmy f = new Foo(42)\nf["x"]` → `42` (bracket get).
- `object Foo is end\nmy f = new Foo()\nf["y"] = 7\nf.y` → `7` (bracket set, dot get).

**`in` operator on instances:**
- `object Foo is fn init(self, x) is self.x = x end fn get(self) is self.x end end\nmy f = new Foo(1)\n"x" in f` → `true` (data field).
- `object Foo is fn init(self, x) is self.x = x end fn get(self) is self.x end end\nmy f = new Foo(1)\n"get" in f` → `true` (method).
- `object Foo is end\nmy f = new Foo()\n"missing" in f` → `false`.

**Init return value is discarded:**
- `object Foo is fn init(self) is 42 end fn check(self) is "ok" end end\nnew Foo().check()` → `"ok"` (new returns instance, not 42).

**Error cases:**
- `new 42()` → runtime error (not an object type).
- `new "hello"()` → runtime error.
- `object Foo is fn init(self, x) is self.x = x end end\nnew Foo()` → runtime error (arity mismatch: init expects 1 arg, got 0).
- `object Foo is fn init(self, x) is self.x = x end end\nnew Foo(1, 2, 3)` → runtime error (arity mismatch).
- `object Foo is end\nnew Foo(1)` → runtime error (no init, but args provided).

**Multiple instances are independent:**
- `object Foo is fn init(self, x) is self.x = x end fn get(self) is self.x end end\nmy a = new Foo(1)\nmy b = new Foo(2)\na.get() + b.get()` → `3`.

Run `make test` — confirm new tests fail (parser passes, VM tests fail because compilation support doesn't exist yet).

**Files touched:** `tests/test_vm.c`

### Step 2: Add opcodes

**`chunk.h`:**

- Add `OP_OBJECT_TYPE` to the `OpCode` enum. Comment: `/* Create object type. 3 operands: name constant index, method count, mixin count. */`
- Add `OP_NEW` to the `OpCode` enum. Comment: `/* Create instance. 1 operand: argc (explicit args, not counting self). */`

**`chunk.c`:**

- Add `"OP_OBJECT_TYPE"` and `"OP_NEW"` to `opcode_name()`.
- Add disassembly for `OP_OBJECT_TYPE` in `disassemble_instruction_to_buf()`: read 3 operand bytes (name index, method count, mixin count). Display the name from the constant pool, the method count, and the mixin count. Study how `OP_CLOSURE` handles its variable-length operands for guidance on the format.
- Add disassembly for `OP_NEW` in `disassemble_instruction_to_buf()`: read 1 operand byte (argc). Display the argc count.

Run `make test && make check`.

**Files touched:** `src/chunk.h`, `src/chunk.c`

### Step 3: Add `is_initializer` to CallFrame

**`vm.h`:**

- Add `bool is_initializer;` field to the `CallFrame` struct.

**`vm.c`:**

- Search for every place where a `CallFrame` is initialized or pushed (grep for `frames[` and `frame_count`). In each location, set `is_initializer = false` for existing call frame creation.
- In the `OP_RETURN` handler: after extracting the return value, check `frame->is_initializer`. If true:
  - The return value to use is `frame->slots[1]` (the instance/self), not the normal return value.
  - Clone this value before collapsing the frame (since `frame->slots` memory will be below `stack_top` after collapse).
  - Free the original return value that was popped.
  - Push the instance clone as the result.

Study the existing `OP_RETURN` handler carefully to understand the frame collapse sequence before making changes.

Run `make test && make check`. Existing tests should still pass (all frames have `is_initializer = false`).

**Files touched:** `src/vm.h`, `src/vm.c`

### Step 4: Compile object definitions

Add `compile_object_def()` as a static function in `compiler.c`:

1. Get the type name from `node->value`. Add it to the constant pool as a string constant.
2. For each method in `node->children` (each is an `AST_FUNCTION`):
   - Add the method's name (`method->value`) to the constant pool as a string constant.
   - Emit `OP_CONSTANT <method_name_idx>` to push the method name.
   - Compile the method function using the existing `compile_function()` mechanism. `compile_function()` handles `AST_FUNCTION` nodes and emits `OP_CLOSURE` (which pushes the closure onto the stack).
3. Emit `OP_OBJECT_TYPE` followed by 3 operand bytes: name constant index, method count (`node->child_count`), mixin count (0).
4. Emit `OP_DEFINE_GLOBAL <name_constant_idx>` to store the type as a global variable.

Add dispatch case in `compile_node()`:
```
case AST_OBJECT_DEF: compile_object_def(c, node); break;
```

**Important:** Study how `compile_function()` is called and what it pushes. It should emit `OP_CLOSURE` which ends up pushing a `VAL_CLOSURE` onto the runtime stack. Each method becomes a closure on the stack, paired with its name string constant.

Run `make test && make check`.

**Files touched:** `src/compiler.c`

### Step 5: Compile `new` expressions

Add `compile_new()` as a static function in `compiler.c`:

1. Add the type name (`node->value`) to the constant pool as a string constant.
2. Emit `OP_GET_GLOBAL <type_name_idx>` to look up the type by name.
3. For each argument in `node->children`:
   - Call `compile_node(c, node->children[i])` to compile the argument expression.
4. Emit `OP_NEW <argc>` where argc is `node->child_count`.

Add dispatch case in `compile_node()`:
```
case AST_NEW: compile_new(c, node); break;
```

Run `make test && make check`.

**Files touched:** `src/compiler.c`

### Step 6: VM — OP_OBJECT_TYPE handler

Add handler in the `vm.c` dispatch loop:

1. Read 3 operand bytes: `name_idx`, `method_count`, `mixin_count`.
2. Get the type name string from the constant pool at `name_idx`.
3. Create `ObjObjectType` via `obj_object_type_new(name_string)`.
4. Pop `2 * method_count` values from the stack. These are name-closure pairs with the last method's closure on top. Pop in reverse order: for each method, pop the closure first, then the name.
5. For each name-closure pair, call `obj_object_type_set_method(type, name.string, closure)`. Free the name and closure values after setting (the method table takes clones internally via `obj_map_set`).
6. Ignore `mixin_count` (must be 0 in this task).
7. Push `make_object_type(type)`.

Run `make test && make check`.

**Files touched:** `src/vm.c`

### Step 7: VM — OP_NEW handler

Add handler in the `vm.c` dispatch loop:

1. Read 1 operand byte: `argc`.
2. Read the type value from `stack_top[-(int)argc - 1]`.
3. If it's not `VAL_OBJECT_TYPE`, return `vm_runtime_error(vm, "can only use 'new' with object types")`.
4. Create instance via `obj_instance_new(type->object_type)`.
5. Look up `init` via `obj_object_type_get_method(type->object_type, "init")`.
6. **If init exists:**
   - Save the argc arguments into a temporary buffer (pop them from the stack).
   - Pop the type value from the stack.
   - Clone the init method value. It must be `VAL_CLOSURE`.
   - Push the init closure (callee — `slots[0]`).
   - Push the instance value (self — `slots[1]`).
   - Push each saved argument.
   - Verify arity: `init_closure->function->arity` must equal `argc + 1`. If not, runtime error with a message like `"init() expects %d argument(s), got %d"` (showing user-facing counts, i.e., arity - 1 and argc).
   - Push a new `CallFrame`: set `closure`, `ip`, `slots` (same pattern as `OP_CALL`), and `is_initializer = true`.
   - Break — execution continues inside init. When init does `OP_RETURN`, the `is_initializer` flag ensures the instance is returned.
7. **If init doesn't exist and argc > 0:** Runtime error `"'TypeName' has no init() method but received N argument(s)"`.
8. **If init doesn't exist and argc == 0:** Pop the type, push the instance.

Study the existing `OP_CALL` handler carefully — the call frame setup for init should follow the same pattern (stack slot calculation, frame count increment, etc.).

Run `make test && make check`.

**Files touched:** `src/vm.c`

### Step 8: VM — Instance field access

Extend existing opcode handlers in `vm.c` to support `VAL_INSTANCE`:

**`OP_INDEX_GET`:**

Add a branch for `VAL_INSTANCE` in the `OP_INDEX_GET` handler (alongside the existing `VAL_MAP` and `VAL_ARRAY` branches):

1. Get the instance from the container value.
2. Check the instance's `data` map via `obj_map_get(inst->data, &index)`.
3. If found, use that value.
4. If not found and `index.type == VAL_STRING`, check the type's method table via `obj_object_type_get_method(inst->type, index.string)`.
5. If found in the method table, use that value.
6. Otherwise, result is `nothing`.

**`OP_INDEX_SET`:**

Add a branch for `VAL_INSTANCE`:
- Call `obj_map_ensure_owned()` on the instance's data map (or equivalent COW handling if applicable).
- Call `obj_map_set(inst->data, &index, &value)`.
- Follow the same result/cleanup pattern as the `VAL_MAP` branch.

**`OP_IN`:**

Add a branch for `VAL_INSTANCE`:
- Check `obj_map_has(inst->data, &key)`.
- If not found and `key.type == VAL_STRING`, check `obj_object_type_get_method(inst->type, key.string) != NULL`.
- Result is `make_bool(found)`.

Run `make test && make check`. All VM tests should now pass.

**Files touched:** `src/vm.c`

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

### Step 1: Write VM tests — DONE (2026-02-28)

Added 24 tests in `tests/test_vm.c` covering:
- Object definition + method call (1 test)
- Instance creation without init (2 tests)
- Instance creation with init (2 tests)
- Method calls including side effects (3 tests)
- Method calling another method on self (1 test)
- Dot access on instances: overwrite, no-init field, missing field (3 tests)
- Bracket access on instances: get and set (2 tests)
- `in` operator on instances: data field, method, missing (3 tests)
- Init return value discarded (1 test)
- Error cases: new on non-type, arity mismatches, no init with args (5 tests)
- Multiple instances are independent (1 test)

All 19 non-error tests fail with "compile: unknown AST node type 24" (AST_OBJECT_DEF not handled by compiler yet). The 5 error-case tests pass because they produce parse/compile errors. All 560 pre-existing VM tests continue to pass. `make format-check` passes.

Autonomous decision: Changed `new 42()` and `new "hello"()` error tests to use variables (`my x = 42\nnew x()`) since the parser requires an identifier after `new`, making literal values a parse error rather than a runtime error. Using variables ensures these tests will exercise the VM runtime error path once the compiler is implemented.

### Step 2: Add opcodes — DONE (2026-02-28)

Added `OP_OBJECT_TYPE` and `OP_NEW` to the `OpCode` enum in `chunk.h` with documentation comments. Added corresponding entries in `opcode_name()` and disassembly support in `disassemble_instruction_to_buf()` in `chunk.c`.

- `OP_OBJECT_TYPE`: 3 operand bytes (name constant index, method count, mixin count). Disassembly shows the type name from the constant pool plus method and mixin counts.
- `OP_NEW`: 1 operand byte (argc). Disassembly shows the argument count.

All 565 pre-existing tests pass. The 19 new VM tests from Step 1 still fail as expected (compiler doesn't handle AST_OBJECT_DEF yet). `make format-check` passes. Pre-existing clang-tidy warnings remain (all in unrelated files).

---

End of plan.
