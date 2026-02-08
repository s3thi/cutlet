/*
 * repl.c - Cutlet REPL core implementation
 *
 * Primary API: repl_eval_line() — parses, evaluates, and optionally
 * produces debug token/AST output.
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
        if (*input == ' ' || *input == '\t' || *input == '\n' || *input == '\r') {
            input++;
        } else if (*input == '#') {
            /* Skip comment: # to end of line */
            input++;
            while (*input && *input != '\n' && *input != '\r') {
                input++;
            }
        } else {
            return false;
        }
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

ReplResult repl_eval_line(const char *input, bool want_tokens, bool want_ast, EvalContext *ctx) {
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

    /* Evaluate the expression using the caller-provided context.
     * Built-in functions like say() use ctx to emit output. */
    Value v = eval(node, ctx);
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
