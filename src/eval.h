/*
 * eval.h - Cutlet expression evaluator interface
 *
 * Evaluates an AST node tree and returns a Value result.
 * Numbers are stored as doubles; division always produces floats.
 * Unknown identifiers and assignment to undeclared variables produce errors.
 *
 * All eval() calls require an EvalContext, which carries a write callback
 * used by built-in functions like say() to emit output. The context is
 * threaded through every recursive eval() call unchanged.
 */

#ifndef CUTLET_EVAL_H
#define CUTLET_EVAL_H

#include "parser.h"
#include <stdbool.h>
#include <stddef.h>

typedef enum { VAL_NUMBER, VAL_STRING, VAL_BOOL, VAL_NOTHING, VAL_ERROR } ValueType;

typedef struct {
    ValueType type;
    double number;
    bool boolean;
    char *string; /* owned; also used for error message */
} Value;

/*
 * Write callback type for built-in output functions (e.g. say()).
 * Called with opaque userdata, a data buffer, and its length.
 */
typedef void (*EvalWriteFn)(void *userdata, const char *data, size_t len);

/*
 * Context threaded through all eval() calls.
 *
 * write_fn: called by say() to emit output.
 * userdata: opaque pointer passed to write_fn (e.g. fd+id for server,
 *           FILE* for file mode, buffer pointer for tests).
 */
typedef struct {
    EvalWriteFn write_fn;
    void *userdata;
} EvalContext;

/*
 * Evaluate an AST node tree and return a Value.
 * ctx must be non-NULL and is threaded through all recursive calls.
 * The caller must call value_free() on the result.
 */
Value eval(const AstNode *node, EvalContext *ctx);

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
