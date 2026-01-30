/*
 * repl.c - Cutlet REPL core implementation
 *
 * Implements the core REPL formatting function that takes an input line
 * and returns a formatted result string.
 *
 * Output format:
 * - Success: OK [TYPE value] [TYPE value] ...
 * - Error: ERR line:col message
 * - Empty/whitespace input: OK
 */

#include "repl.h"
#include "parser.h"
#include "tokenizer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Initial buffer size for result string */
#define INITIAL_BUF_SIZE 256

/*
 * Dynamic string buffer for building results
 */
typedef struct {
    char *data;
    size_t len;
    size_t capacity;
} StringBuf;

/*
 * Initialize a string buffer.
 * Returns false on allocation failure.
 */
static bool strbuf_init(StringBuf *buf) {
    buf->data = malloc(INITIAL_BUF_SIZE);
    if (!buf->data)
        return false;
    buf->data[0] = '\0';
    buf->len = 0;
    buf->capacity = INITIAL_BUF_SIZE;
    return true;
}

/*
 * Ensure buffer has room for additional bytes.
 * Returns false on allocation failure.
 */
static bool strbuf_ensure(StringBuf *buf, size_t additional) {
    size_t needed = buf->len + additional + 1; /* +1 for null terminator */
    if (needed <= buf->capacity)
        return true;

    size_t new_cap = buf->capacity;
    while (new_cap < needed) {
        new_cap *= 2;
    }

    char *new_data = realloc(buf->data, new_cap);
    if (!new_data)
        return false;

    buf->data = new_data;
    buf->capacity = new_cap;
    return true;
}

/*
 * Append a string to the buffer.
 * Returns false on allocation failure.
 */
static bool strbuf_append(StringBuf *buf, const char *str) {
    size_t str_len = strlen(str);
    if (!strbuf_ensure(buf, str_len))
        return false;
    memcpy(buf->data + buf->len, str, str_len + 1);
    buf->len += str_len;
    return true;
}

/*
 * Append n bytes from a string to the buffer.
 * Returns false on allocation failure.
 */
static bool strbuf_append_n(StringBuf *buf, const char *str, size_t n) {
    if (!strbuf_ensure(buf, n))
        return false;
    memcpy(buf->data + buf->len, str, n);
    buf->len += n;
    buf->data[buf->len] = '\0';
    return true;
}

/*
 * Free the buffer's internal data.
 */
static void strbuf_free(StringBuf *buf) {
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->capacity = 0;
}

/*
 * Transfer ownership of buffer data to caller.
 * The buffer is reset to empty state.
 */
static char *strbuf_take(StringBuf *buf) {
    char *result = buf->data;
    buf->data = NULL;
    buf->len = 0;
    buf->capacity = 0;
    return result;
}

/*
 * Map token type to the format name used in output.
 * Returns uppercase type name for NUMBER, STRING, IDENT.
 */
static const char *format_type_name(TokenType type) {
    switch (type) {
    case TOK_NUMBER:
        return "NUMBER";
    case TOK_STRING:
        return "STRING";
    case TOK_IDENT:
        return "IDENT";
    case TOK_OPERATOR:
        return "OPERATOR";
    default:
        return "UNKNOWN";
    }
}

char *repl_format_line(const char *input) {
    /* Handle NULL input as empty string */
    if (input == NULL) {
        input = "";
    }

    Tokenizer *tok = tokenizer_create(input);
    if (!tok) {
        return NULL; /* Allocation failure */
    }

    StringBuf buf;
    if (!strbuf_init(&buf)) {
        tokenizer_destroy(tok);
        return NULL;
    }

    /* Start with "OK" - we'll replace if there's an error */
    if (!strbuf_append(&buf, "OK")) {
        strbuf_free(&buf);
        tokenizer_destroy(tok);
        return NULL;
    }

    Token t;
    bool first_token = true;

    while (tokenizer_next(tok, &t)) {
        if (t.type == TOK_EOF) {
            /* End of input - success */
            break;
        }

        if (t.type == TOK_ERROR) {
            /* Format error: ERR line:col message */
            strbuf_free(&buf);
            if (!strbuf_init(&buf)) {
                tokenizer_destroy(tok);
                return NULL;
            }

            /* Build error message with position */
            char pos_buf[64];
            snprintf(pos_buf, sizeof(pos_buf), "ERR %zu:%zu ", t.line, t.col);
            if (!strbuf_append(&buf, pos_buf)) {
                strbuf_free(&buf);
                tokenizer_destroy(tok);
                return NULL;
            }
            if (!strbuf_append_n(&buf, t.value, t.value_len)) {
                strbuf_free(&buf);
                tokenizer_destroy(tok);
                return NULL;
            }
            break;
        }

        /* Valid token: append " [TYPE value]" */
        first_token = false;
        (void)first_token; /* Suppress unused warning */

        if (!strbuf_append(&buf, " [")) {
            strbuf_free(&buf);
            tokenizer_destroy(tok);
            return NULL;
        }
        if (!strbuf_append(&buf, format_type_name(t.type))) {
            strbuf_free(&buf);
            tokenizer_destroy(tok);
            return NULL;
        }
        if (!strbuf_append(&buf, " ")) {
            strbuf_free(&buf);
            tokenizer_destroy(tok);
            return NULL;
        }
        if (!strbuf_append_n(&buf, t.value, t.value_len)) {
            strbuf_free(&buf);
            tokenizer_destroy(tok);
            return NULL;
        }
        if (!strbuf_append(&buf, "]")) {
            strbuf_free(&buf);
            tokenizer_destroy(tok);
            return NULL;
        }
    }

    tokenizer_destroy(tok);
    return strbuf_take(&buf);
}

char *repl_format_line_ast(const char *input) {
    /* Handle NULL as empty */
    if (input == NULL) {
        input = "";
    }

    /* Check for whitespace-only / empty input: return "AST" */
    const char *p = input;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        p++;
    }
    if (*p == '\0') {
        char *result = malloc(4);
        if (!result)
            return NULL;
        memcpy(result, "AST", 4);
        return result;
    }

    AstNode *node = NULL;
    ParseError err = {0};

    if (parser_parse_single(input, &node, &err)) {
        char *formatted = ast_format(node);
        ast_free(node);
        return formatted;
    }

    /* Format error: ERR line:col message */
    char buf[320];
    snprintf(buf, sizeof(buf), "ERR %zu:%zu %s", err.line, err.col, err.message);
    return strdup(buf);
}
