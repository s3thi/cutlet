# AGENTS.md for Cutlet

Cutlet is a dynamic programming language built entirely using coding agents.

Cutlet is a dynamic programming language similar to Python, Ruby, Lua, and JavaScript. It excels at parsing text, navigating files and directories, inter-process communication, job control, and quickly building simple user interfaces for one-off tasks. It's designed to be a glue language that can bring together and orchestrate disparate programs. It's optimized for REPL-driven programming, similar to how it's done in Common Lisp or Clojure.

Cutlet is written in C. It's designed to run on Linux and macOS.

## Dependency policy

- **Build requirements**: C23 compiler and POSIX `make`. These are the only hard requirements for building the `cutlet` binary.
- **Libraries (linked into the binary)**: Prefer few, high-impact dependencies. Vendor them in `vendor/` whenever possible (e.g., isocline for line editing). Never shy away from libraries guaranteed to be available everywhere (sqlite, curl, etc.).
- **Dev tooling**: Freely use standard developer tools. The project already uses `clang-format`, `clang-tidy`, and sanitizers. Analysis scripts use `ctags`, `cscope`, and Python 3. These are not required to build or run cutlet — only to develop it.
- **System libraries**: POSIX and platform libraries (pthreads, sockets, etc.) are always fine.

## Limitations of the author

The author of this project is an experienced developer of 20 years. But they don't have experience building and designing programming languages outside of college-level theoretical texts. They also don't have experience with C, primarily being a Rust developer. The author will often need guidance on how best to proceed with the implementation of this language. They will also need an explanation of what the C code is doing.

## Important instructions

- Always write tests first. Include a testing strategy in all plans. All code must be exhaustively tested.
- Before implementing any new code, run `make test` and `make check` to prove that your tests are failing. Pause after test failures and require user confirmation to proceed with implementation.
- Run `make test` and `make check` after every code change.
- Never remove, change, or disable any tests without user confirmation.
- Never disable any linter errors without user confirmation.
- Comment your code to include guidance and context for future coding agents.
- Update README if command line flags change. Also update README if build flags change, the dependencies change, or if we change the behavior of the REPL.
- We don't care about backward compatibility. Feel free to change anything across the codebase if it leads to better design, better UX, cleaner code.

## Language feature checklist

When a new language feature is added, remind the user to:

- Update `TUTORIAL.md` to cover the new feature.
- Add an example program in `examples/` exercising the new feature (one `.cutlet` file, small and readable, uses `say()` to show output).

The agent should NOT make these updates itself — just remind the user at the end of the implementation step.

## Codebase understanding tools

Three Python analysis scripts in `scripts/` help you orient yourself in the codebase. They use Universal Ctags and cscope for accurate C symbol indexing and call graph analysis. Run `make understand` to generate all three, or run them individually:

- **`make symbol-index`** — Uses ctags to list every public function and type from `src/*.h` with signatures and line numbers. Use this to find where something is defined without grepping.
- **`make call-graph`** — Uses cscope to show, for each public function, who calls it and what it calls. Use this to understand the impact of changing a function.
- **`make pipeline-trace`** — Runs every `examples/*.cutlet` program through the interpreter with `--tokens`, `--ast`, and `--bytecode` flags, then maps the output back to source locations in the tokenizer, parser, compiler, and VM. **Use this when adding a new language feature** — find the trace for a similar existing feature to see exactly which files and functions to touch.

Requires: `python3`, `ctags` (Universal Ctags), `cscope`. Install with `brew install universal-ctags cscope` or `apt install universal-ctags cscope`.

Output goes to stdout (not committed to git). Pipe to a file if you want to reference it:
```
make pipeline-trace > /tmp/traces.md
```

The `examples/` directory contains small `.cutlet` programs, one per language feature. These serve three purposes: documentation for users, input to the pipeline tracer, and a lightweight test suite (each exercises one feature in isolation).

## REPL debug flags

The REPL supports three debug flags that can be combined:

- `--tokens` — shows tokenizer output (`TOKENS [TYPE value] ...`) before the evaluated result
- `--ast` — shows AST output (`AST [TYPE ...]`) before the evaluated result
- `--bytecode` — shows bytecode disassembly (`BYTECODE\n== bytecode ==\n...`) before the evaluated result

In local REPL mode (the default), flags work directly:
```
cutlet repl --tokens --ast --bytecode   # local REPL with all debug flags
```

In TCP mode, both server and client must use the same flags for debug output to appear. The server only produces debug output when started with the flag, and the client only prints it when started with the flag. If the server sends debug fields the client didn't request, the client warns once on stderr.

Examples:
```
cutlet repl --listen --tokens --ast --bytecode  # start server with all debug flags
cutlet repl --tokens --connect                  # connect with token debug output
cutlet repl --tokens --ast --connect            # connect with token + AST debug output
```
