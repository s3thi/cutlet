# AGENTS.md for Cutlet

Cutlet is a dynamic programming language built entirely using coding agents.

Cutlet is a dynamic programming language similar to Python, Ruby, Lua, and JavaScript. It excels at parsing text, navigating files and directories, inter-process communication, job control, and quickly building simple user interfaces for one-off tasks. It's designed to be a glue language that can bring together and orchestrate disparate programs. It's optimized for REPL-driven programming, similar to how it's done in Common Lisp or Clojure.

Cutlet is written in C and has no external dependencies except platform libraries. It only requires a working C23 compiler and a POSIX compliant `make` program. It's designed to run on Linux and macOS.

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

## REPL debug flags

The REPL supports two debug flags that can be combined:

- `--tokens` — shows tokenizer output (`TOKENS [TYPE value] ...`) before the evaluated result
- `--ast` — shows AST output (`AST [TYPE ...]`) before the evaluated result

Both server and client must use the same flags for debug output to appear. The server only produces debug output when started with the flag, and the client only prints it when started with the flag. If the server sends debug fields the client didn't request, the client warns once on stderr.

Examples:
```
cutlet repl --tokens --ast              # start server with both debug flags
cutlet repl --tokens --connect          # connect with token debug output
cutlet repl --tokens --ast --connect    # connect with both debug outputs
```
