# Quality: Sanitizer Testing

**Status:** Done

Added `make test-sanitize` target:
- Builds with ASan + UBSan (LSan via ASan) and debug flags (`-g`, `-fno-omit-frame-pointer`, low optimization).
- Uses separate build directory (`build-sanitize`) to avoid stale objects.
- Runs the full `tests/` suite under sanitizers with no changes to the tests themselves.
