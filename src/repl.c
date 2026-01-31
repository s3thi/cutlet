/*
 * repl.c - Cutlet REPL core implementation
 *
 * Token mode (repl_format_line): parse expression → eval → format result.
 *   - Success: "OK [TYPE value]" (e.g., "OK [NUMBER 42]", "OK [STRING hello]")
 *   - Error: "ERR message"
 *   - Empty: "OK"
 *
 * AST mode (repl_format_line_ast): parse expression → format AST tree.
 *   - Success: "AST [TYPE ...]" (nested S-expression)
 *   - Error: "ERR line:col message"
 *   - Empty: "AST"
 */

#include "repl.h"
#include "eval.h"
#include "parser.h"
#include "runtime.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Check if input is empty or whitespace-only.
 */
static bool is_blank(const char *input) {
    if (!input)
        return true;
    while (*input) {
        if (*input != ' ' && *input != '\t' && *input != '\n' && *input != '\r')
            return false;
        input++;
    }
    return true;
}

char *repl_format_line(const char *input) {
    /* Serialize evaluation across threads. */
    runtime_eval_lock();

    char *result = NULL;

    /* Handle NULL/empty/whitespace input */
    if (is_blank(input)) {
        result = strdup("OK");
        goto out;
    }

    /* Parse the expression */
    AstNode *node = NULL;
    ParseError perr;
    if (!parser_parse(input, &node, &perr)) {
        /* Format parse error: "ERR line:col message" */
        char buf[320];
        snprintf(buf, sizeof(buf), "ERR %zu:%zu %s", perr.line, perr.col, perr.message);
        result = strdup(buf);
        goto out;
    }

    /* Evaluate the expression */
    Value v = eval(node);
    ast_free(node);

    if (v.type == VAL_ERROR) {
        /* Format eval error: "ERR message" */
        char *msg = value_format(&v);
        value_free(&v);
        result = msg; /* value_format for errors returns "ERR ..." */
        goto out;
    }

    /* Format success: "OK [TYPE value]" */
    char *val_str = value_format(&v);
    if (!val_str) {
        value_free(&v);
        goto out;
    }

    const char *type_str;
    if (v.type == VAL_NUMBER)
        type_str = "NUMBER";
    else
        type_str = "STRING";

    /* "OK [TYPE value]\0" */
    size_t len = 4 + strlen(type_str) + 1 + strlen(val_str) + 1 + 1;
    result = malloc(len);
    if (result) {
        snprintf(result, len, "OK [%s %s]", type_str, val_str);
    }

    free(val_str);
    value_free(&v);

out:
    runtime_eval_unlock();
    return result;
}

char *repl_format_line_ast(const char *input) {
    /* Serialize evaluation across threads. */
    runtime_eval_lock();

    char *result = NULL;

    /* Handle empty/whitespace input: return "AST" */
    if (is_blank(input)) {
        result = strdup("AST");
        goto out;
    }

    AstNode *node = NULL;
    ParseError err = {0};

    if (parser_parse(input, &node, &err)) {
        result = ast_format(node);
        ast_free(node);
        goto out;
    }

    /* Format error: ERR line:col message */
    char buf[320];
    snprintf(buf, sizeof(buf), "ERR %zu:%zu %s", err.line, err.col, err.message);
    result = strdup(buf);

out:
    runtime_eval_unlock();
    return result;
}
