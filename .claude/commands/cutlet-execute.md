Read `AGENTS.md` for project conventions — follow them exactly.

If a plan file name was provided below, read that file from `plans/doing/`. Otherwise, pick a task from `plans/doing/`.

$ARGUMENTS

If the task file is incomplete or ambiguous, stop and ask me before writing any code.

## Step-by-step execution

Plans may contain multiple **implementation steps** (e.g. "Step 1: …", "Step 2: …"). When a plan has multiple steps, **implement only one step per invocation**. This prevents context exhaustion on large plans.

**How to determine which step to work on:**

1. Read the plan file carefully — look for a `## Progress` section at the bottom.
2. If a `## Progress` section exists, it will list which steps are already done. Work on the next incomplete step.
3. If no `## Progress` section exists, start with Step 1.

**After completing a step:**

1. Append or update a `## Progress` section at the bottom of the plan file (before `End of plan.` if present) recording which step was just completed, with a one-line summary. Example:
   ```
   ## Progress

   - [x] Step 1: Add ObjUpvalue, ObjClosure types and VAL_CLOSURE — added types, constructors, refcounting, and unit tests
   - [x] Step 2: Add new opcodes and update disassembler — added 4 opcodes, updated disassembler output
   ```
2. Commit your work.
3. If **all steps are now complete**, also:
   - Move the task file from `plans/doing/` to `plans/done/` with the next consecutive 4-digit number prefix.
   - Add a short summary (what changed, files touched) to the file.
4. If steps remain, **stop**. Do NOT continue to the next step. Tell me what was completed and what the next step will be.

## Execution process (each step)

1. State your implementation approach in 2-3 sentences
2. Flag anything risky — if blockers exist, stop and ask me
3. Implement the step, following the required process in `AGENTS.md` (tests first, confirm failures, then implement, `make test && make check` after every change)
4. When the step's criteria are met and `make test && make check` pass, commit
5. Update the plan's progress section as described above
