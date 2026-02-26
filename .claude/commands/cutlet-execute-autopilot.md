Read `AGENTS.md` for project conventions — follow them exactly.

If a plan file name was provided below, read that file from `plans/doing/`. Otherwise, pick a task from `plans/doing/`.

$ARGUMENTS

If the task file is incomplete or ambiguous, stop and ask me before writing any code.

## Unattended execution

This command implements **all remaining steps** of a plan without stopping between steps. Each step runs in a subagent (via the Task tool) to prevent context exhaustion.

**Before starting**, read the plan file and determine which steps remain (check the `## Progress` section). Then execute each remaining step sequentially by launching a subagent for each one.

## Subagent dispatch (for each step)

For each remaining step, launch a Task (subagent_type: "general-purpose") with a prompt that includes:

1. The full text of `AGENTS.md` (so the subagent knows project conventions).
2. The full text of the plan file.
3. Which step number to implement.
4. These instructions:
   - State the implementation approach in 2-3 sentences.
   - Implement the step, following the required process in `AGENTS.md` (tests first, confirm failures, then implement, `make test && make check` after every change).
   - When the step's criteria are met and `make test && make check` pass, commit.
   - Update the plan's `## Progress` section.
   - If **all steps are now complete**, also add a short summary and run `scripts/plan-done <name>`.
   - Do NOT proceed to any other step.

**Important**: Wait for each subagent to complete before launching the next one. Steps are sequential — each builds on the previous step's commits.

## After all steps complete

After the final subagent finishes successfully, push the branch to the host repo:

```
git push origin <branch>
```

Then report what was completed.

## Error handling

If a subagent reports failure (tests not passing, blockers, ambiguity), **stop**. Do not launch further subagents. Report the failure and the step that failed.
