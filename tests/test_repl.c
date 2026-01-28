/*
 * test_repl.c - Tests for the Cutlet REPL core
 *
 * Test coverage for repl_format_line():
 * - Empty and whitespace-only input
 * - Single tokens (NUMBER, STRING, IDENT)
 * - Multiple tokens
 * - Error formatting with position
 * - NULL input handling
 * - Special characters in strings
 * - Unicode identifiers
 * - Kebab-case identifiers
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

#define RUN_TEST(name) do { \
    tests_run++; \
    printf("  %-50s ", #name); \
    fflush(stdout); \
    name(); \
} while(0)

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("FAIL\n"); \
        printf("    Assertion failed: %s\n", msg); \
        printf("    At %s:%d\n", __FILE__, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_EQ(a, b, msg) ASSERT((a) == (b), msg)
#define ASSERT_STR_EQ(a, b, msg) ASSERT(strcmp((a), (b)) == 0, msg)
#define ASSERT_NOT_NULL(ptr, msg) ASSERT((ptr) != NULL, msg)

#define PASS() do { \
    printf("PASS\n"); \
    tests_passed++; \
} while(0)

/* Helper to run format and compare result */
static int format_matches(const char *input, const char *expected) {
    char *result = repl_format_line(input);
    if (result == NULL) return 0;
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

TEST(test_single_number_negative) {
    ASSERT(format_matches("-7", "OK [NUMBER -7]"), "negative number");
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

TEST(test_single_ident_underscore_start) {
    ASSERT(format_matches("_foo", "OK [IDENT _foo]"), "underscore start");
    PASS();
}

TEST(test_single_ident_with_digits) {
    ASSERT(format_matches("foo123", "OK [IDENT foo123]"), "ident with digits");
    PASS();
}

TEST(test_single_ident_kebab_case) {
    ASSERT(format_matches("kebab-case", "OK [IDENT kebab-case]"), "kebab-case");
    PASS();
}

TEST(test_single_ident_multiple_hyphens) {
    ASSERT(format_matches("a-b-c-d", "OK [IDENT a-b-c-d]"), "multiple hyphens");
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
    ASSERT(format_matches("  foo   42   \"bar\"  ",
           "OK [IDENT foo] [NUMBER 42] [STRING bar]"),
           "tokens with extra whitespace");
    PASS();
}

TEST(test_negative_number_in_sequence) {
    ASSERT(format_matches("foo -42 bar", "OK [IDENT foo] [NUMBER -42] [IDENT bar]"),
           "negative number in sequence");
    PASS();
}

/* ============================================================
 * Error formatting tests
 * ============================================================ */

TEST(test_error_invalid_char) {
    char *result = repl_format_line("@");
    ASSERT_NOT_NULL(result, "result should not be null");
    /* Error format: ERR line:col message */
    ASSERT(strncmp(result, "ERR 1:1 ", 8) == 0, "error should start with ERR 1:1");
    free(result);
    PASS();
}

TEST(test_error_invalid_char_after_whitespace) {
    char *result = repl_format_line("  @");
    ASSERT_NOT_NULL(result, "result should not be null");
    ASSERT(strncmp(result, "ERR 1:3 ", 8) == 0, "error should start with ERR 1:3");
    free(result);
    PASS();
}

TEST(test_error_unterminated_string) {
    char *result = repl_format_line("\"hello");
    ASSERT_NOT_NULL(result, "result should not be null");
    ASSERT(strncmp(result, "ERR ", 4) == 0, "error should start with ERR");
    free(result);
    PASS();
}

TEST(test_error_adjacent_tokens_string_ident) {
    char *result = repl_format_line("\"a\"foo");
    ASSERT_NOT_NULL(result, "result should not be null");
    ASSERT(strncmp(result, "ERR ", 4) == 0, "adjacent tokens should be error");
    free(result);
    PASS();
}

TEST(test_error_adjacent_tokens_number_string) {
    char *result = repl_format_line("42\"hi\"");
    ASSERT_NOT_NULL(result, "result should not be null");
    ASSERT(strncmp(result, "ERR ", 4) == 0, "adjacent number+string should be error");
    free(result);
    PASS();
}

TEST(test_error_trailing_hyphen) {
    char *result = repl_format_line("foo-");
    ASSERT_NOT_NULL(result, "result should not be null");
    ASSERT(strncmp(result, "ERR ", 4) == 0, "trailing hyphen should be error");
    free(result);
    PASS();
}

TEST(test_error_position_multiline) {
    /* Error on line 2, column 1 */
    char *result = repl_format_line("foo\n@");
    ASSERT_NOT_NULL(result, "result should not be null");
    ASSERT(strncmp(result, "ERR 2:1 ", 8) == 0, "error should be at line 2 col 1");
    free(result);
    PASS();
}

TEST(test_error_after_valid_tokens) {
    /* Valid tokens followed by error */
    char *result = repl_format_line("foo 42 @");
    ASSERT_NOT_NULL(result, "result should not be null");
    ASSERT(strncmp(result, "ERR 1:8 ", 8) == 0, "error at position after valid tokens");
    free(result);
    PASS();
}

/* ============================================================
 * Unicode identifier tests
 * ============================================================ */

TEST(test_unicode_cyrillic) {
    ASSERT(format_matches("привет", "OK [IDENT привет]"), "Cyrillic identifier");
    PASS();
}

TEST(test_unicode_greek) {
    ASSERT(format_matches("λ", "OK [IDENT λ]"), "Greek letter");
    PASS();
}

TEST(test_unicode_devanagari) {
    ASSERT(format_matches("नमस्ते", "OK [IDENT नमस्ते]"), "Devanagari identifier");
    PASS();
}

TEST(test_unicode_mixed_with_ascii) {
    ASSERT(format_matches("foo привет bar", "OK [IDENT foo] [IDENT привет] [IDENT bar]"),
           "Unicode mixed with ASCII");
    PASS();
}

/* ============================================================
 * Edge cases
 * ============================================================ */

TEST(test_lone_minus) {
    char *result = repl_format_line("-");
    ASSERT_NOT_NULL(result, "result should not be null");
    ASSERT(strncmp(result, "ERR ", 4) == 0, "lone minus should be error");
    free(result);
    PASS();
}

TEST(test_minus_space_number) {
    /* "- 42" is error (lone minus) then number, but first error wins */
    char *result = repl_format_line("- 42");
    ASSERT_NOT_NULL(result, "result should not be null");
    ASSERT(strncmp(result, "ERR ", 4) == 0, "minus space number should error");
    free(result);
    PASS();
}

TEST(test_string_with_numbers_inside) {
    ASSERT(format_matches("\"123\"", "OK [STRING 123]"), "string containing numbers");
    PASS();
}

TEST(test_ident_then_negative_number) {
    ASSERT(format_matches("x -5", "OK [IDENT x] [NUMBER -5]"), "ident then negative number");
    PASS();
}

TEST(test_multiple_negative_numbers) {
    ASSERT(format_matches("-1 -2 -3", "OK [NUMBER -1] [NUMBER -2] [NUMBER -3]"),
           "multiple negative numbers");
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
    /* Both should be independently valid */
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
    RUN_TEST(test_single_number_negative);
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
    RUN_TEST(test_single_ident_underscore_start);
    RUN_TEST(test_single_ident_with_digits);
    RUN_TEST(test_single_ident_kebab_case);
    RUN_TEST(test_single_ident_multiple_hyphens);
    RUN_TEST(test_single_ident_uppercase);
    RUN_TEST(test_single_ident_mixed_case);

    printf("\nMultiple tokens:\n");
    RUN_TEST(test_two_numbers);
    RUN_TEST(test_two_strings);
    RUN_TEST(test_two_idents);
    RUN_TEST(test_number_string_ident);
    RUN_TEST(test_ident_number_string);
    RUN_TEST(test_many_tokens);
    RUN_TEST(test_tokens_with_extra_whitespace);
    RUN_TEST(test_negative_number_in_sequence);

    printf("\nError formatting:\n");
    RUN_TEST(test_error_invalid_char);
    RUN_TEST(test_error_invalid_char_after_whitespace);
    RUN_TEST(test_error_unterminated_string);
    RUN_TEST(test_error_adjacent_tokens_string_ident);
    RUN_TEST(test_error_adjacent_tokens_number_string);
    RUN_TEST(test_error_trailing_hyphen);
    RUN_TEST(test_error_position_multiline);
    RUN_TEST(test_error_after_valid_tokens);

    printf("\nUnicode identifiers:\n");
    RUN_TEST(test_unicode_cyrillic);
    RUN_TEST(test_unicode_greek);
    RUN_TEST(test_unicode_devanagari);
    RUN_TEST(test_unicode_mixed_with_ascii);

    printf("\nEdge cases:\n");
    RUN_TEST(test_lone_minus);
    RUN_TEST(test_minus_space_number);
    RUN_TEST(test_string_with_numbers_inside);
    RUN_TEST(test_ident_then_negative_number);
    RUN_TEST(test_multiple_negative_numbers);

    printf("\nReturn value tests:\n");
    RUN_TEST(test_result_is_heap_allocated);

    printf("\n========================================\n");
    printf("Tests run: %d\n", tests_run);
    printf("Passed:    %d\n", tests_passed);
    printf("Failed:    %d\n", tests_failed);
    printf("========================================\n");

    return tests_failed > 0 ? 1 : 0;
}
