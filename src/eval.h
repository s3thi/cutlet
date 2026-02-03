/*
 * eval.h - Cutlet expression evaluator interface
 *
 * Evaluates an AST node tree and returns a Value result.
 * Numbers are stored as doubles; division always produces floats.
 * Unknown identifiers and assignment to undeclared variables produce errors.
 */

#ifndef CUTLET_EVAL_H
#define CUTLET_EVAL_H

#include "parser.h"
#include <stdbool.h>

typedef enum { VAL_NUMBER, VAL_STRING, VAL_BOOL, VAL_NOTHING, VAL_ERROR } ValueType;

typedef struct {
    ValueType type;
    double number;
    bool boolean;
    char *string; /* owned; also used for error message */
} Value;

/*
 * Evaluate an AST node tree and return a Value.
 * The caller must call value_free() on the result.
 */
Value eval(const AstNode *node);

/*
 * Free any heap-allocated memory in a Value.
 * Safe to call on zero-initialized Values.
 */
void value_free(Value *v);

/*
 * Format a Value as a human-readable string.
 * Returns a newly allocated string. Caller must free.
 *
 * - VAL_NUMBER: integers without decimal ("8"), floats with minimal
 *   decimals ("8.4")
 * - VAL_STRING: the string value itself
 * - VAL_ERROR: "ERR <message>"
 */
char *value_format(const Value *v);

#endif /* CUTLET_EVAL_H */
