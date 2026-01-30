/*
 * parser.c - Cutlet parser implementation
 *
 * Parses a single-token expression from input using the tokenizer.
 */

#include "parser.h"
#include "tokenizer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool parser_parse_single(const char *input, AstNode **out, ParseError *err) {
    if (!input) {
        if (err) {
            err->line = 1;
            err->col = 1;
            snprintf(err->message, sizeof(err->message), "expected expression, got EOF");
        }
        return false;
    }

    Tokenizer *tok = tokenizer_create(input);
    if (!tok) {
        if (err) {
            err->line = 1;
            err->col = 1;
            snprintf(err->message, sizeof(err->message), "memory allocation failed");
        }
        return false;
    }

    Token t;
    tokenizer_next(tok, &t);

    if (t.type == TOK_ERROR) {
        if (err) {
            err->line = t.line;
            err->col = t.col;
            snprintf(err->message, sizeof(err->message), "%.*s", (int)t.value_len, t.value);
        }
        tokenizer_destroy(tok);
        return false;
    }

    if (t.type == TOK_EOF) {
        if (err) {
            err->line = 1;
            err->col = 1;
            snprintf(err->message, sizeof(err->message), "expected expression, got EOF");
        }
        tokenizer_destroy(tok);
        return false;
    }

    if (t.type == TOK_OPERATOR) {
        if (err) {
            err->line = t.line;
            err->col = t.col;
            snprintf(err->message, sizeof(err->message), "unexpected operator '%.*s'",
                     (int)t.value_len, t.value);
        }
        tokenizer_destroy(tok);
        return false;
    }

    /* Valid token: NUMBER, STRING, or IDENT */
    AstNode *node = malloc(sizeof(AstNode));
    if (!node) {
        if (err) {
            err->line = 1;
            err->col = 1;
            snprintf(err->message, sizeof(err->message), "memory allocation failed");
        }
        tokenizer_destroy(tok);
        return false;
    }

    switch (t.type) {
    case TOK_NUMBER:
        node->type = AST_NUMBER;
        break;
    case TOK_STRING:
        node->type = AST_STRING;
        break;
    case TOK_IDENT:
        node->type = AST_IDENT;
        break;
    default:
        free(node);
        tokenizer_destroy(tok);
        return false;
    }

    node->value = malloc(t.value_len + 1);
    if (!node->value) {
        free(node);
        if (err) {
            err->line = 1;
            err->col = 1;
            snprintf(err->message, sizeof(err->message), "memory allocation failed");
        }
        tokenizer_destroy(tok);
        return false;
    }
    memcpy(node->value, t.value, t.value_len);
    node->value[t.value_len] = '\0';

    /* Check for extra tokens */
    Token t2;
    tokenizer_next(tok, &t2);

    if (t2.type == TOK_ERROR) {
        if (err) {
            err->line = t2.line;
            err->col = t2.col;
            snprintf(err->message, sizeof(err->message), "%.*s", (int)t2.value_len, t2.value);
        }
        ast_free(node);
        tokenizer_destroy(tok);
        return false;
    }

    if (t2.type != TOK_EOF) {
        if (err) {
            err->line = t2.line;
            err->col = t2.col;
            snprintf(err->message, sizeof(err->message), "unexpected extra token");
        }
        ast_free(node);
        tokenizer_destroy(tok);
        return false;
    }

    tokenizer_destroy(tok);
    *out = node;
    return true;
}

void ast_free(AstNode *node) {
    if (!node)
        return;
    free(node->value);
    free(node);
}

const char *ast_node_type_str(AstNodeType type) {
    switch (type) {
    case AST_NUMBER:
        return "NUMBER";
    case AST_STRING:
        return "STRING";
    case AST_IDENT:
        return "IDENT";
    default:
        return "UNKNOWN";
    }
}

char *ast_format(const AstNode *node) {
    if (!node)
        return NULL;

    const char *type_str = ast_node_type_str(node->type);
    /* "AST [TYPE value]\0" = 5 + type + 1 + value + 1 + 1 */
    size_t len = 5 + strlen(type_str) + 1 + strlen(node->value) + 1 + 1;
    char *buf = malloc(len);
    if (!buf)
        return NULL;

    snprintf(buf, len, "AST [%s %s]", type_str, node->value);
    return buf;
}
