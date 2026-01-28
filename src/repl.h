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

#endif /* CUTLET_REPL_H */
