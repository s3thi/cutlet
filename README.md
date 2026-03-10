# Cutlet

> [!WARNING]
> **This project is entirely LLM-generated.** I haven't read any of
> the code. Use it at your own risk. Read more about it
> [here](https://ankursethi.com/blog/programming-language-claude-code/).

Cutlet is a dynamic programming language written in C. Its long-term goal is to
replace Bash for anything beyond trivial one-liners, combining the
expressiveness of Python, Ruby, Lua, and JavaScript with first-class support for
running subprocesses, building pipelines, and scripting your system. It focuses
on REPL-driven development.

```cutlet
# Vectorized math — operations apply to entire arrays at once
my cities  = ["Tokyo", "Paris", "New York", "London", "Sydney"]
my temps-c = [28, 22, 31, 18, 15]

# Convert Celsius to Fahrenheit — no loops, just math on arrays
my temps-f = temps-c @* 1.8 @+ 32
say(cities @: temps-f)              # @: zips two arrays into a map

# Boolean mask indexing — filter without writing a loop
say("Pack light for: " ++ str(cities[temps-f @> 75]))

# @+ folds an array down to a single value
say("Average: " ++ str((@+ temps-c) / len(temps-c)) ++ "°C")

# Your own functions work with @ too
fn max(a, b) is
  if a > b then a else b end
end

say("Hottest: " ++ str(@max temps-c) ++ "°C")      # fold to find the max
say("Floors: " ++ str(temps-c @max [20, 20, 20, 20, 20]))  # element-wise max
```

```
{Tokyo: 82.4, Paris: 71.6, New York: 87.8, London: 64.4, Sydney: 59}
Pack light for: [Tokyo, New York]
Average: 22.8°C
Hottest: 31°C
Floors: [28, 22, 31, 20, 20]
```

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
