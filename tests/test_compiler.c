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
    ASSERT(strcmp(chunk->constants[0].string->chars, "hello") == 0, "value hello");
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
    ASSERT(strcmp(chunk->constants[0].string->chars, "x") == 0, "name is x");
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
    ASSERT(strcmp(chunk->constants[1].string->chars, "x") == 0, "name is x");
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
    /* Stack-based call convention (Step 4):
     * OP_GET_GLOBAL [say_idx], OP_CONSTANT [42_idx], OP_CALL [argc=1], OP_RETURN */
    ASSERT(chunk->code[0] == OP_GET_GLOBAL, "push callee");
    ASSERT(chunk->code[2] == OP_CONSTANT, "push arg");
    ASSERT(chunk->code[4] == OP_CALL, "OP_CALL");
    ASSERT(chunk->code[5] == 1, "argc = 1");
    ASSERT(chunk->code[6] == OP_RETURN, "OP_RETURN");
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
            strcmp(chunk->constants[i].string->chars, "x") == 0) {
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
 * Function definitions
 * ============================================================ */

/* Compile fn foo() is 42 end → OP_CLOSURE (function) + OP_DEFINE_GLOBAL (name) */
TEST(test_compile_fn_def_bytecode) {
    CompileError err;
    Chunk *chunk = compile_input("fn foo() is 42 end", &err);
    ASSERT(chunk != NULL, "should compile");
    /* The top-level chunk should have:
     *   OP_CLOSURE <fn_idx>   (create closure from constant pool function)
     *   OP_DEFINE_GLOBAL <name_idx>  (bind to global "foo")
     *   OP_RETURN
     */
    ASSERT(chunk->code[0] == OP_CLOSURE, "OP_CLOSURE for function value");
    uint8_t fn_idx = chunk->code[1];
    ASSERT(chunk->code[2] == OP_DEFINE_GLOBAL, "OP_DEFINE_GLOBAL for name");
    uint8_t name_idx = chunk->code[3];
    ASSERT(chunk->code[4] == OP_RETURN, "OP_RETURN");

    /* The function constant should be VAL_FUNCTION (VM wraps in closure). */
    ASSERT(fn_idx < chunk->const_count, "fn constant index in range");
    ASSERT(chunk->constants[fn_idx].type == VAL_FUNCTION, "constant is VAL_FUNCTION");
    ASSERT(chunk->constants[fn_idx].function != NULL, "function pointer not NULL");
    ASSERT(strcmp(chunk->constants[fn_idx].function->name, "foo") == 0, "function name is foo");
    ASSERT(chunk->constants[fn_idx].function->arity == 0, "arity is 0");

    /* The name constant should be a string "foo". */
    ASSERT(name_idx < chunk->const_count, "name constant index in range");
    ASSERT(chunk->constants[name_idx].type == VAL_STRING, "name constant is string");
    ASSERT(strcmp(chunk->constants[name_idx].string->chars, "foo") == 0, "name is foo");

    free_chunk(chunk);
    PASS();
}

/* Compile fn with parameters → function has correct arity and param names */
TEST(test_compile_fn_def_with_params) {
    CompileError err;
    Chunk *chunk = compile_input("fn add(a, b) is a end", &err);
    ASSERT(chunk != NULL, "should compile");
    ASSERT(chunk->code[0] == OP_CLOSURE, "OP_CLOSURE for function");
    uint8_t fn_idx = chunk->code[1];
    ASSERT(chunk->constants[fn_idx].type == VAL_FUNCTION, "constant is VAL_FUNCTION");
    ObjFunction *fn = chunk->constants[fn_idx].function;
    ASSERT(fn->arity == 2, "arity is 2");
    ASSERT(fn->params != NULL, "params array exists");
    ASSERT(strcmp(fn->params[0], "a") == 0, "param 0 is a");
    ASSERT(strcmp(fn->params[1], "b") == 0, "param 1 is b");
    free_chunk(chunk);
    PASS();
}

/* Function body gets its own chunk with OP_RETURN at the end */
TEST(test_compile_fn_body_has_return) {
    CompileError err;
    Chunk *chunk = compile_input("fn foo() is 42 end", &err);
    ASSERT(chunk != NULL, "should compile");
    uint8_t fn_idx = chunk->code[1];
    ObjFunction *fn = chunk->constants[fn_idx].function;
    ASSERT(fn->chunk != NULL, "function has its own chunk");
    /* The body chunk should end with OP_RETURN. */
    ASSERT(fn->chunk->count > 0, "body chunk is non-empty");
    ASSERT(fn->chunk->code[fn->chunk->count - 1] == OP_RETURN, "body ends with OP_RETURN");
    free_chunk(chunk);
    PASS();
}

/* ============================================================
 * Function parameters (OP_GET_LOCAL) - Step 6
 * ============================================================ */

/* Function body uses OP_GET_LOCAL for parameters, not OP_GET_GLOBAL */
TEST(test_compile_fn_param_uses_get_local) {
    CompileError err;
    Chunk *chunk = compile_input("fn identity(x) is x end", &err);
    ASSERT(chunk != NULL, "should compile");
    uint8_t fn_idx = chunk->code[1];
    ObjFunction *fn = chunk->constants[fn_idx].function;
    ASSERT(fn->chunk != NULL, "function has its own chunk");
    /* The body chunk should use OP_GET_LOCAL for x, not OP_GET_GLOBAL.
     * Parameter x is at slot 1 (slot 0 is reserved for the callee). */
    ASSERT(fn->chunk->code[0] == OP_GET_LOCAL, "body uses OP_GET_LOCAL for param");
    ASSERT(fn->chunk->code[1] == 1, "param x is at slot 1");
    ASSERT(fn->chunk->code[2] == OP_RETURN, "body ends with OP_RETURN");
    free_chunk(chunk);
    PASS();
}

/* Function body with two params: uses correct slot indices */
TEST(test_compile_fn_two_params_slots) {
    CompileError err;
    Chunk *chunk = compile_input("fn add(a, b) is a + b end", &err);
    ASSERT(chunk != NULL, "should compile");
    uint8_t fn_idx = chunk->code[1];
    ObjFunction *fn = chunk->constants[fn_idx].function;
    ASSERT(fn->chunk != NULL, "function has its own chunk");
    /* Body: OP_GET_LOCAL 1 (a), OP_GET_LOCAL 2 (b), OP_ADD, OP_RETURN */
    ASSERT(fn->chunk->code[0] == OP_GET_LOCAL, "first OP_GET_LOCAL");
    ASSERT(fn->chunk->code[1] == 1, "param a at slot 1");
    ASSERT(fn->chunk->code[2] == OP_GET_LOCAL, "second OP_GET_LOCAL");
    ASSERT(fn->chunk->code[3] == 2, "param b at slot 2");
    ASSERT(fn->chunk->code[4] == OP_ADD, "OP_ADD");
    ASSERT(fn->chunk->code[5] == OP_RETURN, "OP_RETURN");
    free_chunk(chunk);
    PASS();
}

/* Function body falls back to OP_GET_GLOBAL for non-parameter names */
TEST(test_compile_fn_nonparam_uses_get_global) {
    CompileError err;
    Chunk *chunk = compile_input("fn readglobal() is x end", &err);
    ASSERT(chunk != NULL, "should compile");
    uint8_t fn_idx = chunk->code[1];
    ObjFunction *fn = chunk->constants[fn_idx].function;
    ASSERT(fn->chunk != NULL, "function has its own chunk");
    /* x is not a parameter, so the body should use OP_GET_GLOBAL. */
    ASSERT(fn->chunk->code[0] == OP_GET_GLOBAL, "body uses OP_GET_GLOBAL for non-param");
    free_chunk(chunk);
    PASS();
}

/* ============================================================
 * Local variable declarations (OP_SET_LOCAL) - Step 7
 * ============================================================ */

/* my declaration inside function body does NOT use OP_DEFINE_GLOBAL */
TEST(test_compile_fn_local_decl_no_define_global) {
    CompileError err;
    Chunk *chunk = compile_input("fn foo(x) is\nmy y = 1\ny\nend", &err);
    ASSERT(chunk != NULL, "should compile");
    uint8_t fn_idx = chunk->code[1];
    ObjFunction *fn = chunk->constants[fn_idx].function;
    ASSERT(fn->chunk != NULL, "function has its own chunk");
    /* The body chunk should NOT contain OP_DEFINE_GLOBAL for local y. */
    bool found_define_global = false;
    for (size_t i = 0; i < fn->chunk->count; i++) {
        if (fn->chunk->code[i] == OP_DEFINE_GLOBAL) {
            found_define_global = true;
            break;
        }
    }
    ASSERT(!found_define_global, "body should not use OP_DEFINE_GLOBAL for local decl");
    free_chunk(chunk);
    PASS();
}

/* Assignment to parameter uses OP_SET_LOCAL, not OP_SET_GLOBAL */
TEST(test_compile_fn_assign_param_uses_set_local) {
    CompileError err;
    Chunk *chunk = compile_input("fn foo(x) is\nx = 1\nx\nend", &err);
    ASSERT(chunk != NULL, "should compile");
    uint8_t fn_idx = chunk->code[1];
    ObjFunction *fn = chunk->constants[fn_idx].function;
    ASSERT(fn->chunk != NULL, "function has its own chunk");
    /* Body: OP_CONSTANT [1], OP_SET_LOCAL 1, ...
     * The assignment x = 1 should use OP_SET_LOCAL for slot 1 (param x). */
    bool found_set_local = false;
    for (size_t i = 0; i < fn->chunk->count; i++) {
        if (fn->chunk->code[i] == OP_SET_LOCAL) {
            ASSERT(i + 1 < fn->chunk->count, "OP_SET_LOCAL has operand");
            ASSERT(fn->chunk->code[i + 1] == 1, "OP_SET_LOCAL targets slot 1 (param x)");
            found_set_local = true;
            break;
        }
    }
    ASSERT(found_set_local, "body should use OP_SET_LOCAL for param assignment");
    /* Should NOT contain OP_SET_GLOBAL for x. */
    bool found_set_global = false;
    for (size_t i = 0; i < fn->chunk->count; i++) {
        if (fn->chunk->code[i] == OP_SET_GLOBAL) {
            found_set_global = true;
            break;
        }
    }
    ASSERT(!found_set_global, "body should not use OP_SET_GLOBAL for param assignment");
    free_chunk(chunk);
    PASS();
}

/* ============================================================
 * Anonymous function definitions
 * ============================================================ */

/* Compile fn() is 42 end → OP_CLOSURE (function) + OP_RETURN, no OP_DEFINE_GLOBAL */
TEST(test_compile_anon_fn_no_define_global) {
    CompileError err;
    Chunk *chunk = compile_input("fn() is 42 end", &err);
    ASSERT(chunk != NULL, "should compile");
    /* The top-level chunk should have:
     *   OP_CLOSURE <fn_idx>   (create closure from constant pool function)
     *   OP_RETURN
     * No OP_DEFINE_GLOBAL since anonymous. */
    ASSERT(chunk->code[0] == OP_CLOSURE, "OP_CLOSURE for function value");
    uint8_t fn_idx = chunk->code[1];
    ASSERT(chunk->code[2] == OP_RETURN, "OP_RETURN after closure (no DEFINE_GLOBAL)");

    /* The function constant should be VAL_FUNCTION with NULL name. */
    ASSERT(fn_idx < chunk->const_count, "fn constant index in range");
    ASSERT(chunk->constants[fn_idx].type == VAL_FUNCTION, "constant is VAL_FUNCTION");
    ASSERT(chunk->constants[fn_idx].function != NULL, "function pointer not NULL");
    ASSERT(chunk->constants[fn_idx].function->name == NULL, "anonymous function name is NULL");
    ASSERT(chunk->constants[fn_idx].function->arity == 0, "arity is 0");

    free_chunk(chunk);
    PASS();
}

/* Anonymous fn with parameters has correct arity and param names */
TEST(test_compile_anon_fn_with_params) {
    CompileError err;
    Chunk *chunk = compile_input("fn(a, b) is a + b end", &err);
    ASSERT(chunk != NULL, "should compile");
    ASSERT(chunk->code[0] == OP_CLOSURE, "OP_CLOSURE for function");
    uint8_t fn_idx = chunk->code[1];
    ASSERT(chunk->constants[fn_idx].type == VAL_FUNCTION, "constant is VAL_FUNCTION");
    ObjFunction *fn = chunk->constants[fn_idx].function;
    ASSERT(fn->name == NULL, "anonymous function name is NULL");
    ASSERT(fn->arity == 2, "arity is 2");
    ASSERT(fn->params != NULL, "params array exists");
    ASSERT(strcmp(fn->params[0], "a") == 0, "param 0 is a");
    ASSERT(strcmp(fn->params[1], "b") == 0, "param 1 is b");

    /* Body should use OP_GET_LOCAL for params */
    ASSERT(fn->chunk != NULL, "function has its own chunk");
    ASSERT(fn->chunk->code[0] == OP_GET_LOCAL, "body uses OP_GET_LOCAL for param a");
    ASSERT(fn->chunk->code[1] == 1, "param a at slot 1");
    ASSERT(fn->chunk->code[2] == OP_GET_LOCAL, "second OP_GET_LOCAL for param b");
    ASSERT(fn->chunk->code[3] == 2, "param b at slot 2");

    free_chunk(chunk);
    PASS();
}

/* Anonymous fn assigned to variable: my f = fn(x) is x end */
TEST(test_compile_anon_fn_in_decl) {
    CompileError err;
    Chunk *chunk = compile_input("my f = fn(x) is x end", &err);
    ASSERT(chunk != NULL, "should compile");
    /* Should have: OP_CLOSURE (fn), OP_DEFINE_GLOBAL (f), OP_RETURN
     * The DEFINE_GLOBAL is for the variable 'f', not for the fn itself. */
    ASSERT(chunk->code[0] == OP_CLOSURE, "OP_CLOSURE for function value");
    uint8_t fn_idx = chunk->code[1];
    ASSERT(chunk->code[2] == OP_DEFINE_GLOBAL, "OP_DEFINE_GLOBAL for variable f");
    ASSERT(chunk->constants[fn_idx].type == VAL_FUNCTION, "constant is VAL_FUNCTION");
    ASSERT(chunk->constants[fn_idx].function->name == NULL, "fn name is NULL (anonymous)");

    free_chunk(chunk);
    PASS();
}

/* ============================================================
 * Local callee resolution in function calls
 * ============================================================ */

/* Call to a local parameter emits OP_GET_LOCAL for the callee, not OP_GET_GLOBAL.
 * fn apply(f, x) is f(x) end — f is param slot 1. */
TEST(test_compile_call_local_callee) {
    CompileError err;
    Chunk *chunk = compile_input("fn apply(f, x) is f(x) end", &err);
    ASSERT(chunk != NULL, "should compile");
    uint8_t fn_idx = chunk->code[1];
    ObjFunction *fn = chunk->constants[fn_idx].function;
    ASSERT(fn->chunk != NULL, "function has its own chunk");
    /* Body should emit OP_GET_LOCAL 1 (f) to push the callee,
     * then OP_GET_LOCAL 2 (x) to push the argument, then OP_CALL 1. */
    ASSERT(fn->chunk->code[0] == OP_GET_LOCAL, "callee loaded via OP_GET_LOCAL");
    ASSERT(fn->chunk->code[1] == 1, "callee f is at slot 1");
    ASSERT(fn->chunk->code[2] == OP_GET_LOCAL, "arg loaded via OP_GET_LOCAL");
    ASSERT(fn->chunk->code[3] == 2, "arg x is at slot 2");
    ASSERT(fn->chunk->code[4] == OP_CALL, "OP_CALL");
    ASSERT(fn->chunk->code[5] == 1, "argc = 1");
    ASSERT(fn->chunk->code[6] == OP_RETURN, "OP_RETURN");
    free_chunk(chunk);
    PASS();
}

/* Call to a non-local inside a function body still emits OP_GET_GLOBAL.
 * fn foo() is bar() end — bar is not a local. */
TEST(test_compile_call_global_callee_in_function) {
    CompileError err;
    Chunk *chunk = compile_input("fn foo() is bar() end", &err);
    ASSERT(chunk != NULL, "should compile");
    uint8_t fn_idx = chunk->code[1];
    ObjFunction *fn = chunk->constants[fn_idx].function;
    ASSERT(fn->chunk != NULL, "function has its own chunk");
    /* Body should emit OP_GET_GLOBAL for bar (not a local). */
    ASSERT(fn->chunk->code[0] == OP_GET_GLOBAL, "callee loaded via OP_GET_GLOBAL");
    free_chunk(chunk);
    PASS();
}

/* Assignment to non-local still uses OP_SET_GLOBAL */
TEST(test_compile_fn_assign_nonlocal_uses_set_global) {
    CompileError err;
    Chunk *chunk = compile_input("fn foo() is g = 1 end", &err);
    ASSERT(chunk != NULL, "should compile");
    uint8_t fn_idx = chunk->code[1];
    ObjFunction *fn = chunk->constants[fn_idx].function;
    ASSERT(fn->chunk != NULL, "function has its own chunk");
    /* g is not a local, so assignment should use OP_SET_GLOBAL. */
    bool found_set_global = false;
    for (size_t i = 0; i < fn->chunk->count; i++) {
        if (fn->chunk->code[i] == OP_SET_GLOBAL) {
            found_set_global = true;
            break;
        }
    }
    ASSERT(found_set_global, "body should use OP_SET_GLOBAL for non-local");
    free_chunk(chunk);
    PASS();
}

/* ============================================================
 * OP_CLOSURE emission (Step 4: closures-infrastructure)
 * ============================================================ */

/* Top-level named fn emits OP_CLOSURE (not OP_CONSTANT) + OP_DEFINE_GLOBAL */
TEST(test_compile_fn_emits_op_closure) {
    CompileError err;
    Chunk *chunk = compile_input("fn foo() is 42 end", &err);
    ASSERT(chunk != NULL, "should compile");
    /* OP_CLOSURE should be emitted instead of OP_CONSTANT for function values. */
    ASSERT(chunk->code[0] == OP_CLOSURE, "OP_CLOSURE for function value");
    uint8_t fn_idx = chunk->code[1];
    ASSERT(chunk->code[2] == OP_DEFINE_GLOBAL, "OP_DEFINE_GLOBAL for name");
    ASSERT(chunk->code[4] == OP_RETURN, "OP_RETURN");
    /* Constant pool still holds VAL_FUNCTION (VM wraps in closure at runtime). */
    ASSERT(fn_idx < chunk->const_count, "fn constant index in range");
    ASSERT(chunk->constants[fn_idx].type == VAL_FUNCTION, "constant is VAL_FUNCTION");
    free_chunk(chunk);
    PASS();
}

/* Anonymous fn also emits OP_CLOSURE (not OP_CONSTANT) */
TEST(test_compile_anon_fn_emits_op_closure) {
    CompileError err;
    Chunk *chunk = compile_input("fn() is 42 end", &err);
    ASSERT(chunk != NULL, "should compile");
    ASSERT(chunk->code[0] == OP_CLOSURE, "OP_CLOSURE for anonymous function");
    ASSERT(chunk->code[2] == OP_RETURN, "OP_RETURN (no DEFINE_GLOBAL)");
    free_chunk(chunk);
    PASS();
}

/* Named function inside another function is local, not global */
TEST(test_compile_nested_fn_is_local) {
    CompileError err;
    Chunk *chunk = compile_input("fn outer() is\nfn inner() is 42 end\ninner()\nend", &err);
    ASSERT(chunk != NULL, "should compile");
    /* Get the outer function from the constant pool. */
    uint8_t outer_fn_idx = chunk->code[1];
    ASSERT(chunk->constants[outer_fn_idx].type == VAL_FUNCTION, "outer is VAL_FUNCTION");
    ObjFunction *outer_fn = chunk->constants[outer_fn_idx].function;
    ASSERT(outer_fn->chunk != NULL, "outer has its own chunk");
    /* The outer body should NOT contain OP_DEFINE_GLOBAL for inner. */
    bool found_define_global = false;
    for (size_t i = 0; i < outer_fn->chunk->count; i++) {
        if (outer_fn->chunk->code[i] == OP_DEFINE_GLOBAL) {
            found_define_global = true;
            break;
        }
    }
    ASSERT(!found_define_global, "nested fn should not emit OP_DEFINE_GLOBAL");
    /* The outer body should contain OP_CLOSURE for the inner function. */
    bool found_closure = false;
    for (size_t i = 0; i < outer_fn->chunk->count; i++) {
        if (outer_fn->chunk->code[i] == OP_CLOSURE) {
            found_closure = true;
            break;
        }
    }
    ASSERT(found_closure, "nested fn should emit OP_CLOSURE");
    free_chunk(chunk);
    PASS();
}

/* ============================================================
 * Upvalue resolution (closures-capture Step 1)
 * ============================================================ */

/* Inner function captures outer local → OP_CLOSURE has 1 upvalue descriptor,
 * inner body uses OP_GET_UPVALUE instead of OP_GET_GLOBAL.
 * fn() is my x = 1\nfn() is x end end */
TEST(test_compile_upvalue_capture_local) {
    CompileError err;
    Chunk *chunk = compile_input("fn() is\nmy x = 1\nfn() is x end\nend", &err);
    ASSERT(chunk != NULL, "should compile");

    /* Top-level has OP_CLOSURE for the outer fn. */
    ASSERT(chunk->code[0] == OP_CLOSURE, "outer OP_CLOSURE");
    uint8_t outer_fn_idx = chunk->code[1];
    ObjFunction *outer_fn = chunk->constants[outer_fn_idx].function;
    ASSERT(outer_fn != NULL, "outer function exists");
    ASSERT(outer_fn->upvalue_count == 0, "outer fn captures nothing");

    /* Find OP_CLOSURE in the outer body (for the inner fn). */
    Chunk *outer_body = outer_fn->chunk;
    ASSERT(outer_body != NULL, "outer body chunk exists");

    int closure_offset = -1;
    for (size_t i = 0; i < outer_body->count; i++) {
        if (outer_body->code[i] == OP_CLOSURE) {
            closure_offset = (int)i;
            break;
        }
    }
    ASSERT(closure_offset >= 0, "inner OP_CLOSURE found in outer body");

    /* Get the inner function from the outer body's constant pool. */
    uint8_t inner_fn_idx = outer_body->code[closure_offset + 1];
    ASSERT(inner_fn_idx < outer_body->const_count, "inner fn constant index in range");
    ObjFunction *inner_fn = outer_body->constants[inner_fn_idx].function;
    ASSERT(inner_fn != NULL, "inner function exists");
    ASSERT(inner_fn->upvalue_count == 1, "inner fn captures 1 upvalue");

    /* Upvalue descriptor after OP_CLOSURE: (is_local=1, index=1).
     * x is at slot 1 in the outer function (slot 0 = callee). */
    uint8_t is_local = outer_body->code[closure_offset + 2];
    uint8_t uv_index = outer_body->code[closure_offset + 3];
    ASSERT(is_local == 1, "upvalue is_local = 1 (direct capture)");
    ASSERT(uv_index == 1, "upvalue index = 1 (slot of x in outer fn)");

    /* Inner body should use OP_GET_UPVALUE 0 for x. */
    Chunk *inner_body = inner_fn->chunk;
    ASSERT(inner_body != NULL, "inner body chunk exists");
    ASSERT(inner_body->code[0] == OP_GET_UPVALUE, "inner body uses OP_GET_UPVALUE");
    ASSERT(inner_body->code[1] == 0, "upvalue index 0");

    free_chunk(chunk);
    PASS();
}

/* Function with no capture has upvalue_count == 0 and no upvalue descriptors. */
TEST(test_compile_no_capture_zero_upvalues) {
    CompileError err;
    Chunk *chunk = compile_input("fn foo() is 42 end", &err);
    ASSERT(chunk != NULL, "should compile");
    ASSERT(chunk->code[0] == OP_CLOSURE, "OP_CLOSURE");
    uint8_t fn_idx = chunk->code[1];
    ObjFunction *fn = chunk->constants[fn_idx].function;
    ASSERT(fn->upvalue_count == 0, "no upvalues when nothing is captured");
    /* Next byte after OP_CLOSURE+idx should be the next instruction
     * (no upvalue descriptors to skip). */
    ASSERT(chunk->code[2] == OP_DEFINE_GLOBAL, "immediately followed by OP_DEFINE_GLOBAL");
    free_chunk(chunk);
    PASS();
}

/* 3-level nesting: innermost captures outermost's local via chained upvalue.
 * fn() is my x = 1\nfn() is fn() is x end end end
 * - Middle captures x from outermost: (is_local=true, index=1)
 * - Innermost captures x from middle's upvalue: (is_local=false, index=0) */
TEST(test_compile_upvalue_chained_capture) {
    CompileError err;
    Chunk *chunk = compile_input("fn() is\nmy x = 1\nfn() is\nfn() is x end\nend\nend", &err);
    ASSERT(chunk != NULL, "should compile");

    /* Navigate to outermost function. */
    uint8_t outer_fn_idx = chunk->code[1];
    ObjFunction *outer_fn = chunk->constants[outer_fn_idx].function;
    Chunk *outer_body = outer_fn->chunk;

    /* Find OP_CLOSURE for middle function in outer body. */
    int mid_closure_offset = -1;
    for (size_t i = 0; i < outer_body->count; i++) {
        if (outer_body->code[i] == OP_CLOSURE) {
            mid_closure_offset = (int)i;
            break;
        }
    }
    ASSERT(mid_closure_offset >= 0, "middle OP_CLOSURE found");

    uint8_t mid_fn_idx = outer_body->code[mid_closure_offset + 1];
    ObjFunction *mid_fn = outer_body->constants[mid_fn_idx].function;
    ASSERT(mid_fn->upvalue_count == 1, "middle fn captures 1 upvalue");

    /* Middle's upvalue descriptor: (is_local=true, index=1) — captures x directly. */
    uint8_t mid_is_local = outer_body->code[mid_closure_offset + 2];
    uint8_t mid_uv_index = outer_body->code[mid_closure_offset + 3];
    ASSERT(mid_is_local == 1, "middle captures local (is_local=1)");
    ASSERT(mid_uv_index == 1, "middle captures slot 1 (x)");

    /* Find OP_CLOSURE for innermost function in middle body. */
    Chunk *mid_body = mid_fn->chunk;
    ASSERT(mid_body != NULL, "middle body chunk exists");

    int inner_closure_offset = -1;
    for (size_t i = 0; i < mid_body->count; i++) {
        if (mid_body->code[i] == OP_CLOSURE) {
            inner_closure_offset = (int)i;
            break;
        }
    }
    ASSERT(inner_closure_offset >= 0, "inner OP_CLOSURE found in middle body");

    uint8_t inner_fn_idx = mid_body->code[inner_closure_offset + 1];
    ObjFunction *inner_fn = mid_body->constants[inner_fn_idx].function;
    ASSERT(inner_fn->upvalue_count == 1, "innermost fn captures 1 upvalue");

    /* Innermost's upvalue descriptor: (is_local=false, index=0) — from middle's upvalue[0]. */
    uint8_t inner_is_local = mid_body->code[inner_closure_offset + 2];
    uint8_t inner_uv_index = mid_body->code[inner_closure_offset + 3];
    ASSERT(inner_is_local == 0, "innermost captures upvalue (is_local=0)");
    ASSERT(inner_uv_index == 0, "innermost captures middle's upvalue[0]");

    /* Innermost body should use OP_GET_UPVALUE 0. */
    Chunk *inner_body = inner_fn->chunk;
    ASSERT(inner_body != NULL, "inner body chunk exists");
    ASSERT(inner_body->code[0] == OP_GET_UPVALUE, "innermost uses OP_GET_UPVALUE");
    ASSERT(inner_body->code[1] == 0, "innermost upvalue index 0");

    free_chunk(chunk);
    PASS();
}

/* Assignment through upvalue emits OP_SET_UPVALUE.
 * fn() is my x = 1\nfn() is x = 2 end end */
TEST(test_compile_upvalue_set) {
    CompileError err;
    Chunk *chunk = compile_input("fn() is\nmy x = 1\nfn() is x = 2 end\nend", &err);
    ASSERT(chunk != NULL, "should compile");

    /* Navigate to inner function's body. */
    uint8_t outer_fn_idx = chunk->code[1];
    ObjFunction *outer_fn = chunk->constants[outer_fn_idx].function;
    Chunk *outer_body = outer_fn->chunk;

    int closure_offset = -1;
    for (size_t i = 0; i < outer_body->count; i++) {
        if (outer_body->code[i] == OP_CLOSURE) {
            closure_offset = (int)i;
            break;
        }
    }
    ASSERT(closure_offset >= 0, "inner OP_CLOSURE found");

    uint8_t inner_fn_idx = outer_body->code[closure_offset + 1];
    ObjFunction *inner_fn = outer_body->constants[inner_fn_idx].function;
    Chunk *inner_body = inner_fn->chunk;
    ASSERT(inner_body != NULL, "inner body exists");

    /* Inner body: OP_CONSTANT <2>, OP_SET_UPVALUE 0, OP_RETURN */
    ASSERT(inner_body->code[0] == OP_CONSTANT, "push RHS value");
    ASSERT(inner_body->code[2] == OP_SET_UPVALUE, "OP_SET_UPVALUE for assignment");
    ASSERT(inner_body->code[3] == 0, "upvalue index 0");

    free_chunk(chunk);
    PASS();
}

/* Upvalue in call position: callee resolved via OP_GET_UPVALUE.
 * fn() is my f = fn() is 42 end\nfn() is f() end end */
TEST(test_compile_upvalue_call) {
    CompileError err;
    Chunk *chunk = compile_input("fn() is\nmy f = fn() is 42 end\nfn() is f() end\nend", &err);
    ASSERT(chunk != NULL, "should compile");

    /* Navigate to the outer function. */
    uint8_t outer_fn_idx = chunk->code[1];
    ObjFunction *outer_fn = chunk->constants[outer_fn_idx].function;
    Chunk *outer_body = outer_fn->chunk;

    /* Find the SECOND OP_CLOSURE in outer body (first is f's closure,
     * second is the inner fn that captures f). */
    int closure_count = 0;
    int inner_closure_offset = -1;
    for (size_t i = 0; i < outer_body->count; i++) {
        if (outer_body->code[i] == OP_CLOSURE) {
            closure_count++;
            if (closure_count == 2) {
                inner_closure_offset = (int)i;
                break;
            }
            /* Skip upvalue descriptors for first closure. */
            uint8_t fn_idx = outer_body->code[i + 1];
            ObjFunction *fn = outer_body->constants[fn_idx].function;
            i += 1 + (size_t)(fn->upvalue_count * 2);
        }
    }
    ASSERT(inner_closure_offset >= 0, "second OP_CLOSURE found (inner fn)");

    uint8_t inner_fn_idx = outer_body->code[inner_closure_offset + 1];
    ObjFunction *inner_fn = outer_body->constants[inner_fn_idx].function;
    Chunk *inner_body = inner_fn->chunk;
    ASSERT(inner_body != NULL, "inner body exists");

    /* Inner body should use OP_GET_UPVALUE for the callee f,
     * then OP_CALL. */
    ASSERT(inner_body->code[0] == OP_GET_UPVALUE, "callee loaded via OP_GET_UPVALUE");
    ASSERT(inner_body->code[1] == 0, "upvalue index 0 (f)");
    ASSERT(inner_body->code[2] == OP_CALL, "OP_CALL");
    ASSERT(inner_body->code[3] == 0, "argc = 0");

    free_chunk(chunk);
    PASS();
}

/* ============================================================
 * Zip map (@:) compilation
 * ============================================================ */

/* ["a"] @: [1] → OP_ZIP_MAP (no operand byte, unlike OP_VECTORIZE) */
TEST(test_compile_zip_map) {
    CompileError err;
    Chunk *chunk = compile_input("[\"a\"] @: [1]", &err);
    ASSERT(chunk != NULL, "should compile @:");
    /* Bytecode: array construction for left, array construction for right,
     * OP_ZIP_MAP, OP_RETURN. Find OP_ZIP_MAP in the bytecode. */
    bool found = false;
    for (size_t i = 0; i < chunk->count; i++) {
        if (chunk->code[i] == OP_ZIP_MAP) {
            found = true;
            break;
        }
    }
    ASSERT(found, "should emit OP_ZIP_MAP");
    /* Verify OP_VECTORIZE is NOT emitted — @: uses its own opcode. */
    for (size_t i = 0; i < chunk->count; i++) {
        ASSERT(chunk->code[i] != OP_VECTORIZE, "should not emit OP_VECTORIZE for @:");
    }
    free_chunk(chunk);
    PASS();
}

/* @: in prefix position (reduce) should be a compile error. */
TEST(test_compile_zip_map_prefix_error) {
    CompileError err;
    Chunk *chunk = compile_input("@: [1, 2, 3]", &err);
    ASSERT(chunk == NULL, "@: in prefix should fail to compile");
    ASSERT(strstr(err.message, "cannot be used as a reduction") != NULL,
           "error message should mention reduction");
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

    printf("\nFunction definitions:\n");
    RUN_TEST(test_compile_fn_def_bytecode);
    RUN_TEST(test_compile_fn_def_with_params);
    RUN_TEST(test_compile_fn_body_has_return);

    printf("\nFunction parameters (OP_GET_LOCAL):\n");
    RUN_TEST(test_compile_fn_param_uses_get_local);
    RUN_TEST(test_compile_fn_two_params_slots);
    RUN_TEST(test_compile_fn_nonparam_uses_get_global);

    printf("\nLocal variable declarations (OP_SET_LOCAL):\n");
    RUN_TEST(test_compile_fn_local_decl_no_define_global);
    RUN_TEST(test_compile_fn_assign_param_uses_set_local);
    RUN_TEST(test_compile_fn_assign_nonlocal_uses_set_global);

    printf("\nLocal callee resolution in function calls:\n");
    RUN_TEST(test_compile_call_local_callee);
    RUN_TEST(test_compile_call_global_callee_in_function);

    printf("\nAnonymous function definitions:\n");
    RUN_TEST(test_compile_anon_fn_no_define_global);
    RUN_TEST(test_compile_anon_fn_with_params);
    RUN_TEST(test_compile_anon_fn_in_decl);

    printf("\nOP_CLOSURE emission:\n");
    RUN_TEST(test_compile_fn_emits_op_closure);
    RUN_TEST(test_compile_anon_fn_emits_op_closure);
    RUN_TEST(test_compile_nested_fn_is_local);

    printf("\nUpvalue resolution:\n");
    RUN_TEST(test_compile_upvalue_capture_local);
    RUN_TEST(test_compile_no_capture_zero_upvalues);
    RUN_TEST(test_compile_upvalue_chained_capture);
    RUN_TEST(test_compile_upvalue_set);
    RUN_TEST(test_compile_upvalue_call);

    printf("\nZip map (@:) compilation:\n");
    RUN_TEST(test_compile_zip_map);
    RUN_TEST(test_compile_zip_map_prefix_error);

    printf("\n========================================\n");
    printf("Tests run: %d\n", tests_run);
    printf("Passed:    %d\n", tests_passed);
    printf("Failed:    %d\n", tests_failed);
    printf("========================================\n");

    return tests_failed > 0 ? 1 : 0;
}
