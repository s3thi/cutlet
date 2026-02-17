/*
 * test_repl.c - Tests for the Cutlet REPL core
 *
 * All tests use the primary repl_eval_line() API, which returns a
 * structured ReplResult with ok, value, error, tokens, and ast fields.
 *
 * Uses the same simple test harness as test_tokenizer.c.
 */

#include "../src/repl.h"
#include "../src/runtime.h"
#include "../src/value.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* No-op EvalContext for tests that don't need say() output. */
static EvalContext noop_ctx = {.write_fn = NULL, .userdata = NULL};

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

/* ============================================================
 * Helpers that use repl_eval_line() directly
 * ============================================================ */

/* Check that input evaluates successfully to expected_value.
 * Pass NULL for expected_value to check blank/whitespace input. */
static int eval_value_matches(const char *input, const char *expected_value) {
    ReplResult r = repl_eval_line(input, false, false, false, &noop_ctx);
    int match;
    if (expected_value == NULL) {
        match = r.ok && r.value == NULL;
    } else {
        match = r.ok && r.value && strcmp(r.value, expected_value) == 0;
    }
    if (!match) {
        printf("\n    Expected: ok=true value=%s\n    Got:      ok=%d value=%s error=%s\n",
               expected_value ? expected_value : "(null)", r.ok, r.value ? r.value : "(null)",
               r.error ? r.error : "(null)");
    }
    repl_result_free(&r);
    return match;
}

/* Check that input produces an eval/parse error matching expected_error. */
static int eval_error_matches(const char *input, const char *expected_error) {
    ReplResult r = repl_eval_line(input, false, false, false, &noop_ctx);
    int match = !r.ok && r.error && strcmp(r.error, expected_error) == 0;
    if (!match) {
        printf("\n    Expected: ok=false error=%s\n    Got:      ok=%d error=%s\n", expected_error,
               r.ok, r.error ? r.error : "(null)");
    }
    repl_result_free(&r);
    return match;
}

/* Check that input produces the expected AST string.
 * Uses want_ast=true. The eval result (ok/error) is ignored. */
static int eval_ast_matches(const char *input, const char *expected_ast) {
    ReplResult r = repl_eval_line(input, false, true, false, &noop_ctx);
    int match = r.ast && strcmp(r.ast, expected_ast) == 0;
    if (!match) {
        printf("\n    Expected AST: %s\n    Got:          %s\n", expected_ast,
               r.ast ? r.ast : "(null)");
    }
    repl_result_free(&r);
    return match;
}

/* ============================================================
 * Empty and whitespace input tests
 * ============================================================ */

TEST(test_empty_input) {
    ASSERT(eval_value_matches("", NULL), "empty input should return ok with no value");
    PASS();
}

TEST(test_null_input) {
    ASSERT(eval_value_matches(NULL, NULL), "NULL input should return ok with no value");
    PASS();
}

TEST(test_whitespace_only_spaces) {
    ASSERT(eval_value_matches("   ", NULL), "spaces only should return ok with no value");
    PASS();
}

TEST(test_whitespace_only_tabs) {
    ASSERT(eval_value_matches("\t\t", NULL), "tabs only should return ok with no value");
    PASS();
}

TEST(test_whitespace_only_newline) {
    ASSERT(eval_value_matches("\n", NULL), "newline only should return ok with no value");
    PASS();
}

TEST(test_whitespace_mixed) {
    ASSERT(eval_value_matches("  \t\n  ", NULL), "mixed whitespace should return ok with no value");
    PASS();
}

/* ============================================================
 * Single number evaluation
 * ============================================================ */

TEST(test_single_number_zero) {
    ASSERT(eval_value_matches("0", "0"), "zero");
    PASS();
}

TEST(test_single_number_positive) {
    ASSERT(eval_value_matches("42", "42"), "positive number");
    PASS();
}

TEST(test_single_number_large) {
    ASSERT(eval_value_matches("1234567890", "1234567890"), "large number");
    PASS();
}

TEST(test_number_with_leading_whitespace) {
    ASSERT(eval_value_matches("  42", "42"), "number with leading space");
    PASS();
}

TEST(test_number_with_trailing_whitespace) {
    ASSERT(eval_value_matches("42  ", "42"), "number with trailing space");
    PASS();
}

/* ============================================================
 * String evaluation
 * ============================================================ */

TEST(test_single_string_empty) {
    ASSERT(eval_value_matches("\"\"", ""), "empty string");
    PASS();
}

TEST(test_single_string_simple) {
    ASSERT(eval_value_matches("\"hello\"", "hello"), "simple string");
    PASS();
}

TEST(test_single_string_with_spaces) {
    ASSERT(eval_value_matches("\"hello world\"", "hello world"), "string with spaces");
    PASS();
}

TEST(test_single_string_with_digits) {
    ASSERT(eval_value_matches("\"test123\"", "test123"), "string with digits");
    PASS();
}

/* ============================================================
 * Expression evaluation
 * ============================================================ */

TEST(test_eval_addition) {
    ASSERT(eval_value_matches("1 + 2", "3"), "1+2=3");
    PASS();
}

TEST(test_eval_subtraction) {
    ASSERT(eval_value_matches("10 - 3", "7"), "10-3=7");
    PASS();
}

TEST(test_eval_multiplication) {
    ASSERT(eval_value_matches("2 * 3", "6"), "2*3=6");
    PASS();
}

TEST(test_eval_division_exact) {
    ASSERT(eval_value_matches("6 / 3", "2"), "6/3=2");
    PASS();
}

TEST(test_eval_division_float) {
    ASSERT(eval_value_matches("7 / 2", "3.5"), "7/2=3.5");
    PASS();
}

TEST(test_eval_exponent) {
    ASSERT(eval_value_matches("2 ** 10", "1024"), "2**10");
    PASS();
}

TEST(test_eval_precedence) {
    ASSERT(eval_value_matches("1 + 2 * 3", "7"), "1+2*3=7");
    PASS();
}

TEST(test_eval_parens) {
    ASSERT(eval_value_matches("(1 + 2) * 3", "9"), "(1+2)*3=9");
    PASS();
}

TEST(test_eval_unary_minus) {
    ASSERT(eval_value_matches("-3", "-3"), "unary -3");
    PASS();
}

TEST(test_eval_float_division) {
    ASSERT(eval_value_matches("42 / 5", "8.4"), "42/5=8.4");
    PASS();
}

/* ============================================================
 * Error cases
 * ============================================================ */

TEST(test_error_unterminated_string) {
    ASSERT(eval_error_matches("\"hello", "1:1 unterminated string"), "unterminated string error");
    PASS();
}

TEST(test_error_number_adjacent_ident) {
    ASSERT(eval_error_matches("42foo", "1:1 number followed by identifier character"),
           "number adjacent ident error");
    PASS();
}

TEST(test_error_unknown_ident) {
    ASSERT(eval_error_matches("foo", "line 1: unknown variable 'foo'"), "unknown ident error");
    PASS();
}

TEST(test_error_div_by_zero) {
    ASSERT(eval_error_matches("1 / 0", "line 1: division by zero"), "div by zero error");
    PASS();
}

/* ============================================================
 * Variable declaration and assignment tests
 * ============================================================ */

TEST(test_decl_returns_value) {
    ASSERT(eval_value_matches("my x = 2", "2"), "decl returns value");
    PASS();
}

TEST(test_decl_then_read) {
    /* Declare, then read in separate eval calls (persistent env). */
    ReplResult r1 = repl_eval_line("my x = 2", false, false, false, &noop_ctx);
    ASSERT(r1.ok, "decl ok");
    ASSERT_NOT_NULL(r1.value, "decl value set");
    ASSERT_STR_EQ(r1.value, "2", "decl value");
    repl_result_free(&r1);

    ASSERT(eval_value_matches("x + 3", "5"), "x + 3 = 5");
    PASS();
}

TEST(test_decl_expr_precedence) {
    /* my x = 1 + 2 * 3 → assigns 7 (assignment binds looser) */
    ASSERT(eval_value_matches("my y = 1 + 2 * 3", "7"), "decl precedence");
    PASS();
}

TEST(test_assign_returns_value) {
    /* Must declare first, then reassign. */
    ReplResult r1 = repl_eval_line("my z = 10", false, false, false, &noop_ctx);
    repl_result_free(&r1);
    ASSERT(eval_value_matches("z = 20", "20"), "assign returns value");
    PASS();
}

TEST(test_assign_updates_variable) {
    ReplResult r1 = repl_eval_line("my w = 5", false, false, false, &noop_ctx);
    repl_result_free(&r1);
    ReplResult r2 = repl_eval_line("w = 99", false, false, false, &noop_ctx);
    repl_result_free(&r2);
    ASSERT(eval_value_matches("w", "99"), "assign updates var");
    PASS();
}

TEST(test_assign_undeclared_error) {
    /* Assigning to undeclared variable is a runtime error. */
    ASSERT(eval_error_matches("undeclared = 1", "line 1: undefined variable 'undeclared'"),
           "undeclared assign error");
    PASS();
}

TEST(test_decl_chain) {
    /* my a = my b = 2 → both declared with value 2 */
    ReplResult r = repl_eval_line("my aa = my bb = 2", false, false, false, &noop_ctx);
    ASSERT(r.ok, "chain ok");
    ASSERT_NOT_NULL(r.value, "chain value set");
    ASSERT_STR_EQ(r.value, "2", "chain value");
    repl_result_free(&r);

    ASSERT(eval_value_matches("aa", "2"), "aa is 2");
    ASSERT(eval_value_matches("bb", "2"), "bb is 2");
    PASS();
}

TEST(test_assign_chain) {
    /* Declare p and q, then p = q = 42 */
    ReplResult r1 = repl_eval_line("my p = 0", false, false, false, &noop_ctx);
    repl_result_free(&r1);
    ReplResult r2 = repl_eval_line("my q = 0", false, false, false, &noop_ctx);
    repl_result_free(&r2);

    ReplResult r3 = repl_eval_line("p = q = 42", false, false, false, &noop_ctx);
    ASSERT(r3.ok, "assign chain ok");
    ASSERT_NOT_NULL(r3.value, "assign chain value set");
    ASSERT_STR_EQ(r3.value, "42", "chain value");
    repl_result_free(&r3);

    ASSERT(eval_value_matches("p", "42"), "p is 42");
    ASSERT(eval_value_matches("q", "42"), "q is 42");
    PASS();
}

TEST(test_unknown_ident_still_errors) {
    ASSERT(eval_error_matches("nope + 1", "line 1: unknown variable 'nope'"),
           "unknown ident error");
    PASS();
}

TEST(test_invalid_lhs_error) {
    /* 1 = 2 should be a parse error */
    ReplResult r = repl_eval_line("1 = 2", false, false, false, &noop_ctx);
    ASSERT(!r.ok, "should be error");
    ASSERT_NOT_NULL(r.error, "error set");
    ASSERT(strstr(r.error, "invalid assignment target") != NULL, "error message");
    repl_result_free(&r);
    PASS();
}

TEST(test_decl_string_value) {
    ReplResult r1 = repl_eval_line("my greeting = \"hello\"", false, false, false, &noop_ctx);
    ASSERT(r1.ok, "decl string ok");
    ASSERT_NOT_NULL(r1.value, "decl string value set");
    ASSERT_STR_EQ(r1.value, "hello", "decl string value");
    repl_result_free(&r1);

    ASSERT(eval_value_matches("greeting", "hello"), "read string var");
    PASS();
}

TEST(test_decl_ast_format) {
    /* Check AST output for my x = 2 */
    ASSERT(eval_ast_matches("my x = 2", "AST [DECL x [NUMBER 2]]"), "decl ast");
    PASS();
}

TEST(test_assign_ast_format) {
    /* Check AST output for x = 2 */
    ASSERT(eval_ast_matches("x = 2", "AST [ASSIGN x [NUMBER 2]]"), "assign ast");
    PASS();
}

/* ============================================================
 * AST mode tests
 * ============================================================ */

TEST(test_ast_empty) {
    ASSERT(eval_ast_matches("", "AST"), "ast empty");
    PASS();
}

TEST(test_ast_whitespace) {
    ASSERT(eval_ast_matches("   ", "AST"), "ast whitespace");
    PASS();
}

TEST(test_ast_number) {
    ASSERT(eval_ast_matches("42", "AST [NUMBER 42]"), "ast number");
    PASS();
}

TEST(test_ast_string) {
    ASSERT(eval_ast_matches("\"hello\"", "AST [STRING hello]"), "ast string");
    PASS();
}

TEST(test_ast_ident) {
    /* Evaluating "foo" produces an eval error (unknown variable),
     * but the AST is still built before evaluation runs. */
    ASSERT(eval_ast_matches("foo", "AST [IDENT foo]"), "ast ident");
    PASS();
}

TEST(test_ast_binop) {
    ASSERT(eval_ast_matches("1 + 2", "AST [BINOP + [NUMBER 1] [NUMBER 2]]"), "ast binop");
    PASS();
}

TEST(test_ast_precedence) {
    ASSERT(
        eval_ast_matches("1 + 2 * 3", "AST [BINOP + [NUMBER 1] [BINOP * [NUMBER 2] [NUMBER 3]]]"),
        "ast precedence");
    PASS();
}

TEST(test_ast_unary) {
    ASSERT(eval_ast_matches("-3", "AST [UNARY - [NUMBER 3]]"), "ast unary");
    PASS();
}

TEST(test_ast_error) {
    /* Parse error: AST is not available (parsing failed). */
    ReplResult r = repl_eval_line("(", false, true, false, &noop_ctx);
    ASSERT(!r.ok, "should be error");
    ASSERT(r.ast == NULL, "no AST on parse error");
    ASSERT_NOT_NULL(r.error, "error set");
    repl_result_free(&r);
    PASS();
}

/* ============================================================
 * Return value tests
 * ============================================================ */

TEST(test_result_is_heap_allocated) {
    ReplResult r1 = repl_eval_line("42", false, false, false, &noop_ctx);
    ReplResult r2 = repl_eval_line("7", false, false, false, &noop_ctx);
    ASSERT(r1.ok, "first ok");
    ASSERT(r2.ok, "second ok");
    ASSERT_NOT_NULL(r1.value, "first value set");
    ASSERT_NOT_NULL(r2.value, "second value set");
    ASSERT_STR_EQ(r1.value, "42", "first result correct");
    ASSERT_STR_EQ(r2.value, "7", "second result correct");
    repl_result_free(&r1);
    repl_result_free(&r2);
    PASS();
}

/* ============================================================
 * repl_eval_line() API tests
 * ============================================================ */

TEST(test_eval_line_blank) {
    ReplResult r = repl_eval_line("", false, false, false, &noop_ctx);
    ASSERT(r.ok, "ok for blank");
    ASSERT(r.value == NULL, "no value for blank");
    ASSERT(r.error == NULL, "no error");
    ASSERT(r.tokens == NULL, "no tokens");
    ASSERT(r.ast == NULL, "no ast");
    repl_result_free(&r);
    PASS();
}

TEST(test_eval_line_null) {
    ReplResult r = repl_eval_line(NULL, false, false, false, &noop_ctx);
    ASSERT(r.ok, "ok for null");
    ASSERT(r.value == NULL, "no value for null");
    repl_result_free(&r);
    PASS();
}

TEST(test_eval_line_number) {
    ReplResult r = repl_eval_line("42", false, false, false, &noop_ctx);
    ASSERT(r.ok, "ok");
    ASSERT_NOT_NULL(r.value, "value set");
    ASSERT_STR_EQ(r.value, "42", "plain number value");
    ASSERT(r.error == NULL, "no error");
    repl_result_free(&r);
    PASS();
}

TEST(test_eval_line_string) {
    ReplResult r = repl_eval_line("\"hello\"", false, false, false, &noop_ctx);
    ASSERT(r.ok, "ok");
    ASSERT_STR_EQ(r.value, "hello", "plain string value");
    repl_result_free(&r);
    PASS();
}

TEST(test_eval_line_expression) {
    ReplResult r = repl_eval_line("1 + 2 * 3", false, false, false, &noop_ctx);
    ASSERT(r.ok, "ok");
    ASSERT_STR_EQ(r.value, "7", "expression eval");
    repl_result_free(&r);
    PASS();
}

TEST(test_eval_line_parse_error) {
    ReplResult r = repl_eval_line("\"unterminated", false, false, false, &noop_ctx);
    ASSERT(!r.ok, "not ok");
    ASSERT(r.value == NULL, "no value on error");
    ASSERT_NOT_NULL(r.error, "error set");
    ASSERT(strstr(r.error, "unterminated") != NULL, "error message");
    repl_result_free(&r);
    PASS();
}

TEST(test_eval_line_eval_error) {
    ReplResult r = repl_eval_line("1 / 0", false, false, false, &noop_ctx);
    ASSERT(!r.ok, "not ok");
    ASSERT_NOT_NULL(r.error, "error set");
    ASSERT(strstr(r.error, "division by zero") != NULL, "error message");
    repl_result_free(&r);
    PASS();
}

TEST(test_eval_line_with_tokens) {
    ReplResult r = repl_eval_line("42", true, false, false, &noop_ctx);
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
    ReplResult r = repl_eval_line("1 + 2", false, true, false, &noop_ctx);
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
    ReplResult r = repl_eval_line("42", true, true, false, &noop_ctx);
    ASSERT(r.ok, "ok");
    ASSERT_STR_EQ(r.value, "42", "value");
    ASSERT_NOT_NULL(r.tokens, "tokens present");
    ASSERT_NOT_NULL(r.ast, "ast present");
    repl_result_free(&r);
    PASS();
}

TEST(test_eval_line_tokens_on_error) {
    /* Best-effort: tokens should still be produced even on parse error. */
    ReplResult r = repl_eval_line("42foo", true, false, false, &noop_ctx);
    ASSERT(!r.ok, "not ok");
    ASSERT_NOT_NULL(r.tokens, "tokens present despite error");
    ASSERT(strstr(r.tokens, "TOKENS") != NULL, "tokens prefix");
    ASSERT(strstr(r.tokens, "ERR") != NULL, "tokens contain error");
    repl_result_free(&r);
    PASS();
}

/* ============================================================
 * Boolean literal REPL output tests
 * ============================================================ */

TEST(test_bool_true_value) {
    ASSERT(eval_value_matches("true", "true"), "true value");
    PASS();
}

TEST(test_bool_false_value) {
    ASSERT(eval_value_matches("false", "false"), "false value");
    PASS();
}

TEST(test_bool_true_eval_line) {
    ReplResult r = repl_eval_line("true", false, false, false, &noop_ctx);
    ASSERT(r.ok, "ok");
    ASSERT_NOT_NULL(r.value, "value present");
    ASSERT_STR_EQ(r.value, "true", "value is true");
    repl_result_free(&r);
    PASS();
}

TEST(test_bool_false_eval_line) {
    ReplResult r = repl_eval_line("false", false, false, false, &noop_ctx);
    ASSERT(r.ok, "ok");
    ASSERT_NOT_NULL(r.value, "value present");
    ASSERT_STR_EQ(r.value, "false", "value is false");
    repl_result_free(&r);
    PASS();
}

/* ============================================================
 * While loop tests
 * ============================================================ */

TEST(test_while_repl_basic) {
    /* While loop via eval_line: loop runs and returns last body value */
    ASSERT(eval_value_matches("my wh_r1 = 0\nwhile wh_r1 < 3 do\n  wh_r1 = wh_r1 + 1\nend", "3"),
           "while loop returns last body value");
    PASS();
}

TEST(test_while_repl_multiline) {
    /* Multiline while loop in REPL */
    ASSERT(eval_value_matches("while false do\n  42\nend", "nothing"),
           "while that never runs returns nothing");
    PASS();
}

TEST(test_while_repl_as_expression) {
    /* While used as expression in assignment */
    ASSERT(eval_value_matches(
               "my wh_r2 = 0\nmy wh_r3 = while wh_r2 < 2 do\n  wh_r2 = wh_r2 + 1\nend\nwh_r3", "2"),
           "while as expression in REPL");
    PASS();
}

/* ============================================================
 * Break and continue
 * ============================================================ */

TEST(test_break_repl_basic) {
    /* break exits loop, bare break returns nothing */
    ASSERT(eval_value_matches("while true do break end", "nothing"),
           "bare break returns nothing in REPL");
    PASS();
}

TEST(test_break_repl_with_value) {
    /* break with value returns that value */
    ASSERT(eval_value_matches("while true do break 42 end", "42"),
           "break with value returns value in REPL");
    PASS();
}

TEST(test_continue_repl_basic) {
    /* continue skips iteration, loop with continue on every iteration returns nothing */
    ASSERT(eval_value_matches("my cr_i = 0\nwhile cr_i < 3 do\n  cr_i = cr_i + 1\n  continue\nend",
                              "nothing"),
           "continue on every iteration returns nothing in REPL");
    PASS();
}

/* ============================================================
 * String concatenation operator (..)
 * ============================================================ */

TEST(test_concat_basic) {
    ASSERT(eval_value_matches("\"hello\" .. \" world\"", "hello world"), "basic concat");
    PASS();
}

TEST(test_concat_auto_coerce_num) {
    ASSERT(eval_value_matches("\"score: \" .. 42", "score: 42"), "coerce number");
    PASS();
}

TEST(test_concat_auto_coerce_bool) {
    ASSERT(eval_value_matches("true .. \"!\"", "true!"), "coerce bool");
    PASS();
}

TEST(test_concat_auto_coerce_nothing) {
    ASSERT(eval_value_matches("nothing .. \"x\"", "nothingx"), "coerce nothing");
    PASS();
}

TEST(test_concat_chained_repl) {
    ASSERT(eval_value_matches("\"a\" .. \"b\" .. \"c\"", "abc"), "chained concat");
    PASS();
}

/* ============================================================
 * Bytecode debug output tests
 * ============================================================ */

TEST(test_eval_line_with_bytecode) {
    /* want_bytecode=true should produce non-NULL bytecode containing "BYTECODE" and "OP_" */
    ReplResult r = repl_eval_line("42", false, false, true, &noop_ctx);
    ASSERT(r.ok, "ok");
    ASSERT_STR_EQ(r.value, "42", "value");
    ASSERT_NOT_NULL(r.bytecode, "bytecode present");
    ASSERT(strstr(r.bytecode, "BYTECODE") != NULL, "bytecode prefix");
    ASSERT(strstr(r.bytecode, "OP_") != NULL, "bytecode contains opcodes");
    ASSERT(r.tokens == NULL, "no tokens");
    ASSERT(r.ast == NULL, "no ast");
    repl_result_free(&r);
    PASS();
}

TEST(test_eval_line_without_bytecode) {
    /* want_bytecode=false should produce NULL bytecode. */
    ReplResult r = repl_eval_line("42", false, false, false, &noop_ctx);
    ASSERT(r.ok, "ok");
    ASSERT(r.bytecode == NULL, "no bytecode when not requested");
    repl_result_free(&r);
    PASS();
}

TEST(test_eval_line_bytecode_on_parse_error) {
    /* want_bytecode=true on parse error should produce NULL bytecode (can't compile). */
    ReplResult r = repl_eval_line("(", false, false, true, &noop_ctx);
    ASSERT(!r.ok, "should be error");
    ASSERT(r.bytecode == NULL, "no bytecode on parse error");
    repl_result_free(&r);
    PASS();
}

TEST(test_eval_line_bytecode_blank_input) {
    /* Blank input with want_bytecode=true should produce "BYTECODE" string. */
    ReplResult r = repl_eval_line("", false, false, true, &noop_ctx);
    ASSERT(r.ok, "ok for blank");
    ASSERT(r.value == NULL, "no value for blank");
    ASSERT_NOT_NULL(r.bytecode, "bytecode present for blank");
    ASSERT_STR_EQ(r.bytecode, "BYTECODE", "blank bytecode");
    repl_result_free(&r);
    PASS();
}

TEST(test_eval_line_bytecode_with_expression) {
    /* want_bytecode=true with a real expression should contain OP_CONSTANT and OP_RETURN. */
    ReplResult r = repl_eval_line("1 + 2", false, false, true, &noop_ctx);
    ASSERT(r.ok, "ok");
    ASSERT_STR_EQ(r.value, "3", "value");
    ASSERT_NOT_NULL(r.bytecode, "bytecode present");
    ASSERT(strstr(r.bytecode, "OP_CONSTANT") != NULL, "contains OP_CONSTANT");
    ASSERT(strstr(r.bytecode, "OP_ADD") != NULL, "contains OP_ADD");
    ASSERT(strstr(r.bytecode, "OP_RETURN") != NULL, "contains OP_RETURN");
    repl_result_free(&r);
    PASS();
}

TEST(test_eval_line_all_three_debug) {
    /* All three debug flags at once. */
    ReplResult r = repl_eval_line("42", true, true, true, &noop_ctx);
    ASSERT(r.ok, "ok");
    ASSERT_NOT_NULL(r.tokens, "tokens present");
    ASSERT_NOT_NULL(r.ast, "ast present");
    ASSERT_NOT_NULL(r.bytecode, "bytecode present");
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

    printf("\nBoolean literals:\n");
    RUN_TEST(test_bool_true_value);
    RUN_TEST(test_bool_false_value);
    RUN_TEST(test_bool_true_eval_line);
    RUN_TEST(test_bool_false_eval_line);

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

    printf("\nWhile loop:\n");
    RUN_TEST(test_while_repl_basic);
    RUN_TEST(test_while_repl_multiline);
    RUN_TEST(test_while_repl_as_expression);

    printf("\nBreak and continue:\n");
    RUN_TEST(test_break_repl_basic);
    RUN_TEST(test_break_repl_with_value);
    RUN_TEST(test_continue_repl_basic);

    printf("\nBytecode debug output:\n");
    RUN_TEST(test_eval_line_with_bytecode);
    RUN_TEST(test_eval_line_without_bytecode);
    RUN_TEST(test_eval_line_bytecode_on_parse_error);
    RUN_TEST(test_eval_line_bytecode_blank_input);
    RUN_TEST(test_eval_line_bytecode_with_expression);
    RUN_TEST(test_eval_line_all_three_debug);

    printf("\nString concatenation operator (..):\n");
    RUN_TEST(test_concat_basic);
    RUN_TEST(test_concat_auto_coerce_num);
    RUN_TEST(test_concat_auto_coerce_bool);
    RUN_TEST(test_concat_auto_coerce_nothing);
    RUN_TEST(test_concat_chained_repl);

    printf("\n========================================\n");
    printf("Tests run: %d\n", tests_run);
    printf("Passed:    %d\n", tests_passed);
    printf("Failed:    %d\n", tests_failed);
    printf("========================================\n");

    return tests_failed > 0 ? 1 : 0;
}
