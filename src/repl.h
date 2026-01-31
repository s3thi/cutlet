/*
 * repl.h - Cutlet REPL core interface
 *
 * Provides the core REPL formatting functions that parse and evaluate
 * an input expression and return a formatted result string.
 *
 * Token mode (repl_format_line) output format:
 * - Success: "OK [TYPE value]" (e.g., "OK [NUMBER 42]", "OK [STRING hello]")
 * - Parse error: "ERR line:col message"
 * - Eval error: "ERR message"
 * - Empty/whitespace input: "OK"
 *
 * AST mode (repl_format_line_ast) output format:
 * - Success: "AST [TYPE ...]" (nested S-expression)
 * - Parse error: "ERR line:col message"
 * - Empty/whitespace input: "AST"
 */

#ifndef CUTLET_REPL_H
#define CUTLET_REPL_H

/*
 * Format a single line of input into a REPL response.
 *
 * Parses the input as an expression, evaluates it, and returns a
 * newly allocated string containing the formatted result.
 *
 * Return format:
 * - On successful evaluation: "OK [TYPE value]"
 * - On parse error: "ERR line:col message"
 * - On eval error: "ERR message" (no position info)
 * - On empty/whitespace input: "OK"
 *
 * The caller is responsible for freeing the returned string.
 * Returns NULL on allocation failure.
 *
 * If input is NULL, treats it as empty string.
 */
char *repl_format_line(const char *input);

/*
 * Format a single line of input into an AST REPL response.
 *
 * Uses the parser to produce AST output instead of raw tokens.
 *
 * Return format:
 * - On successful parse: "AST [TYPE value]"
 * - On parse error: "ERR line:col message"
 * - On empty/whitespace input: "AST"
 *
 * The caller is responsible for freeing the returned string.
 * Returns NULL on allocation failure.
 */
char *repl_format_line_ast(const char *input);

#endif /* CUTLET_REPL_H */
