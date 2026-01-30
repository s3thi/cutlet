/*
 * test_parser.c - Tests for the Cutlet parser
 */

#include "../src/parser.h"
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

#define ASSERT_STR_EQ(a, b, msg) ASSERT(strcmp((a), (b)) == 0, msg)

#define PASS()                                                                                     \
    do {                                                                                           \
        printf("PASS\n");                                                                          \
        tests_passed++;                                                                            \
    } while (0)

/* ============================================================
 * Parser success tests
 * ============================================================ */

TEST(test_single_number) {
    AstNode *node = NULL;
    ParseError err;
    ASSERT(parser_parse_single("42", &node, &err), "should parse number");
    ASSERT(node->type == AST_NUMBER, "type should be NUMBER");
    ASSERT_STR_EQ(node->value, "42", "value should be 42");
    ast_free(node);
    PASS();
}

TEST(test_single_string) {
    AstNode *node = NULL;
    ParseError err;
    ASSERT(parser_parse_single("\"hello\"", &node, &err), "should parse string");
    ASSERT(node->type == AST_STRING, "type should be STRING");
    ASSERT_STR_EQ(node->value, "hello", "value should be hello");
    ast_free(node);
    PASS();
}

TEST(test_single_ident) {
    AstNode *node = NULL;
    ParseError err;
    ASSERT(parser_parse_single("foo", &node, &err), "should parse ident");
    ASSERT(node->type == AST_IDENT, "type should be IDENT");
    ASSERT_STR_EQ(node->value, "foo", "value should be foo");
    ast_free(node);
    PASS();
}

/* ============================================================
 * Parser error tests
 * ============================================================ */

TEST(test_operator_error) {
    AstNode *node = NULL;
    ParseError err;
    ASSERT(!parser_parse_single("+", &node, &err), "operator should fail");
    ASSERT_STR_EQ(err.message, "unexpected operator '+'", "error message");
    ASSERT(err.line == 1, "error line");
    ASSERT(err.col == 1, "error col");
    PASS();
}

TEST(test_empty_input) {
    AstNode *node = NULL;
    ParseError err;
    ASSERT(!parser_parse_single("", &node, &err), "empty should fail");
    ASSERT_STR_EQ(err.message, "expected expression, got EOF", "error message");
    ASSERT(err.line == 1, "error line");
    ASSERT(err.col == 1, "error col");
    PASS();
}

TEST(test_whitespace_only) {
    AstNode *node = NULL;
    ParseError err;
    ASSERT(!parser_parse_single("   ", &node, &err), "whitespace should fail");
    ASSERT_STR_EQ(err.message, "expected expression, got EOF", "error message");
    PASS();
}

TEST(test_extra_tokens) {
    AstNode *node = NULL;
    ParseError err;
    ASSERT(!parser_parse_single("foo bar", &node, &err), "extra tokens should fail");
    ASSERT_STR_EQ(err.message, "unexpected extra token", "error message");
    ASSERT(err.line == 1, "error line");
    ASSERT(err.col == 5, "error col should be 5");
    PASS();
}

TEST(test_tokenizer_error_passthrough) {
    AstNode *node = NULL;
    ParseError err;
    ASSERT(!parser_parse_single("42foo", &node, &err), "tokenizer error should fail");
    ASSERT_STR_EQ(err.message, "number followed by identifier character", "error message");
    ASSERT(err.line == 1, "error line");
    ASSERT(err.col == 1, "error col");
    PASS();
}

TEST(test_unterminated_string) {
    AstNode *node = NULL;
    ParseError err;
    ASSERT(!parser_parse_single("\"hello", &node, &err), "unterminated string should fail");
    ASSERT_STR_EQ(err.message, "unterminated string", "error message");
    ASSERT(err.line == 1, "error line");
    ASSERT(err.col == 1, "error col");
    PASS();
}

TEST(test_null_input) {
    AstNode *node = NULL;
    ParseError err;
    ASSERT(!parser_parse_single(NULL, &node, &err), "NULL should fail");
    ASSERT_STR_EQ(err.message, "expected expression, got EOF", "error message");
    PASS();
}

/* ============================================================
 * ast_format tests
 * ============================================================ */

TEST(test_ast_format_number) {
    AstNode *node = NULL;
    ParseError err;
    ASSERT(parser_parse_single("42", &node, &err), "parse");
    char *s = ast_format(node);
    ASSERT(s != NULL, "format not null");
    ASSERT_STR_EQ(s, "AST [NUMBER 42]", "format output");
    free(s);
    ast_free(node);
    PASS();
}

TEST(test_ast_format_string) {
    AstNode *node = NULL;
    ParseError err;
    ASSERT(parser_parse_single("\"hi\"", &node, &err), "parse");
    char *s = ast_format(node);
    ASSERT(s != NULL, "format not null");
    ASSERT_STR_EQ(s, "AST [STRING hi]", "format output");
    free(s);
    ast_free(node);
    PASS();
}

TEST(test_ast_format_ident) {
    AstNode *node = NULL;
    ParseError err;
    ASSERT(parser_parse_single("foo", &node, &err), "parse");
    char *s = ast_format(node);
    ASSERT(s != NULL, "format not null");
    ASSERT_STR_EQ(s, "AST [IDENT foo]", "format output");
    free(s);
    ast_free(node);
    PASS();
}

/* ============================================================
 * ast_free(NULL) safety test
 * ============================================================ */

TEST(test_ast_free_null) {
    ast_free(NULL); /* should not crash */
    PASS();
}

/* ============================================================
 * Main
 * ============================================================ */

int main(void) {
    printf("Running parser tests...\n\n");

    printf("Success cases:\n");
    RUN_TEST(test_single_number);
    RUN_TEST(test_single_string);
    RUN_TEST(test_single_ident);

    printf("\nError cases:\n");
    RUN_TEST(test_operator_error);
    RUN_TEST(test_empty_input);
    RUN_TEST(test_whitespace_only);
    RUN_TEST(test_extra_tokens);
    RUN_TEST(test_tokenizer_error_passthrough);
    RUN_TEST(test_unterminated_string);
    RUN_TEST(test_null_input);

    printf("\nast_format:\n");
    RUN_TEST(test_ast_format_number);
    RUN_TEST(test_ast_format_string);
    RUN_TEST(test_ast_format_ident);

    printf("\nSafety:\n");
    RUN_TEST(test_ast_free_null);

    printf("\n========================================\n");
    printf("Tests run: %d\n", tests_run);
    printf("Passed:    %d\n", tests_passed);
    printf("Failed:    %d\n", tests_failed);
    printf("========================================\n");

    return tests_failed > 0 ? 1 : 0;
}
