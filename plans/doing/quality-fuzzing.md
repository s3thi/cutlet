# Quality: Fuzz Testing

Add fuzz testing using Clang + libFuzzer with ASan+UBSan enabled.

## Design

- Add a `fuzz/` directory for harnesses and a seed corpus.
- Start with one or two targets (tokenizer and/or parser entrypoint).
- Run fuzzing in a time-boxed mode for CI and longer locally.

## Makefile targets

- `make fuzz-<target>` — builds and runs a fuzz target for a fixed time budget.
- Fuzz build outputs go in a separate directory (not `build/`).

## Notes

- Use Clang for fuzz targets (libFuzzer is built into Clang).
- Keep the workflow identical on macOS and Linux.
