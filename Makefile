# Cutlet Makefile
#
# Requires: C23 compiler, POSIX make
# Tested on: Linux, macOS, Windows (with appropriate toolchain)

CC ?= cc
CFLAGS = -std=c23 -D_GNU_SOURCE -Wall -Wextra -Wpedantic -g
LDFLAGS =

# Directories
SRC_DIR = src
TEST_DIR = tests
BUILD_DIR = build
VENDOR_DIR = vendor

# Vendor source files (isocline for multiline REPL input)
# Isocline expects: include/ for public header, src/ for implementation
ISOCLINE_DIR = $(VENDOR_DIR)/isocline
ISOCLINE_SRC = $(ISOCLINE_DIR)/src/isocline.c
ISOCLINE_INCLUDES = -I$(ISOCLINE_DIR)/include -I$(ISOCLINE_DIR)/src

# Upstream-recommended build flags for isocline (C99, not C23).
# Source: vendor/isocline/CMakeLists.txt lines 103-106 (AppleClang/Clang flags).
# Some flags are Clang-only (-Wimplicit-int-conversion, -Wno-shorten-64-to-32,
# -Wno-padded). Detect compiler and include them only for Clang.
ISOCLINE_BUILD_CFLAGS = -std=c99 -g \
	-Wall -Wextra -Wpedantic \
	-Wno-unknown-pragmas -Wno-unused-function \
	-Wno-missing-field-initializers \
	-Wsign-conversion
IS_CLANG := $(shell $(CC) --version 2>/dev/null | grep -qi clang && echo 1)
ifeq ($(IS_CLANG),1)
ISOCLINE_BUILD_CFLAGS += -Wno-padded -Wimplicit-int-conversion -Wno-shorten-64-to-32
endif

# Sanitizer-instrumented build flags for isocline.
ISOCLINE_SANITIZE_BUILD_CFLAGS = $(ISOCLINE_BUILD_CFLAGS) \
	-O1 -fno-omit-frame-pointer \
	-fsanitize=address,undefined -fno-sanitize-recover=all

# Library source files (everything except main.c)
LIB_SRCS = $(SRC_DIR)/tokenizer.c $(SRC_DIR)/repl.c $(SRC_DIR)/repl_server.c $(SRC_DIR)/parser.c $(SRC_DIR)/runtime.c $(SRC_DIR)/json.c $(SRC_DIR)/ptr_array.c $(SRC_DIR)/value.c $(SRC_DIR)/chunk.c $(SRC_DIR)/compiler.c $(SRC_DIR)/vm.c $(SRC_DIR)/gc.c
LIB_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(LIB_SRCS))

# Main binary
MAIN_SRC = $(SRC_DIR)/main.c
BIN = $(BUILD_DIR)/cutlet

# Test files
TEST_TOKENIZER_SRC = $(TEST_DIR)/test_tokenizer.c
TEST_TOKENIZER_BIN = $(BUILD_DIR)/test_tokenizer

TEST_REPL_SRC = $(TEST_DIR)/test_repl.c
TEST_REPL_BIN = $(BUILD_DIR)/test_repl

TEST_PARSER_SRC = $(TEST_DIR)/test_parser.c
TEST_PARSER_BIN = $(BUILD_DIR)/test_parser

TEST_REPL_SERVER_SRC = $(TEST_DIR)/test_repl_server.c
TEST_REPL_SERVER_BIN = $(BUILD_DIR)/test_repl_server

TEST_RUNTIME_SRC = $(TEST_DIR)/test_runtime.c
TEST_RUNTIME_BIN = $(BUILD_DIR)/test_runtime

TEST_PTR_ARRAY_SRC = $(TEST_DIR)/test_ptr_array.c
TEST_PTR_ARRAY_BIN = $(BUILD_DIR)/test_ptr_array

TEST_JSON_SRC = $(TEST_DIR)/test_json.c
TEST_JSON_BIN = $(BUILD_DIR)/test_json

TEST_CHUNK_SRC = $(TEST_DIR)/test_chunk.c
TEST_CHUNK_BIN = $(BUILD_DIR)/test_chunk

TEST_COMPILER_SRC = $(TEST_DIR)/test_compiler.c
TEST_COMPILER_BIN = $(BUILD_DIR)/test_compiler

TEST_VM_SRC = $(TEST_DIR)/test_vm.c
TEST_VM_BIN = $(BUILD_DIR)/test_vm

TEST_VALUE_SRC = $(TEST_DIR)/test_value.c
TEST_VALUE_BIN = $(BUILD_DIR)/test_value

TEST_GC_SRC = $(TEST_DIR)/test_gc.c
TEST_GC_BIN = $(BUILD_DIR)/test_gc

# Default target: build the cutlet binary
.PHONY: all
all: $(BIN)

# Create build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Compile source files to object files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# Compile isocline separately with upstream-recommended C99 flags.
ISOCLINE_OBJ = $(BUILD_DIR)/isocline.o
$(ISOCLINE_OBJ): $(ISOCLINE_SRC) | $(BUILD_DIR)
	$(CC) $(ISOCLINE_BUILD_CFLAGS) $(ISOCLINE_INCLUDES) -c -o $@ $<

# Build the main cutlet binary (link pre-compiled isocline object).
$(BIN): $(MAIN_SRC) $(LIB_SRCS) $(ISOCLINE_OBJ) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(ISOCLINE_INCLUDES) -o $@ $(MAIN_SRC) $(LIB_SRCS) $(ISOCLINE_OBJ) $(LDFLAGS) -pthread -lm

# Build and run all tests
.PHONY: test
test: test-tokenizer test-repl test-parser test-repl-server test-runtime test-ptr-array test-json test-chunk test-compiler test-vm test-value test-gc test-cli test-examples

# Run tokenizer tests
.PHONY: test-tokenizer
test-tokenizer: $(TEST_TOKENIZER_BIN)
	./$(TEST_TOKENIZER_BIN)

# Run REPL tests
.PHONY: test-repl
test-repl: $(TEST_REPL_BIN)
	./$(TEST_REPL_BIN)

# Run parser tests
.PHONY: test-parser
test-parser: $(TEST_PARSER_BIN)
	./$(TEST_PARSER_BIN)

# Run REPL server tests
.PHONY: test-repl-server
test-repl-server: $(TEST_REPL_SERVER_BIN)
	./$(TEST_REPL_SERVER_BIN)

# Run runtime tests
.PHONY: test-runtime
test-runtime: $(TEST_RUNTIME_BIN)
	./$(TEST_RUNTIME_BIN)

# Run ptr_array tests
.PHONY: test-ptr-array
test-ptr-array: $(TEST_PTR_ARRAY_BIN)
	./$(TEST_PTR_ARRAY_BIN)

# Run JSON protocol tests
.PHONY: test-json
test-json: $(TEST_JSON_BIN)
	./$(TEST_JSON_BIN)

# Run chunk tests
.PHONY: test-chunk
test-chunk: $(TEST_CHUNK_BIN)
	./$(TEST_CHUNK_BIN)

# Run compiler tests
.PHONY: test-compiler
test-compiler: $(TEST_COMPILER_BIN)
	./$(TEST_COMPILER_BIN)

# Run VM tests
.PHONY: test-vm
test-vm: $(TEST_VM_BIN)
	./$(TEST_VM_BIN)

# Run value (array) tests
.PHONY: test-value
test-value: $(TEST_VALUE_BIN)
	./$(TEST_VALUE_BIN)

# Run GC infrastructure tests
.PHONY: test-gc
test-gc: $(TEST_GC_BIN)
	./$(TEST_GC_BIN)

# Run CLI integration tests
.PHONY: test-cli
test-cli: $(BIN)
	./$(TEST_DIR)/test_cli.sh

# Run example output tests (compares examples/*.cutlet stdout against .expected files)
.PHONY: test-examples
test-examples: $(BIN)
	./$(TEST_DIR)/test_examples.sh

# Build tokenizer test binary
$(TEST_TOKENIZER_BIN): $(TEST_TOKENIZER_SRC) $(SRC_DIR)/tokenizer.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(TEST_TOKENIZER_SRC) $(SRC_DIR)/tokenizer.c $(LDFLAGS)

# Build parser test binary
$(TEST_PARSER_BIN): $(TEST_PARSER_SRC) $(SRC_DIR)/parser.c $(SRC_DIR)/tokenizer.c $(SRC_DIR)/ptr_array.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(TEST_PARSER_SRC) $(SRC_DIR)/parser.c $(SRC_DIR)/tokenizer.c $(SRC_DIR)/ptr_array.c $(LDFLAGS)

# Build REPL test binary
$(TEST_REPL_BIN): $(TEST_REPL_SRC) $(LIB_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(TEST_REPL_SRC) $(LIB_SRCS) $(LDFLAGS) -pthread -lm

# Build REPL server test binary
$(TEST_REPL_SERVER_BIN): $(TEST_REPL_SERVER_SRC) $(LIB_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(TEST_REPL_SERVER_SRC) $(LIB_SRCS) $(LDFLAGS) -pthread -lm

# Build runtime test binary (with CUTLET_TESTING for test hooks)
$(TEST_RUNTIME_BIN): $(TEST_RUNTIME_SRC) $(LIB_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -DCUTLET_TESTING -o $@ $(TEST_RUNTIME_SRC) $(LIB_SRCS) $(LDFLAGS) -pthread -lm

# Build ptr_array test binary
$(TEST_PTR_ARRAY_BIN): $(TEST_PTR_ARRAY_SRC) $(SRC_DIR)/ptr_array.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(TEST_PTR_ARRAY_SRC) $(SRC_DIR)/ptr_array.c $(LDFLAGS)

# Build JSON protocol test binary
$(TEST_JSON_BIN): $(TEST_JSON_SRC) $(SRC_DIR)/json.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(TEST_JSON_SRC) $(SRC_DIR)/json.c $(LDFLAGS)

# Build chunk test binary
$(TEST_CHUNK_BIN): $(TEST_CHUNK_SRC) $(SRC_DIR)/chunk.c $(SRC_DIR)/value.c $(SRC_DIR)/gc.c $(SRC_DIR)/runtime.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(TEST_CHUNK_SRC) $(SRC_DIR)/chunk.c $(SRC_DIR)/value.c $(SRC_DIR)/gc.c $(SRC_DIR)/runtime.c $(LDFLAGS) -pthread -lm

# Build compiler test binary
$(TEST_COMPILER_BIN): $(TEST_COMPILER_SRC) $(SRC_DIR)/compiler.c $(SRC_DIR)/chunk.c $(SRC_DIR)/value.c $(SRC_DIR)/gc.c $(SRC_DIR)/parser.c $(SRC_DIR)/tokenizer.c $(SRC_DIR)/ptr_array.c $(SRC_DIR)/runtime.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(TEST_COMPILER_SRC) $(SRC_DIR)/compiler.c $(SRC_DIR)/chunk.c $(SRC_DIR)/value.c $(SRC_DIR)/gc.c $(SRC_DIR)/parser.c $(SRC_DIR)/tokenizer.c $(SRC_DIR)/ptr_array.c $(SRC_DIR)/runtime.c $(LDFLAGS) -pthread -lm

# Build VM test binary
$(TEST_VM_BIN): $(TEST_VM_SRC) $(SRC_DIR)/vm.c $(SRC_DIR)/compiler.c $(SRC_DIR)/chunk.c $(SRC_DIR)/value.c $(SRC_DIR)/gc.c $(SRC_DIR)/parser.c $(SRC_DIR)/tokenizer.c $(SRC_DIR)/runtime.c $(SRC_DIR)/ptr_array.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(TEST_VM_SRC) $(SRC_DIR)/vm.c $(SRC_DIR)/compiler.c $(SRC_DIR)/chunk.c $(SRC_DIR)/value.c $(SRC_DIR)/gc.c $(SRC_DIR)/parser.c $(SRC_DIR)/tokenizer.c $(SRC_DIR)/runtime.c $(SRC_DIR)/ptr_array.c $(LDFLAGS) -pthread -lm

# Build value (array) test binary
$(TEST_VALUE_BIN): $(TEST_VALUE_SRC) $(SRC_DIR)/value.c $(SRC_DIR)/chunk.c $(SRC_DIR)/gc.c $(SRC_DIR)/runtime.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(TEST_VALUE_SRC) $(SRC_DIR)/value.c $(SRC_DIR)/chunk.c $(SRC_DIR)/gc.c $(SRC_DIR)/runtime.c $(LDFLAGS) -pthread -lm

# Build GC infrastructure test binary
$(TEST_GC_BIN): $(TEST_GC_SRC) $(SRC_DIR)/gc.c $(SRC_DIR)/value.c $(SRC_DIR)/chunk.c $(SRC_DIR)/runtime.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(TEST_GC_SRC) $(SRC_DIR)/gc.c $(SRC_DIR)/value.c $(SRC_DIR)/chunk.c $(SRC_DIR)/runtime.c $(LDFLAGS) -pthread -lm

# ---------- Formatting (clang-format) ----------

# All tracked C source and header files, excluding vendor/ (third-party code).
FORMAT_FILES = $(shell git ls-files '*.c' '*.h' | grep -v '^vendor/')

# Apply formatting in-place.
.PHONY: format
format:
	clang-format -i $(FORMAT_FILES)

# Check formatting without modifying files. Exits non-zero on diff.
.PHONY: format-check
format-check:
	clang-format --dry-run --Werror $(FORMAT_FILES)

# ---------- Compile database ----------

# All source files that contribute to the compile database.
ALL_SRCS = $(MAIN_SRC) $(LIB_SRCS) $(TEST_TOKENIZER_SRC) $(TEST_REPL_SRC) $(TEST_PARSER_SRC) $(TEST_REPL_SERVER_SRC) $(TEST_RUNTIME_SRC) $(TEST_PTR_ARRAY_SRC) $(TEST_JSON_SRC) $(TEST_CHUNK_SRC) $(TEST_COMPILER_SRC) $(TEST_VM_SRC) $(TEST_VALUE_SRC) $(TEST_GC_SRC)

# Auto-generate compile_commands.json when missing or when any source file changes.
compile_commands.json: $(ALL_SRCS) $(shell git ls-files '*.h')
	@command -v bear >/dev/null 2>&1 || { echo "Error: 'bear' is not installed. Install it (e.g. brew install bear) and re-run."; exit 1; }
	bear -- $(MAKE) clean all test

# Convenience alias.
.PHONY: compile-db
compile-db: compile_commands.json

# ---------- Static analysis (clang-tidy) ----------

# clang-tidy binary — use Homebrew LLVM if available, else PATH.
CLANG_TIDY ?= $(shell command -v /opt/homebrew/opt/llvm/bin/clang-tidy 2>/dev/null || echo clang-tidy)

# Only lint .c translation units; headers are checked indirectly via includes.
# Excludes vendor/ directory (third-party code like isocline).
LINT_FILES = $(shell git ls-files '*.c' | grep -v '^vendor/')

# Lint depends on the compile database, which is rebuilt automatically if needed.
.PHONY: lint
lint: compile_commands.json
	$(CLANG_TIDY) $(LINT_FILES) -p . --extra-arg=-isysroot --extra-arg=$(shell xcrun --show-sdk-path)

# ---------- Sanitizer builds (ASan + UBSan, LSan via ASan) ----------

# Separate build directory so sanitizer flags don't mix with regular objects.
SANITIZE_BUILD_DIR = build-sanitize
SANITIZE_CFLAGS = -std=c23 -D_GNU_SOURCE -Wall -Wextra -Wpedantic -g -O1 \
	-fno-omit-frame-pointer \
	-fsanitize=address,undefined \
	-fno-sanitize-recover=all
SANITIZE_LDFLAGS = -fsanitize=address,undefined

SANITIZE_BIN = $(SANITIZE_BUILD_DIR)/cutlet
SANITIZE_TEST_TOKENIZER_BIN = $(SANITIZE_BUILD_DIR)/test_tokenizer
SANITIZE_TEST_REPL_BIN = $(SANITIZE_BUILD_DIR)/test_repl
SANITIZE_TEST_PARSER_BIN = $(SANITIZE_BUILD_DIR)/test_parser
SANITIZE_TEST_REPL_SERVER_BIN = $(SANITIZE_BUILD_DIR)/test_repl_server
SANITIZE_TEST_RUNTIME_BIN = $(SANITIZE_BUILD_DIR)/test_runtime
SANITIZE_TEST_PTR_ARRAY_BIN = $(SANITIZE_BUILD_DIR)/test_ptr_array
SANITIZE_TEST_JSON_BIN = $(SANITIZE_BUILD_DIR)/test_json
SANITIZE_TEST_CHUNK_BIN = $(SANITIZE_BUILD_DIR)/test_chunk
SANITIZE_TEST_COMPILER_BIN = $(SANITIZE_BUILD_DIR)/test_compiler
SANITIZE_TEST_VM_BIN = $(SANITIZE_BUILD_DIR)/test_vm
SANITIZE_TEST_VALUE_BIN = $(SANITIZE_BUILD_DIR)/test_value
SANITIZE_TEST_GC_BIN = $(SANITIZE_BUILD_DIR)/test_gc

$(SANITIZE_BUILD_DIR):
	mkdir -p $(SANITIZE_BUILD_DIR)

# Compile isocline separately under sanitizers with upstream-recommended C99 flags.
ISOCLINE_SANITIZE_OBJ = $(SANITIZE_BUILD_DIR)/isocline.o
$(ISOCLINE_SANITIZE_OBJ): $(ISOCLINE_SRC) | $(SANITIZE_BUILD_DIR)
	$(CC) $(ISOCLINE_SANITIZE_BUILD_CFLAGS) $(ISOCLINE_INCLUDES) -c -o $@ $<

# Build sanitizer-instrumented binaries (link pre-compiled isocline object).
$(SANITIZE_BIN): $(MAIN_SRC) $(LIB_SRCS) $(ISOCLINE_SANITIZE_OBJ) | $(SANITIZE_BUILD_DIR)
	$(CC) $(SANITIZE_CFLAGS) $(ISOCLINE_INCLUDES) -o $@ $(MAIN_SRC) $(LIB_SRCS) $(ISOCLINE_SANITIZE_OBJ) $(SANITIZE_LDFLAGS) -pthread -lm

$(SANITIZE_TEST_TOKENIZER_BIN): $(TEST_TOKENIZER_SRC) $(SRC_DIR)/tokenizer.c | $(SANITIZE_BUILD_DIR)
	$(CC) $(SANITIZE_CFLAGS) -o $@ $(TEST_TOKENIZER_SRC) $(SRC_DIR)/tokenizer.c $(SANITIZE_LDFLAGS)

$(SANITIZE_TEST_PARSER_BIN): $(TEST_PARSER_SRC) $(SRC_DIR)/parser.c $(SRC_DIR)/tokenizer.c $(SRC_DIR)/ptr_array.c | $(SANITIZE_BUILD_DIR)
	$(CC) $(SANITIZE_CFLAGS) -o $@ $(TEST_PARSER_SRC) $(SRC_DIR)/parser.c $(SRC_DIR)/tokenizer.c $(SRC_DIR)/ptr_array.c $(SANITIZE_LDFLAGS)

$(SANITIZE_TEST_REPL_BIN): $(TEST_REPL_SRC) $(LIB_SRCS) | $(SANITIZE_BUILD_DIR)
	$(CC) $(SANITIZE_CFLAGS) -o $@ $(TEST_REPL_SRC) $(LIB_SRCS) $(SANITIZE_LDFLAGS) -pthread -lm

$(SANITIZE_TEST_REPL_SERVER_BIN): $(TEST_REPL_SERVER_SRC) $(LIB_SRCS) | $(SANITIZE_BUILD_DIR)
	$(CC) $(SANITIZE_CFLAGS) -o $@ $(TEST_REPL_SERVER_SRC) $(LIB_SRCS) $(SANITIZE_LDFLAGS) -pthread -lm

$(SANITIZE_TEST_RUNTIME_BIN): $(TEST_RUNTIME_SRC) $(LIB_SRCS) | $(SANITIZE_BUILD_DIR)
	$(CC) $(SANITIZE_CFLAGS) -DCUTLET_TESTING -o $@ $(TEST_RUNTIME_SRC) $(LIB_SRCS) $(SANITIZE_LDFLAGS) -pthread -lm

$(SANITIZE_TEST_PTR_ARRAY_BIN): $(TEST_PTR_ARRAY_SRC) $(SRC_DIR)/ptr_array.c | $(SANITIZE_BUILD_DIR)
	$(CC) $(SANITIZE_CFLAGS) -o $@ $(TEST_PTR_ARRAY_SRC) $(SRC_DIR)/ptr_array.c $(SANITIZE_LDFLAGS)

$(SANITIZE_TEST_JSON_BIN): $(TEST_JSON_SRC) $(SRC_DIR)/json.c | $(SANITIZE_BUILD_DIR)
	$(CC) $(SANITIZE_CFLAGS) -o $@ $(TEST_JSON_SRC) $(SRC_DIR)/json.c $(SANITIZE_LDFLAGS)

$(SANITIZE_TEST_CHUNK_BIN): $(TEST_CHUNK_SRC) $(SRC_DIR)/chunk.c $(SRC_DIR)/value.c $(SRC_DIR)/gc.c $(SRC_DIR)/runtime.c | $(SANITIZE_BUILD_DIR)
	$(CC) $(SANITIZE_CFLAGS) -o $@ $(TEST_CHUNK_SRC) $(SRC_DIR)/chunk.c $(SRC_DIR)/value.c $(SRC_DIR)/gc.c $(SRC_DIR)/runtime.c $(SANITIZE_LDFLAGS) -pthread -lm

$(SANITIZE_TEST_COMPILER_BIN): $(TEST_COMPILER_SRC) $(SRC_DIR)/compiler.c $(SRC_DIR)/chunk.c $(SRC_DIR)/value.c $(SRC_DIR)/gc.c $(SRC_DIR)/parser.c $(SRC_DIR)/tokenizer.c $(SRC_DIR)/ptr_array.c $(SRC_DIR)/runtime.c | $(SANITIZE_BUILD_DIR)
	$(CC) $(SANITIZE_CFLAGS) -o $@ $(TEST_COMPILER_SRC) $(SRC_DIR)/compiler.c $(SRC_DIR)/chunk.c $(SRC_DIR)/value.c $(SRC_DIR)/gc.c $(SRC_DIR)/parser.c $(SRC_DIR)/tokenizer.c $(SRC_DIR)/ptr_array.c $(SRC_DIR)/runtime.c $(SANITIZE_LDFLAGS) -pthread -lm

$(SANITIZE_TEST_VM_BIN): $(TEST_VM_SRC) $(SRC_DIR)/vm.c $(SRC_DIR)/compiler.c $(SRC_DIR)/chunk.c $(SRC_DIR)/value.c $(SRC_DIR)/gc.c $(SRC_DIR)/parser.c $(SRC_DIR)/tokenizer.c $(SRC_DIR)/runtime.c $(SRC_DIR)/ptr_array.c | $(SANITIZE_BUILD_DIR)
	$(CC) $(SANITIZE_CFLAGS) -o $@ $(TEST_VM_SRC) $(SRC_DIR)/vm.c $(SRC_DIR)/compiler.c $(SRC_DIR)/chunk.c $(SRC_DIR)/value.c $(SRC_DIR)/gc.c $(SRC_DIR)/parser.c $(SRC_DIR)/tokenizer.c $(SRC_DIR)/runtime.c $(SRC_DIR)/ptr_array.c $(SANITIZE_LDFLAGS) -pthread -lm

$(SANITIZE_TEST_VALUE_BIN): $(TEST_VALUE_SRC) $(SRC_DIR)/value.c $(SRC_DIR)/chunk.c $(SRC_DIR)/gc.c $(SRC_DIR)/runtime.c | $(SANITIZE_BUILD_DIR)
	$(CC) $(SANITIZE_CFLAGS) -o $@ $(TEST_VALUE_SRC) $(SRC_DIR)/value.c $(SRC_DIR)/chunk.c $(SRC_DIR)/gc.c $(SRC_DIR)/runtime.c $(SANITIZE_LDFLAGS) -pthread -lm

$(SANITIZE_TEST_GC_BIN): $(TEST_GC_SRC) $(SRC_DIR)/gc.c $(SRC_DIR)/value.c $(SRC_DIR)/chunk.c $(SRC_DIR)/runtime.c | $(SANITIZE_BUILD_DIR)
	$(CC) $(SANITIZE_CFLAGS) -o $@ $(TEST_GC_SRC) $(SRC_DIR)/gc.c $(SRC_DIR)/value.c $(SRC_DIR)/chunk.c $(SRC_DIR)/runtime.c $(SANITIZE_LDFLAGS) -pthread -lm

# Run the full test suite under sanitizers.
.PHONY: test-sanitize
test-sanitize: $(SANITIZE_TEST_TOKENIZER_BIN) $(SANITIZE_TEST_REPL_BIN) $(SANITIZE_TEST_PARSER_BIN) $(SANITIZE_TEST_REPL_SERVER_BIN) $(SANITIZE_TEST_RUNTIME_BIN) $(SANITIZE_TEST_PTR_ARRAY_BIN) $(SANITIZE_TEST_JSON_BIN) $(SANITIZE_TEST_CHUNK_BIN) $(SANITIZE_TEST_COMPILER_BIN) $(SANITIZE_TEST_VM_BIN) $(SANITIZE_TEST_VALUE_BIN) $(SANITIZE_TEST_GC_BIN) $(SANITIZE_BIN)
	./$(SANITIZE_TEST_TOKENIZER_BIN)
	./$(SANITIZE_TEST_REPL_BIN)
	./$(SANITIZE_TEST_PARSER_BIN)
	./$(SANITIZE_TEST_REPL_SERVER_BIN)
	./$(SANITIZE_TEST_RUNTIME_BIN)
	./$(SANITIZE_TEST_PTR_ARRAY_BIN)
	./$(SANITIZE_TEST_JSON_BIN)
	./$(SANITIZE_TEST_CHUNK_BIN)
	./$(SANITIZE_TEST_COMPILER_BIN)
	./$(SANITIZE_TEST_VM_BIN)
	./$(SANITIZE_TEST_VALUE_BIN)
	./$(SANITIZE_TEST_GC_BIN)
	CUTLET=./$(SANITIZE_BIN) ./$(TEST_DIR)/test_cli.sh
	CUTLET=./$(SANITIZE_BIN) ./$(TEST_DIR)/test_examples.sh

# ---------- Combined checks ----------

# Required pre-commit checks.
.PHONY: check
check: format-check lint

# ---------- Codebase understanding tools ----------
# Output goes to stdout (not committed to git). Requires: python3, ctags (Universal Ctags), cscope.

.PHONY: understand
understand: symbol-index call-graph pipeline-trace

.PHONY: symbol-index
symbol-index:
	@python3 scripts/symbol_index.py

.PHONY: call-graph
call-graph:
	@python3 scripts/call_graph.py

.PHONY: pipeline-trace
pipeline-trace: $(BIN)
	@for f in examples/*.cutlet; do \
		python3 scripts/pipeline_trace.py "$$f"; \
		echo ""; \
	done

# Clean build artifacts
.PHONY: clean
clean:
	rm -rf $(BUILD_DIR) $(SANITIZE_BUILD_DIR) compile_commands.json

# Help
.PHONY: help
help:
	@echo "Cutlet build system"
	@echo ""
	@echo "Targets:"
	@echo "  all           - Build the cutlet binary (default)"
	@echo "  test          - Build and run all tests"
	@echo "  test-tokenizer - Run tokenizer tests only"
	@echo "  test-repl     - Run REPL tests only"
	@echo "  test-repl-server - Run TCP REPL server tests only"
	@echo "  test-cli      - Run CLI integration tests only"
	@echo "  test-examples - Run example output tests (stdout vs .expected)"
	@echo "  format        - Auto-format all C source and header files"
	@echo "  format-check  - Check formatting (fails on diff)"
	@echo "  compile-db    - Generate compile_commands.json (requires bear)"
	@echo "  lint          - Run clang-tidy static analysis (requires compile-db)"
	@echo "  test-sanitize - Run tests under ASan+UBSan+LSan"
	@echo "  check         - Run all required checks (format-check + lint)"
	@echo "  symbol-index  - Generate symbol index from src/*.h (requires Universal Ctags)"
	@echo "  call-graph    - Generate call graph for public functions (requires cscope + ctags)"
	@echo "  pipeline-trace - Trace example programs through tokenizer/parser/compiler/VM"
	@echo "  understand    - Run all codebase understanding tools (symbol-index + call-graph + pipeline-trace)"
	@echo "  clean         - Remove build artifacts"
	@echo "  help          - Show this help message"
