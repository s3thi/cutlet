/*
 * test_tokenizer.c - Tests for the Cutlet tokenizer
 *
 * Test coverage for the minimal v0 tokenizer:
 * - NUMBER tokens (integers: positive, negative, zero)
 * - STRING tokens (double-quoted, no escapes)
 * - IDENT tokens (identifiers)
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
#define ASSERT_NE(a, b, msg) ASSERT((a) != (b), msg)
#define ASSERT_STR_EQ(a, b, msg) ASSERT(strcmp((a), (b)) == 0, msg)
#define ASSERT_STRN_EQ(a, b, len, msg) ASSERT(strncmp((a), (b), (len)) == 0, msg)
#define ASSERT_TRUE(cond, msg) ASSERT(cond, msg)
#define ASSERT_FALSE(cond, msg) ASSERT(!(cond), msg)
#define ASSERT_NULL(ptr, msg) ASSERT((ptr) == NULL, msg)
#define ASSERT_NOT_NULL(ptr, msg) ASSERT((ptr) != NULL, msg)

#define PASS() do { \
    printf("PASS\n"); \
    tests_passed++; \
} while(0)

/* Helper to check a token matches expected values */
static int token_matches(Token *tok, TokenType type, const char *value, size_t value_len) {
    if (tok->type != type) return 0;
    if (tok->value_len != value_len) return 0;
    if (value_len > 0 && strncmp(tok->value, value, value_len) != 0) return 0;
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

TEST(test_number_negative) {
    Tokenizer *tok = tokenizer_create("-7");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_NUMBER, "-7", 2), "expected NUMBER '-7'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_number_negative_multi_digit) {
    Tokenizer *tok = tokenizer_create("-42");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_NUMBER, "-42", 3), "expected NUMBER '-42'");

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

TEST(test_ident_with_underscore_prefix) {
    Tokenizer *tok = tokenizer_create("_x1");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "_x1", 3), "expected IDENT '_x1'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_ident_underscore_only) {
    Tokenizer *tok = tokenizer_create("_");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "_", 1), "expected IDENT '_'");

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

TEST(test_mixed_no_spaces_string_ident_error) {
    /* Adjacent tokens without whitespace are errors per PLAN.md */
    Tokenizer *tok = tokenizer_create("\"a\"foo");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_STRING, "a", 1), "expected STRING 'a'");

    /* Next token should be ERROR - no whitespace between string and ident */
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_ERROR, "expected ERROR for adjacent tokens without whitespace");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_mixed_complex) {
    Tokenizer *tok = tokenizer_create("x 1 \"a\" y -2 \"b\" z");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "x", 1), "expected IDENT 'x'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_NUMBER, "1", 1), "expected NUMBER '1'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_STRING, "a", 1), "expected STRING 'a'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "y", 1), "expected IDENT 'y'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_NUMBER, "-2", 2), "expected NUMBER '-2'");

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

TEST(test_error_invalid_char) {
    Tokenizer *tok = tokenizer_create("@");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_ERROR, "expected ERROR for '@'");
    ASSERT_EQ(t.pos, 0, "expected error at pos 0");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_error_invalid_char_after_valid_tokens) {
    Tokenizer *tok = tokenizer_create("foo @");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "foo", 3), "expected IDENT 'foo'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_ERROR, "expected ERROR for '@'");
    ASSERT_EQ(t.pos, 4, "expected error at pos 4");

    tokenizer_destroy(tok);
    PASS();
}

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
    /* Strings should not span lines without escapes, so this is an error */
    ASSERT_EQ(t.type, TOK_ERROR, "expected ERROR for unterminated string at newline");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_error_various_invalid_chars) {
    const char *invalid_chars[] = {"!", "#", "$", "%", "&", "*", "+", ",", ".",
                                   "/", ":", ";", "<", "=", ">", "?", "[", "\\",
                                   "]", "^", "`", "{", "|", "}", "~", NULL};

    for (int i = 0; invalid_chars[i] != NULL; i++) {
        Tokenizer *tok = tokenizer_create(invalid_chars[i]);
        ASSERT_NOT_NULL(tok, "tokenizer_create failed");

        Token t;
        ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
        if (t.type != TOK_ERROR) {
            printf("FAIL\n");
            printf("    Expected ERROR for '%s' but got %s\n",
                   invalid_chars[i], token_type_str(t.type));
            tests_failed++;
            tokenizer_destroy(tok);
            return;
        }

        tokenizer_destroy(tok);
    }

    PASS();
}

TEST(test_error_sticky) {
    /* After an error, subsequent calls should also return error */
    Tokenizer *tok = tokenizer_create("@");
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

/* ============================================================
 * EOF behavior tests
 * ============================================================ */

TEST(test_eof_sticky) {
    /* After EOF, subsequent calls should also return EOF */
    Tokenizer *tok = tokenizer_create("x");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_IDENT, "expected IDENT");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_EOF, "expected EOF");

    /* Calling again should still return EOF */
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
 * Kebab-case identifier tests
 * Per PLAN.md: "-" is allowed in the middle of identifiers (not at start)
 * ============================================================ */

TEST(test_ident_kebab_case_simple) {
    Tokenizer *tok = tokenizer_create("kebab-case");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "kebab-case", 10), "expected IDENT 'kebab-case'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_EOF, "expected EOF");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_ident_kebab_case_multiple_hyphens) {
    Tokenizer *tok = tokenizer_create("my-var-name");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "my-var-name", 11), "expected IDENT 'my-var-name'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_ident_kebab_case_single_chars) {
    Tokenizer *tok = tokenizer_create("a-b-c");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "a-b-c", 5), "expected IDENT 'a-b-c'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_ident_kebab_case_with_digits) {
    Tokenizer *tok = tokenizer_create("var1-name2");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "var1-name2", 10), "expected IDENT 'var1-name2'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_ident_kebab_case_with_underscore) {
    Tokenizer *tok = tokenizer_create("my_var-name");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_TRUE(token_matches(&t, TOK_IDENT, "my_var-name", 11), "expected IDENT 'my_var-name'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_ident_trailing_hyphen_error) {
    /* Trailing hyphen should be an error - hyphen must be followed by continue char */
    Tokenizer *tok = tokenizer_create("foo-");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    /* Could be: IDENT "foo" then ERROR for lone "-", or ERROR for "foo-" */
    /* Per plan, this is likely an error since hyphen needs a following char */
    ASSERT_EQ(t.type, TOK_ERROR, "expected ERROR for trailing hyphen");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_ident_multiple_kebab) {
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
 * Unicode identifier tests
 * Per PLAN.md: Unicode letter/mark/connector allowed in identifiers
 * ============================================================ */

TEST(test_ident_unicode_latin_extended) {
    /* Latin letters with diacritics */
    Tokenizer *tok = tokenizer_create("café");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_IDENT, "expected IDENT for 'café'");
    ASSERT_STRN_EQ(t.value, "café", t.value_len, "expected value 'café'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_ident_unicode_naive) {
    Tokenizer *tok = tokenizer_create("naïve");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_IDENT, "expected IDENT for 'naïve'");
    ASSERT_STRN_EQ(t.value, "naïve", t.value_len, "expected value 'naïve'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_ident_unicode_devanagari) {
    /* Devanagari script (Hindi) */
    Tokenizer *tok = tokenizer_create("नमस्ते");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_IDENT, "expected IDENT for Devanagari 'नमस्ते'");
    ASSERT_STRN_EQ(t.value, "नमस्ते", t.value_len, "expected Devanagari value");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_ident_unicode_devanagari_with_ascii) {
    /* Mixed Devanagari and ASCII */
    Tokenizer *tok = tokenizer_create("var_नाम");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_IDENT, "expected IDENT for mixed 'var_नाम'");
    ASSERT_STRN_EQ(t.value, "var_नाम", t.value_len, "expected mixed value");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_ident_unicode_gurmukhi) {
    /* Gurmukhi script (Punjabi) */
    Tokenizer *tok = tokenizer_create("ਸਤਿਨਾਮ");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_IDENT, "expected IDENT for Gurmukhi 'ਸਤਿਨਾਮ'");
    ASSERT_STRN_EQ(t.value, "ਸਤਿਨਾਮ", t.value_len, "expected Gurmukhi value");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_ident_unicode_gurmukhi_with_digits) {
    /* Gurmukhi with ASCII digits */
    Tokenizer *tok = tokenizer_create("ਨੰਬਰ123");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_IDENT, "expected IDENT for Gurmukhi with digits");
    ASSERT_STRN_EQ(t.value, "ਨੰਬਰ123", t.value_len, "expected Gurmukhi with digits value");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_ident_unicode_cyrillic) {
    /* Cyrillic script (Russian) */
    Tokenizer *tok = tokenizer_create("привет");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_IDENT, "expected IDENT for Cyrillic 'привет'");
    ASSERT_STRN_EQ(t.value, "привет", t.value_len, "expected Cyrillic value");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_ident_unicode_greek) {
    /* Greek letters (commonly used in math/science) */
    Tokenizer *tok = tokenizer_create("αβγ");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_IDENT, "expected IDENT for Greek 'αβγ'");
    ASSERT_STRN_EQ(t.value, "αβγ", t.value_len, "expected Greek value");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_ident_unicode_multiple) {
    /* Multiple Unicode identifiers */
    Tokenizer *tok = tokenizer_create("café naïve");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_IDENT, "expected IDENT for 'café'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_IDENT, "expected IDENT for 'naïve'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_ident_unicode_kebab_case) {
    /* Unicode identifier with kebab-case */
    Tokenizer *tok = tokenizer_create("café-au-lait");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_IDENT, "expected IDENT for 'café-au-lait'");
    ASSERT_STRN_EQ(t.value, "café-au-lait", t.value_len, "expected 'café-au-lait'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_ident_unicode_devanagari_kebab) {
    /* Devanagari with kebab-case */
    Tokenizer *tok = tokenizer_create("नमस्ते-दुनिया");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_IDENT, "expected IDENT for Devanagari kebab-case");
    ASSERT_STRN_EQ(t.value, "नमस्ते-दुनिया", t.value_len, "expected Devanagari kebab value");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_ident_unicode_gurmukhi_kebab) {
    /* Gurmukhi with kebab-case */
    Tokenizer *tok = tokenizer_create("ਸਤਿ-ਨਾਮ");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_IDENT, "expected IDENT for Gurmukhi kebab-case");
    ASSERT_STRN_EQ(t.value, "ਸਤਿ-ਨਾਮ", t.value_len, "expected Gurmukhi kebab value");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_ident_underscore_start_unicode) {
    /* Underscore prefix with Unicode */
    Tokenizer *tok = tokenizer_create("_नाम");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_IDENT, "expected IDENT for '_नाम'");
    ASSERT_STRN_EQ(t.value, "_नाम", t.value_len, "expected '_नाम'");

    tokenizer_destroy(tok);
    PASS();
}

/* ============================================================
 * Adjacent token error tests
 * Per PLAN.md: Tokens must be separated by whitespace or EOF
 * ============================================================ */

TEST(test_error_ident_adjacent_string) {
    /* foo"bar" - ident immediately followed by string */
    Tokenizer *tok = tokenizer_create("foo\"bar\"");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    /* Should error - ident followed by quote without whitespace */
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
    /* Should error - number followed by quote without whitespace */
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
    ASSERT_TRUE(token_matches(&t, TOK_STRING, "a", 1), "expected STRING 'a'");

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
    ASSERT_TRUE(token_matches(&t, TOK_STRING, "a", 1), "expected STRING 'a'");

    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_ERROR, "expected ERROR for adjacent strings");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_error_ident_adjacent_number) {
    /* foo42 with no space - but this might be valid as single ident */
    /* Actually "foo42" is a valid identifier (letters then digits) */
    /* The error case is 42foo (number then letters) */
    Tokenizer *tok = tokenizer_create("foo42");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    /* foo42 is a valid identifier - digits allowed after letters */
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
 * Edge cases and boundary tests
 * ============================================================ */

TEST(test_lone_minus) {
    /* A lone minus should be an error (not a number) */
    Tokenizer *tok = tokenizer_create("-");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    ASSERT_EQ(t.type, TOK_ERROR, "expected ERROR for lone '-'");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_minus_followed_by_ident) {
    /* -foo should be error then ident, or just error, not a negative ident */
    Tokenizer *tok = tokenizer_create("-foo");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    /* This should be an error - minus must be followed by digit for number */
    ASSERT_EQ(t.type, TOK_ERROR, "expected ERROR for '-' not followed by digit");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_number_followed_by_ident_no_space_error) {
    /* 42foo is an error per PLAN.md - tokens must be separated by whitespace */
    Tokenizer *tok = tokenizer_create("42foo");
    ASSERT_NOT_NULL(tok, "tokenizer_create failed");

    Token t;
    ASSERT_TRUE(tokenizer_next(tok, &t), "tokenizer_next failed");
    /* Should get ERROR immediately - number followed by letter without whitespace */
    ASSERT_EQ(t.type, TOK_ERROR, "expected ERROR for '42foo' - no whitespace between tokens");

    tokenizer_destroy(tok);
    PASS();
}

TEST(test_token_type_str) {
    ASSERT_STR_EQ(token_type_str(TOK_NUMBER), "NUMBER", "NUMBER string");
    ASSERT_STR_EQ(token_type_str(TOK_STRING), "STRING", "STRING string");
    ASSERT_STR_EQ(token_type_str(TOK_IDENT), "IDENT", "IDENT string");
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
    RUN_TEST(test_number_negative);
    RUN_TEST(test_number_negative_multi_digit);
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
    RUN_TEST(test_ident_with_underscore_prefix);
    RUN_TEST(test_ident_underscore_only);
    RUN_TEST(test_ident_with_digits);
    RUN_TEST(test_ident_with_underscores);
    RUN_TEST(test_ident_uppercase);
    RUN_TEST(test_ident_mixed_case);
    RUN_TEST(test_ident_multiple);

    printf("\nWhitespace handling:\n");
    RUN_TEST(test_whitespace_spaces);
    RUN_TEST(test_whitespace_tabs);
    RUN_TEST(test_whitespace_newlines);
    RUN_TEST(test_whitespace_mixed);
    RUN_TEST(test_whitespace_only);
    RUN_TEST(test_empty_input);

    printf("\nMixed token sequences:\n");
    RUN_TEST(test_mixed_number_string_ident);
    RUN_TEST(test_mixed_complex);

    printf("\nPosition tracking:\n");
    RUN_TEST(test_position_single_token);
    RUN_TEST(test_position_with_leading_space);
    RUN_TEST(test_position_multiline);
    RUN_TEST(test_position_multiple_tokens_same_line);

    printf("\nError cases:\n");
    RUN_TEST(test_error_invalid_char);
    RUN_TEST(test_error_invalid_char_after_valid_tokens);
    RUN_TEST(test_error_unterminated_string);
    RUN_TEST(test_error_unterminated_string_with_newline);
    RUN_TEST(test_error_various_invalid_chars);
    RUN_TEST(test_error_sticky);

    printf("\nEOF behavior:\n");
    RUN_TEST(test_eof_sticky);

    printf("\nReset functionality:\n");
    RUN_TEST(test_reset_tokenizer);

    printf("\nKebab-case identifiers:\n");
    RUN_TEST(test_ident_kebab_case_simple);
    RUN_TEST(test_ident_kebab_case_multiple_hyphens);
    RUN_TEST(test_ident_kebab_case_single_chars);
    RUN_TEST(test_ident_kebab_case_with_digits);
    RUN_TEST(test_ident_kebab_case_with_underscore);
    RUN_TEST(test_ident_trailing_hyphen_error);
    RUN_TEST(test_ident_multiple_kebab);

    printf("\nUnicode identifiers:\n");
    RUN_TEST(test_ident_unicode_latin_extended);
    RUN_TEST(test_ident_unicode_naive);
    RUN_TEST(test_ident_unicode_devanagari);
    RUN_TEST(test_ident_unicode_devanagari_with_ascii);
    RUN_TEST(test_ident_unicode_gurmukhi);
    RUN_TEST(test_ident_unicode_gurmukhi_with_digits);
    RUN_TEST(test_ident_unicode_cyrillic);
    RUN_TEST(test_ident_unicode_greek);
    RUN_TEST(test_ident_unicode_multiple);
    RUN_TEST(test_ident_unicode_kebab_case);
    RUN_TEST(test_ident_unicode_devanagari_kebab);
    RUN_TEST(test_ident_unicode_gurmukhi_kebab);
    RUN_TEST(test_ident_underscore_start_unicode);

    printf("\nAdjacent token errors:\n");
    RUN_TEST(test_mixed_no_spaces_string_ident_error);
    RUN_TEST(test_error_ident_adjacent_string);
    RUN_TEST(test_error_number_adjacent_string);
    RUN_TEST(test_error_string_adjacent_number);
    RUN_TEST(test_error_string_adjacent_string);
    RUN_TEST(test_error_ident_adjacent_number);
    RUN_TEST(test_adjacent_tokens_with_space_ok);

    printf("\nEdge cases:\n");
    RUN_TEST(test_lone_minus);
    RUN_TEST(test_minus_followed_by_ident);
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
