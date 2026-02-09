/*
 * value.c - Cutlet value types and utilities
 *
 * Implements the core Value type: constructors, formatting,
 * truthiness, cloning, and memory management.
 */

#include "value.h"
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

Value make_error(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return (Value){.type = VAL_ERROR, .number = 0, .string = strdup(buf)};
}

void value_free(Value *v) {
    if (v && v->string) {
        free(v->string);
        v->string = NULL;
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
    return true;
}
