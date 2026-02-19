$ARGUMENTS

Read `AGENTS.md` for project conventions. Ask me clarifying questions before writing anything.

Then create a new task file in `plans/doing/` with:

- A clear objective (what "done" looks like)
- Acceptance criteria as a checklist (must include `make test && make check` passing)
- Dependencies on other tasks in `plans/doing/`, if any
- Any constraints or non-goals

Write it so another agent can execute without asking me anything.

Break large features into multiple task files, each with at most 10 steps. A task should be completable in a single session without running out of context. If a feature needs more than 10 steps, ask me how to split it before creating the files.
