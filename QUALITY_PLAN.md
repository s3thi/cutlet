# Quality Plan: Formatting, Static Analysis, and Required Checks

## Scope
Add the following quality-of-life and code-quality features to Cutlet:
- Formatting and format-checking using clang-format.
- Static analysis using clang-tidy.
- A `make check` target that runs required checks (format-check + clang-tidy).

This plan intentionally avoids adding other tools (sanitizers, fuzzing, coverage, etc.).

## Goals
- Single-command, repeatable checks that give fast, actionable feedback to agents.
- Repo-owned configuration for formatting and linting so results are stable across machines.
- Minimal dependencies: clang-format and clang-tidy only.

## Constraints and project rules
- Always write tests first. Include a testing strategy in all plans. All code must be exhaustively tested.
- Comment code to provide guidance for future agents (if touching code).
- Cutlet has no external runtime dependencies; dev tooling is okay.

## Proposed work (tests first)
1) **Tests / verification strategy (first)**
   - Add a small smoke test workflow for the new tooling:
     - `make format-check` should fail if a file is intentionally misformatted.
     - `make lint` (clang-tidy) should run over a small, deterministic file set.
   - The "tests first" requirement is satisfied by defining these checks before wiring them into `make check`.
   - Use the existing test structure; do not add runtime dependencies.

2) **Repository formatting configuration**
   - Add a `.clang-format` file at repo root.
   - Choose a base style (LLVM or Google) and document the rationale in the file header comment.
   - Ensure the style matches C23 and existing code conventions where possible.

3) **clang-tidy configuration**
   - Add a `.clang-tidy` at repo root.
   - Start with a conservative rule set that flags likely bugs and bad patterns but avoids massive churn.
   - Explicitly disable checks that are too noisy for C or conflict with current style.

4) **Makefile targets**
   - Add targets:
     - `format`: runs clang-format on all tracked `.c` and `.h` files.
     - `format-check`: checks formatting (clang-format in dry-run mode).
     - `lint`: runs clang-tidy across the same file set.
     - `check`: runs required checks in order (format-check then lint).
   - Use `git ls-files` or a stable file list to avoid scanning build artifacts.
   - Ensure `make check` exits non-zero on failures.

5) **Developer documentation**
   - Update or add a short section in `README` or `AGENTS.md` describing:
     - Required tools (clang-format, clang-tidy).
     - How to run `make format`, `make lint`, and `make check`.
   - Keep the instructions short and explicit for agent handoff.

## Testing strategy
- **Unit of verification:** the new Makefile targets are the tests.
- **Test steps:**
  1) Run `make format-check` on a clean tree: expect pass.
  2) Introduce a controlled formatting change in a sample file, run `make format-check`: expect fail.
  3) Run `make lint` and confirm it completes with the configured checks.
  4) Run `make check` and confirm it fails/succeeds depending on the above steps.
- **Exhaustiveness:** ensure these checks cover all tracked `.c`/`.h` files in `src/` and `tests/`.

## Deliverables (files/changes)
- `.clang-format`
- `.clang-tidy`
- `Makefile` updates for `format`, `format-check`, `lint`, `check`
- Documentation updates (location to be chosen based on existing docs)

## Notes for next agent
- Keep clang-tidy checks minimal at first to avoid high noise.
- If the project uses a non-standard include layout, clang-tidy may need compilation flags; consider generating or approximating compile flags in the Makefile.
- Prefer clarity and deterministic behavior over “smart” automation.
