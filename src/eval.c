/*
 * eval.c - Cutlet expression evaluator
 *
 * Evaluates an AST node tree produced by the Pratt parser.
 * All arithmetic is done in double precision. Division always
 * produces float results. Integers are formatted without decimals.
 */

#include "eval.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Helper: create a number Value.
 */
static Value make_number(double n) {
    return (Value){.type = VAL_NUMBER, .number = n, .string = NULL};
}

/*
 * Helper: create a string Value (takes ownership of s).
 */
static Value make_string(char *s) { return (Value){.type = VAL_STRING, .number = 0, .string = s}; }

/*
 * Helper: create an error Value with a formatted message.
 */
static Value make_error(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return (Value){.type = VAL_ERROR, .number = 0, .string = strdup(buf)};
}

Value eval(const AstNode *node) {
    if (!node) {
        return make_error("null AST node");
    }

    switch (node->type) {
    case AST_NUMBER: {
        /* Parse the number string to double */
        double n = strtod(node->value, NULL);
        return make_number(n);
    }

    case AST_STRING: {
        /* Return a copy of the string value */
        return make_string(strdup(node->value));
    }

    case AST_IDENT: {
        /* No variable bindings yet — all identifiers are errors */
        return make_error("unknown variable '%s'", node->value);
    }

    case AST_BINOP: {
        /* Evaluate both operands */
        Value left = eval(node->left);
        if (left.type == VAL_ERROR) {
            return left;
        }
        Value right = eval(node->right);
        if (right.type == VAL_ERROR) {
            value_free(&left);
            return right;
        }

        /* Both operands must be numbers for arithmetic */
        if (left.type != VAL_NUMBER || right.type != VAL_NUMBER) {
            value_free(&left);
            value_free(&right);
            return make_error("arithmetic requires numbers");
        }

        double result;
        const char *op = node->value;

        if (strcmp(op, "+") == 0) {
            result = left.number + right.number;
        } else if (strcmp(op, "-") == 0) {
            result = left.number - right.number;
        } else if (strcmp(op, "*") == 0) {
            result = left.number * right.number;
        } else if (strcmp(op, "/") == 0) {
            if (right.number == 0) {
                value_free(&left);
                value_free(&right);
                return make_error("division by zero");
            }
            result = left.number / right.number;
        } else if (strcmp(op, "**") == 0) {
            result = pow(left.number, right.number);
        } else {
            value_free(&left);
            value_free(&right);
            return make_error("unknown operator '%s'", op);
        }

        value_free(&left);
        value_free(&right);
        return make_number(result);
    }

    case AST_UNARY: {
        /* Evaluate the operand */
        Value operand = eval(node->left);
        if (operand.type == VAL_ERROR) {
            return operand;
        }
        if (operand.type != VAL_NUMBER) {
            value_free(&operand);
            return make_error("unary minus requires a number");
        }
        double result = -operand.number;
        value_free(&operand);
        return make_number(result);
    }

    default:
        return make_error("unknown AST node type");
    }
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
        if (v->number == (long long)v->number && fabs(v->number) < 1e15) {
            /* Integer value — print without decimal */
            snprintf(buf, sizeof(buf), "%lld", (long long)v->number);
        } else {
            /* Float value — use %g for minimal representation */
            snprintf(buf, sizeof(buf), "%g", v->number);
        }
        return strdup(buf);
    }

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
