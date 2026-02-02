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
    Value v = eval(node);
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
    Value v = eval(node);
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
    Value v = eval(node);
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
    Value v = eval(node);
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
