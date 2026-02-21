# Vendor Isolation

**Status:** Done

Isocline compiled as separate `.o` with upstream-recommended C99 flags. `vendor/.clang-tidy` and `vendor/.clang-format` sentinel files prevent IDE linting/formatting of vendor code.
