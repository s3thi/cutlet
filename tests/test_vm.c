/*
 * test_vm.c - Tests for the Cutlet bytecode VM
 *
 * Rewrites all test_eval.c assertions to use the parse→compile→execute
 * pipeline. Same input/output expectations as the tree-walking evaluator.
 */

#include "../src/compiler.h"
#include "../src/parser.h"
#include "../src/runtime.h"
#include "../src/value.h"
#include "../src/vm.h"
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* No-op write callback for tests that don't need output capture. */
static void test_write_noop(void *userdata, const char *data, size_t len) {
    (void)userdata;
    (void)data;
    (void)len;
}

static EvalContext test_ctx = {.write_fn = test_write_noop, .userdata = NULL};

/* ============================================================
 * Buffer-capturing EvalContext for say() tests
 * ============================================================ */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} TestBuffer;

static void test_buffer_init(TestBuffer *buf) {
    buf->data = malloc(256);
    buf->data[0] = '\0';
    buf->len = 0;
    buf->cap = 256;
}

static void test_buffer_free(TestBuffer *buf) {
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

static void test_write_capture(void *userdata, const char *data, size_t len) {
    TestBuffer *buf = (TestBuffer *)userdata;
    while (buf->len + len + 1 > buf->cap) {
        buf->cap *= 2;
        char *new_data = realloc(buf->data, buf->cap); // NOLINT(clang-analyzer-unix.Malloc)
        if (!new_data)
            return;
        buf->data = new_data;
    }
    memcpy(buf->data + buf->len, data, len);
    buf->len += len;
    buf->data[buf->len] = '\0';
}

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
            printf("FAIL\n");                                                                      \
            printf("    Assertion failed: %s\n", msg);                                             \
            printf("    At %s:%d\n", __FILE__, __LINE__);                                          \
            tests_failed++;                                                                        \
            return;                                                                                \
        }                                                                                          \
    } while (0)

#define ASSERT_STR_EQ(a, b, msg) ASSERT(strcmp((a), (b)) == 0, msg)

/* Like ASSERT but frees a Value and TestBuffer before returning on failure.
 * Used in say() tests to avoid leaking the capture buffer. */
#define ASSERT_CLEANUP(cond, msg, val, buf)                                                        \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL\n");                                                                      \
            printf("    Assertion failed: %s\n", msg);                                             \
            printf("    At %s:%d\n", __FILE__, __LINE__);                                          \
            tests_failed++;                                                                        \
            value_free(&(val));                                                                    \
            test_buffer_free(&(buf));                                                              \
            return;                                                                                \
        }                                                                                          \
    } while (0)

#define ASSERT_STR_EQ_CLEANUP(a, b, msg, val, buf)                                                 \
    ASSERT_CLEANUP(strcmp((a), (b)) == 0, msg, val, buf)

#define PASS()                                                                                     \
    do {                                                                                           \
        printf("PASS\n");                                                                          \
        tests_passed++;                                                                            \
    } while (0)

/* ============================================================
 * Helper: parse → compile → execute pipeline
 * ============================================================ */

/*
 * Execute input through the full pipeline, returning the result Value.
 * On parse or compile failure, returns a VAL_ERROR.
 */
static Value run_input(const char *input, EvalContext *ctx) {
    AstNode *node = NULL;
    ParseError perr;
    if (!parser_parse(input, &node, &perr)) {
        return make_error("parse: %s", perr.message);
    }
    CompileError cerr;
    Chunk *chunk = compile(node, &cerr);
    ast_free(node);
    if (!chunk) {
        return make_error("compile: %s", cerr.message);
    }
    Value result = vm_execute(chunk, ctx);
    chunk_free(chunk);
    free(chunk);
    return result;
}

/* Helper: run input, check result is a number with expected value. */
static void assert_vm_number(const char *input, double expected, const char *label) {
    Value v = run_input(input, &test_ctx);
    if (v.type != VAL_NUMBER) {
        char *s = value_format(&v);
        printf("FAIL\n    expected number for '%s', got: %s\n", input, s ? s : "(null)");
        free(s);
        value_free(&v);
        tests_failed++;
        return;
    }
    if (fabs(v.number - expected) > 1e-9) {
        printf("FAIL\n    '%s': expected %g, got %g\n", input, expected, v.number);
        value_free(&v);
        tests_failed++;
        return;
    }
    value_free(&v);
    printf("PASS\n");
    tests_passed++;
    (void)label;
}

/* Helper: run input, check result is an error. */
static void assert_vm_error(const char *input, const char *label) {
    Value v = run_input(input, &test_ctx);
    if (v.type != VAL_ERROR) {
        char *s = value_format(&v);
        printf("FAIL\n    expected error for '%s', got: %s\n", input, s ? s : "(null)");
        free(s);
        value_free(&v);
        tests_failed++;
        return;
    }
    value_free(&v);
    printf("PASS\n");
    tests_passed++;
    (void)label;
}

/* Helper: run input, check result is a boolean. */
static void assert_vm_bool(const char *input, bool expected, const char *label) {
    Value v = run_input(input, &test_ctx);
    if (v.type != VAL_BOOL) {
        char *s = value_format(&v);
        printf("FAIL\n    expected bool for '%s', got: %s\n", input, s ? s : "(null)");
        free(s);
        value_free(&v);
        tests_failed++;
        return;
    }
    if (v.boolean != expected) {
        printf("FAIL\n    '%s': expected %s, got %s\n", input, expected ? "true" : "false",
               v.boolean ? "true" : "false");
        value_free(&v);
        tests_failed++;
        return;
    }
    value_free(&v);
    printf("PASS\n");
    tests_passed++;
    (void)label;
}

/* Helper: run input, check result is nothing. */
static void assert_vm_nothing(const char *input, const char *label) {
    Value v = run_input(input, &test_ctx);
    if (v.type != VAL_NOTHING) {
        char *s = value_format(&v);
        printf("FAIL\n    expected nothing for '%s', got: %s\n", input, s ? s : "(null)");
        free(s);
        value_free(&v);
        tests_failed++;
        return;
    }
    value_free(&v);
    printf("PASS\n");
    tests_passed++;
    (void)label;
}

/* Helper: run input, check result is a string with expected value. */
static void assert_vm_string(const char *input, const char *expected, const char *label) {
    Value v = run_input(input, &test_ctx);
    if (v.type != VAL_STRING) {
        char *s = value_format(&v);
        printf("FAIL\n    expected string for '%s', got: %s\n", input, s ? s : "(null)");
        free(s);
        value_free(&v);
        tests_failed++;
        return;
    }
    if (strcmp(v.string, expected) != 0) {
        printf("FAIL\n    '%s': expected \"%s\", got \"%s\"\n", input, expected, v.string);
        value_free(&v);
        tests_failed++;
        return;
    }
    value_free(&v);
    printf("PASS\n");
    tests_passed++;
    (void)label;
}

/*
 * Helper: run input, format the result with value_format(), and compare
 * against expected string. Works for any value type (arrays, nested, etc.).
 */
static void assert_vm_formatted(const char *input, const char *expected, const char *label) {
    Value v = run_input(input, &test_ctx);
    char *s = value_format(&v);
    if (!s) {
        printf("FAIL\n    value_format returned NULL for '%s'\n", input);
        value_free(&v);
        tests_failed++;
        return;
    }
    if (strcmp(s, expected) != 0) {
        printf("FAIL\n    '%s': expected \"%s\", got \"%s\"\n", input, expected, s);
        free(s);
        value_free(&v);
        tests_failed++;
        return;
    }
    free(s);
    value_free(&v);
    printf("PASS\n");
    tests_passed++;
    (void)label;
}

/* Helper: run input, verify the result is NOT an error (any other type is OK). */
static void assert_vm_not_error(const char *input, const char *label) {
    Value v = run_input(input, &test_ctx);
    if (v.type == VAL_ERROR) {
        printf("FAIL\n    expected no error for '%s', got error\n", input);
        value_free(&v);
        tests_failed++;
        return;
    }
    value_free(&v);
    printf("PASS\n");
    tests_passed++;
    (void)label;
}

/* ============================================================
 * Basic arithmetic
 * ============================================================ */

TEST(test_add) { assert_vm_number("1 + 2", 3.0, "1+2=3"); }
TEST(test_sub) { assert_vm_number("10 - 3", 7.0, "10-3=7"); }
TEST(test_mul) { assert_vm_number("2 * 3", 6.0, "2*3=6"); }
TEST(test_div_exact) { assert_vm_number("6 / 3", 2.0, "6/3=2"); }
TEST(test_div_float) { assert_vm_number("7 / 2", 3.5, "7/2=3.5"); }
TEST(test_div_float2) { assert_vm_number("42 / 5", 8.4, "42/5=8.4"); }

/* ============================================================
 * Exponentiation
 * ============================================================ */

TEST(test_exp_basic) { assert_vm_number("2 ** 10", 1024.0, "2**10=1024"); }
TEST(test_exp_right_assoc) { assert_vm_number("2 ** 3 ** 2", 512.0, "right-assoc exponent"); }
TEST(test_exp_zero) { assert_vm_number("5 ** 0", 1.0, "x**0=1"); }

/* ============================================================
 * Unary minus
 * ============================================================ */

TEST(test_unary_neg) { assert_vm_number("-3", -3.0, "unary -3"); }
TEST(test_unary_double_neg) { assert_vm_number("-(-3)", 3.0, "double neg"); }
TEST(test_unary_neg_in_expr) { assert_vm_number("1 + -2", -1.0, "1 + -2"); }

/* ============================================================
 * Precedence
 * ============================================================ */

TEST(test_precedence_add_mul) { assert_vm_number("1 + 2 * 3", 7.0, "1+2*3=7"); }
TEST(test_precedence_parens) { assert_vm_number("(1 + 2) * 3", 9.0, "(1+2)*3=9"); }
TEST(test_precedence_complex) { assert_vm_number("2 + 3 * 4 - 1", 13.0, "2+3*4-1=13"); }

/* ============================================================
 * Division by zero
 * ============================================================ */

TEST(test_div_by_zero) { assert_vm_error("1 / 0", "div by zero"); }

/* ============================================================
 * Unknown identifier
 * ============================================================ */

TEST(test_unknown_ident) { assert_vm_error("x_unknown_vm", "unknown ident"); }

/* ============================================================
 * String literal
 * ============================================================ */

TEST(test_string_value) {
    Value v = run_input("\"hello\"", &test_ctx);
    ASSERT(v.type == VAL_STRING, "should be string");
    ASSERT_STR_EQ(v.string, "hello", "string value");
    value_free(&v);
    PASS();
}

/* ============================================================
 * Number formatting via value_format
 * ============================================================ */

TEST(test_format_integer) {
    Value v = {.type = VAL_NUMBER, .number = 8.0, .string = NULL};
    char *s = value_format(&v);
    ASSERT(s != NULL, "format not null");
    ASSERT_STR_EQ(s, "8", "integer prints without decimal");
    free(s);
    PASS();
}

TEST(test_format_float) {
    Value v = {.type = VAL_NUMBER, .number = 8.4, .string = NULL};
    char *s = value_format(&v);
    ASSERT(s != NULL, "format not null");
    ASSERT_STR_EQ(s, "8.4", "float prints with minimal decimals");
    free(s);
    PASS();
}

TEST(test_format_negative) {
    Value v = {.type = VAL_NUMBER, .number = -3.0, .string = NULL};
    char *s = value_format(&v);
    ASSERT(s != NULL, "format not null");
    ASSERT_STR_EQ(s, "-3", "negative integer");
    free(s);
    PASS();
}

TEST(test_format_string_val) {
    Value v = {.type = VAL_STRING, .number = 0, .string = strdup("hi")};
    char *s = value_format(&v);
    ASSERT(s != NULL, "format not null");
    ASSERT_STR_EQ(s, "hi", "string format");
    free(s);
    value_free(&v);
    PASS();
}

TEST(test_format_error_val) {
    Value v = {.type = VAL_ERROR, .number = 0, .string = strdup("bad")};
    char *s = value_format(&v);
    ASSERT(s != NULL, "format not null");
    ASSERT_STR_EQ(s, "ERR bad", "error format");
    free(s);
    value_free(&v);
    PASS();
}

/* ============================================================
 * Boolean literals
 * ============================================================ */

TEST(test_bool_true_eval) { assert_vm_bool("true", true, "true literal"); }
TEST(test_bool_false_eval) { assert_vm_bool("false", false, "false literal"); }

TEST(test_bool_true_format) {
    Value v = {.type = VAL_BOOL, .boolean = true, .string = NULL};
    char *s = value_format(&v);
    ASSERT(s != NULL, "format not null");
    ASSERT_STR_EQ(s, "true", "true formats as 'true'");
    free(s);
    PASS();
}

TEST(test_bool_false_format) {
    Value v = {.type = VAL_BOOL, .boolean = false, .string = NULL};
    char *s = value_format(&v);
    ASSERT(s != NULL, "format not null");
    ASSERT_STR_EQ(s, "false", "false formats as 'false'");
    free(s);
    PASS();
}

TEST(test_bool_true_assign_error) { assert_vm_error("true = 1", "cannot assign to true"); }
TEST(test_bool_true_decl_error) { assert_vm_error("my true = 1", "cannot declare true"); }

/* ============================================================
 * Comparison operators
 * ============================================================ */

TEST(test_cmp_num_eq_true) { assert_vm_bool("1 == 1", true, "1==1"); }
TEST(test_cmp_num_eq_false) { assert_vm_bool("1 == 2", false, "1==2"); }
TEST(test_cmp_str_eq_true) { assert_vm_bool("\"a\" == \"a\"", true, "a==a"); }
TEST(test_cmp_str_eq_false) { assert_vm_bool("\"a\" == \"b\"", false, "a==b"); }
TEST(test_cmp_bool_eq_true) { assert_vm_bool("true == true", true, "true==true"); }
TEST(test_cmp_bool_eq_false) { assert_vm_bool("true == false", false, "true==false"); }
TEST(test_cmp_mixed_eq) { assert_vm_bool("1 == \"1\"", false, "1==\"1\" mixed"); }

TEST(test_cmp_num_neq_true) { assert_vm_bool("1 != 2", true, "1!=2"); }
TEST(test_cmp_num_neq_false) { assert_vm_bool("1 != 1", false, "1!=1"); }
TEST(test_cmp_mixed_neq) { assert_vm_bool("1 != \"1\"", true, "1!=\"1\" mixed"); }

TEST(test_cmp_lt_true) { assert_vm_bool("1 < 2", true, "1<2"); }
TEST(test_cmp_lt_false) { assert_vm_bool("2 < 1", false, "2<1"); }
TEST(test_cmp_lt_equal) { assert_vm_bool("1 < 1", false, "1<1"); }
TEST(test_cmp_str_lt) { assert_vm_bool("\"a\" < \"b\"", true, "a<b"); }

TEST(test_cmp_gt_true) { assert_vm_bool("2 > 1", true, "2>1"); }
TEST(test_cmp_gt_false) { assert_vm_bool("1 > 2", false, "1>2"); }

TEST(test_cmp_lte_lt) { assert_vm_bool("1 <= 2", true, "1<=2"); }
TEST(test_cmp_lte_eq) { assert_vm_bool("1 <= 1", true, "1<=1"); }
TEST(test_cmp_lte_gt) { assert_vm_bool("2 <= 1", false, "2<=1"); }

TEST(test_cmp_gte_gt) { assert_vm_bool("2 >= 1", true, "2>=1"); }
TEST(test_cmp_gte_eq) { assert_vm_bool("1 >= 1", true, "1>=1"); }
TEST(test_cmp_gte_lt) { assert_vm_bool("1 >= 2", false, "1>=2"); }

TEST(test_cmp_mixed_lt_error) { assert_vm_error("1 < \"1\"", "mixed type ordered cmp"); }
TEST(test_cmp_mixed_gt_error) { assert_vm_error("1 > \"1\"", "mixed type ordered cmp"); }
TEST(test_cmp_mixed_lte_error) { assert_vm_error("1 <= \"1\"", "mixed type ordered cmp"); }
TEST(test_cmp_mixed_gte_error) { assert_vm_error("1 >= \"1\"", "mixed type ordered cmp"); }

TEST(test_cmp_bool_lt_error) { assert_vm_error("true < false", "bool ordered cmp"); }
TEST(test_cmp_bool_gt_error) { assert_vm_error("true > false", "bool ordered cmp"); }
TEST(test_cmp_bool_lte_error) { assert_vm_error("true <= false", "bool ordered cmp"); }
TEST(test_cmp_bool_gte_error) { assert_vm_error("true >= false", "bool ordered cmp"); }

TEST(test_cmp_precedence_add) { assert_vm_bool("1 + 2 == 3", true, "1+2==3"); }

TEST(test_cmp_str_gt) { assert_vm_bool("\"b\" > \"a\"", true, "b>a"); }
TEST(test_cmp_str_lte) { assert_vm_bool("\"a\" <= \"a\"", true, "a<=a"); }
TEST(test_cmp_str_gte) { assert_vm_bool("\"b\" >= \"a\"", true, "b>=a"); }

/* ============================================================
 * Logical operators
 * ============================================================ */

TEST(test_logic_and_tt) { assert_vm_bool("true and true", true, "true and true"); }
TEST(test_logic_and_tf) { assert_vm_bool("true and false", false, "true and false"); }
TEST(test_logic_and_ft) { assert_vm_bool("false and true", false, "false and true"); }
TEST(test_logic_and_ff) { assert_vm_bool("false and false", false, "false and false"); }

TEST(test_logic_or_tt) { assert_vm_bool("true or false", true, "true or false"); }
TEST(test_logic_or_ft) { assert_vm_bool("false or true", true, "false or true"); }
TEST(test_logic_or_ff) { assert_vm_bool("false or false", false, "false or false"); }

TEST(test_logic_not_true) { assert_vm_bool("not true", false, "not true"); }
TEST(test_logic_not_false) { assert_vm_bool("not false", true, "not false"); }

TEST(test_logic_not_zero) { assert_vm_bool("not 0", true, "not 0"); }
TEST(test_logic_not_one) { assert_vm_bool("not 1", false, "not 1"); }
TEST(test_logic_not_empty_str) { assert_vm_bool("not \"\"", true, "not empty string"); }
TEST(test_logic_not_nonempty_str) { assert_vm_bool("not \"hi\"", false, "not nonempty string"); }

TEST(test_logic_and_numbers) { assert_vm_number("1 and 2", 2.0, "1 and 2 → 2"); }
TEST(test_logic_and_zero_short) { assert_vm_number("0 and 2", 0.0, "0 and 2 → 0"); }
TEST(test_logic_or_numbers) { assert_vm_number("0 or 2", 2.0, "0 or 2 → 2"); }
TEST(test_logic_or_falsy_last) { assert_vm_bool("0 or false", false, "0 or false → false"); }

TEST(test_logic_prec_or_and) { assert_vm_bool("true or true and false", true, "or/and prec"); }
TEST(test_logic_not_lt) { assert_vm_bool("not 1 < 2", false, "not 1 < 2"); }
TEST(test_logic_not_and) { assert_vm_bool("not true and false", false, "not true and false"); }

TEST(test_logic_short_circuit_and) {
    /* false and (my x_sc_vm = 1) — x should not be defined */
    Value v = run_input("false and (my x_sc_vm = 1)", &test_ctx);
    ASSERT(v.type == VAL_BOOL, "should be bool");
    ASSERT(v.boolean == false, "should be false");
    value_free(&v);

    /* x_sc_vm should not be defined */
    Value v2 = run_input("x_sc_vm", &test_ctx);
    ASSERT(v2.type == VAL_ERROR, "x_sc_vm should not be defined");
    value_free(&v2);
    PASS();
}

TEST(test_logic_short_circuit_or) {
    Value v = run_input("true or (my y_sc_vm = 1)", &test_ctx);
    ASSERT(v.type == VAL_BOOL, "should be bool");
    ASSERT(v.boolean == true, "should be true");
    value_free(&v);

    Value v2 = run_input("y_sc_vm", &test_ctx);
    ASSERT(v2.type == VAL_ERROR, "y_sc_vm should not be defined");
    value_free(&v2);
    PASS();
}

TEST(test_logic_and_assign_error) { assert_vm_error("and = 1", "and assign"); }
TEST(test_logic_or_assign_error) { assert_vm_error("or = 1", "or assign"); }
TEST(test_logic_not_assign_error) { assert_vm_error("not = 1", "not assign"); }
TEST(test_logic_and_decl_error) { assert_vm_error("my and = 1", "my and"); }
TEST(test_logic_or_decl_error) { assert_vm_error("my or = 1", "my or"); }
TEST(test_logic_not_decl_error) { assert_vm_error("my not = 1", "my not"); }

/* ============================================================
 * Nothing literal
 * ============================================================ */

TEST(test_nothing_eval) { assert_vm_nothing("nothing", "nothing literal"); }
TEST(test_nothing_eq_nothing) { assert_vm_bool("nothing == nothing", true, "nothing == nothing"); }
TEST(test_nothing_eq_false) { assert_vm_bool("nothing == false", false, "nothing == false"); }
TEST(test_nothing_eq_zero) { assert_vm_bool("nothing == 0", false, "nothing == 0"); }
TEST(test_nothing_eq_empty_str) {
    assert_vm_bool("nothing == \"\"", false, "nothing == empty string");
}
TEST(test_nothing_neq_one) { assert_vm_bool("nothing != 1", true, "nothing != 1"); }
TEST(test_nothing_lt_error) { assert_vm_error("nothing < 1", "nothing ordered cmp"); }
TEST(test_nothing_gt_error) { assert_vm_error("nothing > 1", "nothing ordered cmp"); }
TEST(test_nothing_lte_error) { assert_vm_error("nothing <= 1", "nothing ordered cmp"); }
TEST(test_nothing_gte_error) { assert_vm_error("nothing >= 1", "nothing ordered cmp"); }
TEST(test_not_nothing) { assert_vm_bool("not nothing", true, "not nothing"); }
TEST(test_nothing_and_one) { assert_vm_nothing("nothing and 1", "nothing and 1"); }
TEST(test_nothing_or_one) { assert_vm_number("nothing or 1", 1.0, "nothing or 1"); }

TEST(test_nothing_format) {
    Value v = {.type = VAL_NOTHING, .number = 0, .string = NULL};
    char *s = value_format(&v);
    ASSERT(s != NULL, "format not null");
    ASSERT_STR_EQ(s, "nothing", "nothing formats as 'nothing'");
    free(s);
    PASS();
}

TEST(test_nothing_assign_error) { assert_vm_error("nothing = 1", "cannot assign to nothing"); }
TEST(test_nothing_decl_error) { assert_vm_error("my nothing = 1", "cannot declare nothing"); }

/* ============================================================
 * Multi-line input / block evaluation
 * ============================================================ */

TEST(test_block_returns_last) { assert_vm_number("1\n2\n3", 3.0, "block returns last"); }

TEST(test_block_decl_then_use) { assert_vm_number("my xb1 = 1\nxb1 + 2", 3.0, "decl then use"); }

TEST(test_block_multiple_decls) {
    assert_vm_number("my xb2 = 1\nmy yb2 = 2\nxb2 + yb2", 3.0, "multiple decls");
}

TEST(test_block_reassign) {
    assert_vm_number("my xb3 = 1\nxb3 = 5\nxb3", 5.0, "reassign in block");
}

TEST(test_block_blank_lines) { assert_vm_number("\n\n1 + 2\n\n", 3.0, "blank lines"); }

TEST(test_block_with_comparison) {
    assert_vm_bool("my xb4 = 10\nxb4 > 5", true, "block with comparison");
}

TEST(test_block_with_logic) {
    assert_vm_bool("my xb5 = true\nmy yb5 = false\nxb5 and not yb5", true, "block with logic");
}

TEST(test_block_single_expr_not_wrapped) { assert_vm_number("42", 42.0, "single expr"); }

/* ============================================================
 * If/else expression evaluation
 * ============================================================ */

TEST(test_if_true_then_else) { assert_vm_number("if true then 1 else 2 end", 1.0, "if true"); }

TEST(test_if_false_then_else) { assert_vm_number("if false then 1 else 2 end", 2.0, "if false"); }

TEST(test_if_false_no_else) {
    assert_vm_nothing("if false then 1 end", "if false no else → nothing");
}

TEST(test_if_true_no_else) { assert_vm_number("if true then 42 end", 42.0, "if true no else"); }

TEST(test_if_comparison_cond) {
    Value v = run_input("if 1 < 2 then \"yes\" else \"no\" end", &test_ctx);
    ASSERT(v.type == VAL_STRING, "should be string");
    ASSERT_STR_EQ(v.string, "yes", "should be yes");
    value_free(&v);
    PASS();
}

TEST(test_if_comparison_cond_false) {
    Value v = run_input("if 2 < 1 then \"yes\" else \"no\" end", &test_ctx);
    ASSERT(v.type == VAL_STRING, "should be string");
    ASSERT_STR_EQ(v.string, "no", "should be no");
    value_free(&v);
    PASS();
}

TEST(test_if_in_assignment) {
    assert_vm_number("my x_ifv = if true then 42 else 0 end\nx_ifv", 42.0, "if in assignment");
}

TEST(test_if_multiline_body) {
    assert_vm_number("if true then\nmy x_mlv = 1\nx_mlv + 1\nelse\n0\nend", 2.0, "multiline body");
}

TEST(test_if_nested_inner_taken) {
    assert_vm_number("if true then if false then 1 else 2 end else 3 end", 2.0, "nested if");
}

TEST(test_if_nested_outer_false) {
    assert_vm_number("if false then if true then 1 else 2 end else 3 end", 3.0,
                     "nested if outer false");
}

TEST(test_if_else_if) {
    assert_vm_number("if false then 1 else if true then 2 else 3 end", 2.0, "else if");
}

TEST(test_if_else_if_chain) {
    assert_vm_number("if false then 1 else if false then 2 else 3 end", 3.0, "else if chain");
}

TEST(test_if_short_circuit_then) {
    Value v = run_input("if false then\nmy x_sctv = 1\nend\nx_sctv", &test_ctx);
    ASSERT(v.type == VAL_ERROR, "x_sctv should not be defined");
    value_free(&v);
    PASS();
}

TEST(test_if_short_circuit_else) {
    Value v = run_input("if true then 1 else my y_scev = 2 end\ny_scev", &test_ctx);
    ASSERT(v.type == VAL_ERROR, "y_scev should not be defined");
    value_free(&v);
    PASS();
}

TEST(test_if_truthy_number) {
    Value v = run_input("if 1 then \"yes\" else \"no\" end", &test_ctx);
    ASSERT(v.type == VAL_STRING, "should be string");
    ASSERT_STR_EQ(v.string, "yes", "1 is truthy");
    value_free(&v);
    PASS();
}

TEST(test_if_falsy_zero) {
    Value v = run_input("if 0 then \"yes\" else \"no\" end", &test_ctx);
    ASSERT(v.type == VAL_STRING, "should be string");
    ASSERT_STR_EQ(v.string, "no", "0 is falsy");
    value_free(&v);
    PASS();
}

TEST(test_if_falsy_empty_string) {
    Value v = run_input("if \"\" then \"yes\" else \"no\" end", &test_ctx);
    ASSERT(v.type == VAL_STRING, "should be string");
    ASSERT_STR_EQ(v.string, "no", "empty string is falsy");
    value_free(&v);
    PASS();
}

TEST(test_if_falsy_nothing) {
    Value v = run_input("if nothing then \"yes\" else \"no\" end", &test_ctx);
    ASSERT(v.type == VAL_STRING, "should be string");
    ASSERT_STR_EQ(v.string, "no", "nothing is falsy");
    value_free(&v);
    PASS();
}

TEST(test_if_in_expression) {
    assert_vm_number("1 + if true then 2 else 3 end", 3.0, "if in expression");
}

TEST(test_if_complex_condition) {
    assert_vm_number("if 1 < 2 and 3 > 0 then 100 else 0 end", 100.0, "complex condition");
}

/* ============================================================
 * say() built-in function
 * ============================================================ */

TEST(test_say_string) {
    TestBuffer buf;
    test_buffer_init(&buf);
    EvalContext ctx = {.write_fn = test_write_capture, .userdata = &buf};
    Value v = run_input("say(\"hello\")", &ctx);
    ASSERT_CLEANUP(v.type == VAL_NOTHING, "say returns nothing", v, buf);
    ASSERT_STR_EQ_CLEANUP(buf.data, "hello\n", "say writes hello\\n", v, buf);
    value_free(&v);
    test_buffer_free(&buf);
    PASS();
}

TEST(test_say_number) {
    TestBuffer buf;
    test_buffer_init(&buf);
    EvalContext ctx = {.write_fn = test_write_capture, .userdata = &buf};
    Value v = run_input("say(42)", &ctx);
    ASSERT_CLEANUP(v.type == VAL_NOTHING, "say returns nothing", v, buf);
    ASSERT_STR_EQ_CLEANUP(buf.data, "42\n", "say writes 42\\n", v, buf);
    value_free(&v);
    test_buffer_free(&buf);
    PASS();
}

TEST(test_say_bool) {
    TestBuffer buf;
    test_buffer_init(&buf);
    EvalContext ctx = {.write_fn = test_write_capture, .userdata = &buf};
    Value v = run_input("say(true)", &ctx);
    ASSERT_CLEANUP(v.type == VAL_NOTHING, "say returns nothing", v, buf);
    ASSERT_STR_EQ_CLEANUP(buf.data, "true\n", "say writes true\\n", v, buf);
    value_free(&v);
    test_buffer_free(&buf);
    PASS();
}

TEST(test_say_nothing) {
    TestBuffer buf;
    test_buffer_init(&buf);
    EvalContext ctx = {.write_fn = test_write_capture, .userdata = &buf};
    Value v = run_input("say(nothing)", &ctx);
    ASSERT_CLEANUP(v.type == VAL_NOTHING, "say returns nothing", v, buf);
    ASSERT_STR_EQ_CLEANUP(buf.data, "nothing\n", "say writes nothing\\n", v, buf);
    value_free(&v);
    test_buffer_free(&buf);
    PASS();
}

TEST(test_say_expression) {
    TestBuffer buf;
    test_buffer_init(&buf);
    EvalContext ctx = {.write_fn = test_write_capture, .userdata = &buf};
    Value v = run_input("say(1 + 2)", &ctx);
    ASSERT_CLEANUP(v.type == VAL_NOTHING, "say returns nothing", v, buf);
    ASSERT_STR_EQ_CLEANUP(buf.data, "3\n", "say writes 3\\n", v, buf);
    value_free(&v);
    test_buffer_free(&buf);
    PASS();
}

TEST(test_say_no_args) {
    Value v = run_input("say()", &test_ctx);
    ASSERT(v.type == VAL_ERROR, "say no args is error");
    char *msg = value_format(&v);
    ASSERT(strstr(msg, "1 argument") != NULL, "error mentions expected arity");
    ASSERT(strstr(msg, "got 0") != NULL, "error mentions actual count");
    free(msg);
    value_free(&v);
    PASS();
}

TEST(test_say_too_many_args) {
    Value v = run_input("say(1, 2)", &test_ctx);
    ASSERT(v.type == VAL_ERROR, "say too many args is error");
    char *msg = value_format(&v);
    ASSERT(strstr(msg, "1 argument") != NULL, "error mentions expected arity");
    ASSERT(strstr(msg, "got 2") != NULL, "error mentions actual count");
    free(msg);
    value_free(&v);
    PASS();
}

TEST(test_call_unknown_function) {
    Value v = run_input("unknown_fn(1)", &test_ctx);
    ASSERT(v.type == VAL_ERROR, "unknown function is error");
    char *msg = value_format(&v);
    ASSERT(strstr(msg, "unknown_fn") != NULL, "error mentions function name");
    free(msg);
    value_free(&v);
    PASS();
}

TEST(test_say_error_propagation) {
    TestBuffer buf;
    test_buffer_init(&buf);
    EvalContext ctx = {.write_fn = test_write_capture, .userdata = &buf};
    Value v = run_input("say(1/0)", &ctx);
    ASSERT_CLEANUP(v.type == VAL_ERROR, "say propagates error", v, buf);
    char *msg = value_format(&v);
    ASSERT_CLEANUP(strstr(msg, "division by zero") != NULL, "propagates division by zero error", v,
                   buf);
    free(msg);
    ASSERT_STR_EQ_CLEANUP(buf.data, "", "no output on error", v, buf);
    value_free(&v);
    test_buffer_free(&buf);
    PASS();
}

TEST(test_say_null_write_fn) {
    EvalContext ctx = {.write_fn = NULL, .userdata = NULL};
    Value v = run_input("say(\"hello\")", &ctx);
    ASSERT(v.type == VAL_ERROR, "say with null write_fn errors");
    char *msg = value_format(&v);
    ASSERT(strstr(msg, "output") != NULL, "error mentions output");
    free(msg);
    value_free(&v);
    PASS();
}

TEST(test_say_returns_nothing_in_expr) {
    TestBuffer buf;
    test_buffer_init(&buf);
    EvalContext ctx = {.write_fn = test_write_capture, .userdata = &buf};
    Value v = run_input("say(\"hi\") == nothing", &ctx);
    ASSERT_CLEANUP(v.type == VAL_BOOL, "comparison returns bool", v, buf);
    ASSERT_CLEANUP(v.boolean == true, "say() == nothing is true", v, buf);
    ASSERT_STR_EQ_CLEANUP(buf.data, "hi\n", "say still writes output", v, buf);
    value_free(&v);
    test_buffer_free(&buf);
    PASS();
}

TEST(test_say_multiple_in_block) {
    TestBuffer buf;
    test_buffer_init(&buf);
    EvalContext ctx = {.write_fn = test_write_capture, .userdata = &buf};
    Value v = run_input("say(\"a\")\nsay(\"b\")\nsay(\"c\")", &ctx);
    ASSERT_CLEANUP(v.type == VAL_NOTHING, "last say returns nothing", v, buf);
    ASSERT_STR_EQ_CLEANUP(buf.data, "a\nb\nc\n", "all say output accumulated", v, buf);
    value_free(&v);
    test_buffer_free(&buf);
    PASS();
}

TEST(test_say_as_variable) { assert_vm_number("my say = 42\nsay", 42.0, "say as variable"); }

/* ============================================================
 * Comment eval tests
 * ============================================================ */

TEST(test_comment_trailing_eval) { assert_vm_number("42 # comment", 42.0, "trailing comment"); }

TEST(test_comment_multiline_eval) {
    assert_vm_number("my x_cmt2 = 1 # set x\nx_cmt2 + 1 # use x", 2.0, "comment in multi-line");
}

/* ============================================================
 * Line number in error messages
 * ============================================================ */

TEST(test_error_line_number) {
    /* "my x = 1\n1 / 0" — the division by zero is on line 2. */
    Value v = run_input("my x = 1\n1 / 0", &test_ctx);
    ASSERT(v.type == VAL_ERROR, "should be error");
    char *msg = value_format(&v);
    ASSERT(strstr(msg, "line 2") != NULL, "error should mention line 2");
    ASSERT(strstr(msg, "division by zero") != NULL, "error should mention division by zero");
    free(msg);
    value_free(&v);
    PASS();
}

/* ============================================================
 * Stack overflow protection
 * ============================================================ */

TEST(test_stack_overflow) {
    /*
     * Directly build bytecode that pushes more than VM_STACK_MAX values.
     * This bypasses the compiler's constant limit. We push OP_TRUE 300 times
     * (no constant pool needed) then OP_RETURN.
     */
    Chunk chunk;
    chunk_init(&chunk);
    for (int i = 0; i < 300; i++) {
        chunk_write(&chunk, OP_TRUE, 1);
    }
    chunk_write(&chunk, OP_RETURN, 1);

    Value result = vm_execute(&chunk, &test_ctx);
    ASSERT(result.type == VAL_ERROR, "should be error on stack overflow");
    char *msg = value_format(&result);
    ASSERT(strstr(msg, "stack overflow") != NULL, "error should mention stack overflow");
    free(msg);
    value_free(&result);
    chunk_free(&chunk);
    PASS();
}

/* ============================================================
 * Stack underflow protection (peek)
 * ============================================================ */

TEST(test_peek_empty_stack) {
    /*
     * Build bytecode that triggers vm_peek on an empty stack.
     * OP_DEFINE_GLOBAL peeks TOS, so emit it with no prior push.
     * We need a constant for the variable name.
     */
    Chunk chunk;
    chunk_init(&chunk);
    int idx = chunk_add_constant(&chunk, make_string(strdup("x")));
    ASSERT(idx >= 0, "constant added");
    chunk_write(&chunk, OP_DEFINE_GLOBAL, 1);
    chunk_write(&chunk, (uint8_t)idx, 1);
    chunk_write(&chunk, OP_RETURN, 1);

    Value result = vm_execute(&chunk, &test_ctx);
    ASSERT(result.type == VAL_ERROR, "should be error on peek underflow");
    char *msg = value_format(&result);
    ASSERT(strstr(msg, "stack underflow") != NULL, "error should mention stack underflow");
    free(msg);
    value_free(&result);
    chunk_free(&chunk);
    PASS();
}

/* ============================================================
 * Stack underflow protection (pop)
 * ============================================================ */

TEST(test_pop_empty_stack) {
    /*
     * Build bytecode that triggers vm_pop on an empty stack.
     * OP_ADD pops two values, so emit it with nothing pushed.
     */
    Chunk chunk;
    chunk_init(&chunk);
    chunk_write(&chunk, OP_ADD, 1);
    chunk_write(&chunk, OP_RETURN, 1);

    Value result = vm_execute(&chunk, &test_ctx);
    ASSERT(result.type == VAL_ERROR, "should be error on pop underflow");
    char *msg = value_format(&result);
    ASSERT(strstr(msg, "stack underflow") != NULL, "error should mention stack underflow");
    free(msg);
    value_free(&result);
    chunk_free(&chunk);
    PASS();
}

/* ============================================================
 * Single number
 * ============================================================ */

TEST(test_single_number) { assert_vm_number("42", 42.0, "single number"); }
TEST(test_single_zero) { assert_vm_number("0", 0.0, "zero"); }

/* ============================================================
 * Modulo operator
 * ============================================================ */

TEST(test_mod_basic) { assert_vm_number("10 % 3", 1.0, "10%3=1"); }
TEST(test_mod_exact) { assert_vm_number("10 % 5", 0.0, "10%5=0"); }
TEST(test_mod_neg_dividend) { assert_vm_number("-7 % 3", 2.0, "-7%3=2 (Python-style)"); }
TEST(test_mod_neg_divisor) { assert_vm_number("7 % -3", -2.0, "7%-3=-2 (Python-style)"); }
TEST(test_mod_both_neg) { assert_vm_number("-7 % -3", -1.0, "-7%-3=-1 (Python-style)"); }
/* Use arithmetic to produce a float since the tokenizer doesn't support decimal literals yet */
TEST(test_mod_float) { assert_vm_number("(21 / 2) % 3", 1.5, "10.5%3=1.5"); }
TEST(test_mod_by_zero) { assert_vm_error("5 % 0", "modulo by zero"); }
TEST(test_mod_string_error) { assert_vm_error("\"hello\" % 3", "arithmetic requires numbers"); }
TEST(test_mod_bool_error) { assert_vm_error("true % 2", "arithmetic requires numbers"); }

/* ============================================================
 * While loop tests
 * ============================================================ */

/* Loop that runs N times: returns last body value */
TEST(test_while_runs_n_times) {
    /* i starts at 0, loop increments i each time. After 5 iterations, i==5. */
    assert_vm_number("my wh_i1 = 0\nwhile wh_i1 < 5 do\n  wh_i1 = wh_i1 + 1\nend", 5.0,
                     "while loop runs N times");
}

/* Loop that never runs: returns nothing */
TEST(test_while_never_runs) {
    assert_vm_nothing("while false do 42 end", "while that never runs returns nothing");
}

/* Last body value is returned */
TEST(test_while_last_body_value) {
    /* Loop runs once (condition is true, body sets x=false to exit). */
    assert_vm_number("my wh_x1 = 0\nwhile wh_x1 == 0 do\n  wh_x1 = 1\n  42\nend", 42.0,
                     "while returns last body value");
}

/* Loop with say() side effects */
TEST(test_while_with_say) {
    TestBuffer buf;
    test_buffer_init(&buf);
    EvalContext ctx = {.write_fn = test_write_capture, .userdata = &buf};
    Value v =
        run_input("my wh_j1 = 0\nwhile wh_j1 < 3 do\n  say(wh_j1)\n  wh_j1 = wh_j1 + 1\nend", &ctx);
    ASSERT_CLEANUP(v.type == VAL_NUMBER, "loop result should be number", v, buf);
    ASSERT_STR_EQ_CLEANUP(buf.data, "0\n1\n2\n", "say output from loop", v, buf);
    value_free(&v);
    test_buffer_free(&buf);
    PASS();
}

/* Nested while loops */
TEST(test_while_nested) {
    assert_vm_number("my wh_outer = 0\nmy wh_sum1 = 0\n"
                     "while wh_outer < 3 do\n"
                     "  my wh_inner = 0\n"
                     "  while wh_inner < 2 do\n"
                     "    wh_sum1 = wh_sum1 + 1\n"
                     "    wh_inner = wh_inner + 1\n"
                     "  end\n"
                     "  wh_outer = wh_outer + 1\n"
                     "end\n"
                     "wh_sum1",
                     6.0, "nested while loops");
}

/* While used as expression in assignment */
TEST(test_while_as_expression) {
    assert_vm_number("my wh_n1 = 0\n"
                     "my wh_result1 = while wh_n1 < 3 do\n"
                     "  wh_n1 = wh_n1 + 1\n"
                     "end\n"
                     "wh_result1",
                     3.0, "while as expression in assignment");
}

/* While loop that never runs, used as expression → nothing */
TEST(test_while_never_runs_expr) {
    assert_vm_nothing("my wh_result2 = while false do 42 end\nwh_result2",
                      "while never runs as expression returns nothing");
}

/* ============================================================
 * Break and continue
 * ============================================================ */

/* break exits the loop immediately — verify via variable state after loop */
TEST(test_break_exits_loop) {
    /* Loop runs until brk_i1 reaches 3, then break. Check variable after loop. */
    assert_vm_number("my brk_i1 = 0\nwhile brk_i1 < 10 do\n  brk_i1 = brk_i1 + 1\n  if brk_i1 == 3 "
                     "then break end\n  brk_i1\nend\nbrk_i1",
                     3.0, "break exits loop at right time");
}

/* break with value: loop evaluates to the break value */
TEST(test_break_with_value) {
    assert_vm_number("while true do break 42 end", 42.0, "break with value returns that value");
}

/* bare break: loop evaluates to nothing */
TEST(test_bare_break_returns_nothing) {
    assert_vm_nothing("while true do break end", "bare break returns nothing");
}

/* break from if inside loop */
TEST(test_break_from_if_inside_loop) {
    assert_vm_number("my brk_i2 = 0\nwhile true do\n  brk_i2 = brk_i2 + 1\n  if brk_i2 == 5 then "
                     "break brk_i2 * 10 end\n  brk_i2\nend",
                     50.0, "break from if inside loop");
}

/* continue skips the rest of the iteration */
TEST(test_continue_skips_iteration) {
    /* Print only odd numbers from 1 to 5 via say(). Last iteration (i=5)
     * doesn't hit continue, so the loop returns 5. */
    TestBuffer buf;
    test_buffer_init(&buf);
    EvalContext ctx = {.write_fn = test_write_capture, .userdata = &buf};
    Value v = run_input("my cnt_i1 = 0\nwhile cnt_i1 < 5 do\n  cnt_i1 = cnt_i1 + 1\n  if cnt_i1 % "
                        "2 == 0 then continue end\n  say(cnt_i1)\n  cnt_i1\nend",
                        &ctx);
    /* say() should have printed 1, 3, 5 */
    ASSERT_CLEANUP(v.type == VAL_NUMBER, "loop returns last non-continue body value", v, buf);
    ASSERT_STR_EQ_CLEANUP(buf.data, "1\n3\n5\n", "continue skips even iterations", v, buf);
    value_free(&v);
    test_buffer_free(&buf);
    PASS();
}

/* continue on last iteration produces nothing as loop value */
TEST(test_continue_last_iteration_nothing) {
    /* Loop runs once, continue is always hit => loop value is nothing */
    assert_vm_nothing("my cnt_i2 = 0\nwhile cnt_i2 < 1 do\n  cnt_i2 = cnt_i2 + 1\n  continue\nend",
                      "continue on every iteration returns nothing");
}

/* Nested loops: break affects innermost only */
TEST(test_nested_break_affects_inner) {
    assert_vm_number("my brk_out = 0\nwhile brk_out < 3 do\n  brk_out = brk_out + 1\n  while true "
                     "do break end\n  brk_out\nend",
                     3.0, "break affects only inner loop");
}

/* Nested loops: continue affects innermost only */
TEST(test_nested_continue_affects_inner) {
    TestBuffer buf;
    test_buffer_init(&buf);
    EvalContext ctx = {.write_fn = test_write_capture, .userdata = &buf};
    Value v = run_input(
        "my cnt_out = 0\nwhile cnt_out < 2 do\n  cnt_out = cnt_out + 1\n  my cnt_in = 0\n  while "
        "cnt_in < 3 do\n    cnt_in = cnt_in + 1\n    if cnt_in == 2 then continue end\n    "
        "say(str(cnt_out) ++ \"-\" ++ str(cnt_in))\n    cnt_in\n  end\n  cnt_out\nend",
        &ctx);
    /* Should print: 1-1, 1-3, 2-1, 2-3 (skipping cnt_in==2 each time) */
    ASSERT_STR_EQ_CLEANUP(buf.data, "1-1\n1-3\n2-1\n2-3\n", "continue affects inner loop only", v,
                          buf);
    value_free(&v);
    test_buffer_free(&buf);
    PASS();
}

/* break outside loop is a compile error */
TEST(test_break_outside_loop_error) { assert_vm_error("break", "break outside loop"); }

/* continue outside loop is a compile error */
TEST(test_continue_outside_loop_error) { assert_vm_error("continue", "continue outside loop"); }

/* break with say() to ensure output happens before break */
TEST(test_break_with_say) {
    TestBuffer buf;
    test_buffer_init(&buf);
    EvalContext ctx = {.write_fn = test_write_capture, .userdata = &buf};
    Value v = run_input("my brk_s = 0\nwhile brk_s < 5 do\n  say(brk_s)\n  brk_s = brk_s + 1\n  if "
                        "brk_s == 3 then break end\n  brk_s\nend",
                        &ctx);
    ASSERT_CLEANUP(v.type == VAL_NOTHING, "break without value returns nothing", v, buf);
    ASSERT_STR_EQ_CLEANUP(buf.data, "0\n1\n2\n", "say output before break", v, buf);
    value_free(&v);
    test_buffer_free(&buf);
    PASS();
}

/* ============================================================
 * String concatenation operator (++)
 * ============================================================ */

/* Basic string concatenation */
TEST(test_concat_strings) {
    assert_vm_string("\"hello\" ++ \" world\"", "hello world", "basic concat");
}
TEST(test_concat_empty_left) { assert_vm_string("\"\" ++ \"a\"", "a", "empty left"); }
TEST(test_concat_empty_right) { assert_vm_string("\"a\" ++ \"\"", "a", "empty right"); }
TEST(test_concat_both_empty) { assert_vm_string("\"\" ++ \"\"", "", "both empty"); }

/* Strict: ++ rejects non-string operands with a runtime error */
TEST(test_concat_str_num) { assert_vm_error("\"x\" ++ 42", "string ++ number"); }
TEST(test_concat_num_str) { assert_vm_error("42 ++ \"x\"", "number ++ string"); }
TEST(test_concat_bool_str) { assert_vm_error("true ++ \"!\"", "bool ++ string"); }
TEST(test_concat_nothing_str) { assert_vm_error("nothing ++ \"x\"", "nothing ++ string"); }
TEST(test_concat_num_num) { assert_vm_error("1 ++ 2", "number ++ number"); }
TEST(test_concat_float_str) { assert_vm_error("(7 / 2) ++ \"x\"", "float ++ string"); }

/* Explicit conversion via str() works with ++ */
TEST(test_concat_str_explicit_num) {
    assert_vm_string("str(\"x\") ++ str(42)", "x42", "explicit str ++ num");
}
TEST(test_concat_str_explicit_bool) {
    assert_vm_string("str(true) ++ \"!\"", "true!", "explicit bool ++ str");
}
TEST(test_concat_str_explicit_nothing) {
    assert_vm_string("str(nothing) ++ \"x\"", "nothingx", "explicit nothing ++ str");
}

/* Chained: right-associative, "a" ++ "b" ++ "c" → "abc" */
TEST(test_concat_chained) { assert_vm_string("\"a\" ++ \"b\" ++ \"c\"", "abc", "chained concat"); }

/* With variables */
TEST(test_concat_with_var) {
    assert_vm_string("my xc = \"hello\"\nxc ++ \" world\"", "hello world", "concat with var");
}

/* Precedence: + binds tighter than ++, so (1+2) ++ (3+4).
 * Use str() to convert the sums to strings for concatenation. */
TEST(test_concat_precedence) {
    assert_vm_string("str(1 + 2) ++ str(3 + 4)", "37", "concat precedence");
}

/* Confirm + with strings still errors */
TEST(test_add_strings_error) { assert_vm_error("\"a\" + \"b\"", "add strings error"); }

/* ============================================================
 * str() built-in
 * ============================================================ */

/* str(42) → "42" */
TEST(test_str_integer) { assert_vm_string("str(42)", "42", "str integer"); }

/* str(3.5) → "3.5" */
TEST(test_str_float) { assert_vm_string("str(7 / 2)", "3.5", "str float"); }

/* str(true) → "true" */
TEST(test_str_true) { assert_vm_string("str(true)", "true", "str true"); }

/* str(false) → "false" */
TEST(test_str_false) { assert_vm_string("str(false)", "false", "str false"); }

/* str(nothing) → "nothing" */
TEST(test_str_nothing) { assert_vm_string("str(nothing)", "nothing", "str nothing"); }

/* str("hello") → "hello" (identity — already a string) */
TEST(test_str_string_identity) {
    assert_vm_string("str(\"hello\")", "hello", "str string identity");
}

/* str(42) ++ str(true) → "42true" (compose with ++) */
TEST(test_str_compose_concat) {
    assert_vm_string("str(42) ++ str(true)", "42true", "str compose concat");
}

/* ============================================================
 * VAL_FUNCTION value type
 * ============================================================ */

/* Helper: create a minimal ObjFunction for testing.
 * Caller gets a fully heap-allocated ObjFunction suitable for
 * make_function() (which takes ownership). */
static ObjFunction *test_make_obj_function(const char *name, int arity, const char **param_names) {
    ObjFunction *fn = gc_alloc(OBJ_FUNCTION, sizeof(ObjFunction));
    fn->name = name ? strdup(name) : NULL;
    fn->arity = arity;
    if (arity > 0 && param_names) {
        fn->params = (char **)calloc((size_t)arity, sizeof(char *));
        for (int i = 0; i < arity; i++) {
            fn->params[i] = strdup(param_names[i]);
        }
    }
    fn->chunk = malloc(sizeof(Chunk));
    chunk_init(fn->chunk);
    fn->native = NULL;
    return fn;
}

/* Format a named function → "<fn foo>" */
TEST(test_fn_format_named) {
    ObjFunction *fn = test_make_obj_function("foo", 0, NULL);
    Value v = make_function(fn);
    char *s = value_format(&v);
    ASSERT(s != NULL, "format not null");
    ASSERT_STR_EQ(s, "<fn foo>", "named function format");
    free(s);
    value_free(&v);
    PASS();
}

/* Format an anonymous function → "<fn>" */
TEST(test_fn_format_anonymous) {
    ObjFunction *fn = test_make_obj_function(NULL, 0, NULL);
    Value v = make_function(fn);
    char *s = value_format(&v);
    ASSERT(s != NULL, "format not null");
    ASSERT_STR_EQ(s, "<fn>", "anonymous function format");
    free(s);
    value_free(&v);
    PASS();
}

/* Clone a function: deep copy with independent ownership */
TEST(test_fn_clone_independence) {
    /* VAL_FUNCTION clone uses shared ownership via refcount (not deep copy). */
    const char *params[] = {"x", "y"};
    ObjFunction *fn = test_make_obj_function("add", 2, params);
    Value orig = make_function(fn);
    ASSERT(fn->refcount == 1, "initial refcount should be 1");
    Value cloned;
    bool ok = value_clone(&cloned, &orig);
    ASSERT(ok, "clone succeeds");
    ASSERT(cloned.type == VAL_FUNCTION, "clone is function");
    ASSERT(cloned.function == orig.function, "clone shares same ObjFunction pointer");
    ASSERT(fn->refcount == 2, "refcount should be 2 after clone");
    ASSERT(strcmp(cloned.function->name, "add") == 0, "clone name correct");
    ASSERT(cloned.function->arity == 2, "clone arity correct");
    ASSERT(strcmp(cloned.function->params[0], "x") == 0, "clone param 0");
    ASSERT(strcmp(cloned.function->params[1], "y") == 0, "clone param 1");
    value_free(&cloned);
    ASSERT(fn->refcount == 1, "refcount should be 1 after freeing clone");
    value_free(&orig);
    PASS();
}

/* Free a function — sanitizers verify no leaks */
TEST(test_fn_free_no_leak) {
    const char *params[] = {"a"};
    ObjFunction *fn = test_make_obj_function("test", 1, params);
    Value v = make_function(fn);
    value_free(&v);
    /* No crash, no leak (sanitizer will catch). */
    PASS();
}

/* Functions are truthy */
TEST(test_fn_is_truthy) {
    ObjFunction *fn = test_make_obj_function("t", 0, NULL);
    Value v = make_function(fn);
    ASSERT(is_truthy(&v) == true, "functions are truthy");
    value_free(&v);
    PASS();
}

/* make_native creates a function Value with correct properties */
TEST(test_native_fn_create) {
    Value v = make_native("say", 1, NULL);
    ASSERT(v.type == VAL_FUNCTION, "is function");
    ASSERT(v.function != NULL, "has ObjFunction");
    ASSERT(strcmp(v.function->name, "say") == 0, "native name");
    ASSERT(v.function->arity == 1, "native arity");
    ASSERT(v.function->chunk == NULL, "native has no chunk");
    value_free(&v);
    PASS();
}

/* make_native formats as "<fn name>" */
TEST(test_native_fn_format) {
    Value v = make_native("say", 1, NULL);
    char *s = value_format(&v);
    ASSERT(s != NULL, "format not null");
    ASSERT_STR_EQ(s, "<fn say>", "native function format");
    free(s);
    value_free(&v);
    PASS();
}

/* ++ now rejects non-string operands — "val: " ++ true is an error.
 * value_format() still works for direct formatting (used by say/str). */
TEST(test_fn_concat_coercion) {
    assert_vm_error("\"val: \" ++ true", "concat rejects bool");
    /* Verify value_format("<fn foo>") still works via direct construction
     * — it is used by say() and str(), not by ++. */
    ObjFunction *fn = test_make_obj_function("foo", 0, NULL);
    Value v = make_function(fn);
    char *s = value_format(&v);
    ASSERT(s != NULL, "format not null");
    ASSERT_STR_EQ(s, "<fn foo>", "fn formats for say/str");
    free(s);
    value_free(&v);
    PASS();
}

/* ============================================================
 * Function definitions (compile + eval)
 * ============================================================ */

/* fn foo() is 42 end → foo is a VAL_FUNCTION in globals */
TEST(test_fn_def_creates_global) {
    Value v = run_input("fn fndef1() is 42 end\nfndef1", &test_ctx);
    ASSERT(v.type == VAL_CLOSURE, "fn def creates a closure value");
    ASSERT(v.closure != NULL, "closure pointer is non-NULL");
    ASSERT(v.closure->function != NULL, "function pointer is non-NULL");
    ASSERT(strcmp(v.closure->function->name, "fndef1") == 0, "function name is fndef1");
    value_free(&v);
    PASS();
}

/* fn foo() is 42 end evaluates to the closure value itself */
TEST(test_fn_def_evaluates_to_function) {
    Value v = run_input("fn fndef2() is 42 end", &test_ctx);
    ASSERT(v.type == VAL_CLOSURE, "fn def expression returns closure");
    char *s = value_format(&v);
    ASSERT(strcmp(s, "<fn fndef2>") == 0, "formats as <fn fndef2>");
    free(s);
    value_free(&v);
    PASS();
}

/* Retrieving a defined function by name formats correctly */
TEST(test_fn_def_format_via_global) {
    /* Define then read back. */
    Value v = run_input("fn fndef3(x, y) is x end\nfndef3", &test_ctx);
    ASSERT(v.type == VAL_CLOSURE, "is closure");
    char *s = value_format(&v);
    ASSERT(strcmp(s, "<fn fndef3>") == 0, "formats as <fn fndef3>");
    free(s);
    value_free(&v);
    PASS();
}

/* ============================================================
 * Stack-based call dispatch (Step 4)
 * ============================================================ */

/* Calling a number produces a runtime error */
TEST(test_call_number_error) {
    Value v = run_input("my call_num = 42\ncall_num()", &test_ctx);
    ASSERT(v.type == VAL_ERROR, "calling a number should error");
    char *msg = value_format(&v);
    ASSERT(strstr(msg, "cannot call") != NULL, "error mentions cannot call");
    free(msg);
    value_free(&v);
    PASS();
}

/* Calling a boolean produces a runtime error */
TEST(test_call_bool_error) {
    Value v = run_input("my call_b = true\ncall_b()", &test_ctx);
    ASSERT(v.type == VAL_ERROR, "calling a boolean should error");
    char *msg = value_format(&v);
    ASSERT(strstr(msg, "cannot call") != NULL, "error mentions cannot call");
    free(msg);
    value_free(&v);
    PASS();
}

/* ============================================================
 * VM call frames: user-defined function call/return (Step 5)
 * ============================================================ */

/* fn foo() is 42 end\nfoo() → 42 */
TEST(test_user_fn_call_returns_value) {
    assert_vm_number("fn uf1() is 42 end\nuf1()", 42.0, "user fn returns body value");
}

/* fn greet() is say("hi") end\ngreet() → prints "hi", returns nothing */
TEST(test_user_fn_call_with_say) {
    TestBuffer buf;
    test_buffer_init(&buf);
    EvalContext ctx = {.write_fn = test_write_capture, .userdata = &buf};
    Value v = run_input("fn uf2() is say(\"hi\") end\nuf2()", &ctx);
    ASSERT_CLEANUP(v.type == VAL_NOTHING, "greet returns nothing", v, buf);
    ASSERT_STR_EQ_CLEANUP(buf.data, "hi\n", "greet prints hi", v, buf);
    value_free(&v);
    test_buffer_free(&buf);
    PASS();
}

/* fn five() is 2 + 3 end\nsay(five()) → prints 5 */
TEST(test_user_fn_call_result_used_in_say) {
    TestBuffer buf;
    test_buffer_init(&buf);
    EvalContext ctx = {.write_fn = test_write_capture, .userdata = &buf};
    Value v = run_input("fn uf3() is 2 + 3 end\nsay(uf3())", &ctx);
    ASSERT_CLEANUP(v.type == VAL_NOTHING, "say returns nothing", v, buf);
    ASSERT_STR_EQ_CLEANUP(buf.data, "5\n", "say prints fn result", v, buf);
    value_free(&v);
    test_buffer_free(&buf);
    PASS();
}

/* ============================================================
 * Function parameters (OP_GET_LOCAL) - Step 6
 * ============================================================ */

/* fn identity(x) is x end\nidentity(42) → 42 */
TEST(test_fn_param_identity) {
    assert_vm_number("fn identity(x) is x end\nidentity(42)", 42.0, "identity function");
}

/* fn add(a, b) is a + b end\nadd(1, 2) → 3 */
TEST(test_fn_param_add) {
    assert_vm_number("fn add(a, b) is a + b end\nadd(1, 2)", 3.0, "two-param function");
}

/* fn shadow(x) is x end\nmy x = 99\nshadow(1) → 1 (local shadows global) */
TEST(test_fn_param_shadows_global) {
    assert_vm_number("fn shadow(x) is x end\nmy x = 99\nshadow(1)", 1.0, "local shadows global");
}

/* fn readglobal() is x end\nmy x = 99\nreadglobal() → 99 (falls back to global) */
TEST(test_fn_param_falls_back_to_global) {
    assert_vm_number("fn readglobal() is x end\nmy x = 99\nreadglobal()", 99.0,
                     "falls back to global");
}

/* ============================================================
 * Local variable declarations in functions (OP_SET_LOCAL) - Step 7
 * ============================================================ */

/* fn foo(x) is my y = x + 1\ny end\nfoo(10) → 11 */
TEST(test_fn_local_decl_and_use) {
    assert_vm_number("fn foo(x) is\nmy y = x + 1\ny\nend\nfoo(10)", 11.0, "local decl in function");
}

/* fn foo() is my a = 1\nmy b = 2\na + b end\nfoo() → 3 */
TEST(test_fn_multiple_locals) {
    assert_vm_number("fn foo() is\nmy a = 1\nmy b = 2\na + b\nend\nfoo()", 3.0,
                     "multiple locals in function");
}

/* fn foo(x) is x = x + 1\nx end\nfoo(10) → 11 (reassign parameter) */
TEST(test_fn_reassign_param) {
    assert_vm_number("fn foo(x) is\nx = x + 1\nx\nend\nfoo(10)", 11.0, "reassign parameter");
}

/* Local variable doesn't leak to global scope */
TEST(test_fn_local_doesnt_leak) {
    Value v = run_input("fn foo() is my loc7 = 42 end\nfoo()\nloc7", &test_ctx);
    ASSERT(v.type == VAL_ERROR, "local should not leak to global scope");
    char *msg = value_format(&v);
    ASSERT(strstr(msg, "loc7") != NULL, "error mentions the local variable name");
    free(msg);
    value_free(&v);
    PASS();
}

/* ============================================================
 * Recursion (Step 8)
 * ============================================================ */

/* Factorial via recursion: factorial(5) = 120 */
TEST(test_recursion_factorial) {
    assert_vm_number("fn factorial(n) is\n"
                     "  if n <= 1 then 1 else n * factorial(n - 1)\n"
                     "end\n"
                     "factorial(5)",
                     120.0, "factorial(5) = 120");
}

/* Fibonacci via recursion: fib(10) = 55 */
TEST(test_recursion_fibonacci) {
    assert_vm_number("fn fib(n) is\n"
                     "  if n < 2 then n else fib(n - 1) + fib(n - 2)\n"
                     "end\n"
                     "fib(10)",
                     55.0, "fib(10) = 55");
}

/* Mutual recursion: is_even calls is_odd and vice versa */
TEST(test_recursion_mutual) {
    assert_vm_bool("fn is_even(n) is\n"
                   "  if n == 0 then true else is_odd(n - 1)\n"
                   "end\n"
                   "fn is_odd(n) is\n"
                   "  if n == 0 then false else is_even(n - 1)\n"
                   "end\n"
                   "is_even(10)",
                   true, "is_even(10) via mutual recursion");
}

/* ============================================================
 * Error handling for user-defined functions (Step 8)
 * ============================================================ */

/* Wrong arity: too few arguments */
TEST(test_user_fn_arity_too_few) {
    Value v = run_input("fn foo(a, b) is a end\nfoo(1)", &test_ctx);
    ASSERT(v.type == VAL_ERROR, "wrong arity should error");
    char *msg = value_format(&v);
    ASSERT(strstr(msg, "'foo'") != NULL, "error mentions function name");
    ASSERT(strstr(msg, "2 arguments") != NULL, "error mentions expected arity");
    ASSERT(strstr(msg, "got 1") != NULL, "error mentions actual count");
    free(msg);
    value_free(&v);
    PASS();
}

/* Wrong arity: too many arguments */
TEST(test_user_fn_arity_too_many) {
    Value v = run_input("fn bar(a) is a end\nbar(1, 2, 3)", &test_ctx);
    ASSERT(v.type == VAL_ERROR, "wrong arity should error");
    char *msg = value_format(&v);
    ASSERT(strstr(msg, "'bar'") != NULL, "error mentions function name");
    ASSERT(strstr(msg, "1 argument") != NULL, "error mentions expected arity");
    ASSERT(strstr(msg, "got 3") != NULL, "error mentions actual count");
    free(msg);
    value_free(&v);
    PASS();
}

/* Zero-arg function called with args */
TEST(test_user_fn_arity_zero_called_with_args) {
    Value v = run_input("fn noargs() is 42 end\nnoargs(1)", &test_ctx);
    ASSERT(v.type == VAL_ERROR, "wrong arity should error");
    char *msg = value_format(&v);
    ASSERT(strstr(msg, "'noargs'") != NULL, "error mentions function name");
    ASSERT(strstr(msg, "0 arguments") != NULL, "error mentions expected arity");
    ASSERT(strstr(msg, "got 1") != NULL, "error mentions actual count");
    free(msg);
    value_free(&v);
    PASS();
}

/* Call stack overflow via infinite recursion */
TEST(test_call_stack_overflow_recursion) {
    Value v = run_input("fn recurse(n) is recurse(n + 1) end\n"
                        "recurse(0)",
                        &test_ctx);
    ASSERT(v.type == VAL_ERROR, "deep recursion should error");
    char *msg = value_format(&v);
    ASSERT(strstr(msg, "stack overflow") != NULL, "error mentions stack overflow");
    free(msg);
    value_free(&v);
    PASS();
}

/* Error inside function body includes line number */
TEST(test_fn_error_has_line_number) {
    Value v = run_input("fn bad() is\n"
                        "  1 / 0\n"
                        "end\n"
                        "bad()",
                        &test_ctx);
    ASSERT(v.type == VAL_ERROR, "should be error");
    char *msg = value_format(&v);
    ASSERT(strstr(msg, "division by zero") != NULL, "error mentions division by zero");
    ASSERT(strstr(msg, "line") != NULL, "error includes line number");
    free(msg);
    value_free(&v);
    PASS();
}

/* Wrong arity error includes line number */
TEST(test_fn_arity_error_has_line_number) {
    Value v = run_input("fn foo(a, b) is a end\n"
                        "foo(1)",
                        &test_ctx);
    ASSERT(v.type == VAL_ERROR, "should be error");
    char *msg = value_format(&v);
    ASSERT(strstr(msg, "line 2") != NULL, "arity error mentions line 2");
    free(msg);
    value_free(&v);
    PASS();
}

/* ============================================================
 * Anonymous functions
 * ============================================================ */

/* Assign anonymous fn to variable, then call it */
TEST(test_anon_fn_assign_and_call) {
    assert_vm_number("my f = fn(x) is x + 1 end\nf(5)", 6.0, "anon fn assigned and called");
}

/* Anonymous fn with no params */
TEST(test_anon_fn_no_params) {
    assert_vm_number("my g = fn() is 42 end\ng()", 42.0, "anon fn no params");
}

/* Anonymous fn with two params */
TEST(test_anon_fn_two_params) {
    assert_vm_number("my add = fn(a, b) is a + b end\nadd(3, 4)", 7.0, "anon fn two params");
}

/* Anonymous fn value formats as "<fn>" (no name) */
TEST(test_anon_fn_formats_as_fn) {
    Value v = run_input("fn() is 42 end", &test_ctx);
    ASSERT(v.type == VAL_CLOSURE, "anon fn evaluates to closure");
    char *s = value_format(&v);
    ASSERT(strcmp(s, "<fn>") == 0, "anonymous fn formats as <fn>");
    free(s);
    value_free(&v);
    PASS();
}

/* Anonymous fn can use say() */
TEST(test_anon_fn_with_say) {
    TestBuffer buf;
    test_buffer_init(&buf);
    EvalContext ctx = {.write_fn = test_write_capture, .userdata = &buf};
    Value v = run_input("my printer = fn(x) is say(x) end\nprinter(\"hello\")", &ctx);
    ASSERT_CLEANUP(v.type != VAL_ERROR, "no error", v, buf);
    ASSERT_STR_EQ_CLEANUP(buf.data, "hello\n", "anon fn called say correctly", v, buf);
    value_free(&v);
    test_buffer_free(&buf);
    PASS();
}

/* Anonymous fn arity mismatch gives error */
TEST(test_anon_fn_arity_error) {
    Value v = run_input("my f = fn(x) is x end\nf(1, 2)", &test_ctx);
    ASSERT(v.type == VAL_ERROR, "arity mismatch should error");
    char *msg = value_format(&v);
    ASSERT(strstr(msg, "expects 1 argument") != NULL, "error mentions expected arity");
    free(msg);
    value_free(&v);
    PASS();
}

/* Anonymous fn is an expression (can be used inline) */
TEST(test_anon_fn_as_expression) {
    Value v = run_input("fn(x) is x * 2 end", &test_ctx);
    ASSERT(v.type == VAL_CLOSURE, "anon fn as expression returns closure");
    ASSERT(v.closure != NULL, "closure pointer is non-NULL");
    ASSERT(v.closure->function != NULL, "function pointer is non-NULL");
    ASSERT(v.closure->function->name == NULL, "anonymous function has no name");
    ASSERT(v.closure->function->arity == 1, "arity is 1");
    value_free(&v);
    PASS();
}

/* ============================================================
 * Higher-order functions (local callee resolution)
 * ============================================================ */

/* fn apply(f, x) is f(x) end with a user-defined function */
TEST(test_higher_order_apply) {
    assert_vm_number("fn apply(f, x) is f(x) end\n"
                     "fn inc(x) is x + 1 end\n"
                     "apply(inc, 5)",
                     6.0, "higher-order apply with user fn");
}

/* fn apply(f, x) is f(x) end with the built-in say() */
TEST(test_higher_order_builtin) {
    TestBuffer buf;
    test_buffer_init(&buf);
    EvalContext ctx = {.write_fn = test_write_capture, .userdata = &buf};
    Value v = run_input("fn apply(f, x) is f(x) end\n"
                        "apply(say, \"hello\")",
                        &ctx);
    ASSERT_CLEANUP(v.type == VAL_NOTHING, "say returns nothing", v, buf);
    ASSERT_STR_EQ_CLEANUP(buf.data, "hello\n", "say output via higher-order", v, buf);
    value_free(&v);
    test_buffer_free(&buf);
    PASS();
}

/* Nested higher-order: compose(f, g, x) = f(g(x)) */
TEST(test_higher_order_nested) {
    assert_vm_number("fn compose(f, g, x) is f(g(x)) end\n"
                     "fn double(x) is x * 2 end\n"
                     "fn inc(x) is x + 1 end\n"
                     "compose(double, inc, 5)",
                     12.0, "compose(double, inc, 5) = 12");
}

/* Calling a my-declared local function variable */
TEST(test_local_var_as_callee) {
    assert_vm_number("fn foo() is\n"
                     "  my f = fn(x) is x + 10 end\n"
                     "  f(5)\n"
                     "end\n"
                     "foo()",
                     15.0, "my-declared local function as callee");
}

/* ============================================================
 * Nested named functions (lexical scoping, Step 4)
 * ============================================================ */

/* Named function inside another function works (callable from within) */
TEST(test_nested_fn_call_works) {
    assert_vm_number("fn outer() is\n"
                     "  fn inner() is 42 end\n"
                     "  inner()\n"
                     "end\n"
                     "outer()",
                     42.0, "nested named fn callable from outer");
}

/* Named function inside another function is NOT visible as a global */
TEST(test_nested_fn_not_visible_globally) {
    Value v = run_input("fn outer2() is\n"
                        "  fn inner2() is 42 end\n"
                        "  inner2()\n"
                        "end\n"
                        "outer2()\n"
                        "inner2()",
                        &test_ctx);
    ASSERT(v.type == VAL_ERROR, "inner2 should not be visible globally");
    char *msg = value_format(&v);
    ASSERT(strstr(msg, "inner2") != NULL, "error mentions inner2");
    free(msg);
    value_free(&v);
    PASS();
}

/* ============================================================
 * Closure infrastructure (Step 5)
 * ============================================================ */

/* say(fn() is 42 end) should display "<fn>" — closure passed to native fn */
TEST(test_say_displays_closure) {
    TestBuffer buf;
    test_buffer_init(&buf);
    EvalContext ctx = {.write_fn = test_write_capture, .userdata = &buf};

    Value v = run_input("say(fn() is 42 end)", &ctx);
    ASSERT_CLEANUP(v.type != VAL_ERROR, "say(fn()) should not error", v, buf);
    ASSERT_STR_EQ_CLEANUP(buf.data, "<fn>\n", "say(fn()) prints '<fn>\\n'", v, buf);
    value_free(&v);
    test_buffer_free(&buf);
    PASS();
}

/* say("hello") still works — native fn interop with closure infrastructure */
TEST(test_say_hello_with_closures) {
    TestBuffer buf;
    test_buffer_init(&buf);
    EvalContext ctx = {.write_fn = test_write_capture, .userdata = &buf};

    Value v = run_input("say(\"hello\")", &ctx);
    ASSERT_CLEANUP(v.type != VAL_ERROR, "say(hello) should not error", v, buf);
    ASSERT_STR_EQ_CLEANUP(buf.data, "hello\n", "say(hello) prints 'hello\\n'", v, buf);
    value_free(&v);
    test_buffer_free(&buf);
    PASS();
}

/* Function returning a closure: the closure can be called successfully */
TEST(test_closure_as_return_value) {
    assert_vm_number("fn maker() is\n"
                     "  fn inner(x) is x * 2 end\n"
                     "  inner\n"
                     "end\n"
                     "my f = maker()\n"
                     "f(5)",
                     10.0, "closure returned from function is callable");
}

/* Two closures from the same definition are not equal (identity-based) */
TEST(test_closure_equality_different_instances) {
    assert_vm_bool("fn maker2() is fn() is 1 end end\n"
                   "my a = maker2()\n"
                   "my b = maker2()\n"
                   "a == b",
                   false, "two closure instances are not equal");
}

/* Same closure variable compared to itself is equal */
TEST(test_closure_equality_same_instance) {
    assert_vm_bool("fn maker3() is fn() is 1 end end\n"
                   "my a = maker3()\n"
                   "a == a",
                   true, "same closure compared to itself is equal");
}

/* ============================================================
 * Upvalue capture: OP_GET_UPVALUE / OP_SET_UPVALUE (Step 2)
 * ============================================================ */

/* Inner function reads a variable from the outer function via upvalue. */
TEST(test_upvalue_read_outer) {
    assert_vm_number("fn outer() is\n"
                     "  my x = 10\n"
                     "  fn inner() is x end\n"
                     "  inner()\n"
                     "end\n"
                     "outer()",
                     10.0, "inner fn reads outer variable via upvalue");
}

/* Mutation through a closure is visible to the enclosing scope. */
TEST(test_upvalue_mutation_visible) {
    assert_vm_number("fn outer() is\n"
                     "  my x = 10\n"
                     "  fn inner() is x = 20 end\n"
                     "  inner()\n"
                     "  x\n"
                     "end\n"
                     "outer()",
                     20.0, "mutation through closure visible to encloser");
}

/* Two closures sharing the same captured variable share the same ObjUpvalue. */
TEST(test_upvalue_shared_between_closures) {
    assert_vm_number("fn outer() is\n"
                     "  my x = 10\n"
                     "  fn a() is x end\n"
                     "  fn b() is x = 20 end\n"
                     "  b()\n"
                     "  a()\n"
                     "end\n"
                     "outer()",
                     20.0, "two closures share same upvalue");
}

/* ============================================================
 * Closing upvalues on function return (Step 3)
 * ============================================================ */

/* Closure that outlives its enclosing function still works
 * because the upvalue is closed (value moved from stack to heap). */
TEST(test_close_upvalue_outlive_creator) {
    assert_vm_number("fn make() is\n"
                     "  my x = 42\n"
                     "  fn get() is x end\n"
                     "end\n"
                     "my g = make()\n"
                     "g()",
                     42.0, "closure outlives creator via closed upvalue");
}

/* Counter pattern: closed upvalue retains state across calls. */
TEST(test_close_upvalue_counter) {
    assert_vm_number("fn make() is\n"
                     "  my x = 0\n"
                     "  fn inc() is\n"
                     "    x = x + 1\n"
                     "    x\n"
                     "  end\n"
                     "end\n"
                     "my f = make()\n"
                     "f()\n"
                     "f()",
                     2.0, "counter pattern with closed upvalue");
}

/* Two closures from the same factory share a closed upvalue:
 * one writes, the other reads, both see the same variable. */
TEST(test_close_upvalue_shared) {
    assert_vm_number("my setter = nothing\n"
                     "fn make() is\n"
                     "  my x = 0\n"
                     "  fn set(v) is x = v end\n"
                     "  setter = set\n"
                     "  fn() is x end\n"
                     "end\n"
                     "my getter = make()\n"
                     "setter(42)\n"
                     "getter()",
                     42.0, "two closures share closed upvalue");
}

/* ============================================================
 * Decimal number literal tests
 * ============================================================ */

/* 0.5 evaluates to 0.5 */
TEST(test_decimal_literal_half) { assert_vm_number("0.5", 0.5, "0.5 evals to 0.5"); }

/* 0.5 + 0.5 evaluates to 1 */
TEST(test_decimal_literal_add) { assert_vm_number("0.5 + 0.5", 1.0, "0.5 + 0.5 evals to 1"); }

/* 3.14 * 2 evaluates to 6.28 */
TEST(test_decimal_literal_mul) { assert_vm_number("3.14 * 2", 6.28, "3.14 * 2 evals to 6.28"); }

/* 1.0 == 1 evaluates to true */
TEST(test_decimal_literal_eq_int) { assert_vm_bool("1.0 == 1", true, "1.0 == 1 is true"); }

/* say(0.5) prints "0.5\n" */
TEST(test_decimal_literal_say) {
    TestBuffer buf;
    test_buffer_init(&buf);
    EvalContext ctx = {.write_fn = test_write_capture, .userdata = &buf};

    Value v = run_input("say(0.5)", &ctx);
    ASSERT_CLEANUP(v.type != VAL_ERROR, "say(0.5) should not error", v, buf);
    ASSERT_STR_EQ_CLEANUP(buf.data, "0.5\n", "say(0.5) prints '0.5\\n'", v, buf);
    value_free(&v);
    test_buffer_free(&buf);
    PASS();
}

/* ============================================================
 * Single-line forms (if, while, fn without end)
 * ============================================================ */

TEST(test_sl_if_true) { assert_vm_number("if true then 42", 42.0, "single-line if true"); }

TEST(test_sl_if_false_nothing) {
    assert_vm_nothing("if false then 42", "single-line if false → nothing");
}

TEST(test_sl_if_else_true) {
    assert_vm_number("if true then 1 else 2", 1.0, "single-line if/else true");
}

TEST(test_sl_if_else_false) {
    assert_vm_number("if false then 1 else 2", 2.0, "single-line if/else false");
}

TEST(test_sl_while_counter) {
    assert_vm_number("my sl_x = 0\nwhile sl_x < 5 do sl_x = sl_x + 1\nsl_x", 5.0,
                     "single-line while counter");
}

TEST(test_sl_while_break_value) {
    assert_vm_number("while true do break 42", 42.0, "single-line while with break value");
}

TEST(test_sl_fn_named_call) {
    assert_vm_number("fn sl_double(x) is x * 2\nsl_double(5)", 10.0, "single-line named fn call");
}

TEST(test_sl_fn_anon_call) {
    assert_vm_number("my sl_f = fn(x) is x + 1\nsl_f(10)", 11.0, "single-line anon fn call");
}

TEST(test_sl_nested_if) {
    assert_vm_number("if true then if false then 1 else 2", 2.0, "nested single-line if");
}

TEST(test_sl_fn_as_arg) {
    /* Define apply, then pass a single-line anon fn as argument */
    assert_vm_number("fn sl_apply(f, x) is f(x)\nsl_apply(fn(x) is x + 1, 42)", 43.0,
                     "single-line fn as argument to function call");
}

/* ============================================================
 * Block scoping (Steps 3-6)
 * ============================================================ */

/* Step 3: if body scoping */

/* my inside if body is visible inside that scope */
TEST(test_block_scope_if_visible_inside) {
    assert_vm_number("fn bs1() is\nif true then\nmy x = 5\nx\nend\nend\nbs1()", 5.0,
                     "local visible inside if scope");
}

/* my inside if body is NOT visible outside the if.
 * Uses a unique name to avoid colliding with globals from other tests. */
TEST(test_block_scope_if_not_visible_outside) {
    assert_vm_error("fn bs2() is\nif true then\nmy bs_inner1 = 5\nend\nbs_inner1\nend\nbs2()",
                    "local not visible outside if scope");
}

/* Shadowing: inner x = 2 inside if, outer x = 1 preserved */
TEST(test_block_scope_if_shadow_inner) {
    assert_vm_number("fn bs3() is\nmy x = 1\nif true then\nmy x = 2\nx\nend\nend\nbs3()", 2.0,
                     "inner shadows outer in if");
}

/* After if with shadowed x, outer x is still 1 */
TEST(test_block_scope_if_shadow_outer_preserved) {
    assert_vm_number("fn bs4() is\nmy x = 1\nif true then\nmy x = 2\nend\nx\nend\nbs4()", 1.0,
                     "outer x preserved after if scope");
}

/* else branch has its own scope */
TEST(test_block_scope_else_own_scope) {
    assert_vm_number("fn bs5() is\nif false then\nmy x = 1\nelse\nmy y = 2\ny\nend\nend\nbs5()",
                     2.0, "else branch has own scope");
}

/* Step 4: while body scoping */

/* my inside while body works across multiple iterations (no stack corruption).
 * Each iteration declares x, uses it, and the scope cleanup removes x.
 * After the loop, i should be 3. */
TEST(test_block_scope_while_no_corruption) {
    assert_vm_number(
        "fn bs6() is\nmy i = 0\nwhile i < 3 do\nmy x = i * 10\ni = i + 1\nend\ni\nend\nbs6()", 3.0,
        "while body cleans up locals each iteration");
}

/* my inside while body is NOT visible outside.
 * Uses a unique name to avoid colliding with globals from other tests. */
TEST(test_block_scope_while_not_visible_outside) {
    assert_vm_error("fn bs7() is\nwhile false do\nmy bs_inner2 = 1\nend\nbs_inner2\nend\nbs7()",
                    "local not visible outside while scope");
}

/* Accumulator with block-scoped locals: sum = 1+2+3 = 6. */
TEST(test_block_scope_while_accumulator) {
    assert_vm_number("fn bs8() is\nmy sum = 0\nmy i = 0\nwhile i < 3 do\n"
                     "my x = i + 1\nsum = sum + x\ni = i + 1\nend\nsum\nend\nbs8()",
                     6.0, "while accumulator with block-scoped locals (1+2+3)");
}

/* Step 5: break and continue with block-scoped locals */

/* break with value cleans up block-scoped locals */
TEST(test_block_scope_break_cleanup) {
    assert_vm_number("fn bs9() is\nwhile true do\nmy x = 42\nbreak x\nend\nend\nbs9()", 42.0,
                     "break cleans up block-scoped locals");
}

/* break from nested if inside while with block-scoped locals */
TEST(test_block_scope_break_from_if) {
    assert_vm_number("fn bs10() is\nmy i = 0\nwhile i < 5 do\nmy x = i\n"
                     "if x == 3 then break x end\ni = i + 1\nend\nend\nbs10()",
                     3.0, "break from if cleans up block-scoped locals");
}

/* continue cleans up block-scoped locals.
 * Skips iteration where x==2. Body result on non-skipped iterations is x.
 * iter 0: x=0, i=1, body=0.  iter 1: x=1, i=2, body=1.
 * iter 2: x=2, i=3, continue.  iter 3: x=3, i=4, body=3.
 * iter 4: x=4, i=5, body=4.  While result = 4. */
TEST(test_block_scope_continue_cleanup) {
    assert_vm_number("fn bs11() is\nmy i = 0\nwhile i < 5 do\nmy x = i\n"
                     "i = i + 1\nif x == 2 then continue end\nx\nend\nend\nbs11()",
                     4.0, "continue cleans up block-scoped locals");
}

/* Step 6: nested scopes */

/* Nested if scopes: inner sees outer locals */
TEST(test_block_scope_nested_if_inner_sees_outer) {
    assert_vm_number("fn bs12() is\nif true then\nmy a = 1\nif true then\nmy b = 2\na + "
                     "b\nend\nend\nend\nbs12()",
                     3.0, "nested if inner sees outer local");
}

/* Sequential if blocks: first block's locals not visible in second */
TEST(test_block_scope_sequential_if) {
    assert_vm_number(
        "fn bs13() is\nif true then\nmy a = 1\nend\nif true then\nmy b = 2\nb\nend\nend\nbs13()",
        2.0, "sequential if blocks have separate scopes");
}

/* if inside while: locals scoped correctly, no crash.
 * While body result is i = i + 1. After 2 iterations, while result = 2. */
TEST(test_block_scope_if_inside_while) {
    assert_vm_number("fn bs14() is\nmy i = 0\nwhile i < 2 do\n"
                     "if true then\nmy x = i\nend\ni = i + 1\nend\nend\nbs14()",
                     2.0, "if inside while scopes correctly");
}

/* Three-level nesting: while > if > if with my at each level.
 * Inner expression a+b+c = 10+20+30 = 60. Stored in result. */
TEST(test_block_scope_three_level_nesting) {
    assert_vm_number("fn bs15() is\nmy result = 0\nmy i = 0\n"
                     "while i < 1 do\nmy a = 10\n"
                     "if true then\nmy b = 20\n"
                     "if true then\nmy c = 30\nresult = a + b + c\nend\n"
                     "end\ni = i + 1\nend\n"
                     "result\nend\nbs15()",
                     60.0, "three-level nesting works");
}

/* ============================================================
 * Closure integration tests (Step 4)
 * ============================================================ */

/* Counter pattern: make_counter returns a closure that increments and returns */
TEST(test_closure_counter_pattern) {
    assert_vm_number("fn make_counter() is\n"
                     "  my n = 0\n"
                     "  fn() is\n"
                     "    n = n + 1\n"
                     "    n\n"
                     "  end\n"
                     "end\n"
                     "my c = make_counter()\n"
                     "c()\n"
                     "c()\n"
                     "c()",
                     3.0, "counter pattern: 3 increments → 3");
}

/* Adder factory: make_adder(x) returns fn(y) → x + y */
TEST(test_closure_adder_factory) {
    assert_vm_number("fn make_adder(x) is\n"
                     "  fn(y) is x + y end\n"
                     "end\n"
                     "my add5 = make_adder(5)\n"
                     "add5(3)",
                     8.0, "adder factory: make_adder(5)(3) → 8");
}

/* Two adders from same factory are independent */
TEST(test_closure_adder_independent) {
    assert_vm_number("fn make_adder(x) is\n"
                     "  fn(y) is x + y end\n"
                     "end\n"
                     "my add5 = make_adder(5)\n"
                     "my add10 = make_adder(10)\n"
                     "add5(1) + add10(1)",
                     17.0, "two adders are independent: 6 + 11 = 17");
}

/* Shared capture: two closures from same encloser, one reads, one writes */
TEST(test_closure_shared_capture) {
    assert_vm_number("fn make_pair() is\n"
                     "  my val = 0\n"
                     "  fn get() is val end\n"
                     "  fn set(v) is val = v end\n"
                     "end\n"
                     "my setter = nothing\n"
                     "fn make() is\n"
                     "  my val = 0\n"
                     "  fn get() is val end\n"
                     "  fn set(v) is val = v end\n"
                     "end\n"
                     "# Use global variables for setter/getter\n"
                     "my s = nothing\n"
                     "fn mk() is\n"
                     "  my x = 0\n"
                     "  fn get() is x end\n"
                     "  fn set(v) is x = v end\n"
                     "  s = set\n"
                     "  get\n"
                     "end\n"
                     "my g = mk()\n"
                     "s(99)\n"
                     "g()",
                     99.0, "shared capture: setter and getter share upvalue");
}

/* Deep nesting: 3-level function nesting, innermost captures from outermost */
TEST(test_closure_deep_nesting) {
    assert_vm_number("fn level1() is\n"
                     "  my x = 100\n"
                     "  fn level2() is\n"
                     "    fn level3() is x end\n"
                     "    level3()\n"
                     "  end\n"
                     "  level2()\n"
                     "end\n"
                     "level1()",
                     100.0, "3-level nesting: innermost reads outermost");
}

/* Deep nesting: innermost mutates outermost variable */
TEST(test_closure_deep_nesting_mutation) {
    assert_vm_number("fn level1() is\n"
                     "  my x = 1\n"
                     "  fn level2() is\n"
                     "    fn level3() is x = x + 10 end\n"
                     "    level3()\n"
                     "  end\n"
                     "  level2()\n"
                     "  x\n"
                     "end\n"
                     "level1()",
                     11.0, "3-level nesting: innermost mutates outermost");
}

/* Deep nesting with closure outliving creator */
TEST(test_closure_deep_nesting_outlive) {
    assert_vm_number("fn outer() is\n"
                     "  my x = 42\n"
                     "  fn middle() is\n"
                     "    fn inner() is x end\n"
                     "  end\n"
                     "end\n"
                     "my get_inner = outer()\n"
                     "my inner = get_inner()\n"
                     "inner()",
                     42.0, "3-level nesting: innermost outlives all enclosers");
}

/* Closure + parameters: capture a parameter (not just `my` locals) */
TEST(test_closure_capture_parameter) {
    assert_vm_number("fn greet(name) is\n"
                     "  fn() is name end\n"
                     "end\n"
                     "my f = greet(42)\n"
                     "f()",
                     42.0, "closure captures parameter from enclosing function");
}

/* Closure captures multiple parameters */
TEST(test_closure_capture_multiple_params) {
    assert_vm_number("fn make(a, b) is\n"
                     "  fn() is a + b end\n"
                     "end\n"
                     "my f = make(3, 7)\n"
                     "f()",
                     10.0, "closure captures multiple parameters");
}

/* Closure + recursion: a globally-defined recursive function is captured
 * and used as a closure value. (Local recursive functions can't reference
 * themselves because the local variable isn't defined until after the body
 * is compiled.) */
TEST(test_closure_with_recursion) {
    assert_vm_number("fn fact(n) is\n"
                     "  if n <= 1 then 1 else n * fact(n - 1) end\n"
                     "end\n"
                     "fn apply_to_5(f) is f(5) end\n"
                     "apply_to_5(fact)",
                     120.0, "recursive function captured as closure argument");
}

/* Closure + control flow: closure captures variable modified by while loop */
TEST(test_closure_with_while_loop) {
    assert_vm_number("fn make() is\n"
                     "  my total = 0\n"
                     "  my i = 0\n"
                     "  while i < 5 do\n"
                     "    total = total + i\n"
                     "    i = i + 1\n"
                     "  end\n"
                     "  fn() is total end\n"
                     "end\n"
                     "my f = make()\n"
                     "f()",
                     10.0, "closure captures variable modified by while loop (0+1+2+3+4=10)");
}

/* Closure capturing a variable modified by while, called inside loop */
TEST(test_closure_called_in_loop) {
    assert_vm_number("fn make_counter() is\n"
                     "  my n = 0\n"
                     "  fn() is\n"
                     "    n = n + 1\n"
                     "    n\n"
                     "  end\n"
                     "end\n"
                     "my c = make_counter()\n"
                     "my i = 0\n"
                     "while i < 10 do\n"
                     "  c()\n"
                     "  i = i + 1\n"
                     "end\n"
                     "c()",
                     11.0, "closure called in loop: 10 loop calls + 1 final = 11");
}

/* Error case: calling a non-function still errors correctly */
TEST(test_closure_error_call_non_function) {
    Value v = run_input("my x = 42\nx()", &test_ctx);
    ASSERT(v.type == VAL_ERROR, "calling a number should error");
    char *msg = value_format(&v);
    ASSERT(strstr(msg, "cannot call") != NULL, "error mentions 'cannot call'");
    free(msg);
    value_free(&v);
    PASS();
}

/* Error case: arity mismatch on closure still reports function name */
TEST(test_closure_error_arity_named) {
    Value v = run_input("fn make() is\n"
                        "  fn adder(x, y) is x + y end\n"
                        "end\n"
                        "my f = make()\n"
                        "f(1)",
                        &test_ctx);
    ASSERT(v.type == VAL_ERROR, "arity mismatch should error");
    char *msg = value_format(&v);
    ASSERT(strstr(msg, "adder") != NULL, "error mentions function name 'adder'");
    ASSERT(strstr(msg, "2") != NULL, "error mentions expected arity 2");
    free(msg);
    value_free(&v);
    PASS();
}

/* Error case: arity mismatch on anonymous closure reports <fn> */
TEST(test_closure_error_arity_anonymous) {
    Value v = run_input("fn make() is\n"
                        "  fn(x, y) is x + y end\n"
                        "end\n"
                        "my f = make()\n"
                        "f(1)",
                        &test_ctx);
    ASSERT(v.type == VAL_ERROR, "arity mismatch on anon closure should error");
    char *msg = value_format(&v);
    ASSERT(strstr(msg, "<fn>") != NULL, "error mentions '<fn>' for anon closure");
    ASSERT(strstr(msg, "2") != NULL, "error mentions expected arity 2");
    free(msg);
    value_free(&v);
    PASS();
}

/* Counter pattern with say() to verify output */
TEST(test_closure_counter_with_say) {
    TestBuffer buf;
    test_buffer_init(&buf);
    EvalContext ctx = {.write_fn = test_write_capture, .userdata = &buf};

    Value v = run_input("fn make_counter() is\n"
                        "  my count = 0\n"
                        "  fn() is\n"
                        "    count = count + 1\n"
                        "    say(count)\n"
                        "  end\n"
                        "end\n"
                        "my counter = make_counter()\n"
                        "counter()\n"
                        "counter()\n"
                        "counter()",
                        &ctx);
    ASSERT_CLEANUP(v.type != VAL_ERROR, "counter with say should not error", v, buf);
    ASSERT_STR_EQ_CLEANUP(buf.data, "1\n2\n3\n", "counter prints 1, 2, 3", v, buf);
    value_free(&v);
    test_buffer_free(&buf);
    PASS();
}

/* ============================================================
 * Return keyword
 * ============================================================ */

/* Basic return: function returns the given value */
TEST(test_return_basic) {
    assert_vm_number("fn f() is return 42 end\nf()", 42.0, "basic return value");
}

/* Bare return: returns nothing */
TEST(test_return_bare) {
    assert_vm_nothing("fn f() is return end\nf()", "bare return returns nothing");
}

/* Return nothing explicitly */
TEST(test_return_nothing_explicit) {
    assert_vm_nothing("fn f() is return nothing end\nf()", "return nothing explicitly");
}

/* Early return (guard clause): return from if, otherwise continue */
TEST(test_return_early_guard) {
    assert_vm_number("fn f(x) is\nif x < 0 then return -1 end\nx * 2\nend\nf(-5)", -1.0,
                     "early return guard clause negative");
}

TEST(test_return_early_guard_fallthrough) {
    assert_vm_number("fn f(x) is\nif x < 0 then return -1 end\nx * 2\nend\nf(3)", 6.0,
                     "early return guard clause positive falls through");
}

/* Return from inside a while loop exits the function */
TEST(test_return_from_while_loop) {
    assert_vm_number("fn f() is\nmy i = 0\nwhile true do\ni = i + 1\nif i == 5 then return i "
                     "end\ni\nend\nend\nf()",
                     5.0, "return from while loop");
}

/* Return from nested loops exits the function entirely */
TEST(test_return_from_nested_loops) {
    assert_vm_number("fn f() is\nwhile true do\nwhile true do\nreturn 99\nend\nend\nend\nf()", 99.0,
                     "return from nested loops");
}

/* Return with block-scoped locals cleans up correctly */
TEST(test_return_with_block_scoped_locals) {
    assert_vm_number("fn f() is\nmy x = 10\nif true then\nmy y = 20\nreturn y\nend\nx\nend\nf()",
                     20.0, "return with block-scoped locals");
}

/* Return at top level is a compile error */
TEST(test_return_top_level_error) { assert_vm_error("return 42", "return at top level"); }

/* Return with single-line function */
TEST(test_return_single_line_fn) {
    assert_vm_number("fn f(x) is return x * 2\nf(5)", 10.0, "return in single-line fn");
}

/* Last-expression-is-return-value still works without explicit return */
TEST(test_return_last_expr_default) {
    assert_vm_number("fn f() is 42 end\nf()", 42.0,
                     "last expression still returns without explicit return");
}

/* ============================================================
 * Kebab-case identifiers (end-to-end integration tests)
 *
 * These tests verify that dashed identifiers work through the
 * full parse → compile → execute pipeline.
 * ============================================================ */

/* my compute-sum = 42; compute-sum → 42 */
TEST(test_kebab_var_decl_and_read) {
    assert_vm_number("my compute-sum = 42\ncompute-sum", 42.0, "kebab var decl and read");
}

/* my x-val = 1; x-val = 2; x-val → 2 */
TEST(test_kebab_var_assignment) {
    assert_vm_number("my x-val = 1\nx-val = 2\nx-val", 2.0, "kebab var assignment");
}

/* my x-y = 10; my x_y = 20; x-y → 10 (dashed and underscored are distinct) */
TEST(test_kebab_distinct_from_underscore) {
    assert_vm_number("my x-y = 10\nmy x_y = 20\nx-y", 10.0, "kebab distinct from underscore");
}

/* fn add-one(n) is n + 1 end; add-one(5) → 6 */
TEST(test_kebab_fn_def_and_call) {
    assert_vm_number("fn add-one(n) is n + 1 end\nadd-one(5)", 6.0, "kebab function def and call");
}

/* fn f(my-param) is my-param * 2 end; f(3) → 6 */
TEST(test_kebab_param_names) {
    assert_vm_number("fn f(my-param) is my-param * 2 end\nf(3)", 6.0, "kebab parameter names");
}

/* my outer-val = 99; my f = fn() is outer-val end; f() → 99 */
TEST(test_kebab_closure_capture) {
    assert_vm_number("my outer-val = 99\n"
                     "my f = fn() is outer-val end\n"
                     "f()",
                     99.0, "kebab closure capture");
}

/* my foo-bar = 10; foo-bar - 3 → 7
 * Tests that `foo-bar` is one ident and `- 3` is subtraction due to space. */
TEST(test_kebab_ident_in_subtraction_expr) {
    assert_vm_number("my foo-bar = 10\nfoo-bar - 3", 7.0, "kebab ident in subtraction expression");
}

/* ============================================================
 * Array literals
 * ============================================================ */

/* Eval [] => [] */
TEST(test_array_empty_eval) { assert_vm_formatted("[]", "[]", "empty array"); }

/* Eval [1, 2, 3] => [1, 2, 3] */
TEST(test_array_numbers_eval) { assert_vm_formatted("[1, 2, 3]", "[1, 2, 3]", "number array"); }

/* Eval [1 + 1, 2 + 2] => [2, 4] */
TEST(test_array_expressions_eval) {
    assert_vm_formatted("[1 + 1, 2 + 2]", "[2, 4]", "expression array");
}

/* Eval [[1], [2, 3]] => [[1], [2, 3]] */
TEST(test_array_nested_eval) {
    assert_vm_formatted("[[1], [2, 3]]", "[[1], [2, 3]]", "nested array");
}

/* ============================================================
 * Array indexing
 * ============================================================ */

/* [10, 20, 30][0] => 10 */
TEST(test_index_first) { assert_vm_number("[10, 20, 30][0]", 10.0, "index first"); }

/* [10, 20, 30][2] => 30 */
TEST(test_index_last) { assert_vm_number("[10, 20, 30][2]", 30.0, "index last"); }

/* [10, 20, 30][-1] => 30 (negative wraps from end) */
TEST(test_index_negative_one) { assert_vm_number("[10, 20, 30][-1]", 30.0, "index -1"); }

/* [10, 20, 30][-3] => 10 (negative wraps to first) */
TEST(test_index_negative_three) { assert_vm_number("[10, 20, 30][-3]", 10.0, "index -3"); }

/* [10, 20, 30][3] => error "index out of bounds" */
TEST(test_index_oob_positive) { assert_vm_error("[10, 20, 30][3]", "oob positive"); }

/* [10, 20, 30][-4] => error "index out of bounds" */
TEST(test_index_oob_negative) { assert_vm_error("[10, 20, 30][-4]", "oob negative"); }

/* 42[0] => error "cannot index" */
TEST(test_index_non_array) { assert_vm_error("42[0]", "index non-array"); }

/* [1, 2][0.5] => error "index must be an integer" */
TEST(test_index_non_integer) { assert_vm_error("[1, 2][0.5]", "index non-integer"); }

/* my xs = [10, 20, 30]\nxs[0] = 99\nxs => [99, 20, 30] */
TEST(test_index_assign_eval) {
    assert_vm_formatted("my xs = [10, 20, 30]\nxs[0] = 99\nxs", "[99, 20, 30]", "index assign");
}

/* COW: my xs = [1, 2]\nmy ys = xs\nys[0] = 99\nxs => [1, 2] */
TEST(test_index_assign_cow) {
    assert_vm_formatted("my xs = [1, 2]\nmy ys = xs\nys[0] = 99\nxs", "[1, 2]", "COW index assign");
}

/* Nested indexing: [[1, 2], [3, 4]][1][0] => 3 */
TEST(test_index_nested) { assert_vm_number("[[1, 2], [3, 4]][1][0]", 3.0, "nested index"); }

/* Index assignment returns the assigned value */
TEST(test_index_assign_returns_value) {
    assert_vm_number("my xs = [10, 20, 30]\nxs[1] = 42", 42.0, "index assign returns value");
}

/* ============================================================
 * Boolean mask indexing
 * ============================================================ */

/* [10, 20, 30][[true, false, true]] => [10, 30] */
TEST(test_mask_basic) {
    assert_vm_formatted("[10, 20, 30][[true, false, true]]", "[10, 30]", "mask basic");
}

/* [10, 20, 30][[false, false, false]] => [] */
TEST(test_mask_all_false) {
    assert_vm_formatted("[10, 20, 30][[false, false, false]]", "[]", "mask all false");
}

/* [10, 20, 30][[true, true, true]] => [10, 20, 30] */
TEST(test_mask_all_true) {
    assert_vm_formatted("[10, 20, 30][[true, true, true]]", "[10, 20, 30]", "mask all true");
}

/* Combine with vectorization: my xs = [1, 2, 3, 4, 5]\nxs[xs @> 3] => [4, 5] */
TEST(test_mask_with_vectorize) {
    assert_vm_formatted("my xs = [1, 2, 3, 4, 5]\nxs[xs @> 3]", "[4, 5]", "mask with vectorize");
}

/* Mask length mismatch => error */
TEST(test_mask_length_mismatch) {
    assert_vm_error("[10, 20, 30][[true, false]]", "mask length mismatch");
}

/* Non-boolean elements in mask => error */
TEST(test_mask_non_boolean) { assert_vm_error("[1, 2, 3][[1, 0, 1]]", "mask non-boolean"); }

/* ============================================================
 * Array concatenation (++)
 * ============================================================ */

/* [1, 2] ++ [3, 4] => [1, 2, 3, 4] */
TEST(test_concat_arrays_basic) {
    assert_vm_formatted("[1, 2] ++ [3, 4]", "[1, 2, 3, 4]", "array concat basic");
}

/* [] ++ [1] => [1] */
TEST(test_concat_array_empty_left) { assert_vm_formatted("[] ++ [1]", "[1]", "empty left concat"); }

/* [1] ++ [] => [1] */
TEST(test_concat_array_empty_right) {
    assert_vm_formatted("[1] ++ []", "[1]", "empty right concat");
}

/* [] ++ [] => [] */
TEST(test_concat_array_both_empty) { assert_vm_formatted("[] ++ []", "[]", "both empty concat"); }

/* [[1]] ++ [[2]] => [[1], [2]] */
TEST(test_concat_array_nested) {
    assert_vm_formatted("[[1]] ++ [[2]]", "[[1], [2]]", "nested array concat");
}

/* [1] ++ 2 => error */
TEST(test_concat_array_num_error) { assert_vm_error("[1] ++ 2", "array ++ number"); }

/* 1 ++ [2] => error */
TEST(test_concat_num_array_error) { assert_vm_error("1 ++ [2]", "number ++ array"); }

/* "a" ++ "b" still works => "ab" (existing string concat unchanged) */
TEST(test_concat_strings_still_works) {
    assert_vm_string("\"a\" ++ \"b\"", "ab", "string concat unchanged");
}

/* ============================================================
 * Built-in array functions: len, push, pop
 * ============================================================ */

/* len([1, 2, 3]) => 3 */
TEST(test_len_array) { assert_vm_number("len([1, 2, 3])", 3.0, "len array"); }

/* len([]) => 0 */
TEST(test_len_empty) { assert_vm_number("len([])", 0.0, "len empty"); }

/* len("hello") => 5 */
TEST(test_len_string) { assert_vm_number("len(\"hello\")", 5.0, "len string"); }

/* len("") => 0 */
TEST(test_len_empty_string) { assert_vm_number("len(\"\")", 0.0, "len empty string"); }

/* len(42) => error */
TEST(test_len_number_error) { assert_vm_error("len(42)", "len number error"); }

/* len(true) => error */
TEST(test_len_bool_error) { assert_vm_error("len(true)", "len bool error"); }

/* push([1, 2], 3) => [1, 2, 3] */
TEST(test_push_basic) { assert_vm_formatted("push([1, 2], 3)", "[1, 2, 3]", "push basic"); }

/* push([], "a") => ["a"] */
TEST(test_push_empty) { assert_vm_formatted("push([], \"a\")", "[a]", "push onto empty"); }

/* push does not mutate the original */
TEST(test_push_no_mutate) {
    assert_vm_formatted("my xs = [1, 2]\npush(xs, 3)\nxs", "[1, 2]", "push no mutate");
}

/* push returns new array with element appended */
TEST(test_push_returns_new) {
    assert_vm_formatted("push([10, 20], 30)", "[10, 20, 30]", "push returns new");
}

/* push(42, 1) => error (first arg must be array) */
TEST(test_push_non_array_error) { assert_vm_error("push(42, 1)", "push non-array"); }

/* pop([1, 2, 3]) => [1, 2] */
TEST(test_pop_basic) { assert_vm_formatted("pop([1, 2, 3])", "[1, 2]", "pop basic"); }

/* pop([1]) => [] */
TEST(test_pop_single) { assert_vm_formatted("pop([1])", "[]", "pop single"); }

/* pop([]) => error */
TEST(test_pop_empty_error) { assert_vm_error("pop([])", "pop empty"); }

/* pop does not mutate the original */
TEST(test_pop_no_mutate) {
    assert_vm_formatted("my xs = [1, 2, 3]\npop(xs)\nxs", "[1, 2, 3]", "pop no mutate");
}

/* pop(42) => error (must be array) */
TEST(test_pop_non_array_error) { assert_vm_error("pop(42)", "pop non-array"); }

/* Composable: len(push([1, 2], 3)) => 3 */
TEST(test_len_push_compose) { assert_vm_number("len(push([1, 2], 3))", 3.0, "len(push(...))"); }

/* ============================================================
 * Array equality
 * ============================================================ */

/* [1, 2, 3] == [1, 2, 3] => true */
TEST(test_array_eq_same) { assert_vm_bool("[1, 2, 3] == [1, 2, 3]", true, "array == same"); }

/* [1, 2] == [1, 2, 3] => false (different length) */
TEST(test_array_eq_diff_len) { assert_vm_bool("[1, 2] == [1, 2, 3]", false, "array == diff len"); }

/* [1, 2] == [1, 3] => false (different element) */
TEST(test_array_eq_diff_elem) { assert_vm_bool("[1, 2] == [1, 3]", false, "array == diff elem"); }

/* [] == [] => true */
TEST(test_array_eq_empty) { assert_vm_bool("[] == []", true, "[] == []"); }

/* [1, 2] == "hello" => false (different types) */
TEST(test_array_eq_diff_type) { assert_vm_bool("[1, 2] == \"hello\"", false, "array == string"); }

/* [1, 2, 3] != [1, 2, 3] => false */
TEST(test_array_neq_same) { assert_vm_bool("[1, 2, 3] != [1, 2, 3]", false, "array != same"); }

/* [1, 2] != [1, 2, 3] => true */
TEST(test_array_neq_diff) { assert_vm_bool("[1, 2] != [1, 2, 3]", true, "array != diff"); }

/* Nested array equality: [[1, 2], [3]] == [[1, 2], [3]] => true */
TEST(test_array_eq_nested) {
    assert_vm_bool("[[1, 2], [3]] == [[1, 2], [3]]", true, "nested array ==");
}

/* Nested array inequality: [[1, 2], [3]] == [[1, 2], [4]] => false */
TEST(test_array_eq_nested_diff) {
    assert_vm_bool("[[1, 2], [3]] == [[1, 2], [4]]", false, "nested array != diff");
}

/* ============================================================
 * Reduce (@op prefix) tests
 * ============================================================ */

/* @+ [1, 2, 3, 4, 5] → 15 */
TEST(test_reduce_add) { assert_vm_number("@+ [1, 2, 3, 4, 5]", 15.0, "@+ reduce"); }

/* @* [1, 2, 3, 4, 5] → 120 */
TEST(test_reduce_multiply) { assert_vm_number("@* [1, 2, 3, 4, 5]", 120.0, "@* reduce"); }

/* @- [10, 3, 2] → 5 (left fold: (10-3)-2) */
TEST(test_reduce_subtract) { assert_vm_number("@- [10, 3, 2]", 5.0, "@- reduce"); }

/* @++ ["a", "b", "c"] → "abc" */
TEST(test_reduce_concat) { assert_vm_string("@++ [\"a\", \"b\", \"c\"]", "abc", "@++ reduce"); }

/* @+ [42] → 42 (single element returned as-is) */
TEST(test_reduce_single_element) { assert_vm_number("@+ [42]", 42.0, "@+ single element"); }

/* @+ [] → error "cannot reduce empty array" */
TEST(test_reduce_empty_error) { assert_vm_error("@+ []", "@+ empty array error"); }

/* @and [true, true, false] → false (short-circuit) */
TEST(test_reduce_and_short_circuit) {
    assert_vm_bool("@and [true, true, false]", false, "@and short-circuit");
}

/* @and [1, 2, 3] → 3 (all truthy, return last) */
TEST(test_reduce_and_all_truthy) { assert_vm_number("@and [1, 2, 3]", 3.0, "@and all truthy"); }

/* @or [false, 0, \"hi\"] → "hi" (first truthy) */
TEST(test_reduce_or_first_truthy) {
    assert_vm_string("@or [false, 0, \"hi\"]", "hi", "@or first truthy");
}

/* @or [false, 0, ""] → "" (all falsy, return last) */
TEST(test_reduce_or_all_falsy) { assert_vm_string("@or [false, 0, \"\"]", "", "@or all falsy"); }

/* @== [1, 1, 1] → true==1 → false (left-fold behavior) */
TEST(test_reduce_equal_fold) {
    /* (1==1) = true, then true==1 = false because bool!=number */
    assert_vm_bool("@== [1, 1, 1]", false, "@== fold");
}

/* @+ 42 → error (non-array operand) */
TEST(test_reduce_non_array_error) { assert_vm_error("@+ 42", "@+ non-array error"); }

/* ============================================================
 * Map reduce (@op prefix on maps) tests
 * ============================================================ */

/* @+ {a: 1, b: 2, c: 3} → 6 */
TEST(test_reduce_map_add) { assert_vm_number("@+ {a: 1, b: 2, c: 3}", 6.0, "@+ map reduce"); }

/* @* {x: 2, y: 3, z: 4} → 24 */
TEST(test_reduce_map_multiply) { assert_vm_number("@* {x: 2, y: 3, z: 4}", 24.0, "@* map reduce"); }

/* @- {a: 10, b: 3, c: 2} → 5 (left fold: (10-3)-2) */
TEST(test_reduce_map_subtract) { assert_vm_number("@- {a: 10, b: 3, c: 2}", 5.0, "@- map reduce"); }

/* @++ {first: "hello", second: " world"} → "hello world" */
TEST(test_reduce_map_concat) {
    assert_vm_string("@++ {first: \"hello\", second: \" world\"}", "hello world", "@++ map reduce");
}

/* @+ {x: 42} → 42 (single entry) */
TEST(test_reduce_map_single) { assert_vm_number("@+ {x: 42}", 42.0, "@+ map single"); }

/* @+ {} → error "cannot reduce empty map" */
TEST(test_reduce_map_empty_error) { assert_vm_error("@+ {}", "@+ empty map error"); }

/* @and {a: true, b: true, c: false} → false (short-circuit) */
TEST(test_reduce_map_and) {
    assert_vm_bool("@and {a: true, b: true, c: false}", false, "@and map short-circuit");
}

/* @or {a: false, b: 0, c: "hi"} → "hi" (first truthy) */
TEST(test_reduce_map_or) {
    assert_vm_string("@or {a: false, b: 0, c: \"hi\"}", "hi", "@or map first truthy");
}

/* ============================================================
 * Vectorize (@op infix) tests
 * ============================================================ */

/* [1, 2, 3] @+ [4, 5, 6] → [5, 7, 9] */
TEST(test_vectorize_add) {
    assert_vm_formatted("[1, 2, 3] @+ [4, 5, 6]", "[5, 7, 9]", "@+ vectorize");
}

/* [1, 2, 3] @* 10 → [10, 20, 30] (scalar broadcast right) */
TEST(test_vectorize_scalar_right) {
    assert_vm_formatted("[1, 2, 3] @* 10", "[10, 20, 30]", "@* scalar right");
}

/* 10 @- [1, 2, 3] → [9, 8, 7] (scalar broadcast left) */
TEST(test_vectorize_scalar_left) {
    assert_vm_formatted("10 @- [1, 2, 3]", "[9, 8, 7]", "@- scalar left");
}

/* [1, 2, 3] @** 2 → [1, 4, 9] */
TEST(test_vectorize_power) { assert_vm_formatted("[1, 2, 3] @** 2", "[1, 4, 9]", "@** vectorize"); }

/* [1, 2, 3] @> 2 → [false, false, true] */
TEST(test_vectorize_greater) {
    assert_vm_formatted("[1, 2, 3] @> 2", "[false, false, true]", "@> vectorize");
}

/* [1, 2] @+ [1, 2, 3] → error "array length mismatch" */
TEST(test_vectorize_length_mismatch) {
    assert_vm_error("[1, 2] @+ [1, 2, 3]", "@+ length mismatch");
}

/* 1 @+ 2 → error "@ requires at least one array operand" */
TEST(test_vectorize_both_scalars_error) { assert_vm_error("1 @+ 2", "@+ both scalars error"); }

/* ["a", "b"] @++ ["1", "2"] → ["a1", "b2"] */
TEST(test_vectorize_concat) {
    assert_vm_formatted("[\"a\", \"b\"] @++ [\"1\", \"2\"]", "[a1, b2]", "@++ vectorize");
}

/* [true, false] @and [true, true] → [true, false] */
TEST(test_vectorize_and) {
    assert_vm_formatted("[true, false] @and [true, true]", "[true, false]", "@and vectorize");
}

/* Precedence: [1, 2] @+ [3, 4] @* [5, 6] → [1, 2] @+ [15, 24] → [16, 26] */
TEST(test_vectorize_precedence) {
    assert_vm_formatted("[1, 2] @+ [3, 4] @* [5, 6]", "[16, 26]", "@op precedence");
}

/* ============================================================
 * Map vectorize (@op infix with maps)
 * ============================================================ */

/* Map × map: {a: 1, b: 2} @+ {a: 10, b: 20} → {a: 11, b: 22} */
TEST(test_vectorize_map_map_add) {
    assert_vm_formatted("{a: 1, b: 2} @+ {a: 10, b: 20}", "{a: 11, b: 22}", "@+ map-map");
}

/* Map × map key intersection: only shared keys */
TEST(test_vectorize_map_map_intersection) {
    assert_vm_formatted("{a: 1, b: 2, c: 3} @+ {b: 10, c: 20, d: 30}", "{b: 12, c: 23}",
                        "@+ map-map intersection");
}

/* Map × map no shared keys → empty map */
TEST(test_vectorize_map_map_no_shared) {
    assert_vm_formatted("{a: 1} @+ {b: 2}", "{}", "@+ map-map no shared");
}

/* Map × map subtraction: {a: 5, b: 10} @- {a: 1, b: 3} → {a: 4, b: 7} */
TEST(test_vectorize_map_map_subtract) {
    assert_vm_formatted("{a: 5, b: 10} @- {a: 1, b: 3}", "{a: 4, b: 7}", "@- map-map");
}

/* Map × map comparison: {a: 1, b: 2} @> {a: 0, b: 3} → {a: true, b: false} */
TEST(test_vectorize_map_map_greater) {
    assert_vm_formatted("{a: 1, b: 2} @> {a: 0, b: 3}", "{a: true, b: false}", "@> map-map");
}

/* Map × scalar: {a: 1, b: 2} @* 10 → {a: 10, b: 20} */
TEST(test_vectorize_map_scalar_mul) {
    assert_vm_formatted("{a: 1, b: 2} @* 10", "{a: 10, b: 20}", "@* map-scalar");
}

/* Map × scalar comparison: {a: 85, b: 90} @>= 88 → {a: false, b: true} */
TEST(test_vectorize_map_scalar_gte) {
    assert_vm_formatted("{a: 85, b: 90} @>= 88", "{a: false, b: true}", "@>= map-scalar");
}

/* Map × scalar power: {a: 2, b: 3} @** 2 → {a: 4, b: 9} */
TEST(test_vectorize_map_scalar_power) {
    assert_vm_formatted("{a: 2, b: 3} @** 2", "{a: 4, b: 9}", "@** map-scalar");
}

/* Scalar × map: 100 @- {a: 10, b: 20} → {a: 90, b: 80} */
TEST(test_vectorize_scalar_map_sub) {
    assert_vm_formatted("100 @- {a: 10, b: 20}", "{a: 90, b: 80}", "@- scalar-map");
}

/* Scalar × map: 10 @* {a: 2, b: 3} → {a: 20, b: 30} */
TEST(test_vectorize_scalar_map_mul) {
    assert_vm_formatted("10 @* {a: 2, b: 3}", "{a: 20, b: 30}", "@* scalar-map");
}

/* Error: map + array → "cannot vectorize map with array" */
TEST(test_vectorize_map_array_error) { assert_vm_error("{a: 1} @+ [1, 2]", "@+ map-array error"); }

/* Error: array + map → "cannot vectorize array with map" */
TEST(test_vectorize_array_map_error) { assert_vm_error("[1, 2] @+ {a: 1}", "@+ array-map error"); }

/* Existing array behavior unchanged: [1, 2, 3] @+ [4, 5, 6] → [5, 7, 9] */
TEST(test_vectorize_array_still_works) {
    assert_vm_formatted("[1, 2, 3] @+ [4, 5, 6]", "[5, 7, 9]", "@+ array unchanged");
}

/* Existing array scalar broadcast unchanged: [1, 2, 3] @* 10 → [10, 20, 30] */
TEST(test_vectorize_array_scalar_still_works) {
    assert_vm_formatted("[1, 2, 3] @* 10", "[10, 20, 30]", "@* array scalar unchanged");
}

/* ============================================================
 * Map meta-operator composability tests
 * ============================================================ */

/* @+ values({a: 10, b: 20, c: 30}) → 60 (reduce array from values()) */
TEST(test_map_compose_reduce_values) {
    assert_vm_number("@+ values({a: 10, b: 20, c: 30})", 60.0, "@+ values() composability");
}

/* {a: 85, b: 92} @>= 90 → {a: false, b: true} (broadcast then inspect) */
TEST(test_map_compose_broadcast_comparison) {
    assert_vm_formatted("{a: 85, b: 92} @>= 90", "{a: false, b: true}",
                        "@>= map broadcast comparison");
}

/* Chain: reduce a broadcast result — @+ ({a: 1, b: 2} @* 10) → 30 */
TEST(test_map_compose_reduce_broadcast) {
    assert_vm_number("@+ ({a: 1, b: 2} @* 10)", 30.0, "@+ reduce after broadcast");
}

/* Chain: vectorize two maps then reduce — @+ ({a: 1, b: 2} @+ {a: 10, b: 20}) → 33 */
TEST(test_map_compose_reduce_vectorize) {
    assert_vm_number("@+ ({a: 1, b: 2} @+ {a: 10, b: 20})", 33.0, "@+ reduce after vectorize");
}

/* ============================================================
 * Custom function reduction (@fn prefix)
 * ============================================================ */

/* @add [1, 2, 3] with user-defined add function → 6 */
TEST(test_reduce_custom_add) {
    assert_vm_number("fn add(a, b) is a + b end\n@add [1, 2, 3]", 6.0, "@add custom reduce");
}

/* @max [3, 1, 4, 1, 5] with user-defined max → 5 */
TEST(test_reduce_custom_max) {
    assert_vm_number("fn max(a, b) is if a > b then a else b end end\n@max [3, 1, 4, 1, 5]", 5.0,
                     "@max custom reduce");
}

/* @add [42] with single element → 42 */
TEST(test_reduce_custom_single) {
    assert_vm_number("fn add(a, b) is a + b end\n@add [42]", 42.0, "@add single element");
}

/* @add [] with empty array → error */
TEST(test_reduce_custom_empty_error) {
    assert_vm_error("fn add(a, b) is a + b end\n@add []", "@add empty array error");
}

/* Custom reduce with a closure that captures a variable.
 * add_offset(a, b) = a + b + offset where offset = 10.
 * @add_offset [1, 2, 3] = add_offset(add_offset(1, 2), 3)
 *                        = add_offset(13, 3) = 26. */
TEST(test_reduce_custom_closure) {
    assert_vm_number("my offset = 10\nfn add_offset(a, b) is a + b + offset end\n"
                     "@add_offset [1, 2, 3]",
                     26.0, "@add_offset closure reduce");
}

/* ============================================================
 * Custom function vectorization (@fn infix)
 * ============================================================ */

/* [1, 2, 3] @mul [4, 5, 6] with user-defined mul → [4, 10, 18] */
TEST(test_vectorize_custom_mul) {
    assert_vm_formatted("fn mul(a, b) is a * b end\n[1, 2, 3] @mul [4, 5, 6]", "[4, 10, 18]",
                        "@mul custom vectorize");
}

/* [1, 2, 3] @add1 10 with scalar broadcast → [11, 12, 13] */
TEST(test_vectorize_custom_scalar_right) {
    assert_vm_formatted("fn add1(a, b) is a + b end\n[1, 2, 3] @add1 10", "[11, 12, 13]",
                        "@add1 scalar right broadcast");
}

/* Scalar left broadcast: 10 @add1 [1, 2, 3] → [11, 12, 13] */
TEST(test_vectorize_custom_scalar_left) {
    assert_vm_formatted("fn add1(a, b) is a + b end\n10 @add1 [1, 2, 3]", "[11, 12, 13]",
                        "@add1 scalar left broadcast");
}

/* Custom vectorize with length mismatch → error */
TEST(test_vectorize_custom_length_mismatch) {
    assert_vm_error("fn add1(a, b) is a + b end\n[1, 2] @add1 [1, 2, 3]", "@add1 length mismatch");
}

/* Custom vectorize with both scalars → error */
TEST(test_vectorize_custom_both_scalars) {
    assert_vm_error("fn add1(a, b) is a + b end\n1 @add1 2", "@add1 both scalars error");
}

/* Custom vectorize with user-defined max */
TEST(test_vectorize_custom_max) {
    assert_vm_formatted("fn max(a, b) is if a > b then a else b end end\n[1, 5, 3] @max [4, 2, 6]",
                        "[4, 5, 6]", "@max custom vectorize");
}

/* ============================================================
 * Map literals
 * ============================================================ */

/* Eval {} => {} */
TEST(test_map_empty_eval) { assert_vm_formatted("{}", "{}", "empty map"); }

/* Eval {a: 1, b: 2} => {a: 1, b: 2} */
TEST(test_map_bare_keys_eval) {
    assert_vm_formatted("{a: 1, b: 2}", "{a: 1, b: 2}", "bare key map");
}

/* Eval {[1 + 2]: "three"} => {3: three} */
TEST(test_map_computed_key_eval) {
    assert_vm_formatted("{[1 + 2]: \"three\"}", "{3: three}", "computed key map");
}

/* Eval {[true]: "yes", [false]: "no"} => {true: yes, false: no} */
TEST(test_map_bool_keys_eval) {
    assert_vm_formatted("{[true]: \"yes\", [false]: \"no\"}", "{true: yes, false: no}",
                        "bool key map");
}

/* Eval {a: 1, a: 2} => {a: 2} (last value wins for duplicate keys) */
TEST(test_map_duplicate_key_eval) {
    assert_vm_formatted("{a: 1, a: 2}", "{a: 2}", "duplicate key last wins");
}

/* Error: key is a function => runtime error */
TEST(test_map_function_key_error) {
    assert_vm_error("{[fn(x) is x end]: 1}", "function key error");
}

/* ============================================================
 * Map indexing (read)
 * ============================================================ */

/* {a: 1, b: 2}["a"] => 1 */
TEST(test_map_index_get_string_key) {
    assert_vm_number("{a: 1, b: 2}[\"a\"]", 1.0, "map index string key");
}

/* {a: 1, b: 2}["c"] => nothing (missing key) */
TEST(test_map_index_get_missing_key) {
    assert_vm_formatted("{a: 1, b: 2}[\"c\"]", "nothing", "map index missing key");
}

/* {[1]: "one"}[1] => "one" (number key) */
TEST(test_map_index_get_number_key) {
    assert_vm_formatted("{[1]: \"one\"}[1]", "one", "map index number key");
}

/* {[true]: "yes"}[true] => "yes" (boolean key) */
TEST(test_map_index_get_bool_key) {
    assert_vm_formatted("{[true]: \"yes\"}[true]", "yes", "map index bool key");
}

/* Error: invalid key type for map index */
TEST(test_map_index_get_invalid_key) {
    assert_vm_error("{a: 1}[fn(x) is x end]", "map index invalid key");
}

/* ============================================================
 * Map indexing (write / assignment)
 * ============================================================ */

/* my m = {a: 1}\nm["a"] = 99\nm => {a: 99} */
TEST(test_map_index_set_existing) {
    assert_vm_formatted("my m = {a: 1}\nm[\"a\"] = 99\nm", "{a: 99}", "map index set existing");
}

/* my m = {a: 1}\nm["b"] = 2\nm => {a: 1, b: 2} (insert new key) */
TEST(test_map_index_set_new_key) {
    assert_vm_formatted("my m = {a: 1}\nm[\"b\"] = 2\nm", "{a: 1, b: 2}", "map index set new key");
}

/* COW: my m1 = {a: 1}\nmy m2 = m1\nm2["a"] = 99\nm1 => {a: 1} */
TEST(test_map_index_set_cow) {
    assert_vm_formatted("my m1 = {a: 1}\nmy m2 = m1\nm2[\"a\"] = 99\nm1", "{a: 1}",
                        "map COW index set");
}

/* ============================================================
 * Map projection (index with array of keys)
 * ============================================================ */

/* {a: 1, b: 2, c: 3}[["a", "c"]] => {a: 1, c: 3} */
TEST(test_map_projection_basic) {
    assert_vm_formatted("{a: 1, b: 2, c: 3}[[\"a\", \"c\"]]", "{a: 1, c: 3}",
                        "map projection basic");
}

/* {a: 1, b: 2}[["a", "z"]] => {a: 1} (missing keys skipped) */
TEST(test_map_projection_missing_keys) {
    assert_vm_formatted("{a: 1, b: 2}[[\"a\", \"z\"]]", "{a: 1}", "map projection missing keys");
}

/* {a: 1}[[]] => {} (empty key array) */
TEST(test_map_projection_empty_keys) {
    assert_vm_formatted("{a: 1}[[]]", "{}", "map projection empty keys");
}

/* --- Map merge (++) --- */

TEST(test_map_merge_basic) {
    assert_vm_formatted("{a: 1, b: 2} ++ {b: 3, c: 4}", "{a: 1, b: 3, c: 4}", "map merge basic");
}

TEST(test_map_merge_empty_left) {
    assert_vm_formatted("{} ++ {a: 1}", "{a: 1}", "map merge empty left");
}

TEST(test_map_merge_empty_right) {
    assert_vm_formatted("{a: 1} ++ {}", "{a: 1}", "map merge empty right");
}

TEST(test_map_merge_both_empty) { assert_vm_formatted("{} ++ {}", "{}", "map merge both empty"); }

TEST(test_map_merge_map_num_error) { assert_vm_error("{a: 1} ++ 2", "map ++ number"); }

TEST(test_map_merge_string_map_error) { assert_vm_error("\"a\" ++ {b: 1}", "string ++ map"); }

TEST(test_map_merge_array_map_error) { assert_vm_error("[1] ++ {a: 1}", "array ++ map"); }

TEST(test_map_merge_strings_unchanged) {
    assert_vm_string("\"a\" ++ \"b\"", "ab", "string concat still works after map merge");
}

/* --- Map builtin functions: keys(), values(), has_key(), len() --- */

TEST(test_map_keys_basic) {
    assert_vm_formatted("keys({name: \"alice\", age: 30})", "[name, age]", "keys() basic");
}

TEST(test_map_keys_empty) { assert_vm_formatted("keys({})", "[]", "keys() empty map"); }

TEST(test_map_keys_non_map_error) { assert_vm_error("keys(42)", "keys() non-map error"); }

TEST(test_map_values_basic) {
    assert_vm_formatted("values({name: \"alice\", age: 30})", "[alice, 30]", "values() basic");
}

TEST(test_map_values_empty) { assert_vm_formatted("values({})", "[]", "values() empty map"); }

TEST(test_map_values_non_map_error) { assert_vm_error("values(42)", "values() non-map error"); }

TEST(test_map_has_key_true) { assert_vm_bool("has_key({a: 1}, \"a\")", true, "has_key() found"); }

TEST(test_map_has_key_false) {
    assert_vm_bool("has_key({a: 1}, \"b\")", false, "has_key() not found");
}

TEST(test_map_has_key_nothing_value) {
    assert_vm_bool("has_key({a: nothing}, \"a\")", true, "has_key() key with nothing value");
}

TEST(test_map_has_key_empty_map) {
    assert_vm_bool("has_key({}, \"a\")", false, "has_key() empty map");
}

TEST(test_map_has_key_non_map_error) {
    assert_vm_error("has_key(\"str\", \"a\")", "has_key() non-map error");
}

TEST(test_map_len_basic) { assert_vm_number("len({a: 1, b: 2})", 2, "len() map basic"); }

TEST(test_map_len_empty) { assert_vm_number("len({})", 0, "len() empty map"); }

/* ---- Map equality (Step 7) ---- */

TEST(test_map_eq_same_order) {
    assert_vm_bool("{a: 1, b: 2} == {a: 1, b: 2}", true, "map == same order");
}

TEST(test_map_eq_different_order) {
    assert_vm_bool("{a: 1, b: 2} == {b: 2, a: 1}", true, "map == different order");
}

TEST(test_map_eq_different_values) {
    assert_vm_bool("{a: 1} == {a: 2}", false, "map != different value");
}

TEST(test_map_eq_different_count) {
    assert_vm_bool("{a: 1} == {a: 1, b: 2}", false, "map != different count");
}

TEST(test_map_eq_different_type) { assert_vm_bool("{a: 1} == [1]", false, "map != array"); }

TEST(test_map_eq_empty) { assert_vm_bool("{} == {}", true, "empty maps equal"); }

TEST(test_map_neq_true) { assert_vm_bool("{a: 1} != {a: 2}", true, "map != true"); }

TEST(test_map_neq_false) { assert_vm_bool("{a: 1} != {a: 1}", false, "map != false"); }

/* ============================================================
 * Zip-map (@:) operator
 * ============================================================ */

/* ["a", "b", "c"] @: [1, 2, 3] => {a: 1, b: 2, c: 3} */
TEST(test_zip_map_basic) {
    assert_vm_formatted("[\"a\", \"b\", \"c\"] @: [1, 2, 3]", "{a: 1, b: 2, c: 3}",
                        "@: basic string keys");
}

/* [] @: [] => {} */
TEST(test_zip_map_empty) { assert_vm_formatted("[] @: []", "{}", "@: empty arrays"); }

/* ["x"] @: [42] => {x: 42} */
TEST(test_zip_map_single) {
    assert_vm_formatted("[\"x\"] @: [42]", "{x: 42}", "@: single element");
}

/* [1, 2] @: ["one", "two"] => {1: one, 2: two} (number keys) */
TEST(test_zip_map_number_keys) {
    assert_vm_formatted("[1, 2] @: [\"one\", \"two\"]", "{1: one, 2: two}", "@: number keys");
}

/* [true, false] @: ["yes", "no"] => {true: yes, false: no} (boolean keys) */
TEST(test_zip_map_bool_keys) {
    assert_vm_formatted("[true, false] @: [\"yes\", \"no\"]", "{true: yes, false: no}",
                        "@: boolean keys");
}

/* Duplicate keys: ["a", "b", "a"] @: [1, 2, 3] => {a: 3, b: 2} (last wins) */
TEST(test_zip_map_duplicate_keys) {
    assert_vm_formatted("[\"a\", \"b\", \"a\"] @: [1, 2, 3]", "{a: 3, b: 2}",
                        "@: duplicate keys last wins");
}

/* Mismatched lengths: ["a"] @: [1, 2] => runtime error */
TEST(test_zip_map_length_mismatch) { assert_vm_error("[\"a\"] @: [1, 2]", "@: length mismatch"); }

/* Non-array left operand: "a" @: [1] => runtime error */
TEST(test_zip_map_non_array_left) { assert_vm_error("\"a\" @: [1]", "@: non-array left"); }

/* Non-array right operand: [1] @: "a" => runtime error */
TEST(test_zip_map_non_array_right) { assert_vm_error("[1] @: \"a\"", "@: non-array right"); }

/* Invalid key type: [fn(x) is x end] @: [1] => runtime error */
TEST(test_zip_map_invalid_key_type) {
    assert_vm_error("[fn(x) is x end] @: [1]", "@: invalid key type");
}

/* ============================================================
 * Zip-map (@:) composability
 * ============================================================ */

/* Variables: my names = ["a", "b"]; my scores = [10, 20]; names @: scores */
TEST(test_zip_map_with_variables) {
    assert_vm_formatted("my names = [\"a\", \"b\"]\n"
                        "my scores = [10, 20]\n"
                        "names @: scores",
                        "{a: 10, b: 20}", "@: with variables");
}

/* Compose with @*: ["a", "b"] @: ([10, 20] @* 2) => {a: 20, b: 40} */
TEST(test_zip_map_compose_vectorize) {
    assert_vm_formatted("[\"a\", \"b\"] @: ([10, 20] @* 2)", "{a: 20, b: 40}",
                        "@: compose with @*");
}

/* Map inversion: values(m) @: keys(m) */
TEST(test_zip_map_inversion) {
    assert_vm_formatted("my m = {x: 1, y: 2}\n"
                        "values(m) @: keys(m)",
                        "{1: x, 2: y}", "@: map inversion");
}

/* Round-trip: keys(m) @: values(m) => original map */
TEST(test_zip_map_round_trip) {
    assert_vm_formatted("my m = {a: 1, b: 2}\n"
                        "keys(m) @: values(m)",
                        "{a: 1, b: 2}", "@: round-trip");
}

/* ============================================================
 * `in` membership operator
 * ============================================================ */

/* --- Map membership --- */

TEST(test_in_map_key_found) { assert_vm_bool("\"a\" in {a: 1, b: 2}", true, "in map found"); }

TEST(test_in_map_key_missing) { assert_vm_bool("\"c\" in {a: 1, b: 2}", false, "in map missing"); }

TEST(test_in_map_empty) { assert_vm_bool("\"a\" in {}", false, "in empty map"); }

TEST(test_in_map_number_key) { assert_vm_bool("1 in {[1]: \"one\"}", true, "in map number key"); }

TEST(test_in_map_bool_key) { assert_vm_bool("true in {[true]: \"yes\"}", true, "in map bool key"); }

TEST(test_in_map_nothing_value) {
    assert_vm_bool("\"a\" in {a: nothing}", true, "in map key with nothing value");
}

/* --- Array membership --- */

TEST(test_in_array_found) { assert_vm_bool("42 in [1, 2, 42]", true, "in array found"); }

TEST(test_in_array_missing) { assert_vm_bool("99 in [1, 2, 3]", false, "in array missing"); }

TEST(test_in_array_string) {
    assert_vm_bool("\"b\" in [\"a\", \"b\", \"c\"]", true, "in array string");
}

TEST(test_in_array_bool) { assert_vm_bool("true in [1, true, \"hi\"]", true, "in array bool"); }

TEST(test_in_array_empty) { assert_vm_bool("1 in []", false, "in empty array"); }

/* --- String membership (substring search) --- */

TEST(test_in_string_found) { assert_vm_bool("\"lo\" in \"hello\"", true, "in string found"); }

TEST(test_in_string_missing) { assert_vm_bool("\"xyz\" in \"hello\"", false, "in string missing"); }

TEST(test_in_string_empty_needle) {
    assert_vm_bool("\"\" in \"hello\"", true, "in string empty needle");
}

TEST(test_in_string_full_match) {
    assert_vm_bool("\"hello\" in \"hello\"", true, "in string full match");
}

TEST(test_in_string_non_string_lhs) {
    assert_vm_error("42 in \"hello\"", "in string non-string lhs");
}

/* --- `not in` sugar --- */

TEST(test_not_in_array_true) { assert_vm_bool("10 not in [1, 2, 3]", true, "not in array true"); }

TEST(test_not_in_array_false) { assert_vm_bool("1 not in [1, 2, 3]", false, "not in array false"); }

TEST(test_not_in_map_true) { assert_vm_bool("\"z\" not in {a: 1}", true, "not in map true"); }

TEST(test_not_in_map_false) { assert_vm_bool("\"a\" not in {a: 1}", false, "not in map false"); }

TEST(test_not_in_string_true) {
    assert_vm_bool("\"xyz\" not in \"hello\"", true, "not in string true");
}

/* --- Error cases --- */

TEST(test_in_number_error) { assert_vm_error("1 in 42", "in with number"); }

TEST(test_in_bool_error) { assert_vm_error("1 in true", "in with boolean"); }

TEST(test_in_nothing_error) { assert_vm_error("1 in nothing", "in with nothing"); }

/* --- Precedence --- */

/* Arithmetic binds tighter than `in`: (1+1) in [2,3] → true */
TEST(test_in_precedence_arithmetic) {
    assert_vm_bool("1 + 1 in [2, 3]", true, "arithmetic before in");
}

/* `and` binds looser than `in`: true and (1 in [1,2]) → true */
TEST(test_in_precedence_and) { assert_vm_bool("true and 1 in [1, 2]", true, "in before and"); }

/* `not` binds looser than `in`: not (5 in [1,2,3]) → true */
TEST(test_in_precedence_not) { assert_vm_bool("not 5 in [1, 2, 3]", true, "not after in"); }

/* --- Edge cases --- */

/* nothing as a map key: nothing in {[nothing]: 1} → true */
TEST(test_in_nothing_as_map_key) {
    assert_vm_bool("nothing in {[nothing]: 1}", true, "nothing as map key");
}

/* nothing in an array: nothing in [nothing, 1, 2] → true */
TEST(test_in_nothing_in_array) {
    assert_vm_bool("nothing in [nothing, 1, 2]", true, "nothing in array");
}

/* Empty string in empty string: "" in "" → true */
TEST(test_in_empty_string_in_empty_string) {
    assert_vm_bool("\"\" in \"\"", true, "empty string in empty string");
}

/* Composability with keys(): "a" in keys({a: 1, b: 2}) → true */
TEST(test_in_composable_with_keys) {
    assert_vm_bool("\"a\" in keys({a: 1, b: 2})", true, "in with keys()");
}

/* Compound expression: not in + in with variables and `and` */
TEST(test_in_compound_not_in_and_in) {
    assert_vm_bool("my xs = [1, 2, 3]\n5 not in xs and 1 in xs", true, "not in and in compound");
}

/* ============================================================
 * Dot access (get)
 * ============================================================ */

TEST(test_dot_get_basic) { assert_vm_string("{name: \"alice\"}.name", "alice", "dot get basic"); }

TEST(test_dot_get_second_key) { assert_vm_number("{a: 1, b: 2}.b", 2.0, "dot get second key"); }

TEST(test_dot_get_missing_key) {
    assert_vm_nothing("{a: 1}.z", "dot get missing key returns nothing");
}

TEST(test_dot_get_variable) {
    assert_vm_number("my m = {x: 10}\nm.x", 10.0, "dot get via variable");
}

/* ============================================================
 * Dot access (set)
 * ============================================================ */

TEST(test_dot_set_overwrite) {
    assert_vm_string("my obj = {name: \"old\"}\nobj.name = \"new\"\nobj.name", "new",
                     "dot set overwrite");
}

TEST(test_dot_set_new_key) {
    assert_vm_number("my obj = {a: 1}\nobj.b = 2\nobj.b", 2.0, "dot set new key");
}

TEST(test_dot_set_overwrite_number) {
    assert_vm_number("my obj = {a: 1}\nobj.a = 2\nobj.a", 2.0, "dot set overwrite number");
}

/* ============================================================
 * Chained dot access
 * ============================================================ */

TEST(test_dot_chain_two) {
    assert_vm_number("{a: {b: 42}}.a.b", 42.0, "chained dot access two levels");
}

TEST(test_dot_chain_three) {
    assert_vm_string("{a: {b: {c: \"deep\"}}}.a.b.c", "deep", "chained dot access three levels");
}

TEST(test_dot_chain_variable) {
    assert_vm_number("my m = {a: {b: 99}}\nm.a.b", 99.0, "chained dot via variable");
}

/* ============================================================
 * Method calls
 * ============================================================ */

TEST(test_method_call_no_arg) {
    assert_vm_string("my obj = {greet: fn(self) is \"hello\" end}\nobj.greet()", "hello",
                     "method call no args");
}

TEST(test_method_call_self_access) {
    assert_vm_string(
        "my obj = {name: \"alice\", get_name: fn(self) is self.name end}\nobj.get_name()", "alice",
        "method accesses self");
}

TEST(test_method_call_with_arg) {
    assert_vm_number("my obj = {x: 10, add: fn(self, n) is self.x + n end}\nobj.add(5)", 15.0,
                     "method with argument");
}

TEST(test_method_call_multi_fields) {
    assert_vm_number("my obj = {x: 1, y: 2, sum: fn(self) is self.x + self.y end}\nobj.sum()", 3.0,
                     "method accessing multiple fields");
}

TEST(test_method_call_chained_self) {
    assert_vm_number("my inner = {val: 42, get: fn(self) is self.val end}\n"
                     "my outer = {inner: inner}\n"
                     "outer.inner.get()",
                     42.0, "chained dot then method call, self is inner");
}

/* ============================================================
 * Method call edge cases
 * ============================================================ */

TEST(test_method_call_missing_key) {
    assert_vm_error("my obj = {name: \"test\"}\nobj.missing()", "call nothing");
}

TEST(test_method_call_not_callable) {
    assert_vm_error("my obj = {x: 42}\nobj.x()", "call non-function");
}

TEST(test_dot_on_non_map) { assert_vm_error("42.name", "dot on non-map"); }

/* ============================================================
 * Dot access + method call integration / edge cases
 * ============================================================ */

/* Dot access works together with `in` operator. */
TEST(test_dot_access_with_in) {
    assert_vm_bool("my obj = {name: \"alice\"}\n\"name\" in obj and obj.name == \"alice\"", true,
                   "dot access works with in");
}

/* Bracket index after dot access: obj.items[1] chains correctly. */
TEST(test_dot_then_bracket_index) {
    assert_vm_number("my obj = {items: [1, 2, 3]}\nobj.items[1]", 2.0,
                     "bracket index after dot access");
}

/* Chained method calls returning self — should not error. */
TEST(test_chained_method_calls_returning_self) {
    assert_vm_not_error("my obj = {f: fn(self) is self end}\nobj.f().f().f()",
                        "chained method calls returning self");
}

/* Function assigned from variable, called as method. */
TEST(test_method_call_fn_from_variable) {
    assert_vm_number("my f = fn(self, a, b) is a + b end\n"
                     "my obj = {calc: f}\n"
                     "obj.calc(10, 20)",
                     30.0, "function from variable called as method");
}

/* ============================================================
 * Main
 * ============================================================ */

int main(void) {
    runtime_init();
    printf("Running VM tests...\n\n");

    printf("Basic arithmetic:\n");
    RUN_TEST(test_add);
    RUN_TEST(test_sub);
    RUN_TEST(test_mul);
    RUN_TEST(test_div_exact);
    RUN_TEST(test_div_float);
    RUN_TEST(test_div_float2);

    printf("\nExponentiation:\n");
    RUN_TEST(test_exp_basic);
    RUN_TEST(test_exp_right_assoc);
    RUN_TEST(test_exp_zero);

    printf("\nUnary minus:\n");
    RUN_TEST(test_unary_neg);
    RUN_TEST(test_unary_double_neg);
    RUN_TEST(test_unary_neg_in_expr);

    printf("\nPrecedence:\n");
    RUN_TEST(test_precedence_add_mul);
    RUN_TEST(test_precedence_parens);
    RUN_TEST(test_precedence_complex);

    printf("\nError cases:\n");
    RUN_TEST(test_div_by_zero);
    RUN_TEST(test_unknown_ident);

    printf("\nString literal:\n");
    RUN_TEST(test_string_value);

    printf("\nNumber formatting:\n");
    RUN_TEST(test_format_integer);
    RUN_TEST(test_format_float);
    RUN_TEST(test_format_negative);
    RUN_TEST(test_format_string_val);
    RUN_TEST(test_format_error_val);

    printf("\nBoolean literals:\n");
    RUN_TEST(test_bool_true_eval);
    RUN_TEST(test_bool_false_eval);
    RUN_TEST(test_bool_true_format);
    RUN_TEST(test_bool_false_format);
    RUN_TEST(test_bool_true_assign_error);
    RUN_TEST(test_bool_true_decl_error);

    printf("\nComparison operators:\n");
    RUN_TEST(test_cmp_num_eq_true);
    RUN_TEST(test_cmp_num_eq_false);
    RUN_TEST(test_cmp_str_eq_true);
    RUN_TEST(test_cmp_str_eq_false);
    RUN_TEST(test_cmp_bool_eq_true);
    RUN_TEST(test_cmp_bool_eq_false);
    RUN_TEST(test_cmp_mixed_eq);
    RUN_TEST(test_cmp_num_neq_true);
    RUN_TEST(test_cmp_num_neq_false);
    RUN_TEST(test_cmp_mixed_neq);
    RUN_TEST(test_cmp_lt_true);
    RUN_TEST(test_cmp_lt_false);
    RUN_TEST(test_cmp_lt_equal);
    RUN_TEST(test_cmp_str_lt);
    RUN_TEST(test_cmp_gt_true);
    RUN_TEST(test_cmp_gt_false);
    RUN_TEST(test_cmp_lte_lt);
    RUN_TEST(test_cmp_lte_eq);
    RUN_TEST(test_cmp_lte_gt);
    RUN_TEST(test_cmp_gte_gt);
    RUN_TEST(test_cmp_gte_eq);
    RUN_TEST(test_cmp_gte_lt);
    RUN_TEST(test_cmp_mixed_lt_error);
    RUN_TEST(test_cmp_mixed_gt_error);
    RUN_TEST(test_cmp_mixed_lte_error);
    RUN_TEST(test_cmp_mixed_gte_error);
    RUN_TEST(test_cmp_bool_lt_error);
    RUN_TEST(test_cmp_bool_gt_error);
    RUN_TEST(test_cmp_bool_lte_error);
    RUN_TEST(test_cmp_bool_gte_error);
    RUN_TEST(test_cmp_precedence_add);
    RUN_TEST(test_cmp_str_gt);
    RUN_TEST(test_cmp_str_lte);
    RUN_TEST(test_cmp_str_gte);

    printf("\nLogical operators:\n");
    RUN_TEST(test_logic_and_tt);
    RUN_TEST(test_logic_and_tf);
    RUN_TEST(test_logic_and_ft);
    RUN_TEST(test_logic_and_ff);
    RUN_TEST(test_logic_or_tt);
    RUN_TEST(test_logic_or_ft);
    RUN_TEST(test_logic_or_ff);
    RUN_TEST(test_logic_not_true);
    RUN_TEST(test_logic_not_false);
    RUN_TEST(test_logic_not_zero);
    RUN_TEST(test_logic_not_one);
    RUN_TEST(test_logic_not_empty_str);
    RUN_TEST(test_logic_not_nonempty_str);
    RUN_TEST(test_logic_and_numbers);
    RUN_TEST(test_logic_and_zero_short);
    RUN_TEST(test_logic_or_numbers);
    RUN_TEST(test_logic_or_falsy_last);
    RUN_TEST(test_logic_prec_or_and);
    RUN_TEST(test_logic_not_lt);
    RUN_TEST(test_logic_not_and);
    RUN_TEST(test_logic_short_circuit_and);
    RUN_TEST(test_logic_short_circuit_or);
    RUN_TEST(test_logic_and_assign_error);
    RUN_TEST(test_logic_or_assign_error);
    RUN_TEST(test_logic_not_assign_error);
    RUN_TEST(test_logic_and_decl_error);
    RUN_TEST(test_logic_or_decl_error);
    RUN_TEST(test_logic_not_decl_error);

    printf("\nNothing literal:\n");
    RUN_TEST(test_nothing_eval);
    RUN_TEST(test_nothing_eq_nothing);
    RUN_TEST(test_nothing_eq_false);
    RUN_TEST(test_nothing_eq_zero);
    RUN_TEST(test_nothing_eq_empty_str);
    RUN_TEST(test_nothing_neq_one);
    RUN_TEST(test_nothing_lt_error);
    RUN_TEST(test_nothing_gt_error);
    RUN_TEST(test_nothing_lte_error);
    RUN_TEST(test_nothing_gte_error);
    RUN_TEST(test_not_nothing);
    RUN_TEST(test_nothing_and_one);
    RUN_TEST(test_nothing_or_one);
    RUN_TEST(test_nothing_format);
    RUN_TEST(test_nothing_assign_error);
    RUN_TEST(test_nothing_decl_error);

    printf("\nMulti-line input / blocks:\n");
    RUN_TEST(test_block_returns_last);
    RUN_TEST(test_block_decl_then_use);
    RUN_TEST(test_block_multiple_decls);
    RUN_TEST(test_block_reassign);
    RUN_TEST(test_block_blank_lines);
    RUN_TEST(test_block_with_comparison);
    RUN_TEST(test_block_with_logic);
    RUN_TEST(test_block_single_expr_not_wrapped);

    printf("\nIf/else expressions:\n");
    RUN_TEST(test_if_true_then_else);
    RUN_TEST(test_if_false_then_else);
    RUN_TEST(test_if_false_no_else);
    RUN_TEST(test_if_true_no_else);
    RUN_TEST(test_if_comparison_cond);
    RUN_TEST(test_if_comparison_cond_false);
    RUN_TEST(test_if_in_assignment);
    RUN_TEST(test_if_multiline_body);
    RUN_TEST(test_if_nested_inner_taken);
    RUN_TEST(test_if_nested_outer_false);
    RUN_TEST(test_if_else_if);
    RUN_TEST(test_if_else_if_chain);
    RUN_TEST(test_if_short_circuit_then);
    RUN_TEST(test_if_short_circuit_else);
    RUN_TEST(test_if_truthy_number);
    RUN_TEST(test_if_falsy_zero);
    RUN_TEST(test_if_falsy_empty_string);
    RUN_TEST(test_if_falsy_nothing);
    RUN_TEST(test_if_in_expression);
    RUN_TEST(test_if_complex_condition);

    printf("\nsay() built-in function:\n");
    RUN_TEST(test_say_string);
    RUN_TEST(test_say_number);
    RUN_TEST(test_say_bool);
    RUN_TEST(test_say_nothing);
    RUN_TEST(test_say_expression);
    RUN_TEST(test_say_no_args);
    RUN_TEST(test_say_too_many_args);
    RUN_TEST(test_call_unknown_function);
    RUN_TEST(test_say_error_propagation);
    RUN_TEST(test_say_null_write_fn);
    RUN_TEST(test_say_returns_nothing_in_expr);
    RUN_TEST(test_say_multiple_in_block);
    RUN_TEST(test_say_as_variable);

    printf("\nComment eval:\n");
    RUN_TEST(test_comment_trailing_eval);
    RUN_TEST(test_comment_multiline_eval);

    printf("\nLine numbers in errors:\n");
    RUN_TEST(test_error_line_number);

    printf("\nStack overflow protection:\n");
    RUN_TEST(test_stack_overflow);

    printf("\nStack underflow protection (peek):\n");
    RUN_TEST(test_peek_empty_stack);

    printf("\nStack underflow protection (pop):\n");
    RUN_TEST(test_pop_empty_stack);

    printf("\nLeaf nodes:\n");
    RUN_TEST(test_single_number);
    RUN_TEST(test_single_zero);

    printf("\nModulo operator:\n");
    RUN_TEST(test_mod_basic);
    RUN_TEST(test_mod_exact);
    RUN_TEST(test_mod_neg_dividend);
    RUN_TEST(test_mod_neg_divisor);
    RUN_TEST(test_mod_both_neg);
    RUN_TEST(test_mod_float);
    RUN_TEST(test_mod_by_zero);
    RUN_TEST(test_mod_string_error);
    RUN_TEST(test_mod_bool_error);

    printf("\nWhile loop:\n");
    RUN_TEST(test_while_runs_n_times);
    RUN_TEST(test_while_never_runs);
    RUN_TEST(test_while_last_body_value);
    RUN_TEST(test_while_with_say);
    RUN_TEST(test_while_nested);
    RUN_TEST(test_while_as_expression);
    RUN_TEST(test_while_never_runs_expr);

    printf("\nBreak and continue:\n");
    RUN_TEST(test_break_exits_loop);
    RUN_TEST(test_break_with_value);
    RUN_TEST(test_bare_break_returns_nothing);
    RUN_TEST(test_break_from_if_inside_loop);
    RUN_TEST(test_continue_skips_iteration);
    RUN_TEST(test_continue_last_iteration_nothing);
    RUN_TEST(test_nested_break_affects_inner);
    RUN_TEST(test_nested_continue_affects_inner);
    RUN_TEST(test_break_outside_loop_error);
    RUN_TEST(test_continue_outside_loop_error);
    RUN_TEST(test_break_with_say);

    printf("\nString concatenation operator (++):\n");
    RUN_TEST(test_concat_strings);
    RUN_TEST(test_concat_empty_left);
    RUN_TEST(test_concat_empty_right);
    RUN_TEST(test_concat_both_empty);
    RUN_TEST(test_concat_str_num);
    RUN_TEST(test_concat_num_str);
    RUN_TEST(test_concat_bool_str);
    RUN_TEST(test_concat_nothing_str);
    RUN_TEST(test_concat_num_num);
    RUN_TEST(test_concat_float_str);
    RUN_TEST(test_concat_str_explicit_num);
    RUN_TEST(test_concat_str_explicit_bool);
    RUN_TEST(test_concat_str_explicit_nothing);
    RUN_TEST(test_concat_chained);
    RUN_TEST(test_concat_with_var);
    RUN_TEST(test_concat_precedence);
    RUN_TEST(test_add_strings_error);

    printf("\nstr() built-in:\n");
    RUN_TEST(test_str_integer);
    RUN_TEST(test_str_float);
    RUN_TEST(test_str_true);
    RUN_TEST(test_str_false);
    RUN_TEST(test_str_nothing);
    RUN_TEST(test_str_string_identity);
    RUN_TEST(test_str_compose_concat);

    printf("\nVAL_FUNCTION value type:\n");
    RUN_TEST(test_fn_format_named);
    RUN_TEST(test_fn_format_anonymous);
    RUN_TEST(test_fn_clone_independence);
    RUN_TEST(test_fn_free_no_leak);
    RUN_TEST(test_fn_is_truthy);
    RUN_TEST(test_native_fn_create);
    RUN_TEST(test_native_fn_format);
    RUN_TEST(test_fn_concat_coercion);

    printf("\nFunction definitions (compile + eval):\n");
    RUN_TEST(test_fn_def_creates_global);
    RUN_TEST(test_fn_def_evaluates_to_function);
    RUN_TEST(test_fn_def_format_via_global);

    printf("\nStack-based call dispatch:\n");
    RUN_TEST(test_call_number_error);
    RUN_TEST(test_call_bool_error);

    printf("\nVM call frames (user function call/return):\n");
    RUN_TEST(test_user_fn_call_returns_value);
    RUN_TEST(test_user_fn_call_with_say);
    RUN_TEST(test_user_fn_call_result_used_in_say);

    printf("\nFunction parameters (OP_GET_LOCAL):\n");
    RUN_TEST(test_fn_param_identity);
    RUN_TEST(test_fn_param_add);
    RUN_TEST(test_fn_param_shadows_global);
    RUN_TEST(test_fn_param_falls_back_to_global);

    printf("\nLocal variable declarations (OP_SET_LOCAL):\n");
    RUN_TEST(test_fn_local_decl_and_use);
    RUN_TEST(test_fn_multiple_locals);
    RUN_TEST(test_fn_reassign_param);
    RUN_TEST(test_fn_local_doesnt_leak);

    printf("\nRecursion:\n");
    RUN_TEST(test_recursion_factorial);
    RUN_TEST(test_recursion_fibonacci);
    RUN_TEST(test_recursion_mutual);

    printf("\nUser function error handling:\n");
    RUN_TEST(test_user_fn_arity_too_few);
    RUN_TEST(test_user_fn_arity_too_many);
    RUN_TEST(test_user_fn_arity_zero_called_with_args);
    RUN_TEST(test_call_stack_overflow_recursion);
    RUN_TEST(test_fn_error_has_line_number);
    RUN_TEST(test_fn_arity_error_has_line_number);

    printf("\nAnonymous functions:\n");
    RUN_TEST(test_anon_fn_assign_and_call);
    RUN_TEST(test_anon_fn_no_params);
    RUN_TEST(test_anon_fn_two_params);
    RUN_TEST(test_anon_fn_formats_as_fn);
    RUN_TEST(test_anon_fn_with_say);
    RUN_TEST(test_anon_fn_arity_error);
    RUN_TEST(test_anon_fn_as_expression);

    printf("\nHigher-order functions (local callee resolution):\n");
    RUN_TEST(test_higher_order_apply);
    RUN_TEST(test_higher_order_builtin);
    RUN_TEST(test_higher_order_nested);
    RUN_TEST(test_local_var_as_callee);

    printf("\nNested named functions (lexical scoping):\n");
    RUN_TEST(test_nested_fn_call_works);
    RUN_TEST(test_nested_fn_not_visible_globally);

    printf("\nClosure infrastructure:\n");
    RUN_TEST(test_say_displays_closure);
    RUN_TEST(test_say_hello_with_closures);
    RUN_TEST(test_closure_as_return_value);
    RUN_TEST(test_closure_equality_different_instances);
    RUN_TEST(test_closure_equality_same_instance);

    printf("\nUpvalue capture (OP_GET_UPVALUE / OP_SET_UPVALUE):\n");
    RUN_TEST(test_upvalue_read_outer);
    RUN_TEST(test_upvalue_mutation_visible);
    RUN_TEST(test_upvalue_shared_between_closures);

    printf("\nClosing upvalues on function return:\n");
    RUN_TEST(test_close_upvalue_outlive_creator);
    RUN_TEST(test_close_upvalue_counter);
    RUN_TEST(test_close_upvalue_shared);

    printf("\nDecimal number literals:\n");
    RUN_TEST(test_decimal_literal_half);
    RUN_TEST(test_decimal_literal_add);
    RUN_TEST(test_decimal_literal_mul);
    RUN_TEST(test_decimal_literal_eq_int);
    RUN_TEST(test_decimal_literal_say);

    printf("\nSingle-line forms (if, while, fn):\n");
    RUN_TEST(test_sl_if_true);
    RUN_TEST(test_sl_if_false_nothing);
    RUN_TEST(test_sl_if_else_true);
    RUN_TEST(test_sl_if_else_false);
    RUN_TEST(test_sl_while_counter);
    RUN_TEST(test_sl_while_break_value);
    RUN_TEST(test_sl_fn_named_call);
    RUN_TEST(test_sl_fn_anon_call);
    RUN_TEST(test_sl_nested_if);
    RUN_TEST(test_sl_fn_as_arg);

    printf("\nBlock scoping — if body:\n");
    RUN_TEST(test_block_scope_if_visible_inside);
    RUN_TEST(test_block_scope_if_not_visible_outside);
    RUN_TEST(test_block_scope_if_shadow_inner);
    RUN_TEST(test_block_scope_if_shadow_outer_preserved);
    RUN_TEST(test_block_scope_else_own_scope);

    printf("\nBlock scoping — while body:\n");
    RUN_TEST(test_block_scope_while_no_corruption);
    RUN_TEST(test_block_scope_while_not_visible_outside);
    RUN_TEST(test_block_scope_while_accumulator);

    printf("\nBlock scoping — break/continue cleanup:\n");
    RUN_TEST(test_block_scope_break_cleanup);
    RUN_TEST(test_block_scope_break_from_if);
    RUN_TEST(test_block_scope_continue_cleanup);

    printf("\nBlock scoping — nested scopes:\n");
    RUN_TEST(test_block_scope_nested_if_inner_sees_outer);
    RUN_TEST(test_block_scope_sequential_if);
    RUN_TEST(test_block_scope_if_inside_while);
    RUN_TEST(test_block_scope_three_level_nesting);

    printf("\nClosure integration tests:\n");
    RUN_TEST(test_closure_counter_pattern);
    RUN_TEST(test_closure_adder_factory);
    RUN_TEST(test_closure_adder_independent);
    RUN_TEST(test_closure_shared_capture);
    RUN_TEST(test_closure_deep_nesting);
    RUN_TEST(test_closure_deep_nesting_mutation);
    RUN_TEST(test_closure_deep_nesting_outlive);
    RUN_TEST(test_closure_capture_parameter);
    RUN_TEST(test_closure_capture_multiple_params);
    RUN_TEST(test_closure_with_recursion);
    RUN_TEST(test_closure_with_while_loop);
    RUN_TEST(test_closure_called_in_loop);
    RUN_TEST(test_closure_error_call_non_function);
    RUN_TEST(test_closure_error_arity_named);
    RUN_TEST(test_closure_error_arity_anonymous);
    RUN_TEST(test_closure_counter_with_say);

    printf("\nReturn keyword:\n");
    RUN_TEST(test_return_basic);
    RUN_TEST(test_return_bare);
    RUN_TEST(test_return_nothing_explicit);
    RUN_TEST(test_return_early_guard);
    RUN_TEST(test_return_early_guard_fallthrough);
    RUN_TEST(test_return_from_while_loop);
    RUN_TEST(test_return_from_nested_loops);
    RUN_TEST(test_return_with_block_scoped_locals);
    RUN_TEST(test_return_top_level_error);
    RUN_TEST(test_return_single_line_fn);
    RUN_TEST(test_return_last_expr_default);

    printf("\nKebab-case identifiers (integration):\n");
    RUN_TEST(test_kebab_var_decl_and_read);
    RUN_TEST(test_kebab_var_assignment);
    RUN_TEST(test_kebab_distinct_from_underscore);
    RUN_TEST(test_kebab_fn_def_and_call);
    RUN_TEST(test_kebab_param_names);
    RUN_TEST(test_kebab_closure_capture);
    RUN_TEST(test_kebab_ident_in_subtraction_expr);

    printf("\nArray literals:\n");
    RUN_TEST(test_array_empty_eval);
    RUN_TEST(test_array_numbers_eval);
    RUN_TEST(test_array_expressions_eval);
    RUN_TEST(test_array_nested_eval);

    printf("\nArray indexing:\n");
    RUN_TEST(test_index_first);
    RUN_TEST(test_index_last);
    RUN_TEST(test_index_negative_one);
    RUN_TEST(test_index_negative_three);
    RUN_TEST(test_index_oob_positive);
    RUN_TEST(test_index_oob_negative);
    RUN_TEST(test_index_non_array);
    RUN_TEST(test_index_non_integer);
    RUN_TEST(test_index_assign_eval);
    RUN_TEST(test_index_assign_cow);
    RUN_TEST(test_index_nested);
    RUN_TEST(test_index_assign_returns_value);

    printf("\nBoolean mask indexing:\n");
    RUN_TEST(test_mask_basic);
    RUN_TEST(test_mask_all_false);
    RUN_TEST(test_mask_all_true);
    RUN_TEST(test_mask_with_vectorize);
    RUN_TEST(test_mask_length_mismatch);
    RUN_TEST(test_mask_non_boolean);

    printf("\nArray concatenation (++):\n");
    RUN_TEST(test_concat_arrays_basic);
    RUN_TEST(test_concat_array_empty_left);
    RUN_TEST(test_concat_array_empty_right);
    RUN_TEST(test_concat_array_both_empty);
    RUN_TEST(test_concat_array_nested);
    RUN_TEST(test_concat_array_num_error);
    RUN_TEST(test_concat_num_array_error);
    RUN_TEST(test_concat_strings_still_works);

    printf("\nBuilt-in array functions (len, push, pop):\n");
    RUN_TEST(test_len_array);
    RUN_TEST(test_len_empty);
    RUN_TEST(test_len_string);
    RUN_TEST(test_len_empty_string);
    RUN_TEST(test_len_number_error);
    RUN_TEST(test_len_bool_error);
    RUN_TEST(test_push_basic);
    RUN_TEST(test_push_empty);
    RUN_TEST(test_push_no_mutate);
    RUN_TEST(test_push_returns_new);
    RUN_TEST(test_push_non_array_error);
    RUN_TEST(test_pop_basic);
    RUN_TEST(test_pop_single);
    RUN_TEST(test_pop_empty_error);
    RUN_TEST(test_pop_no_mutate);
    RUN_TEST(test_pop_non_array_error);
    RUN_TEST(test_len_push_compose);

    printf("\nArray equality:\n");
    RUN_TEST(test_array_eq_same);
    RUN_TEST(test_array_eq_diff_len);
    RUN_TEST(test_array_eq_diff_elem);
    RUN_TEST(test_array_eq_empty);
    RUN_TEST(test_array_eq_diff_type);
    RUN_TEST(test_array_neq_same);
    RUN_TEST(test_array_neq_diff);
    RUN_TEST(test_array_eq_nested);
    RUN_TEST(test_array_eq_nested_diff);

    /* ---- @op prefix — reduction ---- */
    printf("\nReduce (@op prefix):\n");
    RUN_TEST(test_reduce_add);
    RUN_TEST(test_reduce_multiply);
    RUN_TEST(test_reduce_subtract);
    RUN_TEST(test_reduce_concat);
    RUN_TEST(test_reduce_single_element);
    RUN_TEST(test_reduce_empty_error);
    RUN_TEST(test_reduce_and_short_circuit);
    RUN_TEST(test_reduce_and_all_truthy);
    RUN_TEST(test_reduce_or_first_truthy);
    RUN_TEST(test_reduce_or_all_falsy);
    RUN_TEST(test_reduce_equal_fold);
    RUN_TEST(test_reduce_non_array_error);

    /* ---- @op prefix on maps — map reduction ---- */
    printf("\nMap reduce (@op prefix on maps):\n");
    RUN_TEST(test_reduce_map_add);
    RUN_TEST(test_reduce_map_multiply);
    RUN_TEST(test_reduce_map_subtract);
    RUN_TEST(test_reduce_map_concat);
    RUN_TEST(test_reduce_map_single);
    RUN_TEST(test_reduce_map_empty_error);
    RUN_TEST(test_reduce_map_and);
    RUN_TEST(test_reduce_map_or);

    /* ---- @op infix — vectorize ---- */
    printf("\nVectorize (@op infix):\n");
    RUN_TEST(test_vectorize_add);
    RUN_TEST(test_vectorize_scalar_right);
    RUN_TEST(test_vectorize_scalar_left);
    RUN_TEST(test_vectorize_power);
    RUN_TEST(test_vectorize_greater);
    RUN_TEST(test_vectorize_length_mismatch);
    RUN_TEST(test_vectorize_both_scalars_error);
    RUN_TEST(test_vectorize_concat);
    RUN_TEST(test_vectorize_and);
    RUN_TEST(test_vectorize_precedence);

    /* ---- Map vectorize (@op with maps) ---- */
    printf("\nMap vectorize (@op infix with maps):\n");
    RUN_TEST(test_vectorize_map_map_add);
    RUN_TEST(test_vectorize_map_map_intersection);
    RUN_TEST(test_vectorize_map_map_no_shared);
    RUN_TEST(test_vectorize_map_map_subtract);
    RUN_TEST(test_vectorize_map_map_greater);
    RUN_TEST(test_vectorize_map_scalar_mul);
    RUN_TEST(test_vectorize_map_scalar_gte);
    RUN_TEST(test_vectorize_map_scalar_power);
    RUN_TEST(test_vectorize_scalar_map_sub);
    RUN_TEST(test_vectorize_scalar_map_mul);
    RUN_TEST(test_vectorize_map_array_error);
    RUN_TEST(test_vectorize_array_map_error);
    RUN_TEST(test_vectorize_array_still_works);
    RUN_TEST(test_vectorize_array_scalar_still_works);

    /* ---- Map meta-operator composability ---- */
    printf("\nMap meta-operator composability:\n");
    RUN_TEST(test_map_compose_reduce_values);
    RUN_TEST(test_map_compose_broadcast_comparison);
    RUN_TEST(test_map_compose_reduce_broadcast);
    RUN_TEST(test_map_compose_reduce_vectorize);

    /* ---- @fn prefix — custom function reduce ---- */
    printf("\nCustom function reduce (@fn prefix):\n");
    RUN_TEST(test_reduce_custom_add);
    RUN_TEST(test_reduce_custom_max);
    RUN_TEST(test_reduce_custom_single);
    RUN_TEST(test_reduce_custom_empty_error);
    RUN_TEST(test_reduce_custom_closure);

    /* ---- @fn infix — custom function vectorize ---- */
    printf("\nCustom function vectorize (@fn infix):\n");
    RUN_TEST(test_vectorize_custom_mul);
    RUN_TEST(test_vectorize_custom_scalar_right);
    RUN_TEST(test_vectorize_custom_scalar_left);
    RUN_TEST(test_vectorize_custom_length_mismatch);
    RUN_TEST(test_vectorize_custom_both_scalars);
    RUN_TEST(test_vectorize_custom_max);

    /* ---- Map literals ---- */
    printf("\nMap literals:\n");
    RUN_TEST(test_map_empty_eval);
    RUN_TEST(test_map_bare_keys_eval);
    RUN_TEST(test_map_computed_key_eval);
    RUN_TEST(test_map_bool_keys_eval);
    RUN_TEST(test_map_duplicate_key_eval);
    RUN_TEST(test_map_function_key_error);

    /* ---- Map indexing (read) ---- */
    printf("\nMap indexing (read):\n");
    RUN_TEST(test_map_index_get_string_key);
    RUN_TEST(test_map_index_get_missing_key);
    RUN_TEST(test_map_index_get_number_key);
    RUN_TEST(test_map_index_get_bool_key);
    RUN_TEST(test_map_index_get_invalid_key);

    /* ---- Map indexing (write) ---- */
    printf("\nMap indexing (write):\n");
    RUN_TEST(test_map_index_set_existing);
    RUN_TEST(test_map_index_set_new_key);
    RUN_TEST(test_map_index_set_cow);

    /* ---- Map projection ---- */
    printf("\nMap projection:\n");
    RUN_TEST(test_map_projection_basic);
    RUN_TEST(test_map_projection_missing_keys);
    RUN_TEST(test_map_projection_empty_keys);

    printf("\nMap merge (++):\n");
    RUN_TEST(test_map_merge_basic);
    RUN_TEST(test_map_merge_empty_left);
    RUN_TEST(test_map_merge_empty_right);
    RUN_TEST(test_map_merge_both_empty);
    RUN_TEST(test_map_merge_map_num_error);
    RUN_TEST(test_map_merge_string_map_error);
    RUN_TEST(test_map_merge_array_map_error);
    RUN_TEST(test_map_merge_strings_unchanged);

    printf("\nMap builtin functions (keys, values, has_key, len):\n");
    RUN_TEST(test_map_keys_basic);
    RUN_TEST(test_map_keys_empty);
    RUN_TEST(test_map_keys_non_map_error);
    RUN_TEST(test_map_values_basic);
    RUN_TEST(test_map_values_empty);
    RUN_TEST(test_map_values_non_map_error);
    RUN_TEST(test_map_has_key_true);
    RUN_TEST(test_map_has_key_false);
    RUN_TEST(test_map_has_key_nothing_value);
    RUN_TEST(test_map_has_key_empty_map);
    RUN_TEST(test_map_has_key_non_map_error);
    RUN_TEST(test_map_len_basic);
    RUN_TEST(test_map_len_empty);

    printf("\nMap equality:\n");
    RUN_TEST(test_map_eq_same_order);
    RUN_TEST(test_map_eq_different_order);
    RUN_TEST(test_map_eq_different_values);
    RUN_TEST(test_map_eq_different_count);
    RUN_TEST(test_map_eq_different_type);
    RUN_TEST(test_map_eq_empty);
    RUN_TEST(test_map_neq_true);
    RUN_TEST(test_map_neq_false);

    printf("\nZip-map (@:) operator:\n");
    RUN_TEST(test_zip_map_basic);
    RUN_TEST(test_zip_map_empty);
    RUN_TEST(test_zip_map_single);
    RUN_TEST(test_zip_map_number_keys);
    RUN_TEST(test_zip_map_bool_keys);
    RUN_TEST(test_zip_map_duplicate_keys);
    RUN_TEST(test_zip_map_length_mismatch);
    RUN_TEST(test_zip_map_non_array_left);
    RUN_TEST(test_zip_map_non_array_right);
    RUN_TEST(test_zip_map_invalid_key_type);

    printf("\nZip-map (@:) composability:\n");
    RUN_TEST(test_zip_map_with_variables);
    RUN_TEST(test_zip_map_compose_vectorize);
    RUN_TEST(test_zip_map_inversion);
    RUN_TEST(test_zip_map_round_trip);

    /* ---- `in` membership operator ---- */
    printf("\n`in` operator — map membership:\n");
    RUN_TEST(test_in_map_key_found);
    RUN_TEST(test_in_map_key_missing);
    RUN_TEST(test_in_map_empty);
    RUN_TEST(test_in_map_number_key);
    RUN_TEST(test_in_map_bool_key);
    RUN_TEST(test_in_map_nothing_value);

    printf("\n`in` operator — array membership:\n");
    RUN_TEST(test_in_array_found);
    RUN_TEST(test_in_array_missing);
    RUN_TEST(test_in_array_string);
    RUN_TEST(test_in_array_bool);
    RUN_TEST(test_in_array_empty);

    printf("\n`in` operator — string membership:\n");
    RUN_TEST(test_in_string_found);
    RUN_TEST(test_in_string_missing);
    RUN_TEST(test_in_string_empty_needle);
    RUN_TEST(test_in_string_full_match);
    RUN_TEST(test_in_string_non_string_lhs);

    printf("\n`in` operator — not in:\n");
    RUN_TEST(test_not_in_array_true);
    RUN_TEST(test_not_in_array_false);
    RUN_TEST(test_not_in_map_true);
    RUN_TEST(test_not_in_map_false);
    RUN_TEST(test_not_in_string_true);

    printf("\n`in` operator — error cases:\n");
    RUN_TEST(test_in_number_error);
    RUN_TEST(test_in_bool_error);
    RUN_TEST(test_in_nothing_error);

    printf("\n`in` operator — precedence:\n");
    RUN_TEST(test_in_precedence_arithmetic);
    RUN_TEST(test_in_precedence_and);
    RUN_TEST(test_in_precedence_not);

    printf("\n`in` operator — edge cases:\n");
    RUN_TEST(test_in_nothing_as_map_key);
    RUN_TEST(test_in_nothing_in_array);
    RUN_TEST(test_in_empty_string_in_empty_string);
    RUN_TEST(test_in_composable_with_keys);
    RUN_TEST(test_in_compound_not_in_and_in);

    /* ---- Dot access (get) ---- */
    printf("\nDot access (get):\n");
    RUN_TEST(test_dot_get_basic);
    RUN_TEST(test_dot_get_second_key);
    RUN_TEST(test_dot_get_missing_key);
    RUN_TEST(test_dot_get_variable);

    /* ---- Dot access (set) ---- */
    printf("\nDot access (set):\n");
    RUN_TEST(test_dot_set_overwrite);
    RUN_TEST(test_dot_set_new_key);
    RUN_TEST(test_dot_set_overwrite_number);

    /* ---- Chained dot access ---- */
    printf("\nChained dot access:\n");
    RUN_TEST(test_dot_chain_two);
    RUN_TEST(test_dot_chain_three);
    RUN_TEST(test_dot_chain_variable);

    /* ---- Method calls ---- */
    printf("\nMethod calls:\n");
    RUN_TEST(test_method_call_no_arg);
    RUN_TEST(test_method_call_self_access);
    RUN_TEST(test_method_call_with_arg);
    RUN_TEST(test_method_call_multi_fields);
    RUN_TEST(test_method_call_chained_self);

    /* ---- Method call edge cases ---- */
    printf("\nMethod call edge cases:\n");
    RUN_TEST(test_method_call_missing_key);
    RUN_TEST(test_method_call_not_callable);
    RUN_TEST(test_dot_on_non_map);

    /* ---- Dot access + method call integration / edge cases ---- */
    printf("\nDot access + method call integration:\n");
    RUN_TEST(test_dot_access_with_in);
    RUN_TEST(test_dot_then_bracket_index);
    RUN_TEST(test_chained_method_calls_returning_self);
    RUN_TEST(test_method_call_fn_from_variable);

    printf("\n========================================\n");
    printf("Tests run: %d\n", tests_run);
    printf("Passed:    %d\n", tests_passed);
    printf("Failed:    %d\n", tests_failed);
    printf("========================================\n");

    runtime_destroy();
    return tests_failed > 0 ? 1 : 0;
}
