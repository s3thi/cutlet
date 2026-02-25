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

Value make_array(ObjArray *arr) { return (Value){.type = VAL_ARRAY, .array = arr}; }

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

/* ---- ObjArray utilities ---- */

ObjArray *obj_array_new(void) {
    ObjArray *arr = calloc(1, sizeof(ObjArray));
    if (!arr)
        return NULL;
    arr->refcount = 1;
    arr->data = NULL;
    arr->count = 0;
    arr->capacity = 0;
    return arr;
}

void obj_array_push(ObjArray *arr, Value v) {
    if (!arr)
        return;
    /* Grow backing array when full. */
    if (arr->count >= arr->capacity) {
        size_t new_cap = arr->capacity < 8 ? 8 : arr->capacity * 2;
        Value *new_data = realloc(arr->data, new_cap * sizeof(Value));
        if (!new_data)
            return; /* Silent failure on OOM — keeps existing data intact. */
        arr->data = new_data;
        arr->capacity = new_cap;
    }
    arr->data[arr->count++] = v; /* Takes ownership of v. */
}

ObjArray *obj_array_clone_deep(const ObjArray *src) {
    if (!src)
        return NULL;
    ObjArray *clone = obj_array_new();
    if (!clone)
        return NULL;
    if (src->count > 0) {
        clone->data = malloc(src->count * sizeof(Value));
        if (!clone->data) {
            free(clone);
            return NULL;
        }
        clone->capacity = src->count;
        clone->count = src->count;
        /* Deep-clone each element so strings etc. are independent copies. */
        for (size_t i = 0; i < src->count; i++) {
            value_clone(&clone->data[i], &src->data[i]);
        }
    }
    return clone;
}

void obj_array_ensure_owned(Value *v) {
    if (!v || v->type != VAL_ARRAY || !v->array)
        return;
    if (v->array->refcount <= 1)
        return; /* Already sole owner — no copy needed. */
    /* Detach: decrement the shared refcount and replace with a deep clone. */
    v->array->refcount--;
    v->array = obj_array_clone_deep(v->array);
}

/* Free an ObjArray's elements and backing storage (unconditionally). */
static void obj_array_free(ObjArray *arr) {
    if (!arr)
        return;
    for (size_t i = 0; i < arr->count; i++) {
        value_free(&arr->data[i]);
    }
    free(arr->data);
    free(arr);
}

/* ---- Value equality (used by map key lookup and VM equality ops) ---- */

bool value_equal(const Value *a, const Value *b) {
    if (a->type != b->type)
        return false;
    switch (a->type) {
    case VAL_NUMBER:
        return a->number == b->number;
    case VAL_STRING:
        return strcmp(a->string, b->string) == 0;
    case VAL_BOOL:
        return a->boolean == b->boolean;
    case VAL_NOTHING:
        return true;
    case VAL_FUNCTION:
        /* Identity-based: only equal if pointing to the same ObjFunction. */
        return a->function == b->function;
    case VAL_CLOSURE:
        /* Identity-based: only equal if pointing to the same ObjClosure. */
        return a->closure == b->closure;
    case VAL_ARRAY: {
        /* Structural equality: same length and all elements pairwise equal. */
        const ObjArray *aa = a->array;
        const ObjArray *ba = b->array;
        if (aa == ba)
            return true; /* Same backing store — trivially equal. */
        if (aa->count != ba->count)
            return false;
        for (size_t i = 0; i < aa->count; i++) {
            if (!value_equal(&aa->data[i], &ba->data[i]))
                return false;
        }
        return true;
    }
    case VAL_MAP: {
        /* Structural equality: same number of entries, and for every
         * key in map A, map B has the same key with an equal value.
         * Key order does not matter. */
        const ObjMap *ma = a->map;
        const ObjMap *mb = b->map;
        if (ma == mb)
            return true; /* Same backing store — trivially equal. */
        if (ma->count != mb->count)
            return false;
        for (size_t i = 0; i < ma->count; i++) {
            const Value *bval = obj_map_get(mb, &ma->entries[i].key);
            if (!bval || !value_equal(&ma->entries[i].value, bval))
                return false;
        }
        return true;
    }
    default:
        return false;
    }
}

/* ---- ObjMap utilities ---- */

ObjMap *obj_map_new(void) {
    ObjMap *m = calloc(1, sizeof(ObjMap));
    if (!m)
        return NULL;
    m->refcount = 1;
    m->entries = NULL;
    m->count = 0;
    m->capacity = 0;
    return m;
}

void obj_map_set(ObjMap *m, const Value *key, const Value *val) {
    if (!m)
        return;
    /* Check if key already exists — update in place. */
    for (size_t i = 0; i < m->count; i++) {
        if (value_equal(&m->entries[i].key, key)) {
            /* Replace existing value: free old, clone new. */
            value_free(&m->entries[i].value);
            value_clone(&m->entries[i].value, val);
            return;
        }
    }
    /* Key not found — insert at end. Grow if needed. */
    if (m->count >= m->capacity) {
        size_t new_cap = m->capacity < 8 ? 8 : m->capacity * 2;
        MapEntry *new_entries = realloc(m->entries, new_cap * sizeof(MapEntry));
        if (!new_entries)
            return; /* Silent failure on OOM. */
        m->entries = new_entries;
        m->capacity = new_cap;
    }
    value_clone(&m->entries[m->count].key, key);
    value_clone(&m->entries[m->count].value, val);
    m->count++;
}

Value *obj_map_get(const ObjMap *m, const Value *key) {
    if (!m)
        return NULL;
    for (size_t i = 0; i < m->count; i++) {
        if (value_equal(&m->entries[i].key, key))
            return &m->entries[i].value;
    }
    return NULL;
}

bool obj_map_has(const ObjMap *m, const Value *key) { return obj_map_get(m, key) != NULL; }

ObjMap *obj_map_clone_deep(const ObjMap *src) {
    if (!src)
        return NULL;
    ObjMap *clone = obj_map_new();
    if (!clone)
        return NULL;
    if (src->count > 0) {
        clone->entries = malloc(src->count * sizeof(MapEntry));
        if (!clone->entries) {
            free(clone);
            return NULL;
        }
        clone->capacity = src->count;
        clone->count = src->count;
        /* Deep-clone each key and value so strings etc. are independent. */
        for (size_t i = 0; i < src->count; i++) {
            value_clone(&clone->entries[i].key, &src->entries[i].key);
            value_clone(&clone->entries[i].value, &src->entries[i].value);
        }
    }
    return clone;
}

void obj_map_ensure_owned(Value *v) {
    if (!v || v->type != VAL_MAP || !v->map)
        return;
    if (v->map->refcount <= 1)
        return; /* Already sole owner — no copy needed. */
    /* Detach: decrement the shared refcount and replace with a deep clone. */
    v->map->refcount--;
    v->map = obj_map_clone_deep(v->map);
}

Value make_map(ObjMap *m) { return (Value){.type = VAL_MAP, .map = m}; }

/* Free an ObjMap's entries and backing storage (unconditionally). */
static void obj_map_free(ObjMap *m) {
    if (!m)
        return;
    for (size_t i = 0; i < m->count; i++) {
        value_free(&m->entries[i].key);
        value_free(&m->entries[i].value);
    }
    free(m->entries);
    free(m);
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
    if (v->type == VAL_ARRAY && v->array) {
        if (v->array->refcount > 0)
            v->array->refcount--;
        if (v->array->refcount == 0)
            obj_array_free(v->array);
        v->array = NULL;
    }
    if (v->type == VAL_MAP && v->map) {
        if (v->map->refcount > 0)
            v->map->refcount--;
        if (v->map->refcount == 0)
            obj_map_free(v->map);
        v->map = NULL;
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

    case VAL_MAP: {
        if (!v->map || v->map->count == 0)
            return strdup("{}");
        /*
         * Format as "{key1: value1, key2: value2}".
         * Build by formatting each key-value pair and concatenating.
         */
        size_t count = v->map->count;
        /* Allocate arrays for formatted keys and values. */
        char **key_parts = (char **)malloc(count * sizeof(char *));
        char **val_parts = (char **)malloc(count * sizeof(char *));
        if (!key_parts || !val_parts) {
            free((void *)key_parts);
            free((void *)val_parts);
            return strdup("{...}");
        }
        size_t total = 2; /* "{" and "}" */
        for (size_t i = 0; i < count; i++) {
            key_parts[i] = value_format(&v->map->entries[i].key);
            val_parts[i] = value_format(&v->map->entries[i].value);
            total += strlen(key_parts[i]) + 2 + strlen(val_parts[i]); /* "key: value" */
            if (i > 0)
                total += 2; /* ", " separator */
        }
        /* Assemble the string. */
        char *buf = malloc(total + 1);
        if (!buf) {
            for (size_t i = 0; i < count; i++) {
                free(key_parts[i]);
                free(val_parts[i]);
            }
            free((void *)key_parts);
            free((void *)val_parts);
            return strdup("{...}");
        }
        char *p = buf;
        *p++ = '{';
        for (size_t i = 0; i < count; i++) {
            if (i > 0) {
                *p++ = ',';
                *p++ = ' ';
            }
            size_t klen = strlen(key_parts[i]);
            memcpy(p, key_parts[i], klen);
            p += klen;
            *p++ = ':';
            *p++ = ' ';
            size_t vlen = strlen(val_parts[i]);
            memcpy(p, val_parts[i], vlen);
            p += vlen;
            free(key_parts[i]);
            free(val_parts[i]);
        }
        *p++ = '}';
        *p = '\0';
        free((void *)key_parts);
        free((void *)val_parts);
        return buf;
    }

    case VAL_ARRAY: {
        if (!v->array || v->array->count == 0)
            return strdup("[]");
        /*
         * Format as "[elem1, elem2, ...]".
         * Build by formatting each element and concatenating with ", ".
         */
        /* First pass: format all elements and compute total length. */
        size_t count = v->array->count;
        char **parts = (char **)malloc(count * sizeof(char *));
        if (!parts)
            return strdup("[...]");
        size_t total = 2; /* "[" and "]" */
        for (size_t i = 0; i < count; i++) {
            parts[i] = value_format(&v->array->data[i]);
            total += strlen(parts[i]);
            if (i > 0)
                total += 2; /* ", " separator */
        }
        /* Second pass: assemble the string. */
        char *buf = malloc(total + 1);
        if (!buf) {
            for (size_t i = 0; i < count; i++)
                free(parts[i]);
            free((void *)parts);
            return strdup("[...]");
        }
        char *p = buf;
        *p++ = '[';
        for (size_t i = 0; i < count; i++) {
            if (i > 0) {
                *p++ = ',';
                *p++ = ' ';
            }
            size_t slen = strlen(parts[i]);
            memcpy(p, parts[i], slen);
            p += slen;
            free(parts[i]);
        }
        *p++ = ']';
        *p = '\0';
        free((void *)parts);
        return buf;
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
    case VAL_ARRAY:
        return v->array != NULL && v->array->count > 0;
    case VAL_MAP:
        return v->map != NULL && v->map->count > 0;
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
    if (src->type == VAL_ARRAY) {
        /* Shared ownership via refcount — structural sharing (COW). */
        out->array = src->array;
        if (out->array)
            out->array->refcount++;
    } else {
        out->array = NULL;
    }
    if (src->type == VAL_MAP) {
        /* Shared ownership via refcount — structural sharing (COW). */
        out->map = src->map;
        if (out->map)
            out->map->refcount++;
    } else {
        out->map = NULL;
    }
    return true;
}
