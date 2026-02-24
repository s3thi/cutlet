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

/* The eight value types in the Cutlet language. */
typedef enum {
    VAL_NUMBER,
    VAL_STRING,
    VAL_BOOL,
    VAL_NOTHING,
    VAL_ERROR,
    VAL_FUNCTION, /* Native functions (say, etc.) */
    VAL_CLOSURE,  /* User-defined functions wrapped in a closure */
    VAL_ARRAY,    /* Heap-allocated array with reference-counted backing store */
    VAL_MAP,      /* Heap-allocated map with reference-counted backing store */
} ValueType;

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
 * refcount:      Reference count for shared ownership (closures, clones).
 * name:          Function name (heap-allocated, NULL for anonymous functions).
 * arity:         Number of parameters.
 * upvalue_count: Number of upvalues this function captures (0 until capture is implemented).
 * params:        Parameter names (heap-allocated array of heap-allocated strings).
 * chunk:         Compiled body bytecode (heap-allocated, NULL for native functions).
 * native:        Native function pointer (NULL for user-defined functions).
 */
typedef struct {
    size_t refcount;
    char *name;
    int arity;
    int upvalue_count;
    char **params;
    Chunk *chunk;
    NativeFn native;
} ObjFunction;

/*
 * ObjArray - heap-allocated backing store for arrays.
 *
 * Reference-counted for structural sharing (value semantics with COW).
 * Mutation operations check refcount and deep-clone if shared.
 *
 * refcount: Number of Value references pointing to this ObjArray.
 * data:     Owned array of Values (each element is a full Value).
 * count:    Number of elements currently stored.
 * capacity: Allocated capacity of the data array.
 */
typedef struct {
    size_t refcount;
    Value *data;
    size_t count;
    size_t capacity;
} ObjArray;

/* Forward declarations for types defined after Value.
 * These need the full Value definition (they contain Value fields),
 * but Value needs pointers to them, so we forward-declare here. */
typedef struct ObjUpvalue ObjUpvalue;
typedef struct ObjClosure ObjClosure;
typedef struct ObjMap ObjMap;

/* A tagged union representing a Cutlet value.
 * - VAL_NUMBER: number field holds the value.
 * - VAL_STRING: string field holds a heap-allocated string.
 * - VAL_BOOL: boolean field holds the value.
 * - VAL_NOTHING: no payload.
 * - VAL_ERROR: string field holds a heap-allocated error message.
 * - VAL_FUNCTION: function field holds a refcounted ObjFunction (natives).
 * - VAL_CLOSURE: closure field holds a refcounted ObjClosure (user functions).
 * - VAL_ARRAY: array field holds a refcounted ObjArray. */
// NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding)
struct Value {
    ValueType type;
    double number;
    bool boolean;
    char *string;          /* owned; also used for error message */
    ObjFunction *function; /* refcounted; non-NULL only for VAL_FUNCTION */
    ObjClosure *closure;   /* refcounted; non-NULL only for VAL_CLOSURE */
    ObjArray *array;       /* refcounted; non-NULL only for VAL_ARRAY */
    ObjMap *map;           /* refcounted; non-NULL only for VAL_MAP */
};

/*
 * ObjUpvalue - captures a variable from an enclosing scope.
 *
 * When open, `location` points to the variable's stack slot.
 * When closed (variable goes out of scope), the value is copied into
 * `closed` and `location` is redirected to `&closed`.
 *
 * refcount: Reference count (multiple closures can share an upvalue).
 * location: Points to the stack slot (open) or &closed (closed).
 * closed:   Holds the value after the upvalue is closed.
 * next:     Linked list pointer for the VM's open-upvalue list.
 */
struct ObjUpvalue {
    size_t refcount;
    Value *location;
    Value closed;
    struct ObjUpvalue *next;
};

/*
 * ObjClosure - wraps an ObjFunction with captured upvalues.
 *
 * Every user-defined function at runtime is represented as a closure,
 * even if it captures nothing (upvalue_count == 0).
 *
 * refcount:      Reference count for shared ownership.
 * function:      The underlying compiled function (refcounted, not owned uniquely).
 * upvalues:      Array of pointers to captured upvalues (NULL entries until filled).
 * upvalue_count: Length of the upvalues array.
 */
struct ObjClosure {
    size_t refcount;
    ObjFunction *function;
    ObjUpvalue **upvalues;
    int upvalue_count;
};

/* A single key-value entry in a map. */
typedef struct {
    Value key;   /* Owned key. */
    Value value; /* Owned value. */
} MapEntry;

/* Heap-allocated backing store for maps. Reference-counted.
 * Entries are stored in a dense array in insertion order.
 * Lookup is O(n) linear scan — sufficient for typical shell-script
 * map sizes. Can be upgraded to a hash index later without API changes. */
struct ObjMap {
    size_t refcount;   /* Reference count (1 on creation). */
    MapEntry *entries; /* Owned array of key-value pairs. */
    size_t count;      /* Number of live entries. */
    size_t capacity;   /* Allocated capacity of entries array. */
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

/* Create a function Value. Sets fn->refcount = 1 if not already set. */
Value make_function(ObjFunction *fn);

/*
 * Create a native function Value.
 * Allocates an ObjFunction with native pointer set and chunk NULL.
 * name is copied (caller retains ownership of the original).
 */
Value make_native(const char *name, int arity, NativeFn fn);

/* Create a closure Value (takes ownership of the ObjClosure reference). */
Value make_closure(ObjClosure *cl);

/* Create an array Value (takes ownership of the ObjArray reference). */
Value make_array(ObjArray *arr);

/*
 * Allocate a new ObjUpvalue pointing to the given stack slot.
 * Returns NULL on allocation failure. Initial refcount is 1.
 */
ObjUpvalue *obj_upvalue_new(Value *slot);

/* Decrement an upvalue's refcount; free when it reaches 0. */
void obj_upvalue_free(ObjUpvalue *uv);

/*
 * Allocate a new ObjClosure wrapping the given function.
 * Increments fn->refcount. The upvalue pointer array is allocated
 * with all entries set to NULL. Returns NULL on allocation failure.
 */
ObjClosure *obj_closure_new(ObjFunction *fn, int upvalue_count);

/* Decrement a closure's refcount; free when it reaches 0. */
void obj_closure_free(ObjClosure *cl);

/* Free an ObjFunction when its refcount reaches 0. Exposed for tests. */
void obj_function_free(ObjFunction *fn);

/* Allocate a new empty ObjArray with refcount 1. Returns NULL on failure. */
ObjArray *obj_array_new(void);

/* Append a value to an ObjArray, growing if needed. Takes ownership of v. */
void obj_array_push(ObjArray *arr, Value v);

/* Create a full independent deep copy of an ObjArray (refcount 1). */
ObjArray *obj_array_clone_deep(const ObjArray *src);

/*
 * Copy-on-write helper: if the array's refcount > 1, replace it with
 * a deep clone so the caller can safely mutate it.
 */
void obj_array_ensure_owned(Value *v);

/* Allocate a new empty ObjMap with refcount 1. Returns NULL on failure. */
ObjMap *obj_map_new(void);

/* Insert or update a key-value pair in a map. Clones key and value. */
void obj_map_set(ObjMap *m, const Value *key, const Value *val);

/* Return pointer to value for key, or NULL if not found. Does not clone. */
Value *obj_map_get(const ObjMap *m, const Value *key);

/* Return true if key exists in the map. */
bool obj_map_has(const ObjMap *m, const Value *key);

/* Create a full independent deep copy of an ObjMap (refcount 1). */
ObjMap *obj_map_clone_deep(const ObjMap *src);

/*
 * Copy-on-write helper: if the map's refcount > 1, replace it with
 * a deep clone so the caller can safely mutate it.
 */
void obj_map_ensure_owned(Value *v);

/* Create a map Value (takes ownership of the ObjMap reference). */
Value make_map(ObjMap *m);

/*
 * Compare two values for equality. Used by map key lookup and by
 * the VM's equality operators. Handles all value types including
 * VAL_ARRAY (structural equality). VAL_MAP equality is added in a
 * later step.
 */
bool value_equal(const Value *a, const Value *b);

/* ---- Value utilities ---- */

/*
 * Free any heap-allocated memory in a Value.
 * Safe to call on zero-initialized Values.
 * For VAL_FUNCTION: decrements refcount; frees ObjFunction when 0.
 * For VAL_CLOSURE: decrements refcount; frees ObjClosure when 0.
 * For VAL_ARRAY: decrements refcount; frees ObjArray + elements when 0.
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
 * Clone a Value.
 * For VAL_FUNCTION: increments refcount (shared, not deep-copied).
 * For VAL_CLOSURE: increments closure refcount (shared upvalues).
 * For VAL_ARRAY: increments refcount (shallow copy, structural sharing).
 * Returns true on success, false on allocation failure.
 */
bool value_clone(Value *out, const Value *src);

#endif /* CUTLET_VALUE_H */
