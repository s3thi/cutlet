/*
 * test_eval.c - Tests for the Cutlet expression evaluator
 *
 * Tests eval() on AST nodes: arithmetic, division, exponentiation,
 * unary minus, error cases, and number formatting.
 */

#include "../src/eval.h"
#include "../src/parser.h"
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * No-op write callback for tests that don't need output capture.
 * A proper buffer-capturing context will be used in say() tests.
 */
static void test_write_noop(void *userdata, const char *data, size_t len) {
    (void)userdata;
    (void)data;
    (void)len;
}

/* Shared EvalContext used by all eval test helpers. */
static EvalContext test_ctx = {.write_fn = test_write_noop, .userdata = NULL};

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

#define PASS()                                                                                     \
    do {                                                                                           \
        printf("PASS\n");                                                                          \
        tests_passed++;                                                                            \
    } while (0)

/*
 * Helper: parse input, eval, check result is a number with expected value.
 */
static void assert_eval_number(const char *input, double expected, const char *label) {
    AstNode *node = NULL;
    ParseError perr;
    if (!parser_parse(input, &node, &perr)) {
        printf("FAIL\n    parse failed for '%s': %s\n", input, perr.message);
        tests_failed++;
        return;
    }
    Value v = eval(node, &test_ctx);
    ast_free(node);

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

/*
 * Helper: parse input, eval, check result is an error.
 */
static void assert_eval_error(const char *input, const char *label) {
    AstNode *node = NULL;
    ParseError perr;
    if (!parser_parse(input, &node, &perr)) {
        /* Parse error is fine — we just need to verify it doesn't succeed silently. */
        printf("PASS\n");
        tests_passed++;
        (void)label;
        return;
    }
    Value v = eval(node, &test_ctx);
    ast_free(node);

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

/* ============================================================
 * Basic arithmetic
 * ============================================================ */

TEST(test_add) { assert_eval_number("1 + 2", 3.0, "1+2=3"); }

TEST(test_sub) { assert_eval_number("10 - 3", 7.0, "10-3=7"); }

TEST(test_mul) { assert_eval_number("2 * 3", 6.0, "2*3=6"); }

TEST(test_div_exact) { assert_eval_number("6 / 3", 2.0, "6/3=2"); }

TEST(test_div_float) { assert_eval_number("7 / 2", 3.5, "7/2=3.5"); }

TEST(test_div_float2) { assert_eval_number("42 / 5", 8.4, "42/5=8.4"); }

/* ============================================================
 * Exponentiation
 * ============================================================ */

TEST(test_exp_basic) { assert_eval_number("2 ** 10", 1024.0, "2**10=1024"); }

TEST(test_exp_right_assoc) {
    /* 2 ** 3 ** 2 = 2 ** (3 ** 2) = 2 ** 9 = 512 */
    assert_eval_number("2 ** 3 ** 2", 512.0, "right-assoc exponent");
}

TEST(test_exp_zero) { assert_eval_number("5 ** 0", 1.0, "x**0=1"); }

/* ============================================================
 * Unary minus
 * ============================================================ */

TEST(test_unary_neg) { assert_eval_number("-3", -3.0, "unary -3"); }

TEST(test_unary_double_neg) { assert_eval_number("-(-3)", 3.0, "double neg"); }

TEST(test_unary_neg_in_expr) { assert_eval_number("1 + -2", -1.0, "1 + -2"); }

/* ============================================================
 * Precedence
 * ============================================================ */

TEST(test_precedence_add_mul) { assert_eval_number("1 + 2 * 3", 7.0, "1+2*3=7"); }

TEST(test_precedence_parens) { assert_eval_number("(1 + 2) * 3", 9.0, "(1+2)*3=9"); }

TEST(test_precedence_complex) { assert_eval_number("2 + 3 * 4 - 1", 13.0, "2+3*4-1=13"); }

/* ============================================================
 * Division by zero
 * ============================================================ */

TEST(test_div_by_zero) { assert_eval_error("1 / 0", "div by zero"); }

/* ============================================================
 * Unknown identifier
 * ============================================================ */

TEST(test_unknown_ident) { assert_eval_error("x", "unknown ident"); }

/* ============================================================
 * String literal
 * ============================================================ */

TEST(test_string_value) {
    AstNode *node = NULL;
    ParseError perr;
    ASSERT(parser_parse("\"hello\"", &node, &perr), "parse string");
    Value v = eval(node, &test_ctx);
    ast_free(node);
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

/*
 * Helper: parse input, eval, check result is a boolean with expected value.
 */
static void assert_eval_bool(const char *input, bool expected, const char *label) {
    AstNode *node = NULL;
    ParseError perr;
    if (!parser_parse(input, &node, &perr)) {
        printf("FAIL\n    parse failed for '%s': %s\n", input, perr.message);
        tests_failed++;
        return;
    }
    Value v = eval(node, &test_ctx);
    ast_free(node);

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

TEST(test_bool_true_eval) { assert_eval_bool("true", true, "true literal"); }

TEST(test_bool_false_eval) { assert_eval_bool("false", false, "false literal"); }

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

TEST(test_bool_true_assign_error) { assert_eval_error("true = 1", "cannot assign to true"); }

TEST(test_bool_true_decl_error) { assert_eval_error("my true = 1", "cannot declare true"); }

/* ============================================================
 * Comparison operators
 * ============================================================ */

/* == */
TEST(test_cmp_num_eq_true) { assert_eval_bool("1 == 1", true, "1==1"); }
TEST(test_cmp_num_eq_false) { assert_eval_bool("1 == 2", false, "1==2"); }
TEST(test_cmp_str_eq_true) { assert_eval_bool("\"a\" == \"a\"", true, "a==a"); }
TEST(test_cmp_str_eq_false) { assert_eval_bool("\"a\" == \"b\"", false, "a==b"); }
TEST(test_cmp_bool_eq_true) { assert_eval_bool("true == true", true, "true==true"); }
TEST(test_cmp_bool_eq_false) { assert_eval_bool("true == false", false, "true==false"); }
TEST(test_cmp_mixed_eq) { assert_eval_bool("1 == \"1\"", false, "1==\"1\" mixed"); }

/* != */
TEST(test_cmp_num_neq_true) { assert_eval_bool("1 != 2", true, "1!=2"); }
TEST(test_cmp_num_neq_false) { assert_eval_bool("1 != 1", false, "1!=1"); }
TEST(test_cmp_mixed_neq) { assert_eval_bool("1 != \"1\"", true, "1!=\"1\" mixed"); }

/* < */
TEST(test_cmp_lt_true) { assert_eval_bool("1 < 2", true, "1<2"); }
TEST(test_cmp_lt_false) { assert_eval_bool("2 < 1", false, "2<1"); }
TEST(test_cmp_lt_equal) { assert_eval_bool("1 < 1", false, "1<1"); }
TEST(test_cmp_str_lt) { assert_eval_bool("\"a\" < \"b\"", true, "a<b"); }

/* > */
TEST(test_cmp_gt_true) { assert_eval_bool("2 > 1", true, "2>1"); }
TEST(test_cmp_gt_false) { assert_eval_bool("1 > 2", false, "1>2"); }

/* <= */
TEST(test_cmp_lte_lt) { assert_eval_bool("1 <= 2", true, "1<=2"); }
TEST(test_cmp_lte_eq) { assert_eval_bool("1 <= 1", true, "1<=1"); }
TEST(test_cmp_lte_gt) { assert_eval_bool("2 <= 1", false, "2<=1"); }

/* >= */
TEST(test_cmp_gte_gt) { assert_eval_bool("2 >= 1", true, "2>=1"); }
TEST(test_cmp_gte_eq) { assert_eval_bool("1 >= 1", true, "1>=1"); }
TEST(test_cmp_gte_lt) { assert_eval_bool("1 >= 2", false, "1>=2"); }

/* Ordered comparison on mixed types → error */
TEST(test_cmp_mixed_lt_error) { assert_eval_error("1 < \"1\"", "mixed type ordered cmp"); }
TEST(test_cmp_mixed_gt_error) { assert_eval_error("1 > \"1\"", "mixed type ordered cmp"); }
TEST(test_cmp_mixed_lte_error) { assert_eval_error("1 <= \"1\"", "mixed type ordered cmp"); }
TEST(test_cmp_mixed_gte_error) { assert_eval_error("1 >= \"1\"", "mixed type ordered cmp"); }

/* Ordered comparison on bools → error */
TEST(test_cmp_bool_lt_error) { assert_eval_error("true < false", "bool ordered cmp"); }
TEST(test_cmp_bool_gt_error) { assert_eval_error("true > false", "bool ordered cmp"); }
TEST(test_cmp_bool_lte_error) { assert_eval_error("true <= false", "bool ordered cmp"); }
TEST(test_cmp_bool_gte_error) { assert_eval_error("true >= false", "bool ordered cmp"); }

/* Precedence: 1 + 2 == 3 → true */
TEST(test_cmp_precedence_add) { assert_eval_bool("1 + 2 == 3", true, "1+2==3"); }

/* String ordered comparison */
TEST(test_cmp_str_gt) { assert_eval_bool("\"b\" > \"a\"", true, "b>a"); }
TEST(test_cmp_str_lte) { assert_eval_bool("\"a\" <= \"a\"", true, "a<=a"); }
TEST(test_cmp_str_gte) { assert_eval_bool("\"b\" >= \"a\"", true, "b>=a"); }

/* ============================================================
 * Logical operators
 * ============================================================ */

/* and */
TEST(test_logic_and_tt) { assert_eval_bool("true and true", true, "true and true"); }
TEST(test_logic_and_tf) { assert_eval_bool("true and false", false, "true and false"); }
TEST(test_logic_and_ft) { assert_eval_bool("false and true", false, "false and true"); }
TEST(test_logic_and_ff) { assert_eval_bool("false and false", false, "false and false"); }

/* or */
TEST(test_logic_or_tt) { assert_eval_bool("true or false", true, "true or false"); }
TEST(test_logic_or_ft) { assert_eval_bool("false or true", true, "false or true"); }
TEST(test_logic_or_ff) { assert_eval_bool("false or false", false, "false or false"); }

/* not */
TEST(test_logic_not_true) { assert_eval_bool("not true", false, "not true"); }
TEST(test_logic_not_false) { assert_eval_bool("not false", true, "not false"); }

/* Truthiness: 0 is falsy, nonzero truthy */
TEST(test_logic_not_zero) { assert_eval_bool("not 0", true, "not 0"); }
TEST(test_logic_not_one) { assert_eval_bool("not 1", false, "not 1"); }
TEST(test_logic_not_empty_str) { assert_eval_bool("not \"\"", true, "not empty string"); }
TEST(test_logic_not_nonempty_str) { assert_eval_bool("not \"hi\"", false, "not nonempty string"); }

/* and/or return operand values (Python semantics) */
TEST(test_logic_and_numbers) { assert_eval_number("1 and 2", 2.0, "1 and 2 → 2"); }
TEST(test_logic_and_zero_short) { assert_eval_number("0 and 2", 0.0, "0 and 2 → 0"); }
TEST(test_logic_or_numbers) { assert_eval_number("0 or 2", 2.0, "0 or 2 → 2"); }
TEST(test_logic_or_falsy_last) { assert_eval_bool("0 or false", false, "0 or false → false"); }

/* Precedence: and binds tighter than or */
TEST(test_logic_prec_or_and) { assert_eval_bool("true or true and false", true, "or/and prec"); }

/* not binds looser than comparison (Python model): not 1 < 2 → not (1 < 2) → false */
TEST(test_logic_not_lt) { assert_eval_bool("not 1 < 2", false, "not 1 < 2"); }

/* not true and false → (not true) and false → false */
TEST(test_logic_not_and) { assert_eval_bool("not true and false", false, "not true and false"); }

/* Short-circuit: false and (my x = 1) should not define x */
TEST(test_logic_short_circuit_and) {
    AstNode *node = NULL;
    ParseError perr;
    /* false and (my x = 1) — x should not be defined */
    ASSERT(parser_parse("false and (my x_sc = 1)", &node, &perr), "parse short-circuit and");
    Value v = eval(node, &test_ctx);
    ast_free(node);
    /* Result should be false (the first falsy operand) */
    ASSERT(v.type == VAL_BOOL, "should be bool");
    ASSERT(v.boolean == false, "should be false");
    value_free(&v);

    /* x_sc should not be defined */
    ASSERT(parser_parse("x_sc", &node, &perr), "parse x_sc");
    v = eval(node, &test_ctx);
    ast_free(node);
    ASSERT(v.type == VAL_ERROR, "x_sc should not be defined");
    value_free(&v);
    PASS();
}

/* Short-circuit: true or (my y = 1) should not define y */
TEST(test_logic_short_circuit_or) {
    AstNode *node = NULL;
    ParseError perr;
    ASSERT(parser_parse("true or (my y_sc = 1)", &node, &perr), "parse short-circuit or");
    Value v = eval(node, &test_ctx);
    ast_free(node);
    ASSERT(v.type == VAL_BOOL, "should be bool");
    ASSERT(v.boolean == true, "should be true");
    value_free(&v);

    /* y_sc should not be defined */
    ASSERT(parser_parse("y_sc", &node, &perr), "parse y_sc");
    v = eval(node, &test_ctx);
    ast_free(node);
    ASSERT(v.type == VAL_ERROR, "y_sc should not be defined");
    value_free(&v);
    PASS();
}

/* and/or/not cannot be variable names */
TEST(test_logic_and_assign_error) { assert_eval_error("and = 1", "and assign"); }
TEST(test_logic_or_assign_error) { assert_eval_error("or = 1", "or assign"); }
TEST(test_logic_not_assign_error) { assert_eval_error("not = 1", "not assign"); }
TEST(test_logic_and_decl_error) { assert_eval_error("my and = 1", "my and"); }
TEST(test_logic_or_decl_error) { assert_eval_error("my or = 1", "my or"); }
TEST(test_logic_not_decl_error) { assert_eval_error("my not = 1", "my not"); }

/* ============================================================
 * Nothing literal
 * ============================================================ */

/*
 * Helper: parse input, eval, check result is nothing.
 */
static void assert_eval_nothing(const char *input, const char *label) {
    AstNode *node = NULL;
    ParseError perr;
    if (!parser_parse(input, &node, &perr)) {
        printf("FAIL\n    parse failed for '%s': %s\n", input, perr.message);
        tests_failed++;
        return;
    }
    Value v = eval(node, &test_ctx);
    ast_free(node);

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

TEST(test_nothing_eval) { assert_eval_nothing("nothing", "nothing literal"); }

TEST(test_nothing_eq_nothing) {
    assert_eval_bool("nothing == nothing", true, "nothing == nothing");
}

TEST(test_nothing_eq_false) { assert_eval_bool("nothing == false", false, "nothing == false"); }

TEST(test_nothing_eq_zero) { assert_eval_bool("nothing == 0", false, "nothing == 0"); }

TEST(test_nothing_eq_empty_str) {
    assert_eval_bool("nothing == \"\"", false, "nothing == empty string");
}

TEST(test_nothing_neq_one) { assert_eval_bool("nothing != 1", true, "nothing != 1"); }

TEST(test_nothing_lt_error) { assert_eval_error("nothing < 1", "nothing ordered cmp"); }

TEST(test_nothing_gt_error) { assert_eval_error("nothing > 1", "nothing ordered cmp"); }

TEST(test_nothing_lte_error) { assert_eval_error("nothing <= 1", "nothing ordered cmp"); }

TEST(test_nothing_gte_error) { assert_eval_error("nothing >= 1", "nothing ordered cmp"); }

TEST(test_not_nothing) { assert_eval_bool("not nothing", true, "not nothing"); }

TEST(test_nothing_and_one) {
    /* nothing and 1 → nothing (short-circuit, returns falsy operand) */
    assert_eval_nothing("nothing and 1", "nothing and 1");
}

TEST(test_nothing_or_one) {
    /* nothing or 1 → 1 (returns first truthy operand) */
    assert_eval_number("nothing or 1", 1.0, "nothing or 1");
}

TEST(test_nothing_format) {
    Value v = {.type = VAL_NOTHING, .number = 0, .string = NULL};
    char *s = value_format(&v);
    ASSERT(s != NULL, "format not null");
    ASSERT_STR_EQ(s, "nothing", "nothing formats as 'nothing'");
    free(s);
    PASS();
}

TEST(test_nothing_assign_error) { assert_eval_error("nothing = 1", "cannot assign to nothing"); }

TEST(test_nothing_decl_error) { assert_eval_error("my nothing = 1", "cannot declare nothing"); }

/* ============================================================
 * Multi-line input / AST_BLOCK evaluation (Step 2)
 * ============================================================ */

TEST(test_block_returns_last) {
    /* Block returns value of last expression */
    assert_eval_number("1\n2\n3", 3.0, "block returns last");
}

TEST(test_block_decl_then_use) {
    /* Variable defined in first statement, used in second */
    assert_eval_number("my x = 1\nx + 2", 3.0, "decl then use");
}

TEST(test_block_multiple_decls) {
    /* Multiple declarations, then use */
    assert_eval_number("my x = 1\nmy y = 2\nx + y", 3.0, "multiple decls");
}

TEST(test_block_reassign) {
    /* Reassignment in block */
    assert_eval_number("my x = 1\nx = 5\nx", 5.0, "reassign in block");
}

TEST(test_block_blank_lines) {
    /* Blank lines don't affect result */
    assert_eval_number("\n\n1 + 2\n\n", 3.0, "blank lines");
}

TEST(test_block_with_comparison) {
    /* Block with comparison */
    assert_eval_bool("my x = 10\nx > 5", true, "block with comparison");
}

TEST(test_block_with_logic) {
    /* Block with logical operators */
    assert_eval_bool("my x = true\nmy y = false\nx and not y", true, "block with logic");
}

TEST(test_block_single_expr_not_wrapped) {
    /* Single expression should behave identically */
    assert_eval_number("42", 42.0, "single expr");
}

/* ============================================================
 * If/else expression evaluation (Step 3)
 * ============================================================ */

TEST(test_if_true_then_else) {
    /* if true then 1 else 2 end → 1 */
    assert_eval_number("if true then 1 else 2 end", 1.0, "if true");
}

TEST(test_if_false_then_else) {
    /* if false then 1 else 2 end → 2 */
    assert_eval_number("if false then 1 else 2 end", 2.0, "if false");
}

TEST(test_if_false_no_else) {
    /* if false then 1 end → nothing */
    assert_eval_nothing("if false then 1 end", "if false no else → nothing");
}

TEST(test_if_true_no_else) {
    /* if true then 42 end → 42 */
    assert_eval_number("if true then 42 end", 42.0, "if true no else");
}

TEST(test_if_comparison_cond) {
    /* if 1 < 2 then "yes" else "no" end → "yes" */
    AstNode *node = NULL;
    ParseError perr;
    ASSERT(parser_parse("if 1 < 2 then \"yes\" else \"no\" end", &node, &perr), "parse if");
    Value v = eval(node, &test_ctx);
    ast_free(node);
    ASSERT(v.type == VAL_STRING, "should be string");
    ASSERT_STR_EQ(v.string, "yes", "should be yes");
    value_free(&v);
    PASS();
}

TEST(test_if_comparison_cond_false) {
    /* if 2 < 1 then "yes" else "no" end → "no" */
    AstNode *node = NULL;
    ParseError perr;
    ASSERT(parser_parse("if 2 < 1 then \"yes\" else \"no\" end", &node, &perr), "parse if");
    Value v = eval(node, &test_ctx);
    ast_free(node);
    ASSERT(v.type == VAL_STRING, "should be string");
    ASSERT_STR_EQ(v.string, "no", "should be no");
    value_free(&v);
    PASS();
}

TEST(test_if_in_assignment) {
    /* my x = if true then 42 else 0 end
       x → 42 */
    assert_eval_number("my x_if = if true then 42 else 0 end\nx_if", 42.0, "if in assignment");
}

TEST(test_if_multiline_body) {
    /* if true then
         my x = 1
         x + 1
       else
         0
       end → 2 */
    assert_eval_number("if true then\nmy x_ml = 1\nx_ml + 1\nelse\n0\nend", 2.0, "multiline body");
}

TEST(test_if_nested_inner_taken) {
    /* if true then if false then 1 else 2 end else 3 end → 2 */
    assert_eval_number("if true then if false then 1 else 2 end else 3 end", 2.0, "nested if");
}

TEST(test_if_nested_outer_false) {
    /* if false then if true then 1 else 2 end else 3 end → 3 */
    assert_eval_number("if false then if true then 1 else 2 end else 3 end", 3.0,
                       "nested if outer false");
}

TEST(test_if_else_if) {
    /* if false then 1 else if true then 2 else 3 end → 2 */
    assert_eval_number("if false then 1 else if true then 2 else 3 end", 2.0, "else if");
}

TEST(test_if_else_if_chain) {
    /* if false then 1 else if false then 2 else 3 end → 3 */
    assert_eval_number("if false then 1 else if false then 2 else 3 end", 3.0, "else if chain");
}

TEST(test_if_short_circuit_then) {
    /* Side effects only in taken branch:
       if false then my x_sc_then = 1 end
       x_sc_then → error (not defined) */
    AstNode *node = NULL;
    ParseError perr;
    ASSERT(parser_parse("if false then\nmy x_sc_then = 1\nend\nx_sc_then", &node, &perr),
           "parse short-circuit if");
    Value v = eval(node, &test_ctx);
    ast_free(node);
    ASSERT(v.type == VAL_ERROR, "x_sc_then should not be defined");
    value_free(&v);
    PASS();
}

TEST(test_if_short_circuit_else) {
    /* Side effects only in taken branch:
       if true then 1 else my y_sc_else = 2 end
       y_sc_else → error (not defined) */
    AstNode *node = NULL;
    ParseError perr;
    ASSERT(parser_parse("if true then 1 else my y_sc_else = 2 end\ny_sc_else", &node, &perr),
           "parse short-circuit else");
    Value v = eval(node, &test_ctx);
    ast_free(node);
    ASSERT(v.type == VAL_ERROR, "y_sc_else should not be defined");
    value_free(&v);
    PASS();
}

TEST(test_if_truthy_number) {
    /* if 1 then "yes" else "no" end → "yes" (1 is truthy) */
    AstNode *node = NULL;
    ParseError perr;
    ASSERT(parser_parse("if 1 then \"yes\" else \"no\" end", &node, &perr), "parse if");
    Value v = eval(node, &test_ctx);
    ast_free(node);
    ASSERT(v.type == VAL_STRING, "should be string");
    ASSERT_STR_EQ(v.string, "yes", "1 is truthy");
    value_free(&v);
    PASS();
}

TEST(test_if_falsy_zero) {
    /* if 0 then "yes" else "no" end → "no" (0 is falsy) */
    AstNode *node = NULL;
    ParseError perr;
    ASSERT(parser_parse("if 0 then \"yes\" else \"no\" end", &node, &perr), "parse if");
    Value v = eval(node, &test_ctx);
    ast_free(node);
    ASSERT(v.type == VAL_STRING, "should be string");
    ASSERT_STR_EQ(v.string, "no", "0 is falsy");
    value_free(&v);
    PASS();
}

TEST(test_if_falsy_empty_string) {
    /* if "" then "yes" else "no" end → "no" (empty string is falsy) */
    AstNode *node = NULL;
    ParseError perr;
    ASSERT(parser_parse("if \"\" then \"yes\" else \"no\" end", &node, &perr), "parse if");
    Value v = eval(node, &test_ctx);
    ast_free(node);
    ASSERT(v.type == VAL_STRING, "should be string");
    ASSERT_STR_EQ(v.string, "no", "empty string is falsy");
    value_free(&v);
    PASS();
}

TEST(test_if_falsy_nothing) {
    /* if nothing then "yes" else "no" end → "no" (nothing is falsy) */
    AstNode *node = NULL;
    ParseError perr;
    ASSERT(parser_parse("if nothing then \"yes\" else \"no\" end", &node, &perr), "parse if");
    Value v = eval(node, &test_ctx);
    ast_free(node);
    ASSERT(v.type == VAL_STRING, "should be string");
    ASSERT_STR_EQ(v.string, "no", "nothing is falsy");
    value_free(&v);
    PASS();
}

TEST(test_if_in_expression) {
    /* 1 + if true then 2 else 3 end → 3 */
    assert_eval_number("1 + if true then 2 else 3 end", 3.0, "if in expression");
}

TEST(test_if_complex_condition) {
    /* if 1 < 2 and 3 > 0 then 100 else 0 end → 100 */
    assert_eval_number("if 1 < 2 and 3 > 0 then 100 else 0 end", 100.0, "complex condition");
}

/* ============================================================
 * Single number (leaf node)
 * ============================================================ */

TEST(test_single_number) { assert_eval_number("42", 42.0, "single number"); }

TEST(test_single_zero) { assert_eval_number("0", 0.0, "zero"); }

/* ============================================================
 * Main
 * ============================================================ */

int main(void) {
    printf("Running eval tests...\n\n");

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

    printf("\nMulti-line input / AST_BLOCK:\n");
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

    printf("\nLeaf nodes:\n");
    RUN_TEST(test_single_number);
    RUN_TEST(test_single_zero);

    printf("\n========================================\n");
    printf("Tests run: %d\n", tests_run);
    printf("Passed:    %d\n", tests_passed);
    printf("Failed:    %d\n", tests_failed);
    printf("========================================\n");

    return tests_failed > 0 ? 1 : 0;
}
