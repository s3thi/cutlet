Read `AGENTS.md` for project conventions — follow them exactly.

If a plan file name was provided below, read that file from `plans/doing/`. Otherwise, pick a task from `plans/doing/`.

$ARGUMENTS

If the task file is incomplete or ambiguous, stop and ask me before writing any code.

Otherwise:

1. State your implementation approach in 2-3 sentences
2. Flag anything risky — if blockers exist, stop and ask me
3. Implement the task, following the required process in `AGENTS.md` (tests first, confirm failures, then implement, `make test && make check` after every change)
4. When all acceptance criteria are met and `make test && make check` pass, commit
5. Move the task file from `plans/doing/` to `plans/done/` with the next consecutive 4-digit number prefix
6. Add a short summary (what changed, files touched) to the file
