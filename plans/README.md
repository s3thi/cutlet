# Cutlet Plans

- **`doing/`** — work queue. Pick a task, do the work, move it to `done/` when finished.
- **`done/`** — archive of completed work.
- Files are named with Zettelkasten timestamps: `YYYYMMDDHHmmss-<slug>.md` (e.g., `20260220183612-block-scoping.md`). Timestamps eliminate merge conflicts when multiple branches complete plans concurrently.
- See `AGENTS.md` for project conventions and required process.

## Tools

- **`scripts/plan-create <name>`** — Creates a new plan file in `plans/doing/` with a timestamped prefix and a title header. Prints the created path to stdout.
- **`scripts/plan-done <name>`** — Moves a plan from `plans/doing/` to `plans/done/` with a fresh timestamp reflecting completion time. Accepts a slug (`arrays`), `slug.md`, or full filename. Prints the new path to stdout.
