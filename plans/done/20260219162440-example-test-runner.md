# Example Test Runner

Add `.expected` files alongside `examples/*.cutlet` and a test harness that runs `cutlet run` on each example and compares stdout. Integrate into `make test`.

## Summary

Added an example output test runner that automatically validates all `examples/*.cutlet` programs produce the expected stdout.

### What changed

- **`tests/test_examples.sh`** (new): Shell script that iterates over `examples/*.cutlet`, runs each with `cutlet run`, and diffs stdout against the corresponding `.expected` file. Reports pass/fail with unified diff on mismatch.
- **`examples/*.expected`** (14 new files): Expected stdout for each example program.
- **`Makefile`**: Added `test-examples` target, included it in `make test` and `make test-sanitize`, added help text.
