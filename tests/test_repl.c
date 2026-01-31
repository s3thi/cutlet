/*
 * test_repl.c - Tests for the Cutlet REPL core
 *
 * Token mode (repl_format_line): parse expression → eval → format result.
 * AST mode (repl_format_line_ast): parse expression → format AST tree.
 *
 * Uses the same simple test harness as test_tokenizer.c.
 */

#include "../src/repl.h"
#include "../src/runtime.h"
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
#define ASSERT_NOT_NULL(ptr, msg) ASSERT((ptr) != NULL, msg)

#define PASS()                                                                                     \
    do {                                                                                           \
        printf("PASS\n");                                                                          \
        tests_passed++;                                                                            \
    } while (0)

/* Helper to run format and compare result */
static int format_matches(const char *input, const char *expected) {
    char *result = repl_format_line(input);
    if (result == NULL)
        return 0;
    int match = strcmp(result, expected) == 0;
    if (!match) {
        printf("\n    Expected: %s\n    Got:      %s\n", expected, result);
    }
    free(result);
    return match;
}

/* Helper for AST mode */
static int ast_format_matches(const char *input, const char *expected) {
    char *result = repl_format_line_ast(input);
    if (result == NULL)
        return 0;
    int match = strcmp(result, expected) == 0;
    if (!match) {
        printf("\n    Expected: %s\n    Got:      %s\n", expected, result);
    }
    free(result);
    return match;
}

/* ============================================================
 * Empty and whitespace input tests (token mode)
 * ============================================================ */

TEST(test_empty_input) {
    ASSERT(format_matches("", "OK"), "empty input should return OK");
    PASS();
}

TEST(test_null_input) {
    ASSERT(format_matches(NULL, "OK"), "NULL input should return OK");
    PASS();
}

TEST(test_whitespace_only_spaces) {
    ASSERT(format_matches("   ", "OK"), "spaces only should return OK");
    PASS();
}

TEST(test_whitespace_only_tabs) {
    ASSERT(format_matches("\t\t", "OK"), "tabs only should return OK");
    PASS();
}

TEST(test_whitespace_only_newline) {
    ASSERT(format_matches("\n", "OK"), "newline only should return OK");
    PASS();
}

TEST(test_whitespace_mixed) {
    ASSERT(format_matches("  \t\n  ", "OK"), "mixed whitespace should return OK");
    PASS();
}

/* ============================================================
 * Single number evaluation (token mode)
 * ============================================================ */

TEST(test_single_number_zero) {
    ASSERT(format_matches("0", "OK [NUMBER 0]"), "zero");
    PASS();
}

TEST(test_single_number_positive) {
    ASSERT(format_matches("42", "OK [NUMBER 42]"), "positive number");
    PASS();
}

TEST(test_single_number_large) {
    ASSERT(format_matches("1234567890", "OK [NUMBER 1234567890]"), "large number");
    PASS();
}

TEST(test_number_with_leading_whitespace) {
    ASSERT(format_matches("  42", "OK [NUMBER 42]"), "number with leading space");
    PASS();
}

TEST(test_number_with_trailing_whitespace) {
    ASSERT(format_matches("42  ", "OK [NUMBER 42]"), "number with trailing space");
    PASS();
}

/* ============================================================
 * String evaluation (token mode)
 * ============================================================ */

TEST(test_single_string_empty) {
    ASSERT(format_matches("\"\"", "OK [STRING ]"), "empty string");
    PASS();
}

TEST(test_single_string_simple) {
    ASSERT(format_matches("\"hello\"", "OK [STRING hello]"), "simple string");
    PASS();
}

TEST(test_single_string_with_spaces) {
    ASSERT(format_matches("\"hello world\"", "OK [STRING hello world]"), "string with spaces");
    PASS();
}

TEST(test_single_string_with_digits) {
    ASSERT(format_matches("\"test123\"", "OK [STRING test123]"), "string with digits");
    PASS();
}

/* ============================================================
 * Expression evaluation (token mode)
 * ============================================================ */

TEST(test_eval_addition) {
    ASSERT(format_matches("1 + 2", "OK [NUMBER 3]"), "1+2=3");
    PASS();
}

TEST(test_eval_subtraction) {
    ASSERT(format_matches("10 - 3", "OK [NUMBER 7]"), "10-3=7");
    PASS();
}

TEST(test_eval_multiplication) {
    ASSERT(format_matches("2 * 3", "OK [NUMBER 6]"), "2*3=6");
    PASS();
}

TEST(test_eval_division_exact) {
    ASSERT(format_matches("6 / 3", "OK [NUMBER 2]"), "6/3=2");
    PASS();
}

TEST(test_eval_division_float) {
    ASSERT(format_matches("7 / 2", "OK [NUMBER 3.5]"), "7/2=3.5");
    PASS();
}

TEST(test_eval_exponent) {
    ASSERT(format_matches("2 ** 10", "OK [NUMBER 1024]"), "2**10");
    PASS();
}

TEST(test_eval_precedence) {
    ASSERT(format_matches("1 + 2 * 3", "OK [NUMBER 7]"), "1+2*3=7");
    PASS();
}

TEST(test_eval_parens) {
    ASSERT(format_matches("(1 + 2) * 3", "OK [NUMBER 9]"), "(1+2)*3=9");
    PASS();
}

TEST(test_eval_unary_minus) {
    ASSERT(format_matches("-3", "OK [NUMBER -3]"), "unary -3");
    PASS();
}

TEST(test_eval_float_division) {
    ASSERT(format_matches("42 / 5", "OK [NUMBER 8.4]"), "42/5=8.4");
    PASS();
}

/* ============================================================
 * Error cases (token mode)
 * ============================================================ */

TEST(test_error_unterminated_string) {
    ASSERT(format_matches("\"hello", "ERR 1:1 unterminated string"), "unterminated string error");
    PASS();
}

TEST(test_error_number_adjacent_ident) {
    ASSERT(format_matches("42foo", "ERR 1:1 number followed by identifier character"),
           "number adjacent ident error");
    PASS();
}

TEST(test_error_unknown_ident) {
    ASSERT(format_matches("foo", "ERR unknown variable 'foo'"), "unknown ident error");
    PASS();
}

TEST(test_error_div_by_zero) {
    ASSERT(format_matches("1 / 0", "ERR division by zero"), "div by zero error");
    PASS();
}

/* ============================================================
 * AST mode tests
 * ============================================================ */

TEST(test_ast_empty) {
    ASSERT(ast_format_matches("", "AST"), "ast empty");
    PASS();
}

TEST(test_ast_whitespace) {
    ASSERT(ast_format_matches("   ", "AST"), "ast whitespace");
    PASS();
}

TEST(test_ast_number) {
    ASSERT(ast_format_matches("42", "AST [NUMBER 42]"), "ast number");
    PASS();
}

TEST(test_ast_string) {
    ASSERT(ast_format_matches("\"hello\"", "AST [STRING hello]"), "ast string");
    PASS();
}

TEST(test_ast_ident) {
    ASSERT(ast_format_matches("foo", "AST [IDENT foo]"), "ast ident");
    PASS();
}

TEST(test_ast_binop) {
    ASSERT(ast_format_matches("1 + 2", "AST [BINOP + [NUMBER 1] [NUMBER 2]]"), "ast binop");
    PASS();
}

TEST(test_ast_precedence) {
    ASSERT(
        ast_format_matches("1 + 2 * 3", "AST [BINOP + [NUMBER 1] [BINOP * [NUMBER 2] [NUMBER 3]]]"),
        "ast precedence");
    PASS();
}

TEST(test_ast_unary) {
    ASSERT(ast_format_matches("-3", "AST [UNARY - [NUMBER 3]]"), "ast unary");
    PASS();
}

TEST(test_ast_error) {
    char *result = repl_format_line_ast("(");
    ASSERT_NOT_NULL(result, "result not null");
    ASSERT(strncmp(result, "ERR ", 4) == 0, "should be error");
    free(result);
    PASS();
}

/* ============================================================
 * Return value tests
 * ============================================================ */

TEST(test_result_is_heap_allocated) {
    char *result1 = repl_format_line("42");
    char *result2 = repl_format_line("7");
    ASSERT_NOT_NULL(result1, "first result not null");
    ASSERT_NOT_NULL(result2, "second result not null");
    ASSERT_STR_EQ(result1, "OK [NUMBER 42]", "first result correct");
    ASSERT_STR_EQ(result2, "OK [NUMBER 7]", "second result correct");
    free(result1);
    free(result2);
    PASS();
}

/* ============================================================
 * Main
 * ============================================================ */

int main(void) {
    runtime_init();
    printf("Running REPL core tests...\n\n");

    printf("Empty and whitespace input:\n");
    RUN_TEST(test_empty_input);
    RUN_TEST(test_null_input);
    RUN_TEST(test_whitespace_only_spaces);
    RUN_TEST(test_whitespace_only_tabs);
    RUN_TEST(test_whitespace_only_newline);
    RUN_TEST(test_whitespace_mixed);

    printf("\nSingle NUMBER evaluation:\n");
    RUN_TEST(test_single_number_zero);
    RUN_TEST(test_single_number_positive);
    RUN_TEST(test_single_number_large);
    RUN_TEST(test_number_with_leading_whitespace);
    RUN_TEST(test_number_with_trailing_whitespace);

    printf("\nSTRING evaluation:\n");
    RUN_TEST(test_single_string_empty);
    RUN_TEST(test_single_string_simple);
    RUN_TEST(test_single_string_with_spaces);
    RUN_TEST(test_single_string_with_digits);

    printf("\nExpression evaluation:\n");
    RUN_TEST(test_eval_addition);
    RUN_TEST(test_eval_subtraction);
    RUN_TEST(test_eval_multiplication);
    RUN_TEST(test_eval_division_exact);
    RUN_TEST(test_eval_division_float);
    RUN_TEST(test_eval_exponent);
    RUN_TEST(test_eval_precedence);
    RUN_TEST(test_eval_parens);
    RUN_TEST(test_eval_unary_minus);
    RUN_TEST(test_eval_float_division);

    printf("\nError cases:\n");
    RUN_TEST(test_error_unterminated_string);
    RUN_TEST(test_error_number_adjacent_ident);
    RUN_TEST(test_error_unknown_ident);
    RUN_TEST(test_error_div_by_zero);

    printf("\nAST mode:\n");
    RUN_TEST(test_ast_empty);
    RUN_TEST(test_ast_whitespace);
    RUN_TEST(test_ast_number);
    RUN_TEST(test_ast_string);
    RUN_TEST(test_ast_ident);
    RUN_TEST(test_ast_binop);
    RUN_TEST(test_ast_precedence);
    RUN_TEST(test_ast_unary);
    RUN_TEST(test_ast_error);

    printf("\nReturn value tests:\n");
    RUN_TEST(test_result_is_heap_allocated);

    printf("\n========================================\n");
    printf("Tests run: %d\n", tests_run);
    printf("Passed:    %d\n", tests_passed);
    printf("Failed:    %d\n", tests_failed);
    printf("========================================\n");

    return tests_failed > 0 ? 1 : 0;
}
