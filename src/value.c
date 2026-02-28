/*
 * value.c - Cutlet value types and utilities
 *
 * Implements the core Value type: constructors, formatting,
 * truthiness, cloning, and memory management.
 */

#include "value.h"
#include "chunk.h"
#include "gc.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Value make_number(double n) { return (Value){.type = VAL_NUMBER, .number = n}; }

/*
 * Create a string Value. Takes ownership of the heap-allocated char* `s`.
 * Wraps it in a GC-tracked ObjString via obj_string_take().
 */
Value make_string(char *s) {
    size_t len = s ? strlen(s) : 0;
    ObjString *str = obj_string_take(s, len);
    return (Value){.type = VAL_STRING, .string = str};
}

/*
 * Create a string Value by copying `s` (len bytes).
 * The caller retains ownership of the original buffer.
 * If s is NULL, creates an ObjString wrapping an empty string.
 */
Value make_string_copy(const char *s, size_t len) {
    if (!s) {
        s = "";
        len = 0;
    }
    ObjString *str = obj_string_new(s, len);
    return (Value){.type = VAL_STRING, .string = str};
}

Value make_bool(bool b) { return (Value){.type = VAL_BOOL, .boolean = b}; }

Value make_nothing(void) { return (Value){.type = VAL_NOTHING}; }

Value make_function(ObjFunction *fn) { return (Value){.type = VAL_FUNCTION, .function = fn}; }

Value make_native(const char *name, int arity, NativeFn fn) {
    ObjFunction *obj = gc_alloc(OBJ_FUNCTION, sizeof(ObjFunction));
    if (!obj)
        return make_error("memory allocation failed");
    obj->name = strdup(name);
    obj->arity = arity;
    obj->upvalue_count = 0;
    obj->params = NULL; /* Natives don't expose parameter names. */
    obj->chunk = NULL;  /* No compiled body — dispatch via native pointer. */
    obj->native = fn;
    /* Wrap in a Value via make_function. */
    return make_function(obj);
}

Value make_error(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return (Value){.type = VAL_ERROR, .number = 0, .error = strdup(buf)};
}

Value make_closure(ObjClosure *cl) { return (Value){.type = VAL_CLOSURE, .closure = cl}; }

Value make_array(ObjArray *arr) { return (Value){.type = VAL_ARRAY, .array = arr}; }

ObjUpvalue *obj_upvalue_new(Value *slot) {
    ObjUpvalue *uv = gc_alloc(OBJ_UPVALUE, sizeof(ObjUpvalue));
    if (!uv)
        return NULL;
    uv->location = slot;
    /* closed is zero-initialized by calloc (VAL_NUMBER 0.0). */
    uv->next = NULL;
    return uv;
}

ObjClosure *obj_closure_new(ObjFunction *fn, int upvalue_count) {
    ObjClosure *cl = gc_alloc(OBJ_CLOSURE, sizeof(ObjClosure));
    if (!cl)
        return NULL;
    cl->function = fn;
    cl->upvalue_count = upvalue_count;
    /* Allocate upvalue pointer array (all NULL initially). */
    if (upvalue_count > 0) {
        cl->upvalues = (ObjUpvalue **)calloc((size_t)upvalue_count, sizeof(ObjUpvalue *));
        if (!cl->upvalues) {
            /* Unlink from GC object list before freeing (error path). */
            gc_unlink((Obj *)cl);
            free(cl);
            return NULL;
        }
    } else {
        cl->upvalues = NULL;
    }
    return cl;
}

/* ---- ObjString utilities ---- */

/*
 * Compute FNV-1a hash over `length` bytes of `data`.
 * Same algorithm as runtime.c's fnv1a(), but takes an explicit length
 * instead of relying on null termination. This allows hashing strings
 * that may contain embedded nulls in the future, and avoids depending
 * on the runtime module.
 */
static uint32_t fnv1a_hash(const char *data, size_t length) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < length; i++) {
        hash ^= (uint8_t)data[i];
        hash *= 16777619u;
    }
    return hash;
}

ObjString *obj_string_new(const char *chars, size_t length) {
    /* Compute hash first so we can check the intern table. */
    uint32_t hash = fnv1a_hash(chars, length);

    /* Check intern table: if an identical string already exists, return it
     * directly — no new allocation needed. This is the core deduplication
     * mechanism: identical content always maps to the same ObjString*. */
    ObjString *interned = gc_intern_find(chars, length, hash);
    if (interned != NULL)
        return interned;

    /* No match in the intern table — allocate a new ObjString. */
    ObjString *str = gc_alloc(OBJ_STRING, sizeof(ObjString));
    if (!str)
        return NULL;
    /* Allocate a new buffer and copy the input chars + null terminator. */
    char *buf = malloc(length + 1);
    if (!buf) {
        gc_free_object((Obj *)str);
        return NULL;
    }
    memcpy(buf, chars, length);
    buf[length] = '\0';
    str->chars = buf;
    str->length = length;
    str->hash = hash;

    /* Register in the intern table for future lookups. */
    gc_intern_add(str);
    return str;
}

ObjString *obj_string_take(char *chars, size_t length) {
    /* Compute hash on the input buffer so we can check the intern table. */
    uint32_t hash = fnv1a_hash(chars, length);

    /* Check the intern table: if an identical string exists, free the
     * caller's buffer (we don't need it) and return the interned one. */
    ObjString *interned = gc_intern_find(chars, length, hash);
    if (interned != NULL) {
        free(chars); /* Caller's buffer is redundant — discard it. */
        return interned;
    }

    /* No match — allocate a new ObjString that takes ownership of chars. */
    ObjString *str = gc_alloc(OBJ_STRING, sizeof(ObjString));
    if (!str)
        return NULL;
    /* Take ownership of the caller's buffer — no copy needed. */
    str->chars = chars;
    str->length = length;
    str->hash = hash;

    /* Register in the intern table for future lookups. */
    gc_intern_add(str);
    return str;
}

/* ---- ObjArray utilities ---- */

ObjArray *obj_array_new(void) {
    ObjArray *arr = gc_alloc(OBJ_ARRAY, sizeof(ObjArray));
    if (!arr)
        return NULL;
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
            /* Unlink from GC object list before freeing (error path). */
            gc_unlink((Obj *)clone);
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

/* ---- Value equality (used by map key lookup and VM equality ops) ----
 * For VAL_STRING: O(1) pointer comparison thanks to string interning.
 * Identical string content always maps to the same ObjString*. */

bool value_equal(const Value *a, const Value *b) {
    if (a->type != b->type)
        return false;
    switch (a->type) {
    case VAL_NUMBER:
        return a->number == b->number;
    case VAL_STRING:
        /* With string interning, identical content always maps to the same
         * ObjString*, so pointer comparison is O(1) and definitive.
         * This also handles the case where both are NULL (two null strings
         * are trivially equal). No hash/length/strcmp fallback is needed. */
        return a->string == b->string;
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
    case VAL_OBJECT_TYPE:
        /* Identity-based: only equal if pointing to the same ObjObjectType. */
        return a->object_type == b->object_type;
    case VAL_INSTANCE:
        /* Identity-based: only equal if pointing to the same ObjInstance.
         * Two different instances of the same type are NOT equal. */
        return a->instance == b->instance;
    default:
        return false;
    }
}

/* ---- ObjMap utilities ---- */

ObjMap *obj_map_new(void) {
    ObjMap *m = gc_alloc(OBJ_MAP, sizeof(ObjMap));
    if (!m)
        return NULL;
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
            /* Unlink from GC object list before freeing (error path). */
            gc_unlink((Obj *)clone);
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

Value make_map(ObjMap *m) { return (Value){.type = VAL_MAP, .map = m}; }

/* ---- ObjObjectType utilities ---- */

ObjObjectType *obj_object_type_new(const char *name) {
    ObjObjectType *t = malloc(sizeof(ObjObjectType));
    if (!t)
        return NULL;
    t->refcount = 1;
    t->name = strdup(name);
    t->methods = obj_map_new();
    if (!t->name || !t->methods) {
        /* Cleanup on partial allocation failure. */
        free(t->name);
        if (t->methods)
            gc_free_object((Obj *)t->methods);
        free(t);
        return NULL;
    }
    return t;
}

void obj_object_type_set_method(ObjObjectType *type, const char *name, Value method) {
    if (!type || !name)
        return;
    /* Build a string Value key from the method name. make_string() takes
     * ownership of the strdup'd buffer via obj_string_take(). */
    Value key = make_string(strdup(name));
    obj_map_set(type->methods, &key, &method);
    value_free(&key);
}

Value *obj_object_type_get_method(const ObjObjectType *type, const char *name) {
    if (!type || !name)
        return NULL;
    /* Build a string Value key for the lookup. */
    Value key = make_string(strdup(name));
    Value *result = obj_map_get(type->methods, &key);
    value_free(&key);
    return result;
}

Value make_object_type(ObjObjectType *t) {
    return (Value){.type = VAL_OBJECT_TYPE, .object_type = t};
}

ObjInstance *obj_instance_new(ObjObjectType *type) {
    if (!type)
        return NULL;
    ObjInstance *inst = malloc(sizeof(ObjInstance));
    if (!inst)
        return NULL;
    inst->refcount = 1;
    inst->type = type;
    /* The instance holds a reference to its type — increment refcount. */
    type->refcount++;
    inst->data = obj_map_new();
    if (!inst->data) {
        /* Cleanup on allocation failure: undo the refcount bump. */
        type->refcount--;
        free(inst);
        return NULL;
    }
    return inst;
}

Value make_instance(ObjInstance *inst) { return (Value){.type = VAL_INSTANCE, .instance = inst}; }

/*
 * Helper: free an ObjMap that is owned by a refcount-managed struct
 * (ObjObjectType or ObjInstance). Iterates all entries, frees keys
 * and values, frees the entries array, unlinks from GC, and frees
 * the ObjMap struct itself.
 */
static void free_owned_map(ObjMap *m) {
    if (!m)
        return;
    for (size_t i = 0; i < m->count; i++) {
        value_free(&m->entries[i].key);
        value_free(&m->entries[i].value);
    }
    free(m->entries);
    gc_free_object((Obj *)m);
}

/*
 * Helper: free an ObjObjectType's owned resources and the struct itself.
 * Does NOT decrement refcount — the caller must have already determined
 * that refcount has reached 0. Used by value_free for both VAL_OBJECT_TYPE
 * and VAL_INSTANCE (when the instance's type reference is the last one).
 */
static void free_object_type(ObjObjectType *t) {
    if (!t)
        return;
    free(t->name);
    free_owned_map(t->methods);
    free(t);
}

void value_free(Value *v) {
    if (!v)
        return;
    /* VAL_ERROR: free the heap-allocated error message. */
    if (v->type == VAL_ERROR && v->error) {
        free(v->error);
        v->error = NULL;
    }
    /* VAL_OBJECT_TYPE: decrement refcount; free if it reaches zero. */
    if (v->type == VAL_OBJECT_TYPE && v->object_type) {
        v->object_type->refcount--;
        if (v->object_type->refcount == 0) {
            free_object_type(v->object_type);
        }
        v->object_type = NULL;
    }
    /* VAL_INSTANCE: decrement refcount; free if it reaches zero. */
    if (v->type == VAL_INSTANCE && v->instance) {
        v->instance->refcount--;
        if (v->instance->refcount == 0) {
            /* Free the instance's data map. */
            free_owned_map(v->instance->data);
            /* Release the type reference; free the type if last ref. */
            v->instance->type->refcount--;
            if (v->instance->type->refcount == 0) {
                free_object_type(v->instance->type);
            }
            free(v->instance);
        }
        v->instance = NULL;
    }
    /* GC-managed types: null out the pointer. The GC handles freeing.
     * We null the pointer to prevent dangling references in debug builds. */
    v->function = NULL;
    v->closure = NULL;
    v->array = NULL;
    v->map = NULL;
    v->string = NULL;
    v->object_type = NULL;
    v->instance = NULL;
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
        /* Access chars through the ObjString wrapper. */
        return strdup(v->string ? v->string->chars : "");

    case VAL_ERROR: {
        /* Format: "ERR <message>". Error message is a plain char*. */
        size_t len = 4 + strlen(v->error ? v->error : "") + 1;
        char *buf = malloc(len);
        if (buf) {
            snprintf(buf, len, "ERR %s", v->error ? v->error : "");
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

    case VAL_OBJECT_TYPE: {
        /* Format: "<object Name>" */
        const char *name = v->object_type ? v->object_type->name : "?";
        /* "<object " + name + ">" + NUL */
        size_t len = 8 + strlen(name) + 1 + 1;
        char *buf = malloc(len);
        if (buf)
            snprintf(buf, len, "<object %s>", name);
        return buf;
    }

    case VAL_INSTANCE: {
        /* Format: "<Name instance>" */
        const char *name = (v->instance && v->instance->type) ? v->instance->type->name : "?";
        /* "<" + name + " instance>" + NUL */
        size_t len = 1 + strlen(name) + 10 + 1;
        char *buf = malloc(len);
        if (buf)
            snprintf(buf, len, "<%s instance>", name);
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
        return v->string != NULL && v->string->length > 0;
    case VAL_FUNCTION: // NOLINT(bugprone-branch-clone)
        return true;
    case VAL_CLOSURE:
        return true;
    case VAL_ARRAY:
        return v->array != NULL && v->array->count > 0;
    case VAL_MAP:
        return v->map != NULL && v->map->count > 0;
    case VAL_OBJECT_TYPE: // NOLINT(bugprone-branch-clone)
        return true;
    case VAL_INSTANCE: // NOLINT(bugprone-branch-clone)
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
    /* For VAL_ERROR, deep-copy the error message (not GC-managed). */
    if (src->type == VAL_ERROR) {
        out->error = strdup(src->error ? src->error : "");
        if (!out->error)
            return false;
    }
    /* VAL_OBJECT_TYPE: increment refcount (shared ownership). */
    if (src->type == VAL_OBJECT_TYPE && out->object_type) {
        out->object_type->refcount++;
    }
    /* VAL_INSTANCE: increment refcount (shared ownership). */
    if (src->type == VAL_INSTANCE && out->instance) {
        out->instance->refcount++;
    }
    /* All other types: shallow copy is sufficient.
     * GC-managed pointers (function, closure, array, map, string)
     * are shared references — the GC keeps them alive. */
    return true;
}
