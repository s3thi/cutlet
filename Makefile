# Cutlet Makefile
#
# Requires: C23 compiler, POSIX make
# Tested on: Linux, macOS, Windows (with appropriate toolchain)

CC ?= cc
CFLAGS = -std=c23 -Wall -Wextra -Wpedantic -g
LDFLAGS =

# Directories
SRC_DIR = src
TEST_DIR = tests
BUILD_DIR = build

# Library source files (everything except main.c)
LIB_SRCS = $(SRC_DIR)/tokenizer.c $(SRC_DIR)/repl.c $(SRC_DIR)/repl_server.c
LIB_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(LIB_SRCS))

# Main binary
MAIN_SRC = $(SRC_DIR)/main.c
BIN = $(BUILD_DIR)/cutlet

# Test files
TEST_TOKENIZER_SRC = $(TEST_DIR)/test_tokenizer.c
TEST_TOKENIZER_BIN = $(BUILD_DIR)/test_tokenizer

TEST_REPL_SRC = $(TEST_DIR)/test_repl.c
TEST_REPL_BIN = $(BUILD_DIR)/test_repl

TEST_REPL_SERVER_SRC = $(TEST_DIR)/test_repl_server.c
TEST_REPL_SERVER_BIN = $(BUILD_DIR)/test_repl_server

# Default target: build the cutlet binary
.PHONY: all
all: $(BIN)

# Create build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Compile source files to object files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# Build the main cutlet binary
$(BIN): $(MAIN_SRC) $(LIB_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(MAIN_SRC) $(LIB_SRCS) $(LDFLAGS) -pthread

# Build and run all tests
.PHONY: test
test: test-tokenizer test-repl test-repl-server test-cli

# Run tokenizer tests
.PHONY: test-tokenizer
test-tokenizer: $(TEST_TOKENIZER_BIN)
	./$(TEST_TOKENIZER_BIN)

# Run REPL tests
.PHONY: test-repl
test-repl: $(TEST_REPL_BIN)
	./$(TEST_REPL_BIN)

# Run REPL server tests
.PHONY: test-repl-server
test-repl-server: $(TEST_REPL_SERVER_BIN)
	./$(TEST_REPL_SERVER_BIN)

# Run CLI integration tests
.PHONY: test-cli
test-cli: $(BIN)
	./$(TEST_DIR)/test_cli.sh

# Build tokenizer test binary
$(TEST_TOKENIZER_BIN): $(TEST_TOKENIZER_SRC) $(SRC_DIR)/tokenizer.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(TEST_TOKENIZER_SRC) $(SRC_DIR)/tokenizer.c $(LDFLAGS)

# Build REPL test binary
$(TEST_REPL_BIN): $(TEST_REPL_SRC) $(LIB_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(TEST_REPL_SRC) $(LIB_SRCS) $(LDFLAGS) -pthread

# Build REPL server test binary
$(TEST_REPL_SERVER_BIN): $(TEST_REPL_SERVER_SRC) $(LIB_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(TEST_REPL_SERVER_SRC) $(LIB_SRCS) $(LDFLAGS) -pthread

# ---------- Formatting (clang-format) ----------

# All tracked C source and header files.
FORMAT_FILES = $(shell git ls-files '*.c' '*.h')

# Apply formatting in-place.
.PHONY: format
format:
	clang-format -i $(FORMAT_FILES)

# Check formatting without modifying files. Exits non-zero on diff.
.PHONY: format-check
format-check:
	clang-format --dry-run --Werror $(FORMAT_FILES)

# ---------- Compile database ----------

# Generate compile_commands.json using bear. Required for accurate linting.
.PHONY: compile-db
compile-db:
	@command -v bear >/dev/null 2>&1 || { echo "Error: 'bear' is not installed. Install it (e.g. brew install bear) and re-run."; exit 1; }
	bear -- $(MAKE) clean all test

# ---------- Static analysis (clang-tidy) ----------

# clang-tidy binary — use Homebrew LLVM if available, else PATH.
CLANG_TIDY ?= $(shell command -v /opt/homebrew/opt/llvm/bin/clang-tidy 2>/dev/null || echo clang-tidy)

# Only lint .c translation units; headers are checked indirectly via includes.
LINT_FILES = $(shell git ls-files '*.c')

# Lint requires a compile database. Run `make compile-db` first.
.PHONY: lint
lint:
	@test -f compile_commands.json || { echo "Error: compile_commands.json not found. Run 'make compile-db' first."; exit 1; }
	$(CLANG_TIDY) $(LINT_FILES) -p . --extra-arg=-isysroot --extra-arg=$(shell xcrun --show-sdk-path)

# ---------- Sanitizer builds (ASan + UBSan, LSan via ASan) ----------

# Separate build directory so sanitizer flags don't mix with regular objects.
SANITIZE_BUILD_DIR = build-sanitize
SANITIZE_CFLAGS = -std=c23 -Wall -Wextra -Wpedantic -g -O1 \
	-fno-omit-frame-pointer \
	-fsanitize=address,undefined \
	-fno-sanitize-recover=all
SANITIZE_LDFLAGS = -fsanitize=address,undefined

SANITIZE_BIN = $(SANITIZE_BUILD_DIR)/cutlet
SANITIZE_TEST_TOKENIZER_BIN = $(SANITIZE_BUILD_DIR)/test_tokenizer
SANITIZE_TEST_REPL_BIN = $(SANITIZE_BUILD_DIR)/test_repl
SANITIZE_TEST_REPL_SERVER_BIN = $(SANITIZE_BUILD_DIR)/test_repl_server

$(SANITIZE_BUILD_DIR):
	mkdir -p $(SANITIZE_BUILD_DIR)

# Build sanitizer-instrumented binaries.
$(SANITIZE_BIN): $(MAIN_SRC) $(LIB_SRCS) | $(SANITIZE_BUILD_DIR)
	$(CC) $(SANITIZE_CFLAGS) -o $@ $(MAIN_SRC) $(LIB_SRCS) $(SANITIZE_LDFLAGS) -pthread

$(SANITIZE_TEST_TOKENIZER_BIN): $(TEST_TOKENIZER_SRC) $(SRC_DIR)/tokenizer.c | $(SANITIZE_BUILD_DIR)
	$(CC) $(SANITIZE_CFLAGS) -o $@ $(TEST_TOKENIZER_SRC) $(SRC_DIR)/tokenizer.c $(SANITIZE_LDFLAGS)

$(SANITIZE_TEST_REPL_BIN): $(TEST_REPL_SRC) $(LIB_SRCS) | $(SANITIZE_BUILD_DIR)
	$(CC) $(SANITIZE_CFLAGS) -o $@ $(TEST_REPL_SRC) $(LIB_SRCS) $(SANITIZE_LDFLAGS) -pthread

$(SANITIZE_TEST_REPL_SERVER_BIN): $(TEST_REPL_SERVER_SRC) $(LIB_SRCS) | $(SANITIZE_BUILD_DIR)
	$(CC) $(SANITIZE_CFLAGS) -o $@ $(TEST_REPL_SERVER_SRC) $(LIB_SRCS) $(SANITIZE_LDFLAGS) -pthread

# Run the full test suite under sanitizers.
.PHONY: test-sanitize
test-sanitize: $(SANITIZE_TEST_TOKENIZER_BIN) $(SANITIZE_TEST_REPL_BIN) $(SANITIZE_TEST_REPL_SERVER_BIN) $(SANITIZE_BIN)
	./$(SANITIZE_TEST_TOKENIZER_BIN)
	./$(SANITIZE_TEST_REPL_BIN)
	./$(SANITIZE_TEST_REPL_SERVER_BIN)
	CUTLET=./$(SANITIZE_BIN) ./$(TEST_DIR)/test_cli.sh

# ---------- Combined checks ----------

# Required pre-commit checks.
.PHONY: check
check: format-check lint

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
	@echo "  format        - Auto-format all C source and header files"
	@echo "  format-check  - Check formatting (fails on diff)"
	@echo "  compile-db    - Generate compile_commands.json (requires bear)"
	@echo "  lint          - Run clang-tidy static analysis (requires compile-db)"
	@echo "  test-sanitize - Run tests under ASan+UBSan+LSan"
	@echo "  check         - Run all required checks (format-check + lint)"
	@echo "  clean         - Remove build artifacts"
	@echo "  help          - Show this help message"
