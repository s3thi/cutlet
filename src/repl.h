/*
 * repl.h - Cutlet REPL core interface
 *
 * Provides the core REPL formatting function that takes an input line
 * and returns a formatted result string suitable for output.
 *
 * Output format:
 * - Success: OK [TYPE value] [TYPE value] ...
 * - Error: ERR line:col message
 * - Empty/whitespace input: OK
 */

#ifndef CUTLET_REPL_H
#define CUTLET_REPL_H

/*
 * Format a single line of input into a REPL response.
 *
 * Takes an input string (typically one line from the user) and returns
 * a newly allocated string containing the formatted result.
 *
 * Return format:
 * - On successful tokenization: "OK [TYPE value] [TYPE value] ..."
 * - On tokenization error: "ERR line:col message"
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
