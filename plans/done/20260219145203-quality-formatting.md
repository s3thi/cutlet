# Quality: Formatting

**Status:** Done

Added `.clang-format` at repo root and Makefile targets:
- `make format` — runs clang-format on all tracked `.c` and `.h` files.
- `make format-check` — checks formatting (clang-format in dry-run mode). Exits non-zero on violations.

Part of `make check`.
