/*
 * test_compiler.c - Tests for the Cutlet bytecode compiler
 *
 * Tests compile() on various AST inputs: verifies correct opcode
 * sequences and constants are emitted.
 */

#include "../src/chunk.h"
#include "../src/compiler.h"
#include "../src/parser.h"
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

/* Helper: parse and compile input, return chunk (or NULL). */
static Chunk *compile_input(const char *input, CompileError *err) {
    AstNode *node = NULL;
    ParseError perr;
    if (!parser_parse(input, &node, &perr)) {
        snprintf(err->message, sizeof(err->message), "parse: %s", perr.message);
        return NULL;
    }
    Chunk *chunk = compile(node, err);
    ast_free(node);
    return chunk;
}

/* Helper: free a chunk and its memory. */
static void free_chunk(Chunk *chunk) {
    if (chunk) {
        chunk_free(chunk);
        free(chunk);
    }
}

/* ============================================================
 * Tests
 * ============================================================ */

TEST(test_compile_number) {
    CompileError err;
    Chunk *chunk = compile_input("42", &err);
    ASSERT(chunk != NULL, "should compile");
    /* Expected: OP_CONSTANT 0, OP_RETURN */
    ASSERT(chunk->count == 3, "3 bytes");
    ASSERT(chunk->code[0] == OP_CONSTANT, "OP_CONSTANT");
    ASSERT(chunk->code[1] == 0, "constant index 0");
    ASSERT(chunk->code[2] == OP_RETURN, "OP_RETURN");
    ASSERT(chunk->const_count == 1, "1 constant");
    ASSERT(chunk->constants[0].type == VAL_NUMBER, "number constant");
    ASSERT(chunk->constants[0].number == 42.0, "value 42");
    free_chunk(chunk);
    PASS();
}

TEST(test_compile_string) {
    CompileError err;
    Chunk *chunk = compile_input("\"hello\"", &err);
    ASSERT(chunk != NULL, "should compile");
    ASSERT(chunk->code[0] == OP_CONSTANT, "OP_CONSTANT");
    ASSERT(chunk->constants[0].type == VAL_STRING, "string constant");
    ASSERT(strcmp(chunk->constants[0].string, "hello") == 0, "value hello");
    free_chunk(chunk);
    PASS();
}

TEST(test_compile_true) {
    CompileError err;
    Chunk *chunk = compile_input("true", &err);
    ASSERT(chunk != NULL, "should compile");
    ASSERT(chunk->code[0] == OP_TRUE, "OP_TRUE");
    ASSERT(chunk->code[1] == OP_RETURN, "OP_RETURN");
    free_chunk(chunk);
    PASS();
}

TEST(test_compile_false) {
    CompileError err;
    Chunk *chunk = compile_input("false", &err);
    ASSERT(chunk != NULL, "should compile");
    ASSERT(chunk->code[0] == OP_FALSE, "OP_FALSE");
    free_chunk(chunk);
    PASS();
}

TEST(test_compile_nothing) {
    CompileError err;
    Chunk *chunk = compile_input("nothing", &err);
    ASSERT(chunk != NULL, "should compile");
    ASSERT(chunk->code[0] == OP_NOTHING, "OP_NOTHING");
    free_chunk(chunk);
    PASS();
}

TEST(test_compile_addition) {
    CompileError err;
    Chunk *chunk = compile_input("1 + 2", &err);
    ASSERT(chunk != NULL, "should compile");
    /* OP_CONSTANT 0, OP_CONSTANT 1, OP_ADD, OP_RETURN */
    ASSERT(chunk->code[0] == OP_CONSTANT, "left constant");
    ASSERT(chunk->code[2] == OP_CONSTANT, "right constant");
    ASSERT(chunk->code[4] == OP_ADD, "OP_ADD");
    ASSERT(chunk->code[5] == OP_RETURN, "OP_RETURN");
    free_chunk(chunk);
    PASS();
}

TEST(test_compile_subtraction) {
    CompileError err;
    Chunk *chunk = compile_input("3 - 1", &err);
    ASSERT(chunk != NULL, "should compile");
    ASSERT(chunk->code[4] == OP_SUBTRACT, "OP_SUBTRACT");
    free_chunk(chunk);
    PASS();
}

TEST(test_compile_multiply) {
    CompileError err;
    Chunk *chunk = compile_input("2 * 3", &err);
    ASSERT(chunk != NULL, "should compile");
    ASSERT(chunk->code[4] == OP_MULTIPLY, "OP_MULTIPLY");
    free_chunk(chunk);
    PASS();
}

TEST(test_compile_divide) {
    CompileError err;
    Chunk *chunk = compile_input("6 / 3", &err);
    ASSERT(chunk != NULL, "should compile");
    ASSERT(chunk->code[4] == OP_DIVIDE, "OP_DIVIDE");
    free_chunk(chunk);
    PASS();
}

TEST(test_compile_power) {
    CompileError err;
    Chunk *chunk = compile_input("2 ** 10", &err);
    ASSERT(chunk != NULL, "should compile");
    ASSERT(chunk->code[4] == OP_POWER, "OP_POWER");
    free_chunk(chunk);
    PASS();
}

TEST(test_compile_negate) {
    CompileError err;
    Chunk *chunk = compile_input("-3", &err);
    ASSERT(chunk != NULL, "should compile");
    /* OP_CONSTANT 0, OP_NEGATE, OP_RETURN */
    ASSERT(chunk->code[0] == OP_CONSTANT, "OP_CONSTANT");
    ASSERT(chunk->code[2] == OP_NEGATE, "OP_NEGATE");
    ASSERT(chunk->code[3] == OP_RETURN, "OP_RETURN");
    free_chunk(chunk);
    PASS();
}

TEST(test_compile_not) {
    CompileError err;
    Chunk *chunk = compile_input("not true", &err);
    ASSERT(chunk != NULL, "should compile");
    ASSERT(chunk->code[0] == OP_TRUE, "OP_TRUE");
    ASSERT(chunk->code[1] == OP_NOT, "OP_NOT");
    free_chunk(chunk);
    PASS();
}

TEST(test_compile_comparison_ops) {
    const char *inputs[] = {"1 == 2", "1 != 2", "1 < 2", "1 > 2", "1 <= 2", "1 >= 2"};
    OpCode expected[] = {OP_EQUAL,   OP_NOT_EQUAL,  OP_LESS,
                         OP_GREATER, OP_LESS_EQUAL, OP_GREATER_EQUAL};
    for (int i = 0; i < 6; i++) {
        CompileError err;
        Chunk *chunk = compile_input(inputs[i], &err);
        ASSERT(chunk != NULL, "should compile");
        ASSERT(chunk->code[4] == expected[i], "correct comparison op");
        free_chunk(chunk);
    }
    PASS();
}

TEST(test_compile_ident) {
    CompileError err;
    Chunk *chunk = compile_input("x", &err);
    ASSERT(chunk != NULL, "should compile");
    ASSERT(chunk->code[0] == OP_GET_GLOBAL, "OP_GET_GLOBAL");
    ASSERT(chunk->constants[0].type == VAL_STRING, "name is string constant");
    ASSERT(strcmp(chunk->constants[0].string, "x") == 0, "name is x");
    free_chunk(chunk);
    PASS();
}

TEST(test_compile_decl) {
    CompileError err;
    Chunk *chunk = compile_input("my x = 42", &err);
    ASSERT(chunk != NULL, "should compile");
    /* OP_CONSTANT 0 (42), OP_DEFINE_GLOBAL 1 ("x"), OP_RETURN */
    ASSERT(chunk->code[0] == OP_CONSTANT, "push value");
    ASSERT(chunk->code[2] == OP_DEFINE_GLOBAL, "OP_DEFINE_GLOBAL");
    ASSERT(chunk->constants[0].type == VAL_NUMBER, "number constant");
    ASSERT(chunk->constants[1].type == VAL_STRING, "name constant");
    ASSERT(strcmp(chunk->constants[1].string, "x") == 0, "name is x");
    free_chunk(chunk);
    PASS();
}

TEST(test_compile_assign) {
    CompileError err;
    Chunk *chunk = compile_input("x = 42", &err);
    ASSERT(chunk != NULL, "should compile");
    ASSERT(chunk->code[0] == OP_CONSTANT, "push value");
    ASSERT(chunk->code[2] == OP_SET_GLOBAL, "OP_SET_GLOBAL");
    free_chunk(chunk);
    PASS();
}

TEST(test_compile_block) {
    CompileError err;
    Chunk *chunk = compile_input("1\n2\n3", &err);
    ASSERT(chunk != NULL, "should compile");
    /* OP_CONSTANT 0, OP_POP, OP_CONSTANT 1, OP_POP, OP_CONSTANT 2, OP_RETURN */
    ASSERT(chunk->code[0] == OP_CONSTANT, "first constant");
    ASSERT(chunk->code[2] == OP_POP, "pop first");
    ASSERT(chunk->code[3] == OP_CONSTANT, "second constant");
    ASSERT(chunk->code[5] == OP_POP, "pop second");
    ASSERT(chunk->code[6] == OP_CONSTANT, "third constant");
    /* Last value stays on stack, no POP before RETURN */
    ASSERT(chunk->code[8] == OP_RETURN, "OP_RETURN");
    free_chunk(chunk);
    PASS();
}

TEST(test_compile_call_say) {
    CompileError err;
    Chunk *chunk = compile_input("say(42)", &err);
    ASSERT(chunk != NULL, "should compile");
    /* OP_CONSTANT 0 (42), OP_CALL name_idx argc, OP_RETURN */
    ASSERT(chunk->code[0] == OP_CONSTANT, "push arg");
    ASSERT(chunk->code[2] == OP_CALL, "OP_CALL");
    /* name_idx is code[3], argc is code[4] */
    ASSERT(chunk->code[4] == 1, "argc = 1");
    free_chunk(chunk);
    PASS();
}

TEST(test_compile_if_then_else) {
    CompileError err;
    Chunk *chunk = compile_input("if true then 1 else 2 end", &err);
    ASSERT(chunk != NULL, "should compile");
    /* Verify structure:
     * OP_TRUE, OP_JUMP_IF_FALSE -> else, OP_POP,
     * OP_CONSTANT (1), OP_JUMP -> end,
     * OP_POP, OP_CONSTANT (2),
     * OP_RETURN */
    ASSERT(chunk->code[0] == OP_TRUE, "condition");
    ASSERT(chunk->code[1] == OP_JUMP_IF_FALSE, "jump to else");
    /* After jump operand (2 bytes), OP_POP, then-body, OP_JUMP */
    free_chunk(chunk);
    PASS();
}

TEST(test_compile_if_no_else) {
    CompileError err;
    Chunk *chunk = compile_input("if false then 1 end", &err);
    ASSERT(chunk != NULL, "should compile");
    /* Should have OP_NOTHING for the missing else branch. */
    /* Find OP_NOTHING somewhere in the bytecode. */
    bool found_nothing = false;
    for (size_t i = 0; i < chunk->count; i++) {
        if (chunk->code[i] == OP_NOTHING) {
            found_nothing = true;
            break;
        }
    }
    ASSERT(found_nothing, "should emit OP_NOTHING for missing else");
    free_chunk(chunk);
    PASS();
}

TEST(test_compile_and_short_circuit) {
    CompileError err;
    Chunk *chunk = compile_input("true and false", &err);
    ASSERT(chunk != NULL, "should compile");
    /* OP_TRUE, OP_JUMP_IF_FALSE -> end, OP_POP, OP_FALSE, [end:], OP_RETURN */
    ASSERT(chunk->code[0] == OP_TRUE, "left operand");
    ASSERT(chunk->code[1] == OP_JUMP_IF_FALSE, "short-circuit jump");
    free_chunk(chunk);
    PASS();
}

TEST(test_compile_or_short_circuit) {
    CompileError err;
    Chunk *chunk = compile_input("false or true", &err);
    ASSERT(chunk != NULL, "should compile");
    /* OP_FALSE, OP_JUMP_IF_TRUE -> end, OP_POP, OP_TRUE, [end:], OP_RETURN */
    ASSERT(chunk->code[0] == OP_FALSE, "left operand");
    ASSERT(chunk->code[1] == OP_JUMP_IF_TRUE, "short-circuit jump");
    free_chunk(chunk);
    PASS();
}

TEST(test_compile_precedence) {
    /* 1 + 2 * 3 should compile as: 1, (2, 3, MUL), ADD */
    CompileError err;
    Chunk *chunk = compile_input("1 + 2 * 3", &err);
    ASSERT(chunk != NULL, "should compile");
    /* Bytecode: CONST(1), CONST(2), CONST(3), MUL, ADD, RETURN */
    ASSERT(chunk->code[0] == OP_CONSTANT, "1");
    ASSERT(chunk->code[2] == OP_CONSTANT, "2");
    ASSERT(chunk->code[4] == OP_CONSTANT, "3");
    ASSERT(chunk->code[6] == OP_MULTIPLY, "MUL before ADD");
    ASSERT(chunk->code[7] == OP_ADD, "ADD");
    free_chunk(chunk);
    PASS();
}

TEST(test_compile_nested_if) {
    CompileError err;
    Chunk *chunk = compile_input("if true then if false then 1 else 2 end else 3 end", &err);
    ASSERT(chunk != NULL, "should compile nested if");
    /* Just verify it compiles and has a RETURN. */
    ASSERT(chunk->code[chunk->count - 1] == OP_RETURN, "ends with RETURN");
    free_chunk(chunk);
    PASS();
}

TEST(test_compile_complex_block) {
    CompileError err;
    Chunk *chunk = compile_input("my x = 1\nmy y = 2\nx + y", &err);
    ASSERT(chunk != NULL, "should compile");
    /* Should have: CONST(1), DEFINE(x), POP, CONST(2), DEFINE(y), POP, GET(x), GET(y), ADD, RETURN
     */
    ASSERT(chunk->code[chunk->count - 1] == OP_RETURN, "ends with RETURN");
    free_chunk(chunk);
    PASS();
}

/* ============================================================
 * Constant deduplication
 * ============================================================ */

TEST(test_compile_dedup_constants) {
    CompileError err;
    /* "my x = 1\nx = x + 1\nx" uses "x" 3 times and "1" twice. */
    Chunk *chunk = compile_input("my x = 1\nx = x + 1\nx", &err);
    ASSERT(chunk != NULL, "should compile");
    /* With dedup: "x" should appear only once, and "1" only once. */
    int x_count = 0;
    int one_count = 0;
    for (size_t i = 0; i < chunk->const_count; i++) {
        if (chunk->constants[i].type == VAL_STRING &&
            strcmp(chunk->constants[i].string, "x") == 0) {
            x_count++;
        }
        if (chunk->constants[i].type == VAL_NUMBER && chunk->constants[i].number == 1.0) {
            one_count++;
        }
    }
    ASSERT(x_count == 1, "x should appear only once in constant pool");
    ASSERT(one_count == 1, "1 should appear only once in constant pool");
    free_chunk(chunk);
    PASS();
}

/* ============================================================
 * Main
 * ============================================================ */

int main(void) {
    printf("Running compiler tests...\n\n");

    printf("Literals:\n");
    RUN_TEST(test_compile_number);
    RUN_TEST(test_compile_string);
    RUN_TEST(test_compile_true);
    RUN_TEST(test_compile_false);
    RUN_TEST(test_compile_nothing);

    printf("\nArithmetic:\n");
    RUN_TEST(test_compile_addition);
    RUN_TEST(test_compile_subtraction);
    RUN_TEST(test_compile_multiply);
    RUN_TEST(test_compile_divide);
    RUN_TEST(test_compile_power);
    RUN_TEST(test_compile_negate);
    RUN_TEST(test_compile_not);
    RUN_TEST(test_compile_comparison_ops);
    RUN_TEST(test_compile_precedence);

    printf("\nVariables:\n");
    RUN_TEST(test_compile_ident);
    RUN_TEST(test_compile_decl);
    RUN_TEST(test_compile_assign);

    printf("\nBlocks:\n");
    RUN_TEST(test_compile_block);

    printf("\nFunction calls:\n");
    RUN_TEST(test_compile_call_say);

    printf("\nControl flow:\n");
    RUN_TEST(test_compile_if_then_else);
    RUN_TEST(test_compile_if_no_else);
    RUN_TEST(test_compile_and_short_circuit);
    RUN_TEST(test_compile_or_short_circuit);
    RUN_TEST(test_compile_nested_if);

    printf("\nComplex expressions:\n");
    RUN_TEST(test_compile_complex_block);

    printf("\nConstant deduplication:\n");
    RUN_TEST(test_compile_dedup_constants);

    printf("\n========================================\n");
    printf("Tests run: %d\n", tests_run);
    printf("Passed:    %d\n", tests_passed);
    printf("Failed:    %d\n", tests_failed);
    printf("========================================\n");

    return tests_failed > 0 ? 1 : 0;
}
