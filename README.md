# Cutlet

Cutlet is a dynamic programming language written in C. It aims to be a small
"glue language" in the spirit of Python, Ruby, Lua, and JavaScript, with a
focus on REPL-driven development.

## Dependencies

**Required:**

- A C compiler with C23 support (Clang 18+ or GCC 14+)
- POSIX `make`
- pthreads (ships with your OS on Linux/macOS)

**Optional (for development):**

- [clang-format](https://clang.llvm.org/docs/ClangFormat.html) — auto-format source files
- [clang-tidy](https://clang.llvm.org/extra/clang-tidy/) — static analysis
- [bear](https://github.com/rizsotto/Bear) — generates `compile_commands.json` for clang-tidy and IDE integration

On macOS with Homebrew:

```sh
brew install llvm bear
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

## Code Quality

```sh
make format         # auto-format all C source and header files
make format-check   # check formatting (exits non-zero on diff)
make lint           # run clang-tidy (requires bear; builds compile_commands.json automatically)
make check          # run format-check + lint
```
