/*
 * eval.c - Cutlet expression evaluator
 *
 * Evaluates an AST node tree produced by the Pratt parser.
 * All arithmetic is done in double precision. Division always
 * produces float results. Integers are formatted without decimals.
 */

#include "eval.h"
#include "runtime.h"
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
/*
 * Helper: create a boolean Value.
 */
static Value make_bool(bool b) {
    return (Value){.type = VAL_BOOL, .boolean = b, .number = 0, .string = NULL};
}

/*
 * Helper: determine if a Value is truthy.
 * Truthiness rules:
 * - false is falsy, true is truthy
 * - 0 is falsy, all other numbers are truthy
 * - empty string "" is falsy, all other strings are truthy
 * - errors are falsy
 */
static bool is_truthy(const Value *v) {
    switch (v->type) {
    case VAL_BOOL:
        return v->boolean;
    case VAL_NUMBER:
        return v->number != 0;
    case VAL_STRING:
        return v->string != NULL && v->string[0] != '\0';
    case VAL_ERROR: // NOLINT(bugprone-branch-clone)
        return false;
    default:
        return false;
    }
}

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

    case AST_BOOL: {
        /* Boolean literal: "true" or "false" */
        return make_bool(strcmp(node->value, "true") == 0);
    }

    case AST_IDENT: {
        /* Look up variable in the shared runtime environment. */
        Value out = {0};
        RuntimeVarStatus status = runtime_var_get(node->value, &out);
        if (status == RUNTIME_VAR_OK)
            return out;
        if (status == RUNTIME_VAR_NOT_FOUND)
            return make_error("unknown variable '%s'", node->value);
        return make_error("memory allocation failed");
    }

    case AST_BINOP: {
        const char *op = node->value;

        /* Short-circuit logical operators: and, or.
         * These return operand values (Python semantics), not necessarily bools.
         * "and" returns the first falsy operand, or the last operand.
         * "or" returns the first truthy operand, or the last operand. */
        if (strcmp(op, "and") == 0) {
            Value left = eval(node->left);
            if (left.type == VAL_ERROR)
                return left;
            if (!is_truthy(&left))
                return left; /* short-circuit: return first falsy */
            value_free(&left);
            return eval(node->right);
        }
        if (strcmp(op, "or") == 0) {
            Value left = eval(node->left);
            if (left.type == VAL_ERROR)
                return left;
            if (is_truthy(&left))
                return left; /* short-circuit: return first truthy */
            value_free(&left);
            return eval(node->right);
        }

        /* Non-short-circuit operators: evaluate both operands */
        Value left = eval(node->left);
        if (left.type == VAL_ERROR) {
            return left;
        }
        Value right = eval(node->right);
        if (right.type == VAL_ERROR) {
            value_free(&left);
            return right;
        }

        /* == and != work on any types (mixed types: == is false, != is true) */
        if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0) {
            bool equal = false;
            if (left.type == right.type) {
                if (left.type == VAL_NUMBER) {
                    equal = left.number == right.number;
                } else if (left.type == VAL_STRING) {
                    equal = strcmp(left.string, right.string) == 0;
                } else if (left.type == VAL_BOOL) {
                    equal = left.boolean == right.boolean;
                }
                /* VAL_ERROR and unknown types: equal stays false */
            }
            value_free(&left);
            value_free(&right);
            return make_bool(op[0] == '=' ? equal : !equal);
        }

        /* Ordered comparisons: <, >, <=, >= */
        if (strcmp(op, "<") == 0 || strcmp(op, ">") == 0 || strcmp(op, "<=") == 0 ||
            strcmp(op, ">=") == 0) {
            /* Ordered comparisons require same type, and not bools */
            if (left.type != right.type) {
                value_free(&left);
                value_free(&right);
                return make_error("ordered comparison requires same types");
            }
            if (left.type == VAL_BOOL) {
                value_free(&left);
                value_free(&right);
                return make_error("ordered comparison not supported for booleans");
            }

            int cmp;
            if (left.type == VAL_NUMBER) {
                cmp = (left.number > right.number) - (left.number < right.number);
            } else if (left.type == VAL_STRING) {
                cmp = strcmp(left.string, right.string);
            } else {
                value_free(&left);
                value_free(&right);
                return make_error("ordered comparison not supported for this type");
            }

            bool result;
            if (strcmp(op, "<") == 0)
                result = cmp < 0;
            else if (strcmp(op, ">") == 0)
                result = cmp > 0;
            else if (strcmp(op, "<=") == 0)
                result = cmp <= 0;
            else
                result = cmp >= 0;

            value_free(&left);
            value_free(&right);
            return make_bool(result);
        }

        /* Arithmetic operators require numbers */
        if (left.type != VAL_NUMBER || right.type != VAL_NUMBER) {
            value_free(&left);
            value_free(&right);
            return make_error("arithmetic requires numbers");
        }

        double result;

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

        /* "not" operator: always returns a VAL_BOOL based on truthiness */
        if (strcmp(node->value, "not") == 0) {
            bool truthy = is_truthy(&operand);
            value_free(&operand);
            return make_bool(!truthy);
        }

        /* Unary minus: requires a number */
        if (operand.type != VAL_NUMBER) {
            value_free(&operand);
            return make_error("unary minus requires a number");
        }
        double result = -operand.number;
        value_free(&operand);
        return make_number(result);
    }

    case AST_DECL: {
        /* Declare (or overwrite) a variable and return its value. */
        Value rhs = eval(node->left);
        if (rhs.type == VAL_ERROR)
            return rhs;
        RuntimeVarStatus status = runtime_var_define(node->value, &rhs);
        if (status != RUNTIME_VAR_OK) {
            value_free(&rhs);
            return make_error("memory allocation failed");
        }
        return rhs;
    }

    case AST_ASSIGN: {
        /* Assign to an existing variable; error if undefined. */
        Value existing = {0};
        RuntimeVarStatus exists = runtime_var_get(node->value, &existing);
        if (exists == RUNTIME_VAR_NOT_FOUND)
            return make_error("undefined variable '%s'", node->value);
        if (exists != RUNTIME_VAR_OK)
            return make_error("memory allocation failed");
        value_free(&existing);

        Value rhs = eval(node->left);
        if (rhs.type == VAL_ERROR)
            return rhs;

        RuntimeVarStatus status = runtime_var_assign(node->value, &rhs);
        if (status != RUNTIME_VAR_OK) {
            value_free(&rhs);
            if (status == RUNTIME_VAR_NOT_FOUND)
                return make_error("undefined variable '%s'", node->value);
            return make_error("memory allocation failed");
        }
        return rhs;
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
