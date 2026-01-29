/*
 * test_tokenizer.c - Tests for the Cutlet tokenizer
 *
 * Test coverage for the v0 tokenizer:
 * - NUMBER tokens (positive integers only, digits only)
 * - STRING tokens (double-quoted, no escapes)
 * - IDENT tokens (ASCII letters, digits, symbol sandwiches)
 * - OPERATOR tokens (symbol chars delimited by whitespace)
 * - Whitespace handling
 * - Error cases with position info
 *
 * Uses a simple test harness - no external dependencies.
 */

#include "../src/tokenizer.h"
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
#define ASSERT_NE(a, b, msg) ASSERT((a) != (b), msg)
#define ASSERT_STR_EQ(a, b, msg) ASSERT(strcmp((a), (b)) == 0, msg)
#define ASSERT_STRN_EQ(a, b, len, msg) ASSERT(strncmp((a), (b), (len)) == 0, msg)
#define ASSERT_TRUE(cond, msg) ASSERT(cond, msg)
#define ASSERT_FALSE(cond, msg) ASSERT(!(cond), msg)
#define ASSERT_NULL(ptr, msg) ASSERT((ptr) == NULL, msg)
#define ASSERT_NOT_NULL(ptr, msg) ASSERT((ptr) != NULL, msg)

#define PASS()                                                                                     \
    do {                                                                                           \
        printf("PASS\n");                                                                          \
        tests_passed++;                                                                            \
    } while (0)

/* Helper to check a token matches expected values */
static int token_matches(Token *tok, TokenType type, const char *value, size_t value_len) {
    if (tok->type != type)
        return 0;
    if (tok->value_len != value_len)
        return 0;
    if (value_len > 0 && strncmp(tok->value, value, value_len) != 0)
        return 0;
    return 1;
}

/* ============================================================
 * Tokenizer creation and destruction tests
 * ============================================================ */

TEST(test_create_tokenizer_with_empty_input) {
    Tokenizer *tok = tokenizer_create("");
    ASSERT_NOT_NULL(tok, "tokenizer_create should succeed with empty input");
    tokenizer_destroy(tok);
    PASS();
}

TEST(test_create_tokenizer_with_valid_input) {
    Tokenizer *tok = tokenizer_create("hello 123");
    ASSERT_NOT_NULL(tok, "tokenizer_create should succeed with valid input");
    tokenizer_destroy(tok);
    PASS();
}

TEST(test_destroy_null_tokenizer) {
    /* Should not crash */
    tokenizer_destroy(NULL);
    PASS();
}

/* ============================================================
 * NUMBER token tests
 * ============================================================ */

TEST(test_number_zero) {
    Tokenizer *tok = tokenizer_create("0");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_NUMBER, "0", 1), "expected NUMBER '0'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_EOF, "expected EOF");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_number_positive_single_digit) {
    Tokenizer *tok = tokenizer_create("7");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_NUMBER, "7", 1), "expected NUMBER '7'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_number_positive_multi_digit) {
    Tokenizer *tok = tokenizer_create("42");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_NUMBER, "42", 2), "expected NUMBER '42'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_number_large) {
    Tokenizer *tok = tokenizer_create("123456789");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_NUMBER, "123456789", 9), "expected NUMBER '123456789'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_number_multiple) {
    Tokenizer *tok = tokenizer_create("1 2 3");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_NUMBER, "1", 1), "expected NUMBER '1'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_NUMBER, "2", 1), "expected NUMBER '2'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_NUMBER, "3", 1), "expected NUMBER '3'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_EOF, "expected EOF");

    tokenizer_destroy(tok);
    PASS();
}

/* ============================================================
 * STRING token tests
 * ============================================================ */

TEST(test_string_empty) {
    Tokenizer *tok = tokenizer_create("\"\"");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_STRING, "", 0), "expected empty STRING");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_string_simple) {
    Tokenizer *tok = tokenizer_create("\"hi\"");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_STRING, "hi", 2), "expected STRING 'hi'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_string_with_spaces) {
    Tokenizer *tok = tokenizer_create("\"hello world\"");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_STRING, "hello world", 11), "expected STRING 'hello world'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_string_with_digits) {
    Tokenizer *tok = tokenizer_create("\"abc123\"");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_STRING, "abc123", 6), "expected STRING 'abc123'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_string_multiple) {
    Tokenizer *tok = tokenizer_create("\"a\" \"b\"");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_STRING, "a", 1), "expected STRING 'a'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_STRING, "b", 1), "expected STRING 'b'");

    tokenizer_destroy(tok);
    PASS();
}

/* ============================================================
 * IDENT token tests
 * ============================================================ */

TEST(test_ident_single_letter) {
    Tokenizer *tok = tokenizer_create("x");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "x", 1), "expected IDENT 'x'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_ident_simple) {
    Tokenizer *tok = tokenizer_create("foo");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "foo", 3), "expected IDENT 'foo'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_ident_with_digits) {
    Tokenizer *tok = tokenizer_create("abc123");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "abc123", 6), "expected IDENT 'abc123'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_ident_with_underscores) {
    Tokenizer *tok = tokenizer_create("my_var_name");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "my_var_name", 11), "expected IDENT 'my_var_name'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_ident_uppercase) {
    Tokenizer *tok = tokenizer_create("FOO");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "FOO", 3), "expected IDENT 'FOO'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_ident_mixed_case) {
    Tokenizer *tok = tokenizer_create("FooBar");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "FooBar", 6), "expected IDENT 'FooBar'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_ident_multiple) {
    Tokenizer *tok = tokenizer_create("foo bar baz");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "foo", 3), "expected IDENT 'foo'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "bar", 3), "expected IDENT 'bar'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "baz", 3), "expected IDENT 'baz'");

    tokenizer_destroy(tok);
    PASS();
}

/* ============================================================
 * Identifier with symbol sandwich tests
 * Symbols between ASCII letters are part of the identifier.
 * ============================================================ */

TEST(test_ident_symbol_sandwich_plus) {
    /* hello+world => IDENT("hello+world") */
    Tokenizer *tok = tokenizer_create("hello+world");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "hello+world", 11), "expected IDENT 'hello+world'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_EOF, "expected EOF");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_ident_symbol_sandwich_underscore) {
    /* hello_world => IDENT("hello_world") */
    Tokenizer *tok = tokenizer_create("hello_world");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "hello_world", 11), "expected IDENT 'hello_world'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_ident_symbol_sandwich_complex) {
    /* hello_-_world => IDENT("hello_-_world") */
    Tokenizer *tok = tokenizer_create("hello_-_world");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "hello_-_world", 13),
                "expected IDENT 'hello_-_world'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_ident_letter_digit_letter) {
    /* a_b2 => IDENT("a_b2") - symbol between letters, then digit at end */
    Tokenizer *tok = tokenizer_create("a_b2");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "a_b2", 4), "expected IDENT 'a_b2'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_ident_symbol_sandwich_hyphen) {
    /* kebab-case => IDENT("kebab-case") */
    Tokenizer *tok = tokenizer_create("kebab-case");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "kebab-case", 10), "expected IDENT 'kebab-case'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_ident_symbol_sandwich_multiple_hyphens) {
    /* my-var-name => IDENT("my-var-name") */
    Tokenizer *tok = tokenizer_create("my-var-name");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "my-var-name", 11), "expected IDENT 'my-var-name'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_ident_symbol_sandwich_single_chars) {
    /* a-b-c => IDENT("a-b-c") */
    Tokenizer *tok = tokenizer_create("a-b-c");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "a-b-c", 5), "expected IDENT 'a-b-c'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_ident_multiple_sandwich) {
    /* foo-bar baz-qux => two idents */
    Tokenizer *tok = tokenizer_create("foo-bar baz-qux");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "foo-bar", 7), "expected IDENT 'foo-bar'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "baz-qux", 7), "expected IDENT 'baz-qux'");

    tokenizer_destroy(tok);
    PASS();
}

/* ============================================================
 * OPERATOR token tests
 * Symbol chars surrounded by whitespace (or SOI/EOI) => OPERATOR
 * ============================================================ */

TEST(test_operator_simple) {
    /* hello + world => IDENT, OPERATOR("+"), IDENT */
    Tokenizer *tok = tokenizer_create("hello + world");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "hello", 5), "expected IDENT 'hello'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_OPERATOR, "+", 1), "expected OPERATOR '+'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "world", 5), "expected IDENT 'world'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_EOF, "expected EOF");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_operator_between_numbers) {
    /* 10 + 10 => NUMBER, OPERATOR("+"), NUMBER */
    Tokenizer *tok = tokenizer_create("10 + 10");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_NUMBER, "10", 2), "expected NUMBER '10'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_OPERATOR, "+", 1), "expected OPERATOR '+'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_NUMBER, "10", 2), "expected NUMBER '10'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_operator_multi_symbol) {
    /* a +-XX b => IDENT, OPERATOR("+-XX"), IDENT (multi-symbol operator) */
    Tokenizer *tok = tokenizer_create("a +-*/ b");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "a", 1), "expected IDENT 'a'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_OPERATOR, "+-*/", 4), "expected OPERATOR '+-*/'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "b", 1), "expected IDENT 'b'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_operator_with_tabs) {
    /* hello\t+\tworld => IDENT, OPERATOR("+"), IDENT */
    Tokenizer *tok = tokenizer_create("hello\t+\tworld");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "hello", 5), "expected IDENT 'hello'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_OPERATOR, "+", 1), "expected OPERATOR '+'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "world", 5), "expected IDENT 'world'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_operator_at_start) {
    /* + hello => OPERATOR("+"), IDENT (SOI counts as whitespace) */
    Tokenizer *tok = tokenizer_create("+ hello");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_OPERATOR, "+", 1), "expected OPERATOR '+'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "hello", 5), "expected IDENT 'hello'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_operator_at_end) {
    /* hello + => IDENT, OPERATOR("+") (EOI counts as whitespace) */
    Tokenizer *tok = tokenizer_create("hello +");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "hello", 5), "expected IDENT 'hello'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_OPERATOR, "+", 1), "expected OPERATOR '+'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_EOF, "expected EOF");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_operator_alone) {
    /* + alone => OPERATOR("+") */
    Tokenizer *tok = tokenizer_create("+");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_OPERATOR, "+", 1), "expected OPERATOR '+'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_EOF, "expected EOF");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_operator_minus_alone) {
    /* - alone => OPERATOR("-") */
    Tokenizer *tok = tokenizer_create("-");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_OPERATOR, "-", 1), "expected OPERATOR '-'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_operator_various_symbols) {
    /* @ ! # $ etc are all valid operator chars */
    Tokenizer *tok = tokenizer_create("a @ b");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "a", 1), "expected IDENT 'a'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_OPERATOR, "@", 1), "expected OPERATOR '@'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "b", 1), "expected IDENT 'b'");

    tokenizer_destroy(tok);
    PASS();
}

/* ============================================================
 * Whitespace handling tests
 * ============================================================ */

TEST(test_whitespace_spaces) {
    Tokenizer *tok = tokenizer_create("   foo   ");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "foo", 3), "expected IDENT 'foo'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_EOF, "expected EOF");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_whitespace_tabs) {
    Tokenizer *tok = tokenizer_create("\tfoo\t");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "foo", 3), "expected IDENT 'foo'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_whitespace_newlines) {
    Tokenizer *tok = tokenizer_create("\nfoo\n");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "foo", 3), "expected IDENT 'foo'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_whitespace_mixed) {
    Tokenizer *tok = tokenizer_create("  \t\n  foo  \n\t  ");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "foo", 3), "expected IDENT 'foo'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_whitespace_only) {
    Tokenizer *tok = tokenizer_create("   \t\n   ");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_EOF, "expected EOF for whitespace-only input");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_empty_input) {
    Tokenizer *tok = tokenizer_create("");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_EOF, "expected EOF for empty input");

    tokenizer_destroy(tok);
    PASS();
}

/* ============================================================
 * Mixed token sequence tests
 * ============================================================ */

TEST(test_mixed_number_string_ident) {
    Tokenizer *tok = tokenizer_create("foo 42 \"hello\"");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "foo", 3), "expected IDENT 'foo'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_NUMBER, "42", 2), "expected NUMBER '42'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_STRING, "hello", 5), "expected STRING 'hello'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_EOF, "expected EOF");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_mixed_with_operators) {
    /* x + 1 "a" y - 2 "b" z */
    Tokenizer *tok = tokenizer_create("x + 1 \"a\" y - 2 \"b\" z");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "x", 1), "expected IDENT 'x'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_OPERATOR, "+", 1), "expected OPERATOR '+'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_NUMBER, "1", 1), "expected NUMBER '1'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_STRING, "a", 1), "expected STRING 'a'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "y", 1), "expected IDENT 'y'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_OPERATOR, "-", 1), "expected OPERATOR '-'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_NUMBER, "2", 1), "expected NUMBER '2'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_STRING, "b", 1), "expected STRING 'b'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "z", 1), "expected IDENT 'z'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_mixed_no_spaces_string_ident_error) {
    /* Adjacent tokens without whitespace are errors */
    Tokenizer *tok = tokenizer_create("\"a\"foo");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    /* String followed by non-whitespace => error */
    ASSERT_EQ(t.type, TOK_ERROR, "expected ERROR for adjacent string+ident");

    tokenizer_destroy(tok);
    PASS();
}

/* ============================================================
 * Position tracking tests
 * ============================================================ */

TEST(test_position_single_token) {
    Tokenizer *tok = tokenizer_create("foo");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.pos, 0, "expected pos 0");
    ASSERT_EQ(t.line, 1, "expected line 1");
    ASSERT_EQ(t.col, 1, "expected col 1");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_position_with_leading_space) {
    Tokenizer *tok = tokenizer_create("  foo");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.pos, 2, "expected pos 2");
    ASSERT_EQ(t.line, 1, "expected line 1");
    ASSERT_EQ(t.col, 3, "expected col 3");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_position_multiline) {
    Tokenizer *tok = tokenizer_create("foo\nbar");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.pos, 0, "expected pos 0 for foo");
    ASSERT_EQ(t.line, 1, "expected line 1 for foo");
    ASSERT_EQ(t.col, 1, "expected col 1 for foo");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.pos, 4, "expected pos 4 for bar");
    ASSERT_EQ(t.line, 2, "expected line 2 for bar");
    ASSERT_EQ(t.col, 1, "expected col 1 for bar");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_position_multiple_tokens_same_line) {
    Tokenizer *tok = tokenizer_create("a b c");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.col, 1, "expected col 1 for a");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.col, 3, "expected col 3 for b");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.col, 5, "expected col 5 for c");

    tokenizer_destroy(tok);
    PASS();
}

/* ============================================================
 * Error case tests
 * ============================================================ */

TEST(test_error_unterminated_string) {
    Tokenizer *tok = tokenizer_create("\"hello");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_ERROR, "expected ERROR for unterminated string");
    ASSERT_EQ(t.pos, 0, "expected error at pos 0");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_error_unterminated_string_with_newline) {
    Tokenizer *tok = tokenizer_create("\"hello\nworld");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_ERROR, "expected ERROR for unterminated string at newline");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_error_sticky) {
    /* After an error, subsequent calls should also return error */
    /* Use a scenario that produces an error: number followed by non-whitespace */
    Tokenizer *tok = tokenizer_create("42foo");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_ERROR, "expected ERROR");

    /* Calling again should still return error */
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_ERROR, "expected ERROR on second call");

    tokenizer_destroy(tok);
    PASS();
}

/* Error: symbol between digit and letter in ident */
TEST(test_error_digit_symbol_letter) {
    /* a1_b => ERROR (symbol between digit and letter) */
    Tokenizer *tok = tokenizer_create("a1_b");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_ERROR, "expected ERROR for symbol between digit and letter");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_error_ident_digit_symbol) {
    /* hello10+world => ERROR (symbol after digit in ident) */
    Tokenizer *tok = tokenizer_create("hello10+world");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_ERROR, "expected ERROR for symbol after digit in ident");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_error_symbol_before_digit) {
    /* hello+20world => ERROR (symbol before digit in ident) */
    Tokenizer *tok = tokenizer_create("hello+20world");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_ERROR, "expected ERROR for symbol before digit in ident");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_error_number_adjacent_no_whitespace) {
    /* 10+10 => ERROR (number followed by non-whitespace) */
    Tokenizer *tok = tokenizer_create("10+10");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_ERROR, "expected ERROR for number adjacent without whitespace");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_error_negative_number_removed) {
    /* -10 => OPERATOR("-"), NUMBER(10) when space-separated; but -10 has no space
     * SOI counts as whitespace for operator, but the '-' is followed by '1' not whitespace,
     * so '-' is not a valid operator here. It's a symbol at SOI but next char is digit not ws.
     * Actually: SOI -> symbol char -> try operator -> consume symbols -> check next is ws/EOF
     * -10: '-' consumed, next is '1' (digit, not ws/EOF) -> ERROR */
    Tokenizer *tok = tokenizer_create("-10");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_ERROR, "expected ERROR for -10 (not whitespace-delimited)");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_error_string_adjacent_non_whitespace) {
    /* "a"+b => ERROR (non-whitespace after string) */
    Tokenizer *tok = tokenizer_create("\"a\"+b");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_ERROR, "expected ERROR for non-whitespace after string");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_error_underscore_start) {
    /* _hello => ERROR (symbol can't start identifier, not whitespace-delimited operator) */
    Tokenizer *tok = tokenizer_create("_hello");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_ERROR, "expected ERROR for underscore-started ident");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_error_number_followed_by_ident) {
    /* 10foo => ERROR (number followed by non-whitespace) */
    Tokenizer *tok = tokenizer_create("10foo");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_ERROR, "expected ERROR for '10foo'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_error_letter_digit_symbol_letter) {
    /* ab_12 => ERROR (symbol between letter and digit... wait, this is
     * letter-letter then symbol then digit-digit. The symbol '_' is between
     * 'b' (letter) and '1' (digit). Per rules: symbol must be sandwiched
     * between letters on both sides. Digit after symbol => error. */
    Tokenizer *tok = tokenizer_create("ab_12");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_ERROR, "expected ERROR for symbol before digit in ident");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_error_ident_adjacent_string) {
    /* foo"bar" - ident immediately followed by string */
    Tokenizer *tok = tokenizer_create("foo\"bar\"");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_ERROR, "expected ERROR for ident adjacent to string");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_error_number_adjacent_string) {
    /* 123"x" - number immediately followed by string */
    Tokenizer *tok = tokenizer_create("123\"x\"");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_ERROR, "expected ERROR for number adjacent to string");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_error_string_adjacent_number) {
    /* "a"42 - string immediately followed by number */
    Tokenizer *tok = tokenizer_create("\"a\"42");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_ERROR, "expected ERROR for string adjacent to number");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_error_string_adjacent_string) {
    /* "a""b" - two strings without whitespace */
    Tokenizer *tok = tokenizer_create("\"a\"\"b\"");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_ERROR, "expected ERROR for adjacent strings");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_error_ident_adjacent_number) {
    /* foo42 is a valid identifier (letters then digits) - NOT an error */
    Tokenizer *tok = tokenizer_create("foo42");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "foo42", 5), "expected IDENT 'foo42'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_adjacent_tokens_with_space_ok) {
    /* Verify that properly spaced tokens work */
    Tokenizer *tok = tokenizer_create("\"a\" foo 42");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_STRING, "a", 1), "expected STRING 'a'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "foo", 3), "expected IDENT 'foo'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_NUMBER, "42", 2), "expected NUMBER '42'");

    tokenizer_destroy(tok);
    PASS();
}

/* ============================================================
 * EOF behavior tests
 * ============================================================ */

TEST(test_eof_sticky) {
    Tokenizer *tok = tokenizer_create("x");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_IDENT, "expected IDENT");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_EOF, "expected EOF");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_EOF, "expected EOF on second call");

    tokenizer_destroy(tok);
    PASS();
}

/* ============================================================
 * Reset functionality tests
 * ============================================================ */

TEST(test_reset_tokenizer) {
    Tokenizer *tok = tokenizer_create("foo bar");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "foo", 3), "expected IDENT 'foo'");

    ASSERT_TRUE(tokenizer_reset(tok), "tokenizer_reset failed");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "foo", 3), "expected IDENT 'foo' after reset");

    tokenizer_destroy(tok);
    PASS();
}

/* ============================================================
 * Edge cases and boundary tests
 * ============================================================ */

TEST(test_number_followed_by_ident_no_space_error) {
    Tokenizer *tok = tokenizer_create("42foo");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_ERROR, "expected ERROR for '42foo'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_token_type_str) {
    ASSERT_STR_EQ(token_type_str(TOK_NUMBER), "NUMBER", "NUMBER string");
    ASSERT_STR_EQ(token_type_str(TOK_STRING), "STRING", "STRING string");
    ASSERT_STR_EQ(token_type_str(TOK_IDENT), "IDENT", "IDENT string");
    ASSERT_STR_EQ(token_type_str(TOK_OPERATOR), "OPERATOR", "OPERATOR string");
    ASSERT_STR_EQ(token_type_str(TOK_EOF), "EOF", "EOF string");
    ASSERT_STR_EQ(token_type_str(TOK_ERROR), "ERROR", "ERROR string");
    PASS();
}

/* ============================================================
 * Null input handling
 * ============================================================ */

TEST(test_null_tokenizer_next) {
    Token t;
    ASSERT_FALSE(tokenizer_next(NULL, &t), "tokenizer_next should fail with NULL tokenizer");
    PASS();
}

TEST(test_null_token_output) {
    Tokenizer *tok = tokenizer_create("x");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    ASSERT_FALSE(tokenizer_next(tok, NULL), "tokenizer_next should fail with NULL output");

    tokenizer_destroy(tok);
    PASS();
}

/* ============================================================
 * Main test runner
 * ============================================================ */

int main(void) {
    printf("\n=== Cutlet Tokenizer Tests ===\n\n");

    printf("Creation and destruction:\n");
    RUN_TEST(test_create_tokenizer_with_empty_input);
    RUN_TEST(test_create_tokenizer_with_valid_input);
    RUN_TEST(test_destroy_null_tokenizer);

    printf("\nNUMBER tokens:\n");
    RUN_TEST(test_number_zero);
    RUN_TEST(test_number_positive_single_digit);
    RUN_TEST(test_number_positive_multi_digit);
    RUN_TEST(test_number_large);
    RUN_TEST(test_number_multiple);

    printf("\nSTRING tokens:\n");
    RUN_TEST(test_string_empty);
    RUN_TEST(test_string_simple);
    RUN_TEST(test_string_with_spaces);
    RUN_TEST(test_string_with_digits);
    RUN_TEST(test_string_multiple);

    printf("\nIDENT tokens:\n");
    RUN_TEST(test_ident_single_letter);
    RUN_TEST(test_ident_simple);
    RUN_TEST(test_ident_with_digits);
    RUN_TEST(test_ident_with_underscores);
    RUN_TEST(test_ident_uppercase);
    RUN_TEST(test_ident_mixed_case);
    RUN_TEST(test_ident_multiple);

    printf("\nIdentifier symbol sandwich:\n");
    RUN_TEST(test_ident_symbol_sandwich_plus);
    RUN_TEST(test_ident_symbol_sandwich_underscore);
    RUN_TEST(test_ident_symbol_sandwich_complex);
    RUN_TEST(test_ident_letter_digit_letter);
    RUN_TEST(test_ident_symbol_sandwich_hyphen);
    RUN_TEST(test_ident_symbol_sandwich_multiple_hyphens);
    RUN_TEST(test_ident_symbol_sandwich_single_chars);
    RUN_TEST(test_ident_multiple_sandwich);

    printf("\nOPERATOR tokens:\n");
    RUN_TEST(test_operator_simple);
    RUN_TEST(test_operator_between_numbers);
    RUN_TEST(test_operator_multi_symbol);
    RUN_TEST(test_operator_with_tabs);
    RUN_TEST(test_operator_at_start);
    RUN_TEST(test_operator_at_end);
    RUN_TEST(test_operator_alone);
    RUN_TEST(test_operator_minus_alone);
    RUN_TEST(test_operator_various_symbols);

    printf("\nWhitespace handling:\n");
    RUN_TEST(test_whitespace_spaces);
    RUN_TEST(test_whitespace_tabs);
    RUN_TEST(test_whitespace_newlines);
    RUN_TEST(test_whitespace_mixed);
    RUN_TEST(test_whitespace_only);
    RUN_TEST(test_empty_input);

    printf("\nMixed token sequences:\n");
    RUN_TEST(test_mixed_number_string_ident);
    RUN_TEST(test_mixed_with_operators);
    RUN_TEST(test_mixed_no_spaces_string_ident_error);

    printf("\nPosition tracking:\n");
    RUN_TEST(test_position_single_token);
    RUN_TEST(test_position_with_leading_space);
    RUN_TEST(test_position_multiline);
    RUN_TEST(test_position_multiple_tokens_same_line);

    printf("\nError cases:\n");
    RUN_TEST(test_error_unterminated_string);
    RUN_TEST(test_error_unterminated_string_with_newline);
    RUN_TEST(test_error_sticky);
    RUN_TEST(test_error_digit_symbol_letter);
    RUN_TEST(test_error_ident_digit_symbol);
    RUN_TEST(test_error_symbol_before_digit);
    RUN_TEST(test_error_number_adjacent_no_whitespace);
    RUN_TEST(test_error_negative_number_removed);
    RUN_TEST(test_error_string_adjacent_non_whitespace);
    RUN_TEST(test_error_underscore_start);
    RUN_TEST(test_error_number_followed_by_ident);
    RUN_TEST(test_error_letter_digit_symbol_letter);
    RUN_TEST(test_error_ident_adjacent_string);
    RUN_TEST(test_error_number_adjacent_string);
    RUN_TEST(test_error_string_adjacent_number);
    RUN_TEST(test_error_string_adjacent_string);
    RUN_TEST(test_error_ident_adjacent_number);
    RUN_TEST(test_adjacent_tokens_with_space_ok);

    printf("\nEOF behavior:\n");
    RUN_TEST(test_eof_sticky);

    printf("\nReset functionality:\n");
    RUN_TEST(test_reset_tokenizer);

    printf("\nEdge cases:\n");
    RUN_TEST(test_number_followed_by_ident_no_space_error);
    RUN_TEST(test_token_type_str);

    printf("\nNull input handling:\n");
    RUN_TEST(test_null_tokenizer_next);
    RUN_TEST(test_null_token_output);

    printf("\n=== Summary ===\n");
    printf("Tests run:    %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
