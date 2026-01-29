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

# Clean build artifacts
.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)

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
	@echo "  clean         - Remove build artifacts"
	@echo "  help          - Show this help message"
