# Quality: Static Analysis

**Status:** Done

Added `.clang-tidy` at repo root and Makefile target:
- `make lint` — runs clang-tidy across all tracked `.c` and `.h` files in `src/` and `tests/`.

Conservative rule set that flags likely bugs and bad patterns without massive churn. Part of `make check`.
