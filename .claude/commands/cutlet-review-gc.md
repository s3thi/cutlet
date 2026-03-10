Deep review of GC correctness. This is a specialist review — it focuses exclusively on garbage collection safety and ignores everything else.

## Getting the code to review

Run `scripts/review-diff $ARGUMENTS` to get the list of changed files and the diff.

**Default** (no arguments): reviews working tree changes.
Other modes: `branch <name>`, `merge`, `last <N>`, `files <file1> [file2...]`.

After running the script:
1. Read the **full content** of every changed file listed.
2. Also read these files for reference (they define the GC rules):
   - `src/gc.h` — GC API, pin/unpin interface, temp root limits
   - `src/gc.c` — GC implementation, mark/sweep, root tracing
   - `src/value.h` — Value types, which types contain GC-managed objects

## The GC rules

Cutlet uses a mark-and-sweep collector. The critical rules are:

### Rule 1: Pin before allocating

Any GC-managed object held in a C local variable (not on the VM stack, not in a global, not in another GC-tracked object) is invisible to the collector. If `gc_alloc()` triggers `gc_collect()`, that object gets swept.

**The pattern**: after popping a GC-managed value off the VM stack or creating one, call `gc_pin()` before any operation that might allocate. Call `gc_unpin()` after the value is safely reachable again (pushed back on stack, stored in a global, etc.).

Look for: values popped into C locals followed by `gc_alloc`, `obj_string_*` functions, `value_to_string`, `value_concat`, or any function that might allocate internally. If there's no `gc_pin` in between, that's a bug.

### Rule 2: Pin/unpin must be balanced

Every `gc_pin()` needs a matching `gc_unpin()`. Check error paths — an early return after `gc_pin()` without `gc_unpin()` corrupts the temp root stack.

### Rule 3: Mark functions must be exhaustive

Every object type's mark function must mark ALL reachable sub-objects. When a new field is added to an object struct (e.g., a new `ObjString*` field on `ObjFunction`), the mark function must be updated. Look for struct fields that hold GC-managed objects and verify they're marked.

### Rule 4: Sweep must free all owned resources

`free_object_contents()` in `gc.c` must handle every object type. When a new `OBJ_*` type is added, it must have a case there. Look for missing cases.

### Rule 5: Root tracing must cover all roots

`gc_mark_roots()` must walk everything that can keep objects alive: the VM stack, call frame closures/functions, the global variable table, upvalues, and the temp root stack. When new root sources are added (new global state, new caches, etc.), the root tracer must be updated.

## What to look for

For each changed file, trace every path through the code and check:

1. **Missing pins**: A GC-managed object in a C local variable with a potential allocation between when it was obtained and when it becomes reachable again. This is the #1 bug pattern.

2. **Unbalanced pins**: `gc_pin` without matching `gc_unpin` on any path (including error returns).

3. **Missing marks**: A struct field that holds a GC-managed object (directly or indirectly) but isn't visited by the type's mark function.

4. **Missing sweep cases**: A new object type without a `free_object_contents` case.

5. **Missing roots**: A new source of live objects that `gc_mark_roots` doesn't know about.

6. **Value lifecycle**: `value_free()` called on a value that contains a GC-managed object — this would double-free when the GC sweeps. GC-managed values should NOT be freed with `value_free()` after the GC transition.

## What to ignore

- Everything that isn't GC-related. No style, no performance, no general correctness.
- Code in `tests/` — test GC usage is intentionally simplified and often uses `gc_suppress`.
- Code in `vendor/` — not ours.

## Output format

For each finding:
- **Location**: file, function, and line range
- **Rule violated**: which of the 6 rules above
- **The problem**: what specific sequence of operations is unsafe
- **Trigger**: what would cause this to actually crash (e.g., "if gc_collect runs during the obj_string_concat call on line N")
- **Fix**: concrete suggestion

If you find nothing, say "No GC issues found" and stop. Do not invent findings.
