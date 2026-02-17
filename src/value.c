/*
 * value.c - Cutlet value types and utilities
 *
 * Implements the core Value type: constructors, formatting,
 * truthiness, cloning, and memory management.
 */

#include "value.h"
#include "chunk.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Value make_number(double n) { return (Value){.type = VAL_NUMBER, .number = n, .string = NULL}; }

Value make_string(char *s) { return (Value){.type = VAL_STRING, .number = 0, .string = s}; }

Value make_bool(bool b) {
    return (Value){.type = VAL_BOOL, .boolean = b, .number = 0, .string = NULL};
}

Value make_nothing(void) {
    return (Value){.type = VAL_NOTHING, .boolean = false, .number = 0, .string = NULL};
}

Value make_function(ObjFunction *fn) { return (Value){.type = VAL_FUNCTION, .function = fn}; }

Value make_native(const char *name, int arity, NativeFn fn) {
    ObjFunction *obj = calloc(1, sizeof(ObjFunction));
    if (!obj)
        return make_error("memory allocation failed");
    obj->name = strdup(name);
    obj->arity = arity;
    obj->params = NULL; /* Natives don't expose parameter names. */
    obj->chunk = NULL;  /* No compiled body — dispatch via native pointer. */
    obj->native = fn;
    return make_function(obj);
}

Value make_error(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return (Value){.type = VAL_ERROR, .number = 0, .string = strdup(buf)};
}

/* Free an ObjFunction and all its owned data. */
static void obj_function_free(ObjFunction *fn) {
    if (!fn)
        return;
    free(fn->name);
    if (fn->params) {
        for (int i = 0; i < fn->arity; i++) {
            free(fn->params[i]);
        }
        free((void *)fn->params);
    }
    if (fn->chunk) {
        chunk_free(fn->chunk);
        free(fn->chunk);
    }
    free(fn);
}

void value_free(Value *v) {
    if (!v)
        return;
    if (v->string) {
        free(v->string);
        v->string = NULL;
    }
    if (v->type == VAL_FUNCTION && v->function) {
        obj_function_free(v->function);
        v->function = NULL;
    }
}

char *value_format(const Value *v) {
    if (!v) {
        return NULL;
    }

    switch (v->type) {
    case VAL_NUMBER: {
        /*
         * Format numbers: integers without decimal point, non-integers
         * with minimal decimal places. Use %g which removes trailing zeros.
         */
        char buf[64];
        double truncated = trunc(v->number);
        if (v->number == truncated && fabs(v->number) < 1e15) {
            /* Integer value — print without decimal */
            snprintf(buf, sizeof(buf), "%lld", (long long)truncated);
        } else {
            /* Float value — use %g for minimal representation */
            snprintf(buf, sizeof(buf), "%g", v->number);
        }
        return strdup(buf);
    }

    case VAL_BOOL:
        return strdup(v->boolean ? "true" : "false");

    case VAL_NOTHING:
        return strdup("nothing");

    case VAL_STRING:
        return strdup(v->string ? v->string : "");

    case VAL_ERROR: {
        /* Format: "ERR <message>" */
        size_t len = 4 + strlen(v->string ? v->string : "") + 1;
        char *buf = malloc(len);
        if (buf) {
            snprintf(buf, len, "ERR %s", v->string ? v->string : "");
        }
        return buf;
    }

    case VAL_FUNCTION: {
        /* Format: "<fn name>" for named functions, "<fn>" for anonymous. */
        if (v->function && v->function->name) {
            /* "<fn " + name + ">" + NUL */
            size_t len = 4 + strlen(v->function->name) + 1 + 1;
            char *buf = malloc(len);
            if (buf) {
                snprintf(buf, len, "<fn %s>", v->function->name);
            }
            return buf;
        }
        return strdup("<fn>");
    }

    default:
        return strdup("???");
    }
}

bool is_truthy(const Value *v) {
    switch (v->type) {
    case VAL_BOOL:
        return v->boolean;
    case VAL_NUMBER:
        return v->number != 0;
    case VAL_STRING:
        return v->string != NULL && v->string[0] != '\0';
    case VAL_FUNCTION:
        return true;
    case VAL_NOTHING: // NOLINT(bugprone-branch-clone)
        return false;
    case VAL_ERROR: // NOLINT(bugprone-branch-clone)
        return false;
    default:
        return false;
    }
}

/* Deep-copy an ObjFunction. Returns NULL on allocation failure. */
static ObjFunction *obj_function_clone(const ObjFunction *src) {
    if (!src)
        return NULL;
    ObjFunction *fn = calloc(1, sizeof(ObjFunction));
    if (!fn)
        return NULL;
    fn->name = src->name ? strdup(src->name) : NULL;
    if (src->name && !fn->name) {
        free(fn);
        return NULL;
    }
    fn->arity = src->arity;
    fn->native = src->native;
    /* Deep-copy parameter names. */
    if (src->arity > 0 && src->params) {
        fn->params = (char **)calloc((size_t)src->arity, sizeof(char *));
        if (!fn->params) {
            free(fn->name);
            free(fn);
            return NULL;
        }
        for (int i = 0; i < src->arity; i++) {
            fn->params[i] = strdup(src->params[i]);
            if (!fn->params[i]) {
                obj_function_free(fn);
                return NULL;
            }
        }
    }
    /* Deep-copy the compiled chunk. */
    if (src->chunk) {
        fn->chunk = malloc(sizeof(Chunk));
        if (!fn->chunk) {
            obj_function_free(fn);
            return NULL;
        }
        chunk_init(fn->chunk);
        /* Copy bytecode. */
        for (size_t i = 0; i < src->chunk->count; i++) {
            if (!chunk_write(fn->chunk, src->chunk->code[i], src->chunk->lines[i])) {
                obj_function_free(fn);
                return NULL;
            }
        }
        /* Copy constants. */
        for (size_t i = 0; i < src->chunk->const_count; i++) {
            Value cv;
            if (!value_clone(&cv, &src->chunk->constants[i])) {
                obj_function_free(fn);
                return NULL;
            }
            if (chunk_add_constant(fn->chunk, cv) < 0) {
                value_free(&cv);
                obj_function_free(fn);
                return NULL;
            }
        }
    }
    return fn;
}

bool value_clone(Value *out, const Value *src) {
    if (!out || !src)
        return false;
    *out = *src;
    if (src->type == VAL_STRING || src->type == VAL_ERROR) {
        const char *s = src->string ? src->string : "";
        out->string = strdup(s);
        if (!out->string)
            return false;
    } else {
        out->string = NULL;
    }
    if (src->type == VAL_FUNCTION) {
        out->function = obj_function_clone(src->function);
        if (src->function && !out->function)
            return false;
    } else {
        out->function = NULL;
    }
    return true;
}
