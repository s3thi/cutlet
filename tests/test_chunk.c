/*
 * test_chunk.c - Tests for the Cutlet bytecode chunk
 *
 * Tests chunk_init, chunk_write, chunk_add_constant, chunk_free,
 * and chunk_disassemble for correct behavior.
 */

#include "../src/chunk.h"
#include "../src/value.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * Simple test harness
 * ============================================================ */

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)

#define RUN_TEST(name)                                                                             \
    do {                                                                                           \
        tests_run++;                                                                               \
        printf("  %-55s ", #name);                                                                 \
        fflush(stdout);                                                                            \
        name();                                                                                    \
    } while (0)

#define ASSERT(cond, msg)                                                                          \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL\n    %s:%d: %s\n", __FILE__, __LINE__, msg);                              \
            tests_failed++;                                                                        \
            return;                                                                                \
        }                                                                                          \
    } while (0)

#define PASS()                                                                                     \
    do {                                                                                           \
        printf("PASS\n");                                                                          \
        tests_passed++;                                                                            \
    } while (0)

/* ============================================================
 * Tests
 * ============================================================ */

TEST(test_chunk_init) {
    Chunk c;
    chunk_init(&c);
    ASSERT(c.code == NULL, "code should be NULL");
    ASSERT(c.count == 0, "count should be 0");
    ASSERT(c.capacity == 0, "capacity should be 0");
    ASSERT(c.constants == NULL, "constants should be NULL");
    ASSERT(c.const_count == 0, "const_count should be 0");
    ASSERT(c.lines == NULL, "lines should be NULL");
    chunk_free(&c);
    PASS();
}

TEST(test_chunk_write_single) {
    Chunk c;
    chunk_init(&c);
    chunk_write(&c, OP_RETURN, 1);
    ASSERT(c.count == 1, "count should be 1");
    ASSERT(c.code[0] == OP_RETURN, "byte should be OP_RETURN");
    ASSERT(c.lines[0] == 1, "line should be 1");
    chunk_free(&c);
    PASS();
}

TEST(test_chunk_write_multiple) {
    Chunk c;
    chunk_init(&c);
    chunk_write(&c, OP_TRUE, 1);
    chunk_write(&c, OP_FALSE, 1);
    chunk_write(&c, OP_ADD, 2);
    chunk_write(&c, OP_RETURN, 2);
    ASSERT(c.count == 4, "count should be 4");
    ASSERT(c.code[0] == OP_TRUE, "first byte");
    ASSERT(c.code[1] == OP_FALSE, "second byte");
    ASSERT(c.code[2] == OP_ADD, "third byte");
    ASSERT(c.code[3] == OP_RETURN, "fourth byte");
    ASSERT(c.lines[0] == 1, "line 0");
    ASSERT(c.lines[2] == 2, "line 2");
    chunk_free(&c);
    PASS();
}

TEST(test_chunk_write_grows) {
    Chunk c;
    chunk_init(&c);
    /* Write more than initial capacity to trigger resize. */
    for (int i = 0; i < 100; i++) {
        chunk_write(&c, OP_POP, i);
    }
    ASSERT(c.count == 100, "count should be 100");
    ASSERT(c.capacity >= 100, "capacity should be >= 100");
    for (int i = 0; i < 100; i++) {
        ASSERT(c.code[i] == OP_POP, "all bytes should be OP_POP");
        ASSERT(c.lines[i] == i, "line numbers should match");
    }
    chunk_free(&c);
    PASS();
}

TEST(test_chunk_add_constant_number) {
    Chunk c;
    chunk_init(&c);
    int idx = chunk_add_constant(&c, make_number(42.0));
    ASSERT(idx == 0, "first constant should be index 0");
    ASSERT(c.const_count == 1, "const_count should be 1");
    ASSERT(c.constants[0].type == VAL_NUMBER, "type should be number");
    ASSERT(c.constants[0].number == 42.0, "value should be 42");
    chunk_free(&c);
    PASS();
}

TEST(test_chunk_add_constant_string) {
    Chunk c;
    chunk_init(&c);
    int idx = chunk_add_constant(&c, make_string(strdup("hello")));
    ASSERT(idx == 0, "first constant should be index 0");
    ASSERT(c.const_count == 1, "const_count should be 1");
    ASSERT(c.constants[0].type == VAL_STRING, "type should be string");
    ASSERT(strcmp(c.constants[0].string, "hello") == 0, "value should be hello");
    chunk_free(&c);
    PASS();
}

TEST(test_chunk_add_multiple_constants) {
    Chunk c;
    chunk_init(&c);
    int i0 = chunk_add_constant(&c, make_number(1.0));
    int i1 = chunk_add_constant(&c, make_number(2.0));
    int i2 = chunk_add_constant(&c, make_string(strdup("three")));
    ASSERT(i0 == 0, "first index");
    ASSERT(i1 == 1, "second index");
    ASSERT(i2 == 2, "third index");
    ASSERT(c.const_count == 3, "const_count");
    chunk_free(&c);
    PASS();
}

TEST(test_chunk_constant_overflow) {
    Chunk c;
    chunk_init(&c);
    /* Add 256 constants (max), then the 257th should fail. */
    for (int i = 0; i < 256; i++) {
        int idx = chunk_add_constant(&c, make_number((double)i));
        ASSERT(idx == i, "index should match");
    }
    ASSERT(c.const_count == 256, "should have 256 constants");
    int overflow_idx = chunk_add_constant(&c, make_number(999.0));
    ASSERT(overflow_idx == -1, "257th constant should return -1");
    ASSERT(c.const_count == 256, "count should still be 256");
    chunk_free(&c);
    PASS();
}

TEST(test_chunk_free_resets) {
    Chunk c;
    chunk_init(&c);
    chunk_write(&c, OP_RETURN, 1);
    chunk_add_constant(&c, make_number(1.0));
    chunk_free(&c);
    ASSERT(c.code == NULL, "code should be NULL after free");
    ASSERT(c.count == 0, "count should be 0 after free");
    ASSERT(c.constants == NULL, "constants should be NULL after free");
    ASSERT(c.const_count == 0, "const_count should be 0 after free");
    ASSERT(c.lines == NULL, "lines should be NULL after free");
    PASS();
}

TEST(test_chunk_constant_with_bytecode) {
    /* Simulate: OP_CONSTANT 0, OP_RETURN */
    Chunk c;
    chunk_init(&c);
    int idx = chunk_add_constant(&c, make_number(3.14));
    chunk_write(&c, OP_CONSTANT, 1);
    chunk_write(&c, (uint8_t)idx, 1);
    chunk_write(&c, OP_RETURN, 1);
    ASSERT(c.count == 3, "3 bytes of bytecode");
    ASSERT(c.code[0] == OP_CONSTANT, "first byte is OP_CONSTANT");
    ASSERT(c.code[1] == 0, "second byte is constant index 0");
    ASSERT(c.code[2] == OP_RETURN, "third byte is OP_RETURN");
    chunk_free(&c);
    PASS();
}

TEST(test_opcode_names) {
    ASSERT(strcmp(opcode_name(OP_CONSTANT), "OP_CONSTANT") == 0, "OP_CONSTANT name");
    ASSERT(strcmp(opcode_name(OP_ADD), "OP_ADD") == 0, "OP_ADD name");
    ASSERT(strcmp(opcode_name(OP_RETURN), "OP_RETURN") == 0, "OP_RETURN name");
    ASSERT(strcmp(opcode_name(OP_JUMP), "OP_JUMP") == 0, "OP_JUMP name");
    ASSERT(strcmp(opcode_name(OP_CALL), "OP_CALL") == 0, "OP_CALL name");
    PASS();
}

TEST(test_chunk_disassemble_runs) {
    /* Just verify disassemble doesn't crash. Output goes to stdout. */
    Chunk c;
    chunk_init(&c);
    int idx = chunk_add_constant(&c, make_number(42.0));
    chunk_write(&c, OP_CONSTANT, 1);
    chunk_write(&c, (uint8_t)idx, 1);
    chunk_write(&c, OP_NEGATE, 1);
    chunk_write(&c, OP_RETURN, 1);
    /* Redirect stdout to /dev/null for this test. */
    FILE *saved = stdout;
    stdout = fopen("/dev/null", "w");
    chunk_disassemble(&c, "test");
    fclose(stdout);
    stdout = saved;
    chunk_free(&c);
    PASS();
}

TEST(test_chunk_jump_bytecode) {
    /* Simulate: OP_JUMP_IF_FALSE with 2-byte offset */
    Chunk c;
    chunk_init(&c);
    chunk_write(&c, OP_JUMP_IF_FALSE, 1);
    uint16_t offset = 258;
    chunk_write(&c, (uint8_t)((offset >> 8) & 0xFF), 1); /* high byte */
    chunk_write(&c, (uint8_t)(offset & 0xFF), 1);        /* low byte */
    chunk_write(&c, OP_RETURN, 1);
    ASSERT(c.count == 4, "4 bytes");
    ASSERT(c.code[0] == OP_JUMP_IF_FALSE, "opcode");
    uint16_t decoded = (uint16_t)((c.code[1] << 8) | c.code[2]);
    ASSERT(decoded == 258, "decoded offset should be 258");
    chunk_free(&c);
    PASS();
}

TEST(test_chunk_call_bytecode) {
    /* Simulate: OP_CALL name_idx argc */
    Chunk c;
    chunk_init(&c);
    int name_idx = chunk_add_constant(&c, make_string(strdup("say")));
    chunk_write(&c, OP_CALL, 1);
    chunk_write(&c, (uint8_t)name_idx, 1);
    chunk_write(&c, 1, 1); /* argc = 1 */
    chunk_write(&c, OP_RETURN, 1);
    ASSERT(c.count == 4, "4 bytes");
    ASSERT(c.code[0] == OP_CALL, "opcode");
    ASSERT(c.code[1] == (uint8_t)name_idx, "name index");
    ASSERT(c.code[2] == 1, "argc");
    chunk_free(&c);
    PASS();
}

/* ============================================================
 * Main
 * ============================================================ */

int main(void) {
    printf("Running chunk tests...\n\n");

    printf("Initialization:\n");
    RUN_TEST(test_chunk_init);

    printf("\nWriting bytecode:\n");
    RUN_TEST(test_chunk_write_single);
    RUN_TEST(test_chunk_write_multiple);
    RUN_TEST(test_chunk_write_grows);

    printf("\nConstants pool:\n");
    RUN_TEST(test_chunk_add_constant_number);
    RUN_TEST(test_chunk_add_constant_string);
    RUN_TEST(test_chunk_add_multiple_constants);
    RUN_TEST(test_chunk_constant_overflow);

    printf("\nMemory management:\n");
    RUN_TEST(test_chunk_free_resets);

    printf("\nBytecode encoding:\n");
    RUN_TEST(test_chunk_constant_with_bytecode);
    RUN_TEST(test_chunk_jump_bytecode);
    RUN_TEST(test_chunk_call_bytecode);

    printf("\nUtilities:\n");
    RUN_TEST(test_opcode_names);
    RUN_TEST(test_chunk_disassemble_runs);

    printf("\n========================================\n");
    printf("Tests run: %d\n", tests_run);
    printf("Passed:    %d\n", tests_passed);
    printf("Failed:    %d\n", tests_failed);
    printf("========================================\n");

    return tests_failed > 0 ? 1 : 0;
}
