/*
 * repl.c - Cutlet REPL core implementation
 *
 * Primary API: repl_eval_line() — parses, evaluates, and optionally
 * produces debug token/AST output.
 *
 * Legacy wrappers repl_format_line() and repl_format_line_ast() are
 * kept for backward compatibility (used by existing tests).
 */

#include "repl.h"
#include "eval.h"
#include "parser.h"
#include "runtime.h"
#include "tokenizer.h"
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

/*
 * Build a token debug string from the input.
 * Format: "TOKENS [TYPE value] [TYPE value] ..."
 * On tokenizer error: appends "ERR line:col message".
 * Returns a heap-allocated string.
 */
static char *build_tokens_string(const char *input) {
    Tokenizer *tok = tokenizer_create(input);
    if (!tok)
        return NULL;

    /* Start with "TOKENS" prefix. */
    size_t cap = 256;
    size_t len = 0;
    char *buf = malloc(cap);
    if (!buf) {
        tokenizer_destroy(tok);
        return NULL;
    }
    len = (size_t)sprintf(buf, "TOKENS");

    Token t;
    while (tokenizer_next(tok, &t)) {
        if (t.type == TOK_EOF)
            break;

        /* Ensure enough room. */
        size_t needed = len + 32 + t.value_len;
        if (needed > cap) {
            cap = needed * 2;
            char *tmp = realloc(buf, cap);
            if (!tmp) {
                free(buf);
                tokenizer_destroy(tok);
                return NULL;
            }
            buf = tmp;
        }

        if (t.type == TOK_ERROR) {
            /* Append error info. len not read after break, but kept for clarity. */
            (void)sprintf(buf + len, " ERR %zu:%zu %.*s", t.line, t.col, (int)t.value_len, t.value);
            break;
        }

        len += (size_t)sprintf(buf + len, " [%s %.*s]", token_type_str(t.type), (int)t.value_len,
                               t.value);
    }

    tokenizer_destroy(tok);
    return buf;
}

ReplResult repl_eval_line(const char *input, bool want_tokens, bool want_ast) {
    /* Serialize evaluation across threads. */
    runtime_eval_lock();

    ReplResult r = {0};

    /* Handle NULL/empty/whitespace input. */
    if (is_blank(input)) {
        r.ok = true;
        /* value stays NULL for blank input. */
        /* Still produce tokens/ast if requested (they'll be just the prefix). */
        if (want_tokens)
            r.tokens = strdup("TOKENS");
        if (want_ast)
            r.ast = strdup("AST");
        goto out;
    }

    /* Build token debug string before parsing (best-effort). */
    if (want_tokens) {
        r.tokens = build_tokens_string(input);
    }

    /* Parse the expression. */
    AstNode *node = NULL;
    ParseError perr;
    if (!parser_parse(input, &node, &perr)) {
        /* Parse error. */
        r.ok = false;
        char buf[320];
        snprintf(buf, sizeof(buf), "%zu:%zu %s", perr.line, perr.col, perr.message);
        r.error = strdup(buf);
        /* AST not available on parse error. */
        goto out;
    }

    /* Build AST debug string if requested. */
    if (want_ast) {
        r.ast = ast_format(node);
    }

    /* Evaluate the expression. */
    Value v = eval(node);
    ast_free(node);

    if (v.type == VAL_ERROR) {
        r.ok = false;
        /* value_format for errors returns "ERR <message>"; strip "ERR " prefix. */
        char *msg = value_format(&v);
        if (msg && strncmp(msg, "ERR ", 4) == 0) {
            r.error = strdup(msg + 4);
            free(msg);
        } else {
            r.error = msg;
        }
        value_free(&v);
        goto out;
    }

    /* Success: format plain value. */
    r.ok = true;
    r.value = value_format(&v);
    value_free(&v);

out:
    runtime_eval_unlock();
    return r;
}

void repl_result_free(ReplResult *r) {
    if (!r)
        return;
    free(r->value);
    free(r->error);
    free(r->tokens);
    free(r->ast);
    r->value = NULL;
    r->error = NULL;
    r->tokens = NULL;
    r->ast = NULL;
}

/* ============================================================
 * Legacy API wrappers
 * ============================================================ */

char *repl_format_line(const char *input) {
    ReplResult r = repl_eval_line(input, false, false);

    char *result = NULL;

    if (!r.ok && r.error) {
        /* Format: "ERR <error>" */
        size_t len = 4 + strlen(r.error) + 1;
        result = malloc(len);
        if (result) {
            snprintf(result, len, "ERR %s", r.error);
        }
    } else if (!r.ok) {
        result = strdup("ERR unknown error");
    } else if (r.value == NULL) {
        /* Blank input. */
        result = strdup("OK");
    } else {
        /* Determine type from value string. */
        const char *type_str;
        const char *v = r.value;
        bool is_num = false;
        bool is_bool = (strcmp(v, "true") == 0 || strcmp(v, "false") == 0);
        if (*v == '-')
            v++;
        if (*v >= '0' && *v <= '9')
            is_num = true;

        type_str = is_bool ? "BOOL" : is_num ? "NUMBER" : "STRING";
        size_t len = 4 + strlen(type_str) + 1 + strlen(r.value) + 1 + 1;
        result = malloc(len);
        if (result) {
            snprintf(result, len, "OK [%s %s]", type_str, r.value);
        }
    }

    repl_result_free(&r);
    return result;
}

char *repl_format_line_ast(const char *input) {
    /* AST mode: parse only, no evaluation. This preserves the legacy
     * behavior where identifiers produce AST output rather than eval errors. */
    runtime_eval_lock();

    char *result = NULL;

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

    char buf[320];
    snprintf(buf, sizeof(buf), "ERR %zu:%zu %s", err.line, err.col, err.message);
    result = strdup(buf);

out:
    runtime_eval_unlock();
    return result;
}
