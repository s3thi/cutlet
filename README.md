# Cutlet

Cutlet is a dynamic programming language written in C, designed primarily for
writing better shell scripts. It aims to replace Bash for anything beyond
trivial one-liners, combining the expressiveness of Python, Ruby, Lua, and
JavaScript with first-class support for running subprocesses, building
pipelines, and scripting your system. It focuses on REPL-driven development.

## Dependencies

**Required:**

- A C compiler with C23 support (Clang 18+ or GCC 14+)
- POSIX `make`
- pthreads (ships with your OS on Linux/macOS)

**Optional (for development):**

- [clang-format](https://clang.llvm.org/docs/ClangFormat.html) — auto-format source files
- [clang-tidy](https://clang.llvm.org/extra/clang-tidy/) — static analysis
- [bear](https://github.com/rizsotto/Bear) — generates `compile_commands.json` for clang-tidy and IDE integration
- [Universal Ctags](https://ctags.io/) — symbol indexing for codebase analysis scripts
- [cscope](http://cscope.sourceforge.net/) — call graph analysis for codebase analysis scripts
- Python 3 — runs the analysis scripts in `scripts/`

On macOS with Homebrew:

```sh
brew install llvm bear universal-ctags cscope python3
```

## Building

```sh
make            # build the cutlet binary (output: build/cutlet)
make clean      # remove all build artifacts
```

## Running

```sh
./build/cutlet repl                 # start a local interactive REPL
./build/cutlet repl --listen        # start a TCP REPL server
./build/cutlet repl --connect       # connect to a running TCP REPL server
./build/cutlet run script.cutlet    # execute a source file
```

## Testing

Run the full test suite:

```sh
make test
```

Run individual test suites:

Run all tests under AddressSanitizer, UndefinedBehaviorSanitizer, and
LeakSanitizer:

```sh
make test-sanitize
```

## Debug Flags

The REPL and `run` subcommand support debug flags that show internal pipeline
stages. All flags can be combined:

```sh
./build/cutlet repl --tokens               # show token stream
./build/cutlet repl --ast                  # show AST
./build/cutlet repl --bytecode             # show bytecode disassembly
./build/cutlet run script.cutlet --ast     # debug flags work with file execution too
```

## Codebase Analysis

Analysis scripts in `scripts/` use ctags, cscope, and the cutlet interpreter to
produce markdown reports about the codebase. Useful for understanding the code
before making changes.

```sh
make understand       # run all three analysis tools
make symbol-index     # list all public functions and types with signatures
make call-graph       # show callers and callees for every public function
make pipeline-trace   # trace example programs through tokenizer → parser → compiler → VM
```

Output goes to stdout. Pipe to a file for reference:

```sh
make pipeline-trace > /tmp/traces.md
```

## Code Quality

```sh
make format         # auto-format all C source and header files
make format-check   # check formatting (exits non-zero on diff)
make lint           # run clang-tidy (requires bear; builds compile_commands.json automatically)
make check          # run format-check + lint
```
