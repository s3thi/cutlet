/*
 * test_chunk.c - Tests for the Cutlet bytecode chunk
 *
 * Tests chunk_init, chunk_write, chunk_add_constant, chunk_free,
 * and chunk_disassemble for correct behavior.
 */

#include "../src/chunk.h"
#include "../src/gc.h"
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
    ASSERT(strcmp(c.constants[0].string->chars, "hello") == 0, "value should be hello");
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
 * chunk_disassemble_to_string() tests
 * ============================================================ */

TEST(test_disassemble_to_string_header) {
    /* Verify the returned string contains the "== name ==" header. */
    Chunk c;
    chunk_init(&c);
    int idx = chunk_add_constant(&c, make_number(42.0));
    chunk_write(&c, OP_CONSTANT, 1);
    chunk_write(&c, (uint8_t)idx, 1);
    chunk_write(&c, OP_RETURN, 1);
    char *s = chunk_disassemble_to_string(&c, "test_chunk");
    ASSERT(s != NULL, "should return non-NULL string");
    ASSERT(strstr(s, "== test_chunk ==") != NULL, "should contain header");
    free(s);
    chunk_free(&c);
    PASS();
}

TEST(test_disassemble_to_string_opcodes) {
    /* Verify the returned string contains expected opcode names. */
    Chunk c;
    chunk_init(&c);
    int idx = chunk_add_constant(&c, make_number(42.0));
    chunk_write(&c, OP_CONSTANT, 1);
    chunk_write(&c, (uint8_t)idx, 1);
    chunk_write(&c, OP_NEGATE, 1);
    chunk_write(&c, OP_RETURN, 1);
    char *s = chunk_disassemble_to_string(&c, "bytecode");
    ASSERT(s != NULL, "should return non-NULL string");
    ASSERT(strstr(s, "OP_CONSTANT") != NULL, "should contain OP_CONSTANT");
    ASSERT(strstr(s, "OP_NEGATE") != NULL, "should contain OP_NEGATE");
    ASSERT(strstr(s, "OP_RETURN") != NULL, "should contain OP_RETURN");
    free(s);
    chunk_free(&c);
    PASS();
}

TEST(test_disassemble_to_string_constant_value) {
    /* Verify the returned string shows the constant's value. */
    Chunk c;
    chunk_init(&c);
    int idx = chunk_add_constant(&c, make_number(3.14));
    chunk_write(&c, OP_CONSTANT, 1);
    chunk_write(&c, (uint8_t)idx, 1);
    chunk_write(&c, OP_RETURN, 1);
    char *s = chunk_disassemble_to_string(&c, "constants");
    ASSERT(s != NULL, "should return non-NULL string");
    ASSERT(strstr(s, "3.14") != NULL, "should contain constant value 3.14");
    free(s);
    chunk_free(&c);
    PASS();
}

TEST(test_disassemble_to_string_empty_chunk) {
    /* An empty chunk should still produce the header. */
    Chunk c;
    chunk_init(&c);
    char *s = chunk_disassemble_to_string(&c, "empty");
    ASSERT(s != NULL, "should return non-NULL string");
    ASSERT(strstr(s, "== empty ==") != NULL, "should contain header");
    free(s);
    chunk_free(&c);
    PASS();
}

TEST(test_disassemble_recursive_function) {
    /*
     * When a chunk contains a VAL_FUNCTION constant, the disassembly
     * should recursively include the function's inner chunk.
     */
    Chunk outer;
    chunk_init(&outer);

    /* Build a simple inner function chunk: OP_CONSTANT 42, OP_RETURN */
    Chunk *inner = malloc(sizeof(Chunk));
    chunk_init(inner);
    int inner_idx = chunk_add_constant(inner, make_number(42.0));
    chunk_write(inner, OP_CONSTANT, 1);
    chunk_write(inner, (uint8_t)inner_idx, 1);
    chunk_write(inner, OP_RETURN, 1);

    /* Create an ObjFunction wrapping the inner chunk. */
    ObjFunction *fn = malloc(sizeof(ObjFunction));
    fn->name = strdup("myfunc");
    fn->arity = 0;
    fn->params = NULL;
    fn->chunk = inner;
    fn->native = NULL;

    /* Add the function as a constant in the outer chunk. */
    int fn_idx = chunk_add_constant(&outer, make_function(fn));
    chunk_write(&outer, OP_CONSTANT, 1);
    chunk_write(&outer, (uint8_t)fn_idx, 1);
    chunk_write(&outer, OP_RETURN, 1);

    char *s = chunk_disassemble_to_string(&outer, "script");
    ASSERT(s != NULL, "should return non-NULL string");

    /* Outer chunk header and opcodes should be present. */
    ASSERT(strstr(s, "== script ==") != NULL, "should contain outer header");

    /* Inner function's chunk should be recursively disassembled. */
    ASSERT(strstr(s, "== myfunc ==") != NULL, "should contain inner function header");

    /* The inner chunk's constant (42) should appear in the output. */
    ASSERT(strstr(s, "42") != NULL, "should contain inner constant value");

    free(s);
    chunk_free(&outer);
    PASS();
}

TEST(test_disassemble_recursive_anonymous_function) {
    /*
     * Anonymous functions (name == NULL) should use a fallback label
     * like "<fn>" in their disassembly header.
     */
    Chunk outer;
    chunk_init(&outer);

    Chunk *inner = malloc(sizeof(Chunk));
    chunk_init(inner);
    chunk_write(inner, OP_NOTHING, 1);
    chunk_write(inner, OP_RETURN, 1);

    ObjFunction *fn = malloc(sizeof(ObjFunction));
    fn->name = NULL; /* anonymous */
    fn->arity = 0;
    fn->params = NULL;
    fn->chunk = inner;
    fn->native = NULL;

    int fn_idx = chunk_add_constant(&outer, make_function(fn));
    chunk_write(&outer, OP_CONSTANT, 1);
    chunk_write(&outer, (uint8_t)fn_idx, 1);
    chunk_write(&outer, OP_RETURN, 1);

    char *s = chunk_disassemble_to_string(&outer, "script");
    ASSERT(s != NULL, "should return non-NULL string");

    /* Should use a fallback label for anonymous functions. */
    ASSERT(strstr(s, "== <fn> ==") != NULL, "should contain anonymous function header");

    free(s);
    chunk_free(&outer);
    PASS();
}

/* ============================================================
 * New closure opcode tests (Step 2)
 * ============================================================ */

TEST(test_opcode_names_closure_opcodes) {
    /* Verify opcode_name returns correct strings for the 4 new closure opcodes. */
    ASSERT(strcmp(opcode_name(OP_CLOSURE), "OP_CLOSURE") == 0, "OP_CLOSURE name");
    ASSERT(strcmp(opcode_name(OP_GET_UPVALUE), "OP_GET_UPVALUE") == 0, "OP_GET_UPVALUE name");
    ASSERT(strcmp(opcode_name(OP_SET_UPVALUE), "OP_SET_UPVALUE") == 0, "OP_SET_UPVALUE name");
    ASSERT(strcmp(opcode_name(OP_CLOSE_UPVALUE), "OP_CLOSE_UPVALUE") == 0, "OP_CLOSE_UPVALUE name");
    PASS();
}

TEST(test_disassemble_get_upvalue) {
    /* OP_GET_UPVALUE has a 1-byte upvalue index operand. */
    Chunk c;
    chunk_init(&c);
    chunk_write(&c, OP_GET_UPVALUE, 1);
    chunk_write(&c, 3, 1); /* upvalue index 3 */
    chunk_write(&c, OP_RETURN, 1);
    char *s = chunk_disassemble_to_string(&c, "test");
    ASSERT(s != NULL, "should return non-NULL string");
    ASSERT(strstr(s, "OP_GET_UPVALUE") != NULL, "should contain OP_GET_UPVALUE");
    ASSERT(strstr(s, "3") != NULL, "should contain upvalue index 3");
    free(s);
    chunk_free(&c);
    PASS();
}

TEST(test_disassemble_set_upvalue) {
    /* OP_SET_UPVALUE has a 1-byte upvalue index operand. */
    Chunk c;
    chunk_init(&c);
    chunk_write(&c, OP_SET_UPVALUE, 1);
    chunk_write(&c, 5, 1); /* upvalue index 5 */
    chunk_write(&c, OP_RETURN, 1);
    char *s = chunk_disassemble_to_string(&c, "test");
    ASSERT(s != NULL, "should return non-NULL string");
    ASSERT(strstr(s, "OP_SET_UPVALUE") != NULL, "should contain OP_SET_UPVALUE");
    ASSERT(strstr(s, "5") != NULL, "should contain upvalue index 5");
    free(s);
    chunk_free(&c);
    PASS();
}

TEST(test_disassemble_close_upvalue) {
    /* OP_CLOSE_UPVALUE is a simple instruction with no operand. */
    Chunk c;
    chunk_init(&c);
    chunk_write(&c, OP_CLOSE_UPVALUE, 1);
    chunk_write(&c, OP_RETURN, 1);
    char *s = chunk_disassemble_to_string(&c, "test");
    ASSERT(s != NULL, "should return non-NULL string");
    ASSERT(strstr(s, "OP_CLOSE_UPVALUE") != NULL, "should contain OP_CLOSE_UPVALUE");
    free(s);
    chunk_free(&c);
    PASS();
}

TEST(test_disassemble_closure_no_upvalues) {
    /*
     * OP_CLOSURE with 0 upvalues: [OP_CLOSURE] [const_idx]
     * The function in the constant pool has upvalue_count == 0,
     * so no upvalue descriptor pairs follow.
     */
    Chunk outer;
    chunk_init(&outer);

    /* Build a minimal inner function. */
    Chunk *inner = malloc(sizeof(Chunk));
    chunk_init(inner);
    chunk_write(inner, OP_NOTHING, 1);
    chunk_write(inner, OP_RETURN, 1);

    ObjFunction *fn = gc_alloc(OBJ_FUNCTION, sizeof(ObjFunction));
    fn->name = strdup("my_closure");
    fn->arity = 0;
    fn->params = NULL;
    fn->chunk = inner;
    fn->native = NULL;
    fn->refcount = 1;
    fn->upvalue_count = 0;

    int fn_idx = chunk_add_constant(&outer, make_function(fn));
    chunk_write(&outer, OP_CLOSURE, 1);
    chunk_write(&outer, (uint8_t)fn_idx, 1);
    /* No upvalue descriptors (upvalue_count == 0) */
    chunk_write(&outer, OP_RETURN, 1);

    char *s = chunk_disassemble_to_string(&outer, "test");
    ASSERT(s != NULL, "should return non-NULL string");
    ASSERT(strstr(s, "OP_CLOSURE") != NULL, "should contain OP_CLOSURE");
    ASSERT(strstr(s, "my_closure") != NULL, "should show function name");
    free(s);
    chunk_free(&outer);
    PASS();
}

TEST(test_disassemble_closure_with_upvalues) {
    /*
     * OP_CLOSURE with 2 upvalues:
     * [OP_CLOSURE] [const_idx] [is_local=1, index=0] [is_local=0, index=1]
     * The disassembler should show each upvalue descriptor.
     */
    Chunk outer;
    chunk_init(&outer);

    Chunk *inner = malloc(sizeof(Chunk));
    chunk_init(inner);
    chunk_write(inner, OP_NOTHING, 1);
    chunk_write(inner, OP_RETURN, 1);

    ObjFunction *fn = gc_alloc(OBJ_FUNCTION, sizeof(ObjFunction));
    fn->name = strdup("capturing");
    fn->arity = 0;
    fn->params = NULL;
    fn->chunk = inner;
    fn->native = NULL;
    fn->refcount = 1;
    fn->upvalue_count = 2;

    int fn_idx = chunk_add_constant(&outer, make_function(fn));
    chunk_write(&outer, OP_CLOSURE, 1);
    chunk_write(&outer, (uint8_t)fn_idx, 1);
    /* Upvalue descriptor 0: is_local=1, index=0 */
    chunk_write(&outer, 1, 1);
    chunk_write(&outer, 0, 1);
    /* Upvalue descriptor 1: is_local=0, index=1 */
    chunk_write(&outer, 0, 1);
    chunk_write(&outer, 1, 1);
    chunk_write(&outer, OP_RETURN, 1);

    char *s = chunk_disassemble_to_string(&outer, "test");
    ASSERT(s != NULL, "should return non-NULL string");
    ASSERT(strstr(s, "OP_CLOSURE") != NULL, "should contain OP_CLOSURE");
    ASSERT(strstr(s, "capturing") != NULL, "should show function name");
    /* Should show upvalue descriptors: local and upvalue indicators */
    ASSERT(strstr(s, "local") != NULL, "should show 'local' for is_local=1 upvalue");
    ASSERT(strstr(s, "upvalue") != NULL, "should show 'upvalue' for is_local=0 upvalue");
    free(s);
    chunk_free(&outer);
    PASS();
}

/* ============================================================
 * ObjUpvalue and ObjClosure tests
 * ============================================================ */

TEST(test_obj_upvalue_new) {
    /* Create an ObjUpvalue pointing to a stack value and verify location. */
    Value slot = make_number(99.0);
    ObjUpvalue *uv = obj_upvalue_new(&slot);
    ASSERT(uv != NULL, "upvalue should be allocated");
    ASSERT(uv->refcount == 1, "initial refcount should be 1");
    ASSERT(uv->location == &slot, "location should point to the slot");
    ASSERT(uv->location->number == 99.0, "should read value through location");
    ASSERT(uv->next == NULL, "next should be NULL");
    obj_upvalue_free(uv);
    value_free(&slot);
    PASS();
}

TEST(test_obj_closure_new) {
    /* Create an ObjClosure wrapping a simple ObjFunction. */
    ObjFunction *fn = gc_alloc(OBJ_FUNCTION, sizeof(ObjFunction));
    fn->name = strdup("test_fn");
    fn->arity = 0;
    fn->params = NULL;
    fn->chunk = NULL;
    fn->native = NULL;
    fn->refcount = 1;
    fn->upvalue_count = 0;

    /* Mark fn so it survives GC triggered by obj_closure_new's
     * gc_alloc call (especially in GC_STRESS mode). Without this,
     * fn has no GC roots and would be swept immediately. */
    fn->obj.is_marked = true;

    ObjClosure *cl = obj_closure_new(fn, 0);
    ASSERT(cl != NULL, "closure should be allocated");
    ASSERT(cl->refcount == 1, "closure initial refcount should be 1");
    ASSERT(cl->function == fn, "closure should reference the function");
    ASSERT(cl->upvalue_count == 0, "upvalue_count should be 0");
    /* obj_closure_new increments fn->refcount */
    ASSERT(fn->refcount == 2, "function refcount should be 2 after closure creation");

    obj_closure_free(cl);
    /* After closure free, fn refcount should be decremented back to 1 */
    ASSERT(fn->refcount == 1, "function refcount should be 1 after closure free");
    obj_function_free(fn);
    PASS();
}

TEST(test_closure_value_format_named) {
    /* value_format for VAL_CLOSURE with a named function shows "<fn name>". */
    ObjFunction *fn = gc_alloc(OBJ_FUNCTION, sizeof(ObjFunction));
    fn->name = strdup("greet");
    fn->arity = 0;
    fn->params = NULL;
    fn->chunk = NULL;
    fn->native = NULL;
    fn->refcount = 1;
    fn->upvalue_count = 0;

    /* Protect fn from GC during obj_closure_new (see test_obj_closure_new). */
    fn->obj.is_marked = true;
    ObjClosure *cl = obj_closure_new(fn, 0);
    Value v = make_closure(cl);
    char *fmt = value_format(&v);
    ASSERT(fmt != NULL, "format should return non-NULL");
    ASSERT(strcmp(fmt, "<fn greet>") == 0, "should format as <fn greet>");
    free(fmt);
    value_free(&v);
    /* value_free freed the closure, decrementing fn->refcount from 2 to 1.
     * We still hold the test's reference, so free it explicitly. */
    obj_function_free(fn);
    PASS();
}

TEST(test_closure_value_format_anonymous) {
    /* value_format for VAL_CLOSURE with anonymous function shows "<fn>". */
    ObjFunction *fn = gc_alloc(OBJ_FUNCTION, sizeof(ObjFunction));
    fn->name = NULL;
    fn->arity = 0;
    fn->params = NULL;
    fn->chunk = NULL;
    fn->native = NULL;
    fn->refcount = 1;
    fn->upvalue_count = 0;

    /* Protect fn from GC during obj_closure_new (see test_obj_closure_new). */
    fn->obj.is_marked = true;
    ObjClosure *cl = obj_closure_new(fn, 0);
    Value v = make_closure(cl);
    char *fmt = value_format(&v);
    ASSERT(fmt != NULL, "format should return non-NULL");
    ASSERT(strcmp(fmt, "<fn>") == 0, "should format as <fn>");
    free(fmt);
    value_free(&v);
    /* value_free freed the closure, decrementing fn->refcount from 2 to 1.
     * We still hold the test's reference, so free it explicitly. */
    obj_function_free(fn);
    PASS();
}

TEST(test_closure_clone_refcount) {
    /* Clone a VAL_CLOSURE, verify refcount is 2. Free clone, verify refcount is 1. */
    ObjFunction *fn = gc_alloc(OBJ_FUNCTION, sizeof(ObjFunction));
    fn->name = strdup("cloned_fn");
    fn->arity = 0;
    fn->params = NULL;
    fn->chunk = NULL;
    fn->native = NULL;
    fn->refcount = 1;
    fn->upvalue_count = 0;

    /* Protect fn from GC during obj_closure_new (see test_obj_closure_new). */
    fn->obj.is_marked = true;
    ObjClosure *cl = obj_closure_new(fn, 0);
    Value v = make_closure(cl);
    ASSERT(cl->refcount == 1, "initial closure refcount should be 1");

    Value cloned;
    bool ok = value_clone(&cloned, &v);
    ASSERT(ok, "clone should succeed");
    ASSERT(cloned.type == VAL_CLOSURE, "cloned type should be VAL_CLOSURE");
    ASSERT(cloned.closure == cl, "cloned should share same ObjClosure");
    ASSERT(cl->refcount == 2, "refcount should be 2 after clone");

    value_free(&cloned);
    ASSERT(cl->refcount == 1, "refcount should be 1 after freeing clone");

    value_free(&v);
    /* value_free freed the closure, decrementing fn->refcount from 2 to 1.
     * We still hold the test's reference, so free it explicitly. */
    obj_function_free(fn);
    PASS();
}

TEST(test_closure_is_truthy) {
    /* Closures are always truthy. */
    ObjFunction *fn = gc_alloc(OBJ_FUNCTION, sizeof(ObjFunction));
    fn->name = NULL;
    fn->arity = 0;
    fn->params = NULL;
    fn->chunk = NULL;
    fn->native = NULL;
    fn->refcount = 1;
    fn->upvalue_count = 0;

    /* Protect fn from GC during obj_closure_new (see test_obj_closure_new). */
    fn->obj.is_marked = true;
    ObjClosure *cl = obj_closure_new(fn, 0);
    Value v = make_closure(cl);
    ASSERT(is_truthy(&v) == true, "closure should be truthy");
    value_free(&v);
    /* value_free freed the closure, decrementing fn->refcount from 2 to 1.
     * We still hold the test's reference, so free it explicitly. */
    obj_function_free(fn);
    PASS();
}

TEST(test_function_refcount_clone) {
    /* VAL_FUNCTION clone should increment refcount (not deep-copy). */
    ObjFunction *fn = gc_alloc(OBJ_FUNCTION, sizeof(ObjFunction));
    fn->name = strdup("native_fn");
    fn->arity = 0;
    fn->params = NULL;
    fn->chunk = NULL;
    fn->native = NULL;
    fn->refcount = 1;
    fn->upvalue_count = 0;

    Value v = make_function(fn);
    ASSERT(fn->refcount == 1, "initial refcount should be 1");

    Value cloned;
    bool ok = value_clone(&cloned, &v);
    ASSERT(ok, "clone should succeed");
    ASSERT(fn->refcount == 2, "refcount should be 2 after clone");

    value_free(&cloned);
    ASSERT(fn->refcount == 1, "refcount should be 1 after freeing clone");

    value_free(&v);
    PASS();
}

/* ============================================================
 * OP_ZIP_MAP tests
 * ============================================================ */

TEST(test_opcode_name_zip_map) {
    /* Verify opcode_name returns correct string for OP_ZIP_MAP. */
    ASSERT(strcmp(opcode_name(OP_ZIP_MAP), "OP_ZIP_MAP") == 0, "OP_ZIP_MAP name");
    PASS();
}

TEST(test_disassemble_zip_map) {
    /* OP_ZIP_MAP is a simple (no-operand) instruction. */
    Chunk c;
    chunk_init(&c);
    chunk_write(&c, OP_ZIP_MAP, 1);
    chunk_write(&c, OP_RETURN, 1);
    char *s = chunk_disassemble_to_string(&c, "test");
    ASSERT(s != NULL, "should return non-NULL string");
    ASSERT(strstr(s, "OP_ZIP_MAP") != NULL, "should contain OP_ZIP_MAP");
    free(s);
    chunk_free(&c);
    PASS();
}

/* ============================================================
 * Main
 * ============================================================ */

int main(void) {
    /* Initialize GC state so gc_alloc() has a proper threshold and
     * doesn't trigger premature collections during non-GC tests. */
    gc_init();

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

    printf("\nDisassemble to string:\n");
    RUN_TEST(test_disassemble_to_string_header);
    RUN_TEST(test_disassemble_to_string_opcodes);
    RUN_TEST(test_disassemble_to_string_constant_value);
    RUN_TEST(test_disassemble_to_string_empty_chunk);

    printf("\nRecursive function disassembly:\n");
    RUN_TEST(test_disassemble_recursive_function);
    RUN_TEST(test_disassemble_recursive_anonymous_function);

    printf("\nClosure opcodes:\n");
    RUN_TEST(test_opcode_names_closure_opcodes);
    RUN_TEST(test_disassemble_get_upvalue);
    RUN_TEST(test_disassemble_set_upvalue);
    RUN_TEST(test_disassemble_close_upvalue);
    RUN_TEST(test_disassemble_closure_no_upvalues);
    RUN_TEST(test_disassemble_closure_with_upvalues);

    printf("\nObjUpvalue and ObjClosure:\n");
    RUN_TEST(test_obj_upvalue_new);
    RUN_TEST(test_obj_closure_new);
    RUN_TEST(test_closure_value_format_named);
    RUN_TEST(test_closure_value_format_anonymous);
    RUN_TEST(test_closure_clone_refcount);
    RUN_TEST(test_closure_is_truthy);
    RUN_TEST(test_function_refcount_clone);

    printf("\nOP_ZIP_MAP:\n");
    RUN_TEST(test_opcode_name_zip_map);
    RUN_TEST(test_disassemble_zip_map);

    printf("\n========================================\n");
    printf("Tests run: %d\n", tests_run);
    printf("Passed:    %d\n", tests_passed);
    printf("Failed:    %d\n", tests_failed);
    printf("========================================\n");

    /* Free all remaining GC-tracked objects to prevent LSan leak reports. */
    gc_free_all();

    return tests_failed > 0 ? 1 : 0;
}
