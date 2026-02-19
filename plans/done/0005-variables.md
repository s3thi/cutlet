# Variables

**Status:** Done

`my x = expr` declares, `x = expr` assigns. Linked-list environment with thread-safe get/define/assign. Global pthread rwlock serializes eval. Inside functions, parameters and `my` declarations use stack slots (`OP_GET_LOCAL`/`OP_SET_LOCAL`); globals unchanged outside functions.
