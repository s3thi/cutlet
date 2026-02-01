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
 * Variable declaration and assignment tests
 * ============================================================ */

TEST(test_decl_returns_value) {
    ASSERT(format_matches("my x = 2", "OK [NUMBER 2]"), "decl returns value");
    PASS();
}

TEST(test_decl_then_read) {
    /* Declare, then read in separate eval calls (persistent env). */
    char *r1 = repl_format_line("my x = 2");
    ASSERT_NOT_NULL(r1, "decl result");
    ASSERT_STR_EQ(r1, "OK [NUMBER 2]", "decl value");
    free(r1);

    char *r2 = repl_format_line("x + 3");
    ASSERT_NOT_NULL(r2, "read result");
    ASSERT_STR_EQ(r2, "OK [NUMBER 5]", "x + 3 = 5");
    free(r2);
    PASS();
}

TEST(test_decl_expr_precedence) {
    /* my x = 1 + 2 * 3 → assigns 7 (assignment binds looser) */
    ASSERT(format_matches("my y = 1 + 2 * 3", "OK [NUMBER 7]"), "decl precedence");
    PASS();
}

TEST(test_assign_returns_value) {
    /* Must declare first, then reassign. */
    char *r1 = repl_format_line("my z = 10");
    free(r1);
    ASSERT(format_matches("z = 20", "OK [NUMBER 20]"), "assign returns value");
    PASS();
}

TEST(test_assign_updates_variable) {
    char *r1 = repl_format_line("my w = 5");
    free(r1);
    char *r2 = repl_format_line("w = 99");
    free(r2);
    ASSERT(format_matches("w", "OK [NUMBER 99]"), "assign updates var");
    PASS();
}

TEST(test_assign_undeclared_error) {
    /* Assigning to undeclared variable is a runtime error. */
    ASSERT(format_matches("undeclared = 1", "ERR undefined variable 'undeclared'"),
           "undeclared assign error");
    PASS();
}

TEST(test_decl_chain) {
    /* my a = my b = 2 → both declared with value 2 */
    char *r = repl_format_line("my aa = my bb = 2");
    ASSERT_NOT_NULL(r, "chain result");
    ASSERT_STR_EQ(r, "OK [NUMBER 2]", "chain value");
    free(r);

    ASSERT(format_matches("aa", "OK [NUMBER 2]"), "aa is 2");
    ASSERT(format_matches("bb", "OK [NUMBER 2]"), "bb is 2");
    PASS();
}

TEST(test_assign_chain) {
    /* Declare p and q, then p = q = 42 */
    char *r1 = repl_format_line("my p = 0");
    free(r1);
    char *r2 = repl_format_line("my q = 0");
    free(r2);

    char *r3 = repl_format_line("p = q = 42");
    ASSERT_NOT_NULL(r3, "assign chain result");
    ASSERT_STR_EQ(r3, "OK [NUMBER 42]", "chain value");
    free(r3);

    ASSERT(format_matches("p", "OK [NUMBER 42]"), "p is 42");
    ASSERT(format_matches("q", "OK [NUMBER 42]"), "q is 42");
    PASS();
}

TEST(test_unknown_ident_still_errors) {
    ASSERT(format_matches("nope + 1", "ERR unknown variable 'nope'"), "unknown ident error");
    PASS();
}

TEST(test_invalid_lhs_error) {
    /* 1 = 2 should be a parse error */
    char *r = repl_format_line("1 = 2");
    ASSERT_NOT_NULL(r, "result not null");
    ASSERT(strncmp(r, "ERR", 3) == 0, "should be error");
    ASSERT(strstr(r, "invalid assignment target") != NULL, "error message");
    free(r);
    PASS();
}

TEST(test_decl_string_value) {
    char *r1 = repl_format_line("my greeting = \"hello\"");
    ASSERT_NOT_NULL(r1, "decl string result");
    ASSERT_STR_EQ(r1, "OK [STRING hello]", "decl string value");
    free(r1);

    ASSERT(format_matches("greeting", "OK [STRING hello]"), "read string var");
    PASS();
}

TEST(test_decl_ast_format) {
    /* Check AST output for my x = 2 */
    ASSERT(ast_format_matches("my x = 2", "AST [DECL x [NUMBER 2]]"), "decl ast");
    PASS();
}

TEST(test_assign_ast_format) {
    /* Check AST output for x = 2 */
    ASSERT(ast_format_matches("x = 2", "AST [ASSIGN x [NUMBER 2]]"), "assign ast");
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
 * repl_eval_line() API tests
 * ============================================================ */

TEST(test_eval_line_blank) {
    ReplResult r = repl_eval_line("", false, false);
    ASSERT(r.ok, "ok for blank");
    ASSERT(r.value == NULL, "no value for blank");
    ASSERT(r.error == NULL, "no error");
    ASSERT(r.tokens == NULL, "no tokens");
    ASSERT(r.ast == NULL, "no ast");
    repl_result_free(&r);
    PASS();
}

TEST(test_eval_line_null) {
    ReplResult r = repl_eval_line(NULL, false, false);
    ASSERT(r.ok, "ok for null");
    ASSERT(r.value == NULL, "no value for null");
    repl_result_free(&r);
    PASS();
}

TEST(test_eval_line_number) {
    ReplResult r = repl_eval_line("42", false, false);
    ASSERT(r.ok, "ok");
    ASSERT_NOT_NULL(r.value, "value set");
    ASSERT_STR_EQ(r.value, "42", "plain number value");
    ASSERT(r.error == NULL, "no error");
    repl_result_free(&r);
    PASS();
}

TEST(test_eval_line_string) {
    ReplResult r = repl_eval_line("\"hello\"", false, false);
    ASSERT(r.ok, "ok");
    ASSERT_STR_EQ(r.value, "hello", "plain string value");
    repl_result_free(&r);
    PASS();
}

TEST(test_eval_line_expression) {
    ReplResult r = repl_eval_line("1 + 2 * 3", false, false);
    ASSERT(r.ok, "ok");
    ASSERT_STR_EQ(r.value, "7", "expression eval");
    repl_result_free(&r);
    PASS();
}

TEST(test_eval_line_parse_error) {
    ReplResult r = repl_eval_line("\"unterminated", false, false);
    ASSERT(!r.ok, "not ok");
    ASSERT(r.value == NULL, "no value on error");
    ASSERT_NOT_NULL(r.error, "error set");
    ASSERT(strstr(r.error, "unterminated") != NULL, "error message");
    repl_result_free(&r);
    PASS();
}

TEST(test_eval_line_eval_error) {
    ReplResult r = repl_eval_line("1 / 0", false, false);
    ASSERT(!r.ok, "not ok");
    ASSERT_NOT_NULL(r.error, "error set");
    ASSERT(strstr(r.error, "division by zero") != NULL, "error message");
    repl_result_free(&r);
    PASS();
}

TEST(test_eval_line_with_tokens) {
    ReplResult r = repl_eval_line("42", true, false);
    ASSERT(r.ok, "ok");
    ASSERT_STR_EQ(r.value, "42", "value");
    ASSERT_NOT_NULL(r.tokens, "tokens present");
    ASSERT(strstr(r.tokens, "TOKENS") != NULL, "tokens prefix");
    ASSERT(strstr(r.tokens, "[NUMBER 42]") != NULL, "token content");
    ASSERT(r.ast == NULL, "no ast");
    repl_result_free(&r);
    PASS();
}

TEST(test_eval_line_with_ast) {
    ReplResult r = repl_eval_line("1 + 2", false, true);
    ASSERT(r.ok, "ok");
    ASSERT_STR_EQ(r.value, "3", "value");
    ASSERT_NOT_NULL(r.ast, "ast present");
    ASSERT(strstr(r.ast, "AST") != NULL, "ast prefix");
    ASSERT(strstr(r.ast, "BINOP") != NULL, "ast content");
    ASSERT(r.tokens == NULL, "no tokens");
    repl_result_free(&r);
    PASS();
}

TEST(test_eval_line_with_both_debug) {
    ReplResult r = repl_eval_line("42", true, true);
    ASSERT(r.ok, "ok");
    ASSERT_STR_EQ(r.value, "42", "value");
    ASSERT_NOT_NULL(r.tokens, "tokens present");
    ASSERT_NOT_NULL(r.ast, "ast present");
    repl_result_free(&r);
    PASS();
}

TEST(test_eval_line_tokens_on_error) {
    /* Best-effort: tokens should still be produced even on parse error. */
    ReplResult r = repl_eval_line("42foo", true, false);
    ASSERT(!r.ok, "not ok");
    ASSERT_NOT_NULL(r.tokens, "tokens present despite error");
    ASSERT(strstr(r.tokens, "TOKENS") != NULL, "tokens prefix");
    ASSERT(strstr(r.tokens, "ERR") != NULL, "tokens contain error");
    repl_result_free(&r);
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

    printf("\nVariable declaration (my):\n");
    RUN_TEST(test_decl_returns_value);
    RUN_TEST(test_decl_then_read);
    RUN_TEST(test_decl_expr_precedence);
    RUN_TEST(test_decl_string_value);
    RUN_TEST(test_decl_chain);
    RUN_TEST(test_decl_ast_format);

    printf("\nAssignment:\n");
    RUN_TEST(test_assign_returns_value);
    RUN_TEST(test_assign_updates_variable);
    RUN_TEST(test_assign_undeclared_error);
    RUN_TEST(test_assign_chain);
    RUN_TEST(test_unknown_ident_still_errors);
    RUN_TEST(test_invalid_lhs_error);
    RUN_TEST(test_assign_ast_format);

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

    printf("\nrepl_eval_line() API:\n");
    RUN_TEST(test_eval_line_blank);
    RUN_TEST(test_eval_line_null);
    RUN_TEST(test_eval_line_number);
    RUN_TEST(test_eval_line_string);
    RUN_TEST(test_eval_line_expression);
    RUN_TEST(test_eval_line_parse_error);
    RUN_TEST(test_eval_line_eval_error);
    RUN_TEST(test_eval_line_with_tokens);
    RUN_TEST(test_eval_line_with_ast);
    RUN_TEST(test_eval_line_with_both_debug);
    RUN_TEST(test_eval_line_tokens_on_error);

    printf("\n========================================\n");
    printf("Tests run: %d\n", tests_run);
    printf("Passed:    %d\n", tests_passed);
    printf("Failed:    %d\n", tests_failed);
    printf("========================================\n");

    return tests_failed > 0 ? 1 : 0;
}
