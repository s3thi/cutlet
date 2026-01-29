# Quality Plan: Formatting, Static Analysis, and Memory-Safety Checks

## Scope
Add the following quality-of-life and code-quality features to Cutlet:
- Formatting and format-checking using clang-format.
- Static analysis using clang-tidy.
- Cross-platform memory-safety testing using sanitizers (ASan, UBSan, LSan).
- Cross-platform fuzz testing using Clang + libFuzzer.
- A `make check` target that runs required checks (format-check + clang-tidy), plus optional extended targets for sanitizers and fuzzing.

No OS-specific diagnostics (e.g., macOS malloc diagnostics, valgrind/heaptrack, Instruments) will be added.

## Goals
- Single-command, repeatable checks that give fast, actionable feedback to agents.
- Repo-owned configuration for formatting and linting so results are stable across machines.
- Minimal dependencies: clang-format and clang-tidy only.
- Cross-platform memory-safety testing with a consistent workflow on macOS and Linux.

## Constraints and project rules
- Always write tests first. Include a testing strategy in all plans. All code must be exhaustively tested.
- Run `make test` after every code change.
- Comment code to provide guidance for future agents (if touching code).
- Cutlet has no external runtime dependencies; dev tooling is okay.
- Prefer cross-platform tooling only; no OS-specific memory diagnostics.

## Proposed work (tests first)
1) **Tests / verification strategy (first)**
   - Treat Makefile targets as the tests for tooling:
     - `make format-check` should fail if a file is intentionally misformatted.
     - `make lint` (clang-tidy) should run over a small, deterministic file set.
     - `make test-sanitize` should run the existing `tests/` suite under ASan+UBSan+LSan.
     - `make fuzz-<target>` should run a short, time-boxed fuzz pass.
   - The "tests first" requirement is satisfied by defining these checks before wiring them into `make check` and before adding new harnesses.
   - Use the existing test structure; do not add runtime dependencies.

2) **Repository formatting configuration**
   - Add a `.clang-format` file at repo root.
   - Choose a base style (LLVM or Google) and document the rationale in the file header comment.
   - Ensure the style matches C23 and existing code conventions where possible.

3) **clang-tidy configuration**
   - Add a `.clang-tidy` at repo root.
   - Start with a conservative rule set that flags likely bugs and bad patterns but avoids massive churn.
   - Explicitly disable checks that are too noisy for C or conflict with current style.

4) **Sanitizer test mode**
   - Add a sanitizer build configuration that compiles and links with:
     - ASan + UBSan (LSan via ASan).
     - Debug-friendly flags (`-g`, `-fno-omit-frame-pointer`, and a low optimization level).
   - Use a separate build directory (e.g., `build-sanitize`) because Make does not rebuild when `CFLAGS` changes.
   - Ensure the existing `tests/` suite is executed under sanitizers with no changes to the tests themselves.

5) **Fuzz testing (cross-platform)**
   - Add a `fuzz/` directory for harnesses and a seed corpus.
   - Use Clang + libFuzzer with ASan+UBSan enabled for fuzz targets.
   - Start with one or two targets (tokenizer and/or parser entrypoint).
   - Run fuzzing in a time-boxed mode for CI and longer locally.

6) **Makefile targets**
   - Add targets:
     - `format`: runs clang-format on all tracked `.c` and `.h` files.
     - `format-check`: checks formatting (clang-format in dry-run mode).
     - `lint`: runs clang-tidy across the same file set.
     - `check`: runs required checks in order (format-check then lint).
     - `test-sanitize`: builds with sanitizers and runs the full `tests/` suite.
     - `fuzz-<target>`: builds and runs a fuzz target for a fixed time budget.
   - Use `git ls-files` or a stable file list to avoid scanning build artifacts.
   - Ensure `make check` exits non-zero on failures.
   - Keep sanitizer and fuzz build outputs separate from default `build/`.

7) **Developer documentation**
   - Update or add a short section in `README` or `AGENTS.md` describing:
     - Required tools (clang-format, clang-tidy).
     - How to run `make format`, `make lint`, and `make check`.
     - How to run `make test-sanitize` and `make fuzz-<target>`.
   - Keep the instructions short and explicit for agent handoff.

## Testing strategy
- **Unit of verification:** the new Makefile targets are the tests.
- **Test steps:**
  1) Run `make format-check` on a clean tree: expect pass.
  2) Introduce a controlled formatting change in a sample file, run `make format-check`: expect fail.
  3) Run `make lint` and confirm it completes with the configured checks.
  4) Run `make test-sanitize` and confirm the full `tests/` suite runs under ASan+UBSan+LSan.
  5) Run `make fuzz-<target>` for a short, fixed time budget and confirm it completes without crashes.
  6) Run `make check` and confirm it fails/succeeds depending on the above steps.
- **Exhaustiveness:** ensure these checks cover all tracked `.c`/`.h` files in `src/` and `tests/`, and that fuzz targets exercise key parser/tokenizer entrypoints.

## Deliverables (files/changes)
- `.clang-format`
- `.clang-tidy`
- `Makefile` updates for `format`, `format-check`, `lint`, `check`
- `Makefile` updates for `test-sanitize` and `fuzz-<target>`
- `fuzz/` harnesses and seed corpus
- Documentation updates (location to be chosen based on existing docs)

## Notes for next agent
- Keep clang-tidy checks minimal at first to avoid high noise.
- If the project uses a non-standard include layout, clang-tidy may need compilation flags; consider generating or approximating compile flags in the Makefile.
- Prefer clarity and deterministic behavior over “smart” automation.
- Use Clang for fuzz targets (libFuzzer is built into Clang).
- Sanitizer builds should use a dedicated build dir to avoid stale objects.
- Do not add OS-specific diagnostics; keep the workflow identical on macOS and Linux.
