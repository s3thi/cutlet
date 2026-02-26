Read `AGENTS.md` for project conventions — follow them exactly, **except** where this file explicitly overrides them.

If a plan file name was provided below, read that file from `plans/doing/`. Otherwise, pick a task from `plans/doing/`.

$ARGUMENTS

## Autonomy override

This command runs **unattended** — no human is watching. The following `AGENTS.md` rules are overridden:

1. **Never stop to ask the user.** If a plan is ambiguous, make your best judgment call and note it in the summary. If tests fail after implementation, debug and fix them yourself. If you encounter blockers, try to work around them.
2. **No confirmation before proceeding after test failures.** Write tests, confirm they fail, then immediately proceed to implementation — do not pause.
3. **Removing/changing tests and disabling linter errors**: Use your best judgment. If a test or lint rule is genuinely wrong or obsolete given the new code, fix it. Note any such changes in the summary.
4. **Modifying `.expected` files**: If language behavior intentionally changed, update the `.expected` files to match. Note which ones changed in the summary.
5. **Language feature checklist — do it yourself.** When a step adds or changes a language feature:
   - Update `TUTORIAL.md` to cover the new feature.
   - Add or update an example in `examples/` (one `.cutlet` file, small and readable, uses `say()` to show output).
   - Generate the `.expected` file: `./build/cutlet run examples/<name>.cutlet > examples/<name>.expected`.
   Do this in the same step, before committing.

In short: **act, don't ask.** Make the best decision you can and document what you did.

## Unattended execution

This command implements **all remaining steps** of a plan without stopping between steps. Each step runs in a subagent (via the Task tool) to prevent context exhaustion.

**Before starting**, read the plan file and determine which steps remain (check the `## Progress` section). Then execute each remaining step sequentially by launching a subagent for each one.

## Subagent dispatch (for each step)

For each remaining step, launch a Task (subagent_type: "general-purpose") with a prompt that includes:

1. The full text of `AGENTS.md` (so the subagent knows project conventions).
2. The full text of the plan file.
3. Which step number to implement.
4. The **full "Autonomy override" section above** (so the subagent knows it must never stop to ask and must handle tutorial/example updates itself).
5. These instructions:
   - State the implementation approach in 2-3 sentences.
   - Implement the step, following the required process in `AGENTS.md` (tests first, confirm failures, then implement, `make test && make check` after every change) — but never pause for user confirmation (see autonomy override).
   - When a step adds or changes a language feature, update `TUTORIAL.md` and add/update the relevant example and `.expected` file before committing.
   - When the step's criteria are met and `make test && make check` pass, commit.
   - Update the plan's `## Progress` section.
   - If **all steps are now complete**, also add a short summary and run `scripts/plan-done <name>`.
   - Do NOT proceed to any other step.
   - **Return a structured summary** of what was done, including: files changed, tests added/modified, any decisions made autonomously (ambiguity resolved, tests changed, linter rules adjusted, `.expected` files updated), and any concerns or risks.

**Important**: Wait for each subagent to complete before launching the next one. Steps are sequential — each builds on the previous step's commits.

## After all steps complete

After the final subagent finishes successfully, push the branch to the host repo:

```
git push origin <branch>
```

Then print a **final summary** covering all steps. The summary must include:

- **What was implemented**: one-line summary per step.
- **Decisions made autonomously**: anything where the agent used its judgment instead of asking — ambiguous plan requirements, test changes, linter adjustments, `.expected` file modifications, tutorial/example choices, etc.
- **Risks or concerns**: anything the user should review carefully, potential issues, or places where the agent was unsure.

This summary is the user's primary review artifact, so be thorough and honest.

## Error handling

If a subagent reports failure (tests not passing after repeated attempts, fundamental blockers that can't be worked around), **stop**. Do not launch further subagents. Report the failure, what was attempted, and the step that failed.
