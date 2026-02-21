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

Value make_function(ObjFunction *fn) {
    /* Ensure refcount starts at 1 for the owning Value. */
    if (fn)
        fn->refcount = 1;
    return (Value){.type = VAL_FUNCTION, .function = fn};
}

Value make_native(const char *name, int arity, NativeFn fn) {
    ObjFunction *obj = calloc(1, sizeof(ObjFunction));
    if (!obj)
        return make_error("memory allocation failed");
    obj->name = strdup(name);
    obj->arity = arity;
    obj->upvalue_count = 0;
    obj->params = NULL; /* Natives don't expose parameter names. */
    obj->chunk = NULL;  /* No compiled body — dispatch via native pointer. */
    obj->native = fn;
    /* refcount is set to 1 by make_function. */
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

Value make_closure(ObjClosure *cl) { return (Value){.type = VAL_CLOSURE, .closure = cl}; }

ObjUpvalue *obj_upvalue_new(Value *slot) {
    ObjUpvalue *uv = calloc(1, sizeof(ObjUpvalue));
    if (!uv)
        return NULL;
    uv->refcount = 1;
    uv->location = slot;
    /* closed is zero-initialized by calloc (VAL_NUMBER 0.0). */
    uv->next = NULL;
    return uv;
}

void obj_upvalue_free(ObjUpvalue *uv) {
    if (!uv)
        return;
    if (uv->refcount > 0)
        uv->refcount--;
    if (uv->refcount == 0) {
        /* If the upvalue is closed, free the captured value. */
        if (uv->location == &uv->closed)
            value_free(&uv->closed);
        free(uv);
    }
}

ObjClosure *obj_closure_new(ObjFunction *fn, int upvalue_count) {
    ObjClosure *cl = calloc(1, sizeof(ObjClosure));
    if (!cl)
        return NULL;
    cl->refcount = 1;
    cl->function = fn;
    cl->upvalue_count = upvalue_count;
    /* Increment the function's refcount — the closure holds a reference. */
    if (fn)
        fn->refcount++;
    /* Allocate upvalue pointer array (all NULL initially). */
    if (upvalue_count > 0) {
        cl->upvalues = (ObjUpvalue **)calloc((size_t)upvalue_count, sizeof(ObjUpvalue *));
        if (!cl->upvalues) {
            if (fn)
                fn->refcount--;
            free(cl);
            return NULL;
        }
    } else {
        cl->upvalues = NULL;
    }
    return cl;
}

void obj_closure_free(ObjClosure *cl) {
    if (!cl)
        return;
    if (cl->refcount > 0)
        cl->refcount--;
    if (cl->refcount == 0) {
        /* Release each upvalue reference. */
        for (int i = 0; i < cl->upvalue_count; i++) {
            if (cl->upvalues[i])
                obj_upvalue_free(cl->upvalues[i]);
        }
        free((void *)cl->upvalues);
        /* Decrement the function's refcount; free if last reference. */
        if (cl->function) {
            if (cl->function->refcount > 0)
                cl->function->refcount--;
            if (cl->function->refcount == 0)
                obj_function_free(cl->function);
        }
        free(cl);
    }
}

/* Free an ObjFunction and all its owned data (unconditionally). */
void obj_function_free(ObjFunction *fn) {
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
        /* Decrement refcount; only free when no references remain. */
        if (v->function->refcount > 0)
            v->function->refcount--;
        if (v->function->refcount == 0)
            obj_function_free(v->function);
        v->function = NULL;
    }
    if (v->type == VAL_CLOSURE && v->closure) {
        obj_closure_free(v->closure);
        v->closure = NULL;
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

    case VAL_CLOSURE: {
        /* Delegate to the underlying function's name. */
        ObjFunction *fn = v->closure ? v->closure->function : NULL;
        if (fn && fn->name) {
            size_t len = 4 + strlen(fn->name) + 1 + 1;
            char *buf = malloc(len);
            if (buf) {
                snprintf(buf, len, "<fn %s>", fn->name);
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
    case VAL_FUNCTION: // NOLINT(bugprone-branch-clone)
        return true;
    case VAL_CLOSURE:
        return true;
    case VAL_NOTHING: // NOLINT(bugprone-branch-clone)
        return false;
    case VAL_ERROR: // NOLINT(bugprone-branch-clone)
        return false;
    default:
        return false;
    }
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
        /* Shared ownership via refcount — no deep copy needed. */
        out->function = src->function;
        if (out->function)
            out->function->refcount++;
    } else {
        out->function = NULL;
    }
    if (src->type == VAL_CLOSURE) {
        /* Shared ownership via refcount — closures share upvalues. */
        out->closure = src->closure;
        if (out->closure)
            out->closure->refcount++;
    } else {
        out->closure = NULL;
    }
    return true;
}
