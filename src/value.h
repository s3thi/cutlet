/*
 * value.h - Cutlet value types and utilities
 *
 * Core value representation used throughout the interpreter:
 * compiler, VM, runtime, and REPL. Numbers are stored as doubles;
 * strings and error messages are heap-allocated.
 *
 * EvalContext provides a write callback used by built-in functions
 * like say() to emit output.
 */

#ifndef CUTLET_VALUE_H
#define CUTLET_VALUE_H

#include <stdbool.h>
#include <stddef.h>

/* Forward declaration of Chunk (defined in chunk.h).
 * Needed here because ObjFunction owns a compiled Chunk. */
typedef struct Chunk Chunk;

/* The six value types in the Cutlet language. */
typedef enum { VAL_NUMBER, VAL_STRING, VAL_BOOL, VAL_NOTHING, VAL_ERROR, VAL_FUNCTION } ValueType;

/* Forward declaration for the Value type (needed by NativeFn). */
typedef struct Value Value;

/*
 * Write callback type for built-in output functions (e.g. say()).
 * Called with opaque userdata, a data buffer, and its length.
 */
typedef void (*EvalWriteFn)(void *userdata, const char *data, size_t len);

/*
 * Context threaded through evaluation.
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
 * Native function pointer type.
 * Used for built-in functions like say(). Takes argc, an array of
 * argument Values, and the EvalContext for I/O.
 */
typedef Value (*NativeFn)(int argc, Value *args, EvalContext *ctx);

/*
 * ObjFunction - represents a user-defined or native function.
 *
 * name:   Function name (heap-allocated, NULL for anonymous functions).
 * arity:  Number of parameters.
 * params: Parameter names (heap-allocated array of heap-allocated strings).
 * chunk:  Compiled body bytecode (heap-allocated, NULL for native functions).
 * native: Native function pointer (NULL for user-defined functions).
 */
typedef struct {
    char *name;
    int arity;
    char **params;
    Chunk *chunk;
    NativeFn native;
} ObjFunction;

/* A tagged union representing a Cutlet value.
 * - VAL_NUMBER: number field holds the value.
 * - VAL_STRING: string field holds a heap-allocated string.
 * - VAL_BOOL: boolean field holds the value.
 * - VAL_NOTHING: no payload.
 * - VAL_ERROR: string field holds a heap-allocated error message.
 * - VAL_FUNCTION: function field holds an owned ObjFunction. */
// NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding)
struct Value {
    ValueType type;
    double number;
    bool boolean;
    char *string;          /* owned; also used for error message */
    ObjFunction *function; /* owned; non-NULL only for VAL_FUNCTION */
};

/* ---- Value constructors ---- */

/* Create a number Value. */
Value make_number(double n);

/* Create a string Value (takes ownership of s). */
Value make_string(char *s);

/* Create a boolean Value. */
Value make_bool(bool b);

/* Create a nothing Value. */
Value make_nothing(void);

/* Create an error Value with a formatted message. */
Value make_error(const char *fmt, ...);

/* Create a function Value (takes ownership of the ObjFunction). */
Value make_function(ObjFunction *fn);

/*
 * Create a native function Value.
 * Allocates an ObjFunction with native pointer set and chunk NULL.
 * name is copied (caller retains ownership of the original).
 */
Value make_native(const char *name, int arity, NativeFn fn);

/* ---- Value utilities ---- */

/*
 * Free any heap-allocated memory in a Value.
 * Safe to call on zero-initialized Values.
 * For VAL_FUNCTION, frees the ObjFunction and all its owned data.
 */
void value_free(Value *v);

/*
 * Format a Value as a human-readable string.
 * Returns a newly allocated string. Caller must free.
 *
 * - VAL_NUMBER: integers without decimal ("8"), floats with minimal
 *   decimals ("8.4")
 * - VAL_STRING: the string value itself
 * - VAL_BOOL: "true" or "false"
 * - VAL_NOTHING: "nothing"
 * - VAL_ERROR: "ERR <message>"
 * - VAL_FUNCTION: "<fn name>" for named, "<fn>" for anonymous
 */
char *value_format(const Value *v);

/*
 * Determine if a Value is truthy.
 * Truthiness rules:
 * - false is falsy, true is truthy
 * - 0 is falsy, all other numbers are truthy
 * - empty string "" is falsy, all other strings are truthy
 * - nothing is falsy
 * - errors are falsy
 * - functions are truthy
 */
bool is_truthy(const Value *v);

/*
 * Clone a Value, deep-copying any owned data.
 * For VAL_FUNCTION, deep-copies the ObjFunction and all its fields.
 * Returns true on success, false on allocation failure.
 */
bool value_clone(Value *out, const Value *src);

#endif /* CUTLET_VALUE_H */
