/*
 * repl.h - Cutlet REPL core interface
 *
 * Provides the core REPL evaluation function that parses and evaluates
 * an input expression and returns a structured result.
 *
 * The primary API is repl_eval_line(), which returns:
 * - ok=true, value set: successful evaluation
 * - ok=false, error set: parse or eval error
 * - ok=true, value=NULL: empty/whitespace input
 *
 * Optional debug fields (tokens, ast) are populated when requested.
 */

#ifndef CUTLET_REPL_H
#define CUTLET_REPL_H

#include "value.h" /* for EvalContext */
#include <stdbool.h>

/*
 * Structured result from evaluating a REPL line.
 *
 * ok=true means evaluation succeeded (or input was blank).
 * ok=false means a parse or eval error occurred.
 *
 * Fields are mutually exclusive: value is set on success, error on failure.
 * tokens and ast are optional debug outputs, set only when requested.
 * All string fields are heap-allocated; use repl_result_free() to clean up.
 */
typedef struct {
    bool ok;
    char *value;  /* plain value string (e.g. "42", "hello"), NULL for blank input */
    char *error;  /* error string (e.g. "1:5 unterminated string") */
    char *tokens; /* debug: token dump, e.g. "TOKENS [NUMBER 42]" */
    char *ast;    /* debug: AST dump, e.g. "AST [NUMBER 42]" */
} ReplResult;

/*
 * Evaluate a single line of input.
 *
 * Parses the input as an expression, evaluates it, and returns
 * a structured result. Optionally includes token and/or AST
 * debug output.
 *
 * ctx must be non-NULL. Built-in functions like say() use ctx
 * to emit output. The caller is responsible for constructing an
 * appropriate EvalContext (e.g. server output callback, or no-op
 * for contexts where say() output is not needed).
 *
 * Thread-safe: acquires the global eval lock internally.
 *
 * The caller must call repl_result_free() on the result.
 *
 * If input is NULL, treats it as empty string.
 */
ReplResult repl_eval_line(const char *input, bool want_tokens, bool want_ast, EvalContext *ctx);

/*
 * Free all heap-allocated fields in a ReplResult.
 */
void repl_result_free(ReplResult *r);

#endif /* CUTLET_REPL_H */
