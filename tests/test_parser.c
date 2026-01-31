/*
 * test_parser.c - Tests for the Cutlet parser
 *
 * Tests both single-token parsing (backward compat) and full expression
 * parsing with Pratt precedence climbing.
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
    ASSERT(parser_parse("42", &node, &err), "should parse number");
    ASSERT(node->type == AST_NUMBER, "type should be NUMBER");
    ASSERT_STR_EQ(node->value, "42", "value should be 42");
    ast_free(node);
    PASS();
}

TEST(test_single_string) {
    AstNode *node = NULL;
    ParseError err;
    ASSERT(parser_parse("\"hello\"", &node, &err), "should parse string");
    ASSERT(node->type == AST_STRING, "type should be STRING");
    ASSERT_STR_EQ(node->value, "hello", "value should be hello");
    ast_free(node);
    PASS();
}

TEST(test_single_ident) {
    AstNode *node = NULL;
    ParseError err;
    ASSERT(parser_parse("foo", &node, &err), "should parse ident");
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
    ASSERT(!parser_parse("+", &node, &err), "operator should fail");
    /* With Pratt parser, '+' as a leading token is an unexpected token error */
    ASSERT(err.line == 1, "error line");
    ASSERT(err.col == 1, "error col");
    PASS();
}

TEST(test_empty_input) {
    AstNode *node = NULL;
    ParseError err;
    ASSERT(!parser_parse("", &node, &err), "empty should fail");
    ASSERT_STR_EQ(err.message, "expected expression, got EOF", "error message");
    ASSERT(err.line == 1, "error line");
    ASSERT(err.col == 1, "error col");
    PASS();
}

TEST(test_whitespace_only) {
    AstNode *node = NULL;
    ParseError err;
    ASSERT(!parser_parse("   ", &node, &err), "whitespace should fail");
    ASSERT_STR_EQ(err.message, "expected expression, got EOF", "error message");
    PASS();
}

TEST(test_extra_tokens) {
    AstNode *node = NULL;
    ParseError err;
    ASSERT(!parser_parse("foo bar", &node, &err), "extra tokens should fail");
    /* After parsing 'foo', 'bar' is unexpected (no operator between) */
    ASSERT(err.line == 1, "error line");
    PASS();
}

TEST(test_tokenizer_error_passthrough) {
    AstNode *node = NULL;
    ParseError err;
    ASSERT(!parser_parse("42foo", &node, &err), "tokenizer error should fail");
    ASSERT_STR_EQ(err.message, "number followed by identifier character", "error message");
    ASSERT(err.line == 1, "error line");
    ASSERT(err.col == 1, "error col");
    PASS();
}

TEST(test_unterminated_string) {
    AstNode *node = NULL;
    ParseError err;
    ASSERT(!parser_parse("\"hello", &node, &err), "unterminated string should fail");
    ASSERT_STR_EQ(err.message, "unterminated string", "error message");
    ASSERT(err.line == 1, "error line");
    ASSERT(err.col == 1, "error col");
    PASS();
}

TEST(test_null_input) {
    AstNode *node = NULL;
    ParseError err;
    ASSERT(!parser_parse(NULL, &node, &err), "NULL should fail");
    ASSERT_STR_EQ(err.message, "expected expression, got EOF", "error message");
    PASS();
}

/* ============================================================
 * Defensive NULL-parameter tests
 * ============================================================ */

TEST(test_null_out_with_err) {
    /* Passing NULL for out should return false and populate err. */
    ParseError err;
    ASSERT(!parser_parse("42", NULL, &err), "NULL out should fail");
    ASSERT_STR_EQ(err.message, "parser output pointer is NULL", "error message for NULL out");
    PASS();
}

TEST(test_null_err_param) {
    /* Passing NULL for err should return false without crashing. */
    AstNode *node = NULL;
    ASSERT(!parser_parse("+", &node, NULL), "NULL err should not crash");
    ASSERT(node == NULL, "*out should be NULL after failure");
    PASS();
}

TEST(test_both_null) {
    /* Both out and err NULL — should return false without crashing. */
    ASSERT(!parser_parse("42", NULL, NULL), "both NULL should not crash");
    PASS();
}

TEST(test_out_null_after_failure) {
    /* *out must be NULL after any parse failure. */
    AstNode *node = (AstNode *)0xDEAD; /* sentinel */
    ParseError err;
    ASSERT(!parser_parse("+", &node, &err), "operator should fail");
    ASSERT(node == NULL, "*out should be NULL after failure");
    PASS();
}

/* ============================================================
 * ast_format tests
 * ============================================================ */

TEST(test_ast_format_number) {
    AstNode *node = NULL;
    ParseError err;
    ASSERT(parser_parse("42", &node, &err), "parse");
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
    ASSERT(parser_parse("\"hi\"", &node, &err), "parse");
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
    ASSERT(parser_parse("foo", &node, &err), "parse");
    char *s = ast_format(node);
    ASSERT(s != NULL, "format not null");
    ASSERT_STR_EQ(s, "AST [IDENT foo]", "format output");
    free(s);
    ast_free(node);
    PASS();
}

/* ============================================================
 * Expression parsing tests (Pratt parser)
 * ============================================================ */

/*
 * Helper: parse input, format AST, compare to expected string.
 */
static int ast_matches(const char *input, const char *expected) {
    AstNode *node = NULL;
    ParseError err;
    if (!parser_parse(input, &node, &err)) {
        printf("\n    Parse failed for '%s': %s\n", input, err.message);
        return 0;
    }
    char *s = ast_format(node);
    ast_free(node);
    if (!s) {
        printf("\n    ast_format returned NULL for '%s'\n", input);
        return 0;
    }
    int match = strcmp(s, expected) == 0;
    if (!match) {
        printf("\n    Input:    '%s'\n    Expected: %s\n    Got:      %s\n", input, expected, s);
    }
    free(s);
    return match;
}

TEST(test_expr_add) {
    ASSERT(ast_matches("1 + 2", "AST [BINOP + [NUMBER 1] [NUMBER 2]]"), "1+2");
    PASS();
}

TEST(test_expr_precedence_add_mul) {
    ASSERT(ast_matches("1 + 2 * 3", "AST [BINOP + [NUMBER 1] [BINOP * [NUMBER 2] [NUMBER 3]]]"),
           "add/mul precedence");
    PASS();
}

TEST(test_expr_parens) {
    ASSERT(ast_matches("(1 + 2) * 3", "AST [BINOP * [BINOP + [NUMBER 1] [NUMBER 2]] [NUMBER 3]]"),
           "parens");
    PASS();
}

TEST(test_expr_right_assoc_exp) {
    ASSERT(ast_matches("2 ** 3 ** 2", "AST [BINOP ** [NUMBER 2] [BINOP ** [NUMBER 3] [NUMBER 2]]]"),
           "right-assoc **");
    PASS();
}

TEST(test_expr_unary_minus) {
    ASSERT(ast_matches("-3", "AST [UNARY - [NUMBER 3]]"), "unary minus");
    PASS();
}

TEST(test_expr_unary_vs_exp) {
    /* -2 ** 2 parses as -(2 ** 2), not (-2) ** 2 */
    ASSERT(ast_matches("-2 ** 2", "AST [UNARY - [BINOP ** [NUMBER 2] [NUMBER 2]]]"),
           "unary vs exp");
    PASS();
}

TEST(test_expr_nested_unary) {
    ASSERT(ast_matches("--3", "AST [UNARY - [UNARY - [NUMBER 3]]]"), "nested unary");
    PASS();
}

TEST(test_expr_sub) {
    ASSERT(ast_matches("10 - 3", "AST [BINOP - [NUMBER 10] [NUMBER 3]]"), "subtraction");
    PASS();
}

TEST(test_expr_div) {
    ASSERT(ast_matches("6 / 3", "AST [BINOP / [NUMBER 6] [NUMBER 3]]"), "division");
    PASS();
}

TEST(test_expr_complex) {
    /* 2 + 3 * 4 - 1 → (2 + (3 * 4)) - 1 */
    ASSERT(ast_matches("2 + 3 * 4 - 1",
                       "AST [BINOP - [BINOP + [NUMBER 2] [BINOP * [NUMBER 3] [NUMBER 4]]] "
                       "[NUMBER 1]]"),
           "complex expr");
    PASS();
}

TEST(test_expr_nested_parens) {
    ASSERT(ast_matches("((42))", "AST [NUMBER 42]"), "nested parens");
    PASS();
}

TEST(test_expr_unary_in_parens) {
    ASSERT(ast_matches("-(-3)", "AST [UNARY - [UNARY - [NUMBER 3]]]"), "unary in parens");
    PASS();
}

TEST(test_expr_string_leaf) {
    ASSERT(ast_matches("\"hello\"", "AST [STRING hello]"), "string leaf");
    PASS();
}

/* ============================================================
 * Expression error tests
 * ============================================================ */

TEST(test_expr_mismatched_paren) {
    AstNode *node = NULL;
    ParseError err;
    ASSERT(!parser_parse("(1 + 2", &node, &err), "mismatched paren should fail");
    ASSERT(node == NULL, "node should be NULL");
    PASS();
}

TEST(test_expr_trailing_op) {
    AstNode *node = NULL;
    ParseError err;
    ASSERT(!parser_parse("1 +", &node, &err), "trailing op should fail");
    ASSERT(node == NULL, "node should be NULL");
    PASS();
}

TEST(test_expr_missing_operand) {
    AstNode *node = NULL;
    ParseError err;
    ASSERT(!parser_parse("* 2", &node, &err), "leading * should fail");
    ASSERT(node == NULL, "node should be NULL");
    PASS();
}

TEST(test_expr_empty_parens) {
    AstNode *node = NULL;
    ParseError err;
    ASSERT(!parser_parse("()", &node, &err), "empty parens should fail");
    ASSERT(node == NULL, "node should be NULL");
    PASS();
}

TEST(test_expr_extra_close_paren) {
    AstNode *node = NULL;
    ParseError err;
    ASSERT(!parser_parse("1 + 2)", &node, &err), "extra close paren should fail");
    ASSERT(node == NULL, "node should be NULL");
    PASS();
}

TEST(test_expr_adjacent_numbers) {
    AstNode *node = NULL;
    ParseError err;
    ASSERT(!parser_parse("1 2", &node, &err), "adjacent numbers should fail");
    ASSERT(node == NULL, "node should be NULL");
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

    printf("\nDefensive NULL-parameter tests:\n");
    RUN_TEST(test_null_out_with_err);
    RUN_TEST(test_null_err_param);
    RUN_TEST(test_both_null);
    RUN_TEST(test_out_null_after_failure);

    printf("\nast_format:\n");
    RUN_TEST(test_ast_format_number);
    RUN_TEST(test_ast_format_string);
    RUN_TEST(test_ast_format_ident);

    printf("\nExpression parsing:\n");
    RUN_TEST(test_expr_add);
    RUN_TEST(test_expr_precedence_add_mul);
    RUN_TEST(test_expr_parens);
    RUN_TEST(test_expr_right_assoc_exp);
    RUN_TEST(test_expr_unary_minus);
    RUN_TEST(test_expr_unary_vs_exp);
    RUN_TEST(test_expr_nested_unary);
    RUN_TEST(test_expr_sub);
    RUN_TEST(test_expr_div);
    RUN_TEST(test_expr_complex);
    RUN_TEST(test_expr_nested_parens);
    RUN_TEST(test_expr_unary_in_parens);
    RUN_TEST(test_expr_string_leaf);

    printf("\nExpression error cases:\n");
    RUN_TEST(test_expr_mismatched_paren);
    RUN_TEST(test_expr_trailing_op);
    RUN_TEST(test_expr_missing_operand);
    RUN_TEST(test_expr_empty_parens);
    RUN_TEST(test_expr_extra_close_paren);
    RUN_TEST(test_expr_adjacent_numbers);

    printf("\nSafety:\n");
    RUN_TEST(test_ast_free_null);

    printf("\n========================================\n");
    printf("Tests run: %d\n", tests_run);
    printf("Passed:    %d\n", tests_passed);
    printf("Failed:    %d\n", tests_failed);
    printf("========================================\n");

    return tests_failed > 0 ? 1 : 0;
}
