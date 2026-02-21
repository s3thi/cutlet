# Codebase Understanding Tools

**Status:** Done

Three Python analysis scripts in `scripts/`. `make understand` runs all three. Requires `python3`, Universal Ctags, and `cscope`.

- `scripts/symbol_index.py` (`make symbol-index`) — extracts all public symbols from `src/*.h` via ctags.
- `scripts/call_graph.py` (`make call-graph`) — caller/callee cross-reference via cscope.
- `scripts/pipeline_trace.py` (`make pipeline-trace`) — traces `.cutlet` files through every pipeline stage with source location cross-references.
