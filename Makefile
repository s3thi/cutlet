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

# Source files
SRCS = $(SRC_DIR)/tokenizer.c
OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))

# Test files
TEST_SRCS = $(TEST_DIR)/test_tokenizer.c
TEST_BIN = $(BUILD_DIR)/test_tokenizer

# Default target
.PHONY: all
all: $(BUILD_DIR) $(OBJS)

# Create build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Compile source files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# Build and run tests
.PHONY: test
test: $(TEST_BIN)
	./$(TEST_BIN)

# Build test binary
$(TEST_BIN): $(TEST_SRCS) $(SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(TEST_SRCS) $(SRCS) $(LDFLAGS)

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
	@echo "  all    - Build all source files (default)"
	@echo "  test   - Build and run tests"
	@echo "  clean  - Remove build artifacts"
	@echo "  help   - Show this help message"
