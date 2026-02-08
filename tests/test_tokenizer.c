/*
 * test_tokenizer.c - Tests for the Cutlet tokenizer
 *
 * Test coverage for the simplified tokenizer:
 * - NUMBER tokens (positive integers only, digits only)
 * - STRING tokens (double-quoted, no escapes)
 * - IDENT tokens (Python/Ruby style: [A-Za-z_][A-Za-z0-9_]*)
 * - OPERATOR tokens (one or more symbol chars, no whitespace delimiter required)
 * - Adjacency: tokens may be adjacent without whitespace
 * - Only adjacency error: number followed by ident-start char
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
 * Comparison operator tokenization tests
 * ============================================================ */

TEST(test_tok_eq_eq) {
    Tokenizer *tok = tokenizer_create("==");
    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "next");
    ASSERT_TRUE(token_matches(&t, TOK_OPERATOR, "==", 2), "== is single operator");
    ASSERT_TRUE(tokenizer_next(tok, &t), "next");
    ASSERT_EQ(t.type, TOK_EOF, "then EOF");
    tokenizer_destroy(tok);
    PASS();
}

TEST(test_tok_not_eq) {
    Tokenizer *tok = tokenizer_create("!=");
    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "next");
    ASSERT_TRUE(token_matches(&t, TOK_OPERATOR, "!=", 2), "!= is single operator");
    tokenizer_destroy(tok);
    PASS();
}

TEST(test_tok_less_eq) {
    Tokenizer *tok = tokenizer_create("<=");
    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "next");
    ASSERT_TRUE(token_matches(&t, TOK_OPERATOR, "<=", 2), "<= is single operator");
    tokenizer_destroy(tok);
    PASS();
}

TEST(test_tok_greater_eq) {
    Tokenizer *tok = tokenizer_create(">=");
    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "next");
    ASSERT_TRUE(token_matches(&t, TOK_OPERATOR, ">=", 2), ">= is single operator");
    tokenizer_destroy(tok);
    PASS();
}

TEST(test_tok_less_than) {
    Tokenizer *tok = tokenizer_create("< 2");
    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "next");
    ASSERT_TRUE(token_matches(&t, TOK_OPERATOR, "<", 1), "< is operator");
    ASSERT_TRUE(tokenizer_next(tok, &t), "next");
    ASSERT_TRUE(token_matches(&t, TOK_NUMBER, "2", 1), "then 2");
    tokenizer_destroy(tok);
    PASS();
}

TEST(test_tok_greater_than) {
    Tokenizer *tok = tokenizer_create("> 2");
    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "next");
    ASSERT_TRUE(token_matches(&t, TOK_OPERATOR, ">", 1), "> is operator");
    tokenizer_destroy(tok);
    PASS();
}

TEST(test_tok_comparison_in_expr) {
    /* "1 == 2" should tokenize as NUMBER OPERATOR NUMBER */
    Tokenizer *tok = tokenizer_create("1 == 2");
    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "next");
    ASSERT_TRUE(token_matches(&t, TOK_NUMBER, "1", 1), "1");
    ASSERT_TRUE(tokenizer_next(tok, &t), "next");
    ASSERT_TRUE(token_matches(&t, TOK_OPERATOR, "==", 2), "==");
    ASSERT_TRUE(tokenizer_next(tok, &t), "next");
    ASSERT_TRUE(token_matches(&t, TOK_NUMBER, "2", 1), "2");
    ASSERT_TRUE(tokenizer_next(tok, &t), "next");
    ASSERT_EQ(t.type, TOK_EOF, "EOF");
    tokenizer_destroy(tok);
    PASS();
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
 * Python/Ruby style: [A-Za-z_][A-Za-z0-9_]*
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

/* Underscore-started identifiers (now valid) */
TEST(test_ident_underscore_only) {
    /* _ => IDENT("_") */
    Tokenizer *tok = tokenizer_create("_");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "_", 1), "expected IDENT '_'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_EOF, "expected EOF");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_ident_underscore_start) {
    /* _foo => IDENT("_foo") */
    Tokenizer *tok = tokenizer_create("_foo");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "_foo", 4), "expected IDENT '_foo'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_ident_trailing_underscore) {
    /* foo_ => IDENT("foo_") */
    Tokenizer *tok = tokenizer_create("foo_");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "foo_", 4), "expected IDENT 'foo_'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_EOF, "expected EOF");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_ident_dunder_init) {
    /* __init__ => IDENT("__init__") */
    Tokenizer *tok = tokenizer_create("__init__");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "__init__", 8), "expected IDENT '__init__'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_ident_a_b2) {
    /* a_b2 => IDENT("a_b2") - underscores are ident-continue */
    Tokenizer *tok = tokenizer_create("a_b2");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "a_b2", 4), "expected IDENT 'a_b2'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_ident_ab_12) {
    /* ab_12 => IDENT("ab_12") - underscores and digits are ident-continue */
    Tokenizer *tok = tokenizer_create("ab_12");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "ab_12", 5), "expected IDENT 'ab_12'");

    tokenizer_destroy(tok);
    PASS();
}

/* ============================================================
 * Former symbol sandwich tests - now produce multiple tokens
 * ============================================================ */

TEST(test_no_sandwich_plus) {
    /* hello+world => IDENT("hello"), OPERATOR("+"), IDENT("world") */
    Tokenizer *tok = tokenizer_create("hello+world");
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

TEST(test_no_sandwich_hyphen) {
    /* kebab-case => IDENT("kebab"), OPERATOR("-"), IDENT("case") */
    Tokenizer *tok = tokenizer_create("kebab-case");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "kebab", 5), "expected IDENT 'kebab'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_OPERATOR, "-", 1), "expected OPERATOR '-'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "case", 4), "expected IDENT 'case'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_no_sandwich_multiple_hyphens) {
    /* a-b-c => IDENT, OP, IDENT, OP, IDENT */
    Tokenizer *tok = tokenizer_create("a-b-c");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "a", 1), "expected IDENT 'a'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_OPERATOR, "-", 1), "expected OPERATOR '-'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "b", 1), "expected IDENT 'b'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_OPERATOR, "-", 1), "expected OPERATOR '-'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "c", 1), "expected IDENT 'c'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_no_sandwich_complex) {
    /* hello_-_world => IDENT("hello_"), OPERATOR("-"), IDENT("_world") */
    Tokenizer *tok = tokenizer_create("hello_-_world");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "hello_", 6), "expected IDENT 'hello_'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_OPERATOR, "-", 1), "expected OPERATOR '-'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "_world", 6), "expected IDENT '_world'");

    tokenizer_destroy(tok);
    PASS();
}

/* ============================================================
 * OPERATOR token tests
 * Symbol chars => OPERATOR. No whitespace delimiter required.
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
    /* Solo symbols (+, -, /, (, ), ,) are emitted as single-char tokens.
       Non-solo symbols group with adjacent non-solo symbols. */
    Tokenizer *tok = tokenizer_create("a +-*/ b");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "a", 1), "expected IDENT 'a'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_OPERATOR, "+", 1), "expected OPERATOR '+'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_OPERATOR, "-", 1), "expected OPERATOR '-'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_OPERATOR, "*", 1), "expected OPERATOR '*'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_OPERATOR, "/", 1), "expected OPERATOR '/'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "b", 1), "expected IDENT 'b'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_operator_with_tabs) {
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
    Tokenizer *tok = tokenizer_create("-");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_OPERATOR, "-", 1), "expected OPERATOR '-'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_operator_various_symbols) {
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

/* Operators without whitespace (adjacency allowed) */
TEST(test_operator_no_whitespace_a_plus_b) {
    /* a+b => IDENT("a"), OPERATOR("+"), IDENT("b") */
    Tokenizer *tok = tokenizer_create("a+b");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "a", 1), "expected IDENT 'a'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_OPERATOR, "+", 1), "expected OPERATOR '+'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "b", 1), "expected IDENT 'b'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_operator_no_whitespace_x_eq_eq_y) {
    /* x==y => IDENT("x"), OPERATOR("=="), IDENT("y") */
    Tokenizer *tok = tokenizer_create("x==y");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "x", 1), "expected IDENT 'x'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_OPERATOR, "==", 2), "expected OPERATOR '=='");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "y", 1), "expected IDENT 'y'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_operator_parens) {
    /* (foo) => OPERATOR("("), IDENT("foo"), OPERATOR(")") */
    Tokenizer *tok = tokenizer_create("(foo)");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_OPERATOR, "(", 1), "expected OPERATOR '('");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "foo", 3), "expected IDENT 'foo'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_OPERATOR, ")", 1), "expected OPERATOR ')'");

    tokenizer_destroy(tok);
    PASS();
}

/* ============================================================
 * Adjacency tests (now valid - Python/Ruby style)
 * ============================================================ */

TEST(test_adjacency_number_op_number) {
    /* 10+10 => NUMBER("10"), OPERATOR("+"), NUMBER("10") */
    Tokenizer *tok = tokenizer_create("10+10");
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

TEST(test_adjacency_negative_number) {
    /* -10 => OPERATOR("-"), NUMBER("10") */
    Tokenizer *tok = tokenizer_create("-10");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_OPERATOR, "-", 1), "expected OPERATOR '-'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_NUMBER, "10", 2), "expected NUMBER '10'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_adjacency_string_op_ident) {
    /* "a"+b => STRING("a"), OPERATOR("+"), IDENT("b") */
    Tokenizer *tok = tokenizer_create("\"a\"+b");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_STRING, "a", 1), "expected STRING 'a'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_OPERATOR, "+", 1), "expected OPERATOR '+'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "b", 1), "expected IDENT 'b'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_adjacency_ident_string) {
    /* foo"bar" => IDENT("foo"), STRING("bar") */
    Tokenizer *tok = tokenizer_create("foo\"bar\"");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "foo", 3), "expected IDENT 'foo'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_STRING, "bar", 3), "expected STRING 'bar'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_adjacency_string_string) {
    /* "a""b" => STRING("a"), STRING("b") */
    Tokenizer *tok = tokenizer_create("\"a\"\"b\"");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_STRING, "a", 1), "expected STRING 'a'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_STRING, "b", 1), "expected STRING 'b'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_adjacency_string_number) {
    /* "a"42 => STRING("a"), NUMBER("42") */
    Tokenizer *tok = tokenizer_create("\"a\"42");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_STRING, "a", 1), "expected STRING 'a'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_NUMBER, "42", 2), "expected NUMBER '42'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_adjacency_number_string) {
    /* 123"x" => NUMBER("123"), STRING("x") */
    Tokenizer *tok = tokenizer_create("123\"x\"");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_NUMBER, "123", 3), "expected NUMBER '123'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_STRING, "x", 1), "expected STRING 'x'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_adjacency_string_ident) {
    /* "a"foo => STRING("a"), IDENT("foo") */
    Tokenizer *tok = tokenizer_create("\"a\"foo");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_STRING, "a", 1), "expected STRING 'a'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "foo", 3), "expected IDENT 'foo'");

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
    /* With TOK_NEWLINE, newlines are tokens, not whitespace.
     * "\nfoo\n" produces: NEWLINE, IDENT, NEWLINE, EOF */
    Tokenizer *tok = tokenizer_create("\nfoo\n");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_NEWLINE, "expected leading NEWLINE");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "foo", 3), "expected IDENT 'foo'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_NEWLINE, "expected trailing NEWLINE");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_EOF, "expected EOF");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_whitespace_mixed) {
    /* With TOK_NEWLINE, newlines are tokens. Spaces/tabs are still skipped.
     * "  \t\n  foo  \n\t  " produces: NEWLINE, IDENT, NEWLINE, EOF */
    Tokenizer *tok = tokenizer_create("  \t\n  foo  \n\t  ");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_NEWLINE, "expected leading NEWLINE");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "foo", 3), "expected IDENT 'foo'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_NEWLINE, "expected trailing NEWLINE");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_EOF, "expected EOF");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_whitespace_only) {
    /* With TOK_NEWLINE, newlines are tokens. "   \t\n   " produces: NEWLINE, EOF */
    Tokenizer *tok = tokenizer_create("   \t\n   ");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_NEWLINE, "expected NEWLINE");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_EOF, "expected EOF");

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
    /* With TOK_NEWLINE: "foo\nbar" → IDENT(foo), NEWLINE, IDENT(bar), EOF */
    Tokenizer *tok = tokenizer_create("foo\nbar");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.pos, 0, "expected pos 0 for foo");
    ASSERT_EQ(t.line, 1, "expected line 1 for foo");
    ASSERT_EQ(t.col, 1, "expected col 1 for foo");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_NEWLINE, "expected NEWLINE");
    ASSERT_EQ(t.pos, 3, "expected pos 3 for newline");
    ASSERT_EQ(t.line, 1, "expected line 1 for newline");

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
    ASSERT_EQ(t.line, 1, "expected error at line 1");
    ASSERT_EQ(t.col, 1, "expected error at col 1");
    ASSERT_STR_EQ(t.value, "unterminated string", "expected exact error message");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_error_unterminated_string_with_newline) {
    /* Newline inside a string triggers unterminated string error at the opening quote */
    Tokenizer *tok = tokenizer_create("\"hello\nworld");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_ERROR, "expected ERROR for unterminated string at newline");
    ASSERT_EQ(t.pos, 0, "expected error at pos 0");
    ASSERT_EQ(t.line, 1, "expected error at line 1");
    ASSERT_EQ(t.col, 1, "expected error at col 1");
    ASSERT_STR_EQ(t.value, "unterminated string", "expected exact error message");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_error_sticky) {
    /* After an error, subsequent calls should return the same error with
     * the same position and message (Option A: preserve original error location) */
    Tokenizer *tok = tokenizer_create("42foo");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_ERROR, "expected ERROR");
    ASSERT_EQ(t.pos, 0, "expected error at pos 0");
    ASSERT_EQ(t.line, 1, "expected error at line 1");
    ASSERT_EQ(t.col, 1, "expected error at col 1");
    ASSERT_STR_EQ(t.value, "number followed by identifier character",
                  "expected exact error message");

    /* Calling again should return the same error with same position */
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_ERROR, "expected ERROR on second call");
    ASSERT_EQ(t.pos, 0, "sticky error should preserve original pos");
    ASSERT_EQ(t.line, 1, "sticky error should preserve original line");
    ASSERT_EQ(t.col, 1, "sticky error should preserve original col");
    ASSERT_STR_EQ(t.value, "number followed by identifier character",
                  "sticky error should preserve original message");

    tokenizer_destroy(tok);
    PASS();
}

/* Number followed by ident-start char is the ONLY adjacency error */
TEST(test_error_number_followed_by_ident) {
    /* 123abc => ERROR at pos 0, line 1, col 1 */
    Tokenizer *tok = tokenizer_create("123abc");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_ERROR, "expected ERROR for '123abc'");
    ASSERT_EQ(t.pos, 0, "expected error at pos 0");
    ASSERT_EQ(t.line, 1, "expected error at line 1");
    ASSERT_EQ(t.col, 1, "expected error at col 1");
    ASSERT_STR_EQ(t.value, "number followed by identifier character",
                  "expected exact error message");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_error_number_followed_by_underscore) {
    /* 123_ => ERROR at pos 0, line 1, col 1 */
    Tokenizer *tok = tokenizer_create("123_");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_ERROR, "expected ERROR for '123_'");
    ASSERT_EQ(t.pos, 0, "expected error at pos 0");
    ASSERT_EQ(t.line, 1, "expected error at line 1");
    ASSERT_EQ(t.col, 1, "expected error at col 1");
    ASSERT_STR_EQ(t.value, "number followed by identifier character",
                  "expected exact error message");

    tokenizer_destroy(tok);
    PASS();
}

/* ============================================================
 * Additional adjacency and edge case tests
 * ============================================================ */

TEST(test_adjacency_op_number_not_soi) {
    /* a+10 => IDENT("a"), OPERATOR("+"), NUMBER("10") */
    Tokenizer *tok = tokenizer_create("a+10");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "a", 1), "expected IDENT 'a'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_OPERATOR, "+", 1), "expected OPERATOR '+'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_NUMBER, "10", 2), "expected NUMBER '10'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_adjacency_multi_op_no_whitespace) {
    /* a>=b => IDENT("a"), OPERATOR(">="), IDENT("b") */
    Tokenizer *tok = tokenizer_create("a>=b");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "a", 1), "expected IDENT 'a'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_OPERATOR, ">=", 2), "expected OPERATOR '>='");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "b", 1), "expected IDENT 'b'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_adjacency_number_op_at_end) {
    /* 10+ => NUMBER("10"), OPERATOR("+") */
    Tokenizer *tok = tokenizer_create("10+");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_NUMBER, "10", 2), "expected NUMBER '10'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_OPERATOR, "+", 1), "expected OPERATOR '+'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_EOF, "expected EOF");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_adjacency_ident_digit_op_ident) {
    /* foo2+bar => IDENT("foo2"), OPERATOR("+"), IDENT("bar") */
    Tokenizer *tok = tokenizer_create("foo2+bar");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "foo2", 4), "expected IDENT 'foo2'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_OPERATOR, "+", 1), "expected OPERATOR '+'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "bar", 3), "expected IDENT 'bar'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_adjacency_three_strings) {
    /* "a""b""c" => STRING("a"), STRING("b"), STRING("c") */
    Tokenizer *tok = tokenizer_create("\"a\"\"b\"\"c\"");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_STRING, "a", 1), "expected STRING 'a'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_STRING, "b", 1), "expected STRING 'b'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_STRING, "c", 1), "expected STRING 'c'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_EOF, "expected EOF");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_non_ascii_byte_is_operator) {
    /* A 0x80 byte is not whitespace, letter, digit, underscore, or quote,
     * so it should be treated as a symbol char => OPERATOR */
    const char input[] = {(char)0x80, '\0'};
    Tokenizer *tok = tokenizer_create(input);
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_OPERATOR, "expected OPERATOR for 0x80 byte");
    ASSERT_EQ(t.value_len, 1, "expected length 1");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_EOF, "expected EOF");

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

TEST(test_token_type_str) {
    ASSERT_STR_EQ(token_type_str(TOK_NUMBER), "NUMBER", "NUMBER string");
    ASSERT_STR_EQ(token_type_str(TOK_STRING), "STRING", "STRING string");
    ASSERT_STR_EQ(token_type_str(TOK_IDENT), "IDENT", "IDENT string");
    ASSERT_STR_EQ(token_type_str(TOK_OPERATOR), "OPERATOR", "OPERATOR string");
    ASSERT_STR_EQ(token_type_str(TOK_EOF), "EOF", "EOF string");
    ASSERT_STR_EQ(token_type_str(TOK_ERROR), "ERROR", "ERROR string");
    PASS();
}

TEST(test_adjacent_tokens_with_space_ok) {
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
 * NEWLINE token tests (Step 2: multi-line input)
 * ============================================================ */

TEST(test_newline_single_lf) {
    /* \n emits TOK_NEWLINE */
    Tokenizer *tok = tokenizer_create("a\nb");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "a", 1), "expected IDENT 'a'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_NEWLINE, "expected NEWLINE after 'a'");
    ASSERT_EQ(t.line, 1, "newline should be on line 1");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "b", 1), "expected IDENT 'b'");
    ASSERT_EQ(t.line, 2, "'b' should be on line 2");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_EOF, "expected EOF");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_newline_single_cr) {
    /* \r emits TOK_NEWLINE (old Mac style) */
    Tokenizer *tok = tokenizer_create("a\rb");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "a", 1), "expected IDENT 'a'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_NEWLINE, "expected NEWLINE after 'a'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "b", 1), "expected IDENT 'b'");
    ASSERT_EQ(t.line, 2, "'b' should be on line 2");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_newline_crlf) {
    /* \r\n emits single TOK_NEWLINE (Windows style) */
    Tokenizer *tok = tokenizer_create("a\r\nb");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "a", 1), "expected IDENT 'a'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_NEWLINE, "expected single NEWLINE for CRLF");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "b", 1), "expected IDENT 'b'");
    ASSERT_EQ(t.line, 2, "'b' should be on line 2");

    /* Should NOT get another newline token - CRLF is a single newline */
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_EOF, "expected EOF, not another NEWLINE");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_newline_multiple) {
    /* Multiple newlines emit multiple TOK_NEWLINE tokens */
    Tokenizer *tok = tokenizer_create("a\n\nb");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "a", 1), "expected IDENT 'a'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_NEWLINE, "expected first NEWLINE");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_NEWLINE, "expected second NEWLINE");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "b", 1), "expected IDENT 'b'");
    ASSERT_EQ(t.line, 3, "'b' should be on line 3");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_newline_leading) {
    /* Leading newlines are emitted */
    Tokenizer *tok = tokenizer_create("\na");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_NEWLINE, "expected leading NEWLINE");
    ASSERT_EQ(t.line, 1, "newline on line 1");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "a", 1), "expected IDENT 'a'");
    ASSERT_EQ(t.line, 2, "'a' should be on line 2");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_newline_trailing) {
    /* Trailing newlines are emitted */
    Tokenizer *tok = tokenizer_create("a\n");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "a", 1), "expected IDENT 'a'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_NEWLINE, "expected trailing NEWLINE");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_EOF, "expected EOF");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_newline_only) {
    /* Input with only newlines */
    Tokenizer *tok = tokenizer_create("\n\n");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_NEWLINE, "expected first NEWLINE");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_NEWLINE, "expected second NEWLINE");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_EOF, "expected EOF");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_newline_with_spaces) {
    /* Spaces around newlines - spaces are skipped, newlines are tokens */
    Tokenizer *tok = tokenizer_create("a  \n  b");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "a", 1), "expected IDENT 'a'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_NEWLINE, "expected NEWLINE");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "b", 1), "expected IDENT 'b'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_newline_type_str) {
    ASSERT_STR_EQ(token_type_str(TOK_NEWLINE), "NEWLINE", "NEWLINE string");
    PASS();
}

/* ============================================================
 * Comment tests (# line comments)
 *
 * Comments start with # and extend to end of line.
 * They are stripped at the tokenizer level (skip_whitespace).
 * The newline itself is NOT consumed — it becomes TOK_NEWLINE.
 * # inside strings is NOT a comment.
 * ============================================================ */

TEST(test_comment_only) {
    /* "# comment" → EOF (comment-only input) */
    Tokenizer *tok = tokenizer_create("# comment");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_EOF, "comment-only input should produce EOF");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_comment_trailing) {
    /* "42 # comment" → NUMBER(42), EOF */
    Tokenizer *tok = tokenizer_create("42 # comment");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_NUMBER, "42", 2), "expected NUMBER '42'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_EOF, "comment should be skipped, then EOF");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_comment_before_newline_and_code) {
    /* "42 # comment\n7" → NUMBER(42), NEWLINE, NUMBER(7) */
    Tokenizer *tok = tokenizer_create("42 # comment\n7");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_NUMBER, "42", 2), "expected NUMBER '42'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_NEWLINE, "expected NEWLINE after comment");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_NUMBER, "7", 1), "expected NUMBER '7'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_EOF, "expected EOF");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_comment_at_start_of_line) {
    /* "# comment\n42" → NEWLINE, NUMBER(42), EOF */
    Tokenizer *tok = tokenizer_create("# comment\n42");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_NEWLINE, "expected NEWLINE after comment");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_NUMBER, "42", 2), "expected NUMBER '42'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_EOF, "expected EOF");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_comment_not_in_string) {
    /* '"hello # world"' → STRING("hello # world") — # is not a comment inside strings */
    Tokenizer *tok = tokenizer_create("\"hello # world\"");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_STRING, "hello # world", 13),
                "expected STRING 'hello # world'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_EOF, "expected EOF");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_comment_multiple_lines) {
    /* "# comment1\n# comment2\n42" → NEWLINE, NEWLINE, NUMBER(42), EOF */
    Tokenizer *tok = tokenizer_create("# comment1\n# comment2\n42");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_NEWLINE, "expected first NEWLINE");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_NEWLINE, "expected second NEWLINE");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_NUMBER, "42", 2), "expected NUMBER '42'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_EOF, "expected EOF");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_comment_interleaved_with_code) {
    /* "42 # comment\n# another\n7" → NUMBER(42), NEWLINE, NEWLINE, NUMBER(7), EOF */
    Tokenizer *tok = tokenizer_create("42 # comment\n# another\n7");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_NUMBER, "42", 2), "expected NUMBER '42'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_NEWLINE, "expected first NEWLINE");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_NEWLINE, "expected second NEWLINE");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_NUMBER, "7", 1), "expected NUMBER '7'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_EOF, "expected EOF");

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

    printf("\nIDENT tokens (Python/Ruby style):\n");
    RUN_TEST(test_ident_single_letter);
    RUN_TEST(test_ident_simple);
    RUN_TEST(test_ident_with_digits);
    RUN_TEST(test_ident_with_underscores);
    RUN_TEST(test_ident_uppercase);
    RUN_TEST(test_ident_mixed_case);
    RUN_TEST(test_ident_multiple);
    RUN_TEST(test_ident_underscore_only);
    RUN_TEST(test_ident_underscore_start);
    RUN_TEST(test_ident_trailing_underscore);
    RUN_TEST(test_ident_dunder_init);
    RUN_TEST(test_ident_a_b2);
    RUN_TEST(test_ident_ab_12);

    printf("\nFormer symbol sandwich (now multi-token):\n");
    RUN_TEST(test_no_sandwich_plus);
    RUN_TEST(test_no_sandwich_hyphen);
    RUN_TEST(test_no_sandwich_multiple_hyphens);
    RUN_TEST(test_no_sandwich_complex);

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
    RUN_TEST(test_operator_no_whitespace_a_plus_b);
    RUN_TEST(test_operator_no_whitespace_x_eq_eq_y);
    RUN_TEST(test_operator_parens);

    printf("\nAdjacency (Python/Ruby style, now valid):\n");
    RUN_TEST(test_adjacency_number_op_number);
    RUN_TEST(test_adjacency_negative_number);
    RUN_TEST(test_adjacency_string_op_ident);
    RUN_TEST(test_adjacency_ident_string);
    RUN_TEST(test_adjacency_string_string);
    RUN_TEST(test_adjacency_string_number);
    RUN_TEST(test_adjacency_number_string);
    RUN_TEST(test_adjacency_string_ident);
    RUN_TEST(test_adjacency_op_number_not_soi);
    RUN_TEST(test_adjacency_multi_op_no_whitespace);
    RUN_TEST(test_adjacency_number_op_at_end);
    RUN_TEST(test_adjacency_ident_digit_op_ident);
    RUN_TEST(test_adjacency_three_strings);
    RUN_TEST(test_non_ascii_byte_is_operator);

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

    printf("\nPosition tracking:\n");
    RUN_TEST(test_position_single_token);
    RUN_TEST(test_position_with_leading_space);
    RUN_TEST(test_position_multiline);
    RUN_TEST(test_position_multiple_tokens_same_line);

    printf("\nError cases:\n");
    RUN_TEST(test_error_unterminated_string);
    RUN_TEST(test_error_unterminated_string_with_newline);
    RUN_TEST(test_error_sticky);
    RUN_TEST(test_error_number_followed_by_ident);
    RUN_TEST(test_error_number_followed_by_underscore);

    printf("\nEOF behavior:\n");
    RUN_TEST(test_eof_sticky);

    printf("\nReset functionality:\n");
    RUN_TEST(test_reset_tokenizer);

    printf("\nEdge cases:\n");
    RUN_TEST(test_token_type_str);
    RUN_TEST(test_adjacent_tokens_with_space_ok);

    printf("\nComparison operator tokenization:\n");
    RUN_TEST(test_tok_eq_eq);
    RUN_TEST(test_tok_not_eq);
    RUN_TEST(test_tok_less_eq);
    RUN_TEST(test_tok_greater_eq);
    RUN_TEST(test_tok_less_than);
    RUN_TEST(test_tok_greater_than);
    RUN_TEST(test_tok_comparison_in_expr);

    printf("\nNull input handling:\n");
    RUN_TEST(test_null_tokenizer_next);
    RUN_TEST(test_null_token_output);

    printf("\nNEWLINE tokens:\n");
    RUN_TEST(test_newline_single_lf);
    RUN_TEST(test_newline_single_cr);
    RUN_TEST(test_newline_crlf);
    RUN_TEST(test_newline_multiple);
    RUN_TEST(test_newline_leading);
    RUN_TEST(test_newline_trailing);
    RUN_TEST(test_newline_only);
    RUN_TEST(test_newline_with_spaces);
    RUN_TEST(test_newline_type_str);

    printf("\nComment tests (# line comments):\n");
    RUN_TEST(test_comment_only);
    RUN_TEST(test_comment_trailing);
    RUN_TEST(test_comment_before_newline_and_code);
    RUN_TEST(test_comment_at_start_of_line);
    RUN_TEST(test_comment_not_in_string);
    RUN_TEST(test_comment_multiple_lines);
    RUN_TEST(test_comment_interleaved_with_code);

    printf("\n=== Summary ===\n");
    printf("Tests run:    %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
