/*
 * test_repl.c - Tests for the Cutlet REPL core
 *
 * Test coverage for repl_format_line():
 * - Empty and whitespace-only input
 * - Single tokens (NUMBER, STRING, IDENT, OPERATOR)
 * - Multiple tokens
 * - Error formatting with position
 * - NULL input handling
 * - Special characters in strings
 * - Symbol sandwich identifiers
 *
 * Uses the same simple test harness as test_tokenizer.c.
 */

#include "../src/repl.h"
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
        printf("  %-50s ", #name);                                                                 \
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

#define ASSERT_EQ(a, b, msg) ASSERT((a) == (b), msg)
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

/* ============================================================
 * Empty and whitespace input tests
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
 * Single NUMBER token tests
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
 * Single STRING token tests
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

TEST(test_string_with_leading_whitespace) {
    ASSERT(format_matches("  \"hi\"", "OK [STRING hi]"), "string with leading space");
    PASS();
}

/* ============================================================
 * Single IDENT token tests
 * ============================================================ */

TEST(test_single_ident_letter) {
    ASSERT(format_matches("x", "OK [IDENT x]"), "single letter ident");
    PASS();
}

TEST(test_single_ident_simple) {
    ASSERT(format_matches("foo", "OK [IDENT foo]"), "simple ident");
    PASS();
}

TEST(test_single_ident_with_digits) {
    ASSERT(format_matches("foo123", "OK [IDENT foo123]"), "ident with digits");
    PASS();
}

TEST(test_single_ident_kebab_case) {
    /* kebab-case is now IDENT, OPERATOR, IDENT (no symbol sandwich) */
    ASSERT(format_matches("kebab-case", "OK [IDENT kebab] [OPERATOR -] [IDENT case]"),
           "kebab-case");
    PASS();
}

TEST(test_single_ident_multiple_hyphens) {
    /* a-b-c-d is now multiple tokens (no symbol sandwich) */
    ASSERT(format_matches(
               "a-b-c-d",
               "OK [IDENT a] [OPERATOR -] [IDENT b] [OPERATOR -] [IDENT c] [OPERATOR -] [IDENT d]"),
           "multiple hyphens");
    PASS();
}

TEST(test_single_ident_uppercase) {
    ASSERT(format_matches("FOO", "OK [IDENT FOO]"), "uppercase ident");
    PASS();
}

TEST(test_single_ident_mixed_case) {
    ASSERT(format_matches("FooBar", "OK [IDENT FooBar]"), "mixed case ident");
    PASS();
}

TEST(test_single_ident_symbol_sandwich) {
    /* hello+world is now IDENT, OPERATOR, IDENT (no symbol sandwich) */
    ASSERT(format_matches("hello+world", "OK [IDENT hello] [OPERATOR +] [IDENT world]"),
           "no symbol sandwich");
    PASS();
}

TEST(test_single_ident_underscore_sandwich) {
    ASSERT(format_matches("my_var_name", "OK [IDENT my_var_name]"), "underscore sandwich ident");
    PASS();
}

/* ============================================================
 * Single OPERATOR token tests
 * ============================================================ */

TEST(test_single_operator_plus) {
    ASSERT(format_matches("+", "OK [OPERATOR +]"), "plus operator");
    PASS();
}

TEST(test_single_operator_minus) {
    ASSERT(format_matches("-", "OK [OPERATOR -]"), "minus operator");
    PASS();
}

TEST(test_single_operator_at) {
    ASSERT(format_matches("@", "OK [OPERATOR @]"), "at operator");
    PASS();
}

/* ============================================================
 * Multiple token tests
 * ============================================================ */

TEST(test_two_numbers) {
    ASSERT(format_matches("1 2", "OK [NUMBER 1] [NUMBER 2]"), "two numbers");
    PASS();
}

TEST(test_two_strings) {
    ASSERT(format_matches("\"a\" \"b\"", "OK [STRING a] [STRING b]"), "two strings");
    PASS();
}

TEST(test_two_idents) {
    ASSERT(format_matches("foo bar", "OK [IDENT foo] [IDENT bar]"), "two idents");
    PASS();
}

TEST(test_number_string_ident) {
    ASSERT(format_matches("42 \"hello\" foo", "OK [NUMBER 42] [STRING hello] [IDENT foo]"),
           "number string ident");
    PASS();
}

TEST(test_ident_number_string) {
    ASSERT(format_matches("foo 42 \"bar\"", "OK [IDENT foo] [NUMBER 42] [STRING bar]"),
           "ident number string");
    PASS();
}

TEST(test_many_tokens) {
    ASSERT(format_matches("a 1 \"x\" b 2 \"y\"",
                          "OK [IDENT a] [NUMBER 1] [STRING x] [IDENT b] [NUMBER 2] [STRING y]"),
           "many tokens");
    PASS();
}

TEST(test_tokens_with_extra_whitespace) {
    ASSERT(format_matches("  foo   42   \"bar\"  ", "OK [IDENT foo] [NUMBER 42] [STRING bar]"),
           "tokens with extra whitespace");
    PASS();
}

TEST(test_operator_in_sequence) {
    ASSERT(format_matches("foo + bar", "OK [IDENT foo] [OPERATOR +] [IDENT bar]"),
           "operator in sequence");
    PASS();
}

TEST(test_minus_operator_and_number) {
    /* "- 42" is OPERATOR then NUMBER (space-separated) */
    ASSERT(format_matches("- 42", "OK [OPERATOR -] [NUMBER 42]"), "minus operator then number");
    PASS();
}

/* ============================================================
 * Error formatting tests
 * ============================================================ */

TEST(test_error_unterminated_string) {
    /* Exact error format: ERR line:col message */
    ASSERT(format_matches("\"hello", "ERR 1:1 unterminated string"),
           "unterminated string exact error format");
    PASS();
}

TEST(test_adjacent_tokens_string_ident) {
    /* "a"foo is now valid: STRING, IDENT */
    ASSERT(format_matches("\"a\"foo", "OK [STRING a] [IDENT foo]"),
           "adjacent string+ident is valid");
    PASS();
}

TEST(test_adjacent_tokens_number_string) {
    /* 42"hi" is now valid: NUMBER, STRING */
    ASSERT(format_matches("42\"hi\"", "OK [NUMBER 42] [STRING hi]"),
           "adjacent number+string is valid");
    PASS();
}

TEST(test_trailing_symbol) {
    /* foo- is now valid: IDENT, OPERATOR */
    ASSERT(format_matches("foo-", "OK [IDENT foo] [OPERATOR -]"), "trailing symbol is valid");
    PASS();
}

TEST(test_underscore_start) {
    /* _foo is now a valid identifier */
    ASSERT(format_matches("_foo", "OK [IDENT _foo]"), "underscore start is valid ident");
    PASS();
}

TEST(test_negative_number) {
    /* -10 is now valid: OPERATOR, NUMBER */
    ASSERT(format_matches("-10", "OK [OPERATOR -] [NUMBER 10]"), "negative number is op+num");
    PASS();
}

TEST(test_error_number_adjacent_ident) {
    /* Exact error format: ERR line:col message */
    ASSERT(format_matches("42foo", "ERR 1:1 number followed by identifier character"),
           "number adjacent ident exact error format");
    PASS();
}

/* ============================================================
 * Edge cases
 * ============================================================ */

TEST(test_string_with_numbers_inside) {
    ASSERT(format_matches("\"123\"", "OK [STRING 123]"), "string containing numbers");
    PASS();
}

/* ============================================================
 * Return value tests
 * ============================================================ */

TEST(test_result_is_heap_allocated) {
    char *result1 = repl_format_line("foo");
    char *result2 = repl_format_line("bar");
    ASSERT_NOT_NULL(result1, "first result not null");
    ASSERT_NOT_NULL(result2, "second result not null");
    ASSERT_STR_EQ(result1, "OK [IDENT foo]", "first result correct");
    ASSERT_STR_EQ(result2, "OK [IDENT bar]", "second result correct");
    free(result1);
    free(result2);
    PASS();
}

/* ============================================================
 * Main
 * ============================================================ */

int main(void) {
    printf("Running REPL core tests...\n\n");

    printf("Empty and whitespace input:\n");
    RUN_TEST(test_empty_input);
    RUN_TEST(test_null_input);
    RUN_TEST(test_whitespace_only_spaces);
    RUN_TEST(test_whitespace_only_tabs);
    RUN_TEST(test_whitespace_only_newline);
    RUN_TEST(test_whitespace_mixed);

    printf("\nSingle NUMBER tokens:\n");
    RUN_TEST(test_single_number_zero);
    RUN_TEST(test_single_number_positive);
    RUN_TEST(test_single_number_large);
    RUN_TEST(test_number_with_leading_whitespace);
    RUN_TEST(test_number_with_trailing_whitespace);

    printf("\nSingle STRING tokens:\n");
    RUN_TEST(test_single_string_empty);
    RUN_TEST(test_single_string_simple);
    RUN_TEST(test_single_string_with_spaces);
    RUN_TEST(test_single_string_with_digits);
    RUN_TEST(test_string_with_leading_whitespace);

    printf("\nSingle IDENT tokens:\n");
    RUN_TEST(test_single_ident_letter);
    RUN_TEST(test_single_ident_simple);
    RUN_TEST(test_single_ident_with_digits);
    RUN_TEST(test_single_ident_kebab_case);
    RUN_TEST(test_single_ident_multiple_hyphens);
    RUN_TEST(test_single_ident_uppercase);
    RUN_TEST(test_single_ident_mixed_case);
    RUN_TEST(test_single_ident_symbol_sandwich);
    RUN_TEST(test_single_ident_underscore_sandwich);

    printf("\nSingle OPERATOR tokens:\n");
    RUN_TEST(test_single_operator_plus);
    RUN_TEST(test_single_operator_minus);
    RUN_TEST(test_single_operator_at);

    printf("\nMultiple tokens:\n");
    RUN_TEST(test_two_numbers);
    RUN_TEST(test_two_strings);
    RUN_TEST(test_two_idents);
    RUN_TEST(test_number_string_ident);
    RUN_TEST(test_ident_number_string);
    RUN_TEST(test_many_tokens);
    RUN_TEST(test_tokens_with_extra_whitespace);
    RUN_TEST(test_operator_in_sequence);
    RUN_TEST(test_minus_operator_and_number);

    printf("\nError formatting:\n");
    RUN_TEST(test_error_unterminated_string);
    RUN_TEST(test_adjacent_tokens_string_ident);
    RUN_TEST(test_adjacent_tokens_number_string);
    RUN_TEST(test_trailing_symbol);
    RUN_TEST(test_underscore_start);
    RUN_TEST(test_negative_number);
    RUN_TEST(test_error_number_adjacent_ident);

    printf("\nEdge cases:\n");
    RUN_TEST(test_string_with_numbers_inside);

    printf("\nReturn value tests:\n");
    RUN_TEST(test_result_is_heap_allocated);

    printf("\n========================================\n");
    printf("Tests run: %d\n", tests_run);
    printf("Passed:    %d\n", tests_passed);
    printf("Failed:    %d\n", tests_failed);
    printf("========================================\n");

    return tests_failed > 0 ? 1 : 0;
}
