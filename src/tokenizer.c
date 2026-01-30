/*
 * tokenizer.c - Cutlet tokenizer implementation
 *
 * Implements the simplified tokenizer with Python/Ruby-style rules:
 * - NUMBER: integer literals (digits only, no negatives)
 * - STRING: double-quoted strings (no escapes, no adjacency error)
 * - IDENT: [A-Za-z_][A-Za-z0-9_]* (ASCII only, no symbol sandwiches)
 * - OPERATOR: one or more symbol chars (no whitespace delimiter required)
 *
 * Key rules:
 * - Symbol char: anything that is not whitespace, ASCII letter, digit, '_', or '"'
 * - Underscore is an ident-start and ident-continue character (not a symbol)
 * - Tokens may be adjacent without whitespace (Python/Ruby style)
 * - The only adjacency error: number followed by ident-start char [A-Za-z_]
 * - Errors and EOF are sticky
 */

#include "tokenizer.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Maximum size for error messages and token value buffer */
#define MAX_ERROR_MSG 256
#define INITIAL_VALUE_BUF 64

/* Tokenizer state */
struct Tokenizer {
    const char *input; /* Input string (not owned) */
    size_t input_len;  /* Length of input */
    size_t pos;        /* Current byte position (0-indexed) */
    size_t line;       /* Current line (1-indexed) */
    size_t col;        /* Current column (1-indexed) */

    /* Value buffer for token values */
    char *value_buf;
    size_t value_buf_size;

    /* Error message buffer */
    char error_msg[MAX_ERROR_MSG];

    /* Sticky state flags */
    bool at_eof;
    bool at_error;
};

/* ============================================================
 * Character classification helpers
 * ============================================================ */

static bool is_ascii_digit(char c) { return c >= '0' && c <= '9'; }

static bool is_ascii_letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

static bool is_whitespace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

/* Ident start: ASCII letter or underscore */
static bool is_ident_start(char c) { return is_ascii_letter(c) || c == '_'; }

/* Ident continue: ASCII letter, digit, or underscore */
static bool is_ident_continue(char c) {
    return is_ascii_letter(c) || is_ascii_digit(c) || c == '_';
}

/*
 * A symbol char is anything that is not whitespace, not ASCII letter,
 * not ASCII digit, not '_', and not '"'. This differs from the old tokenizer
 * in that underscore is no longer a symbol — it's part of identifiers.
 * Non-ASCII bytes are treated as symbol chars for now.
 */
static bool is_symbol_char(char c) {
    if (c == '\0')
        return false;
    if (is_whitespace(c))
        return false;
    if (is_ascii_letter(c))
        return false;
    if (is_ascii_digit(c))
        return false;
    if (c == '_')
        return false;
    if (c == '"')
        return false;
    return true;
}

/* ============================================================
 * Buffer management
 * ============================================================ */

static bool ensure_value_buf(Tokenizer *tok, size_t needed) {
    if (needed <= tok->value_buf_size) {
        return true;
    }

    size_t new_size = tok->value_buf_size * 2;
    if (new_size < needed) {
        new_size = needed;
    }

    char *new_buf = realloc(tok->value_buf, new_size);
    if (!new_buf) {
        return false;
    }

    tok->value_buf = new_buf;
    tok->value_buf_size = new_size;
    return true;
}

/* ============================================================
 * Tokenizer API implementation
 * ============================================================ */

Tokenizer *tokenizer_create(const char *input) {
    Tokenizer *tok = malloc(sizeof(Tokenizer));
    if (!tok) {
        return NULL;
    }

    tok->input = input ? input : "";
    tok->input_len = input ? strlen(input) : 0;
    tok->pos = 0;
    tok->line = 1;
    tok->col = 1;

    tok->value_buf = malloc(INITIAL_VALUE_BUF);
    if (!tok->value_buf) {
        free(tok);
        return NULL;
    }
    tok->value_buf_size = INITIAL_VALUE_BUF;

    tok->error_msg[0] = '\0';
    tok->at_eof = false;
    tok->at_error = false;

    return tok;
}

void tokenizer_destroy(Tokenizer *tok) {
    if (tok) {
        free(tok->value_buf);
        free(tok);
    }
}

/*
 * Skip whitespace and update position tracking.
 * Returns the number of whitespace characters skipped.
 */
static size_t skip_whitespace(Tokenizer *tok) {
    size_t start_pos = tok->pos;

    while (tok->pos < tok->input_len) {
        char c = tok->input[tok->pos];
        if (c == ' ' || c == '\t') {
            tok->pos++;
            tok->col++;
        } else if (c == '\n') {
            tok->pos++;
            tok->line++;
            tok->col = 1;
        } else if (c == '\r') {
            tok->pos++;
            /* Handle CRLF as single newline */
            if (tok->pos < tok->input_len && tok->input[tok->pos] == '\n') {
                tok->pos++;
            }
            tok->line++;
            tok->col = 1;
        } else {
            break;
        }
    }

    return tok->pos - start_pos;
}

/*
 * Set an error token with position info.
 */
static void set_error(Tokenizer *tok, Token *out, const char *msg, size_t error_pos,
                      size_t error_line, size_t error_col) {
    tok->at_error = true;

    out->type = TOK_ERROR;
    out->value = msg;
    out->value_len = strlen(msg);
    out->pos = error_pos;
    out->line = error_line;
    out->col = error_col;
}

/*
 * Read a number token (digits only).
 * Called when current char is a digit.
 *
 * After consuming digits, the only error is if the next char is an
 * ident-start character (letter or underscore). All other adjacency
 * (operator, string, EOF, whitespace) is allowed.
 */
static bool read_number(Tokenizer *tok, Token *out, size_t start_pos, size_t start_line,
                        size_t start_col) {
    size_t value_start = tok->pos;

    /* Consume digits */
    while (tok->pos < tok->input_len && is_ascii_digit(tok->input[tok->pos])) {
        tok->pos++;
        tok->col++;
    }

    size_t value_len = tok->pos - value_start;

    /* Error if next char is an ident-start character (letter or underscore) */
    if (tok->pos < tok->input_len && is_ident_start(tok->input[tok->pos])) {
        snprintf(tok->error_msg, MAX_ERROR_MSG, "number followed by identifier character");
        set_error(tok, out, tok->error_msg, start_pos, start_line, start_col);
        return true;
    }

    /* Copy value to buffer */
    if (!ensure_value_buf(tok, value_len + 1)) {
        snprintf(tok->error_msg, MAX_ERROR_MSG, "memory allocation failed");
        set_error(tok, out, tok->error_msg, start_pos, start_line, start_col);
        return true;
    }

    memcpy(tok->value_buf, tok->input + value_start, value_len);
    tok->value_buf[value_len] = '\0';

    out->type = TOK_NUMBER;
    out->value = tok->value_buf;
    out->value_len = value_len;
    out->pos = start_pos;
    out->line = start_line;
    out->col = start_col;

    return true;
}

/*
 * Read a string token (double-quoted, no escapes).
 * Called when current char is '"'.
 *
 * No adjacency error after the closing quote — any token may follow.
 */
static bool read_string(Tokenizer *tok, Token *out, size_t start_pos, size_t start_line,
                        size_t start_col) {
    /* Skip opening quote */
    tok->pos++;
    tok->col++;

    size_t content_start = tok->pos;

    /* Find closing quote */
    while (tok->pos < tok->input_len) {
        char c = tok->input[tok->pos];

        if (c == '"') {
            size_t content_len = tok->pos - content_start;

            /* Skip closing quote */
            tok->pos++;
            tok->col++;

            /* No adjacency check — any token may follow a string */

            /* Copy content to buffer */
            if (!ensure_value_buf(tok, content_len + 1)) {
                snprintf(tok->error_msg, MAX_ERROR_MSG, "memory allocation failed");
                set_error(tok, out, tok->error_msg, start_pos, start_line, start_col);
                return true;
            }

            memcpy(tok->value_buf, tok->input + content_start, content_len);
            tok->value_buf[content_len] = '\0';

            out->type = TOK_STRING;
            out->value = tok->value_buf;
            out->value_len = content_len;
            out->pos = start_pos;
            out->line = start_line;
            out->col = start_col;

            return true;
        }

        if (c == '\n' || c == '\r') {
            snprintf(tok->error_msg, MAX_ERROR_MSG, "unterminated string");
            set_error(tok, out, tok->error_msg, start_pos, start_line, start_col);
            return true;
        }

        tok->pos++;
        tok->col++;
    }

    /* Reached EOF without closing quote */
    snprintf(tok->error_msg, MAX_ERROR_MSG, "unterminated string");
    set_error(tok, out, tok->error_msg, start_pos, start_line, start_col);
    return true;
}

/*
 * Read an identifier token.
 * Called when current char is an ident-start char (letter or underscore).
 *
 * Simple rule: [A-Za-z_][A-Za-z0-9_]*
 * No symbol sandwich logic. Identifier ends when a non-ident-continue
 * character is encountered.
 */
static bool read_ident(Tokenizer *tok, Token *out, size_t start_pos, size_t start_line,
                       size_t start_col) {
    size_t value_start = tok->pos;

    /* Consume ident-start (already validated by caller) */
    tok->pos++;
    tok->col++;

    /* Consume ident-continue characters */
    while (tok->pos < tok->input_len && is_ident_continue(tok->input[tok->pos])) {
        tok->pos++;
        tok->col++;
    }

    size_t value_len = tok->pos - value_start;

    /* No adjacency check — any token may follow an identifier */

    /* Copy value to buffer */
    if (!ensure_value_buf(tok, value_len + 1)) {
        snprintf(tok->error_msg, MAX_ERROR_MSG, "memory allocation failed");
        set_error(tok, out, tok->error_msg, start_pos, start_line, start_col);
        return true;
    }

    memcpy(tok->value_buf, tok->input + value_start, value_len);
    tok->value_buf[value_len] = '\0';

    out->type = TOK_IDENT;
    out->value = tok->value_buf;
    out->value_len = value_len;
    out->pos = start_pos;
    out->line = start_line;
    out->col = start_col;

    return true;
}

/*
 * Read an operator token.
 * Called when current char is a symbol char.
 * Consumes a run of symbol chars. No whitespace delimiter required.
 */
static bool read_operator(Tokenizer *tok, Token *out, size_t start_pos, size_t start_line,
                          size_t start_col) {
    size_t value_start = tok->pos;

    /* Consume run of symbol chars */
    while (tok->pos < tok->input_len && is_symbol_char(tok->input[tok->pos])) {
        tok->pos++;
        tok->col++;
    }

    size_t value_len = tok->pos - value_start;

    /* No adjacency check — any token may follow an operator */

    /* Copy value to buffer */
    if (!ensure_value_buf(tok, value_len + 1)) {
        snprintf(tok->error_msg, MAX_ERROR_MSG, "memory allocation failed");
        set_error(tok, out, tok->error_msg, start_pos, start_line, start_col);
        return true;
    }

    memcpy(tok->value_buf, tok->input + value_start, value_len);
    tok->value_buf[value_len] = '\0';

    out->type = TOK_OPERATOR;
    out->value = tok->value_buf;
    out->value_len = value_len;
    out->pos = start_pos;
    out->line = start_line;
    out->col = start_col;

    return true;
}

bool tokenizer_next(Tokenizer *tok, Token *out) {
    /* Handle NULL inputs */
    if (!tok || !out) {
        return false;
    }

    /* Handle sticky error */
    if (tok->at_error) {
        out->type = TOK_ERROR;
        out->value = tok->error_msg;
        out->value_len = strlen(tok->error_msg);
        out->pos = tok->pos;
        out->line = tok->line;
        out->col = tok->col;
        return true;
    }

    /* Handle sticky EOF */
    if (tok->at_eof) {
        out->type = TOK_EOF;
        out->value = "";
        out->value_len = 0;
        out->pos = tok->pos;
        out->line = tok->line;
        out->col = tok->col;
        return true;
    }

    /* Skip whitespace */
    skip_whitespace(tok);

    /* Check for EOF */
    if (tok->pos >= tok->input_len) {
        tok->at_eof = true;
        out->type = TOK_EOF;
        out->value = "";
        out->value_len = 0;
        out->pos = tok->pos;
        out->line = tok->line;
        out->col = tok->col;
        return true;
    }

    /* Record position for this token */
    size_t token_start_pos = tok->pos;
    size_t token_start_line = tok->line;
    size_t token_start_col = tok->col;

    char c = tok->input[tok->pos];

    /* String */
    if (c == '"') {
        return read_string(tok, out, token_start_pos, token_start_line, token_start_col);
    }

    /* Number (digit) */
    if (is_ascii_digit(c)) {
        return read_number(tok, out, token_start_pos, token_start_line, token_start_col);
    }

    /* Identifier (letter or underscore) */
    if (is_ident_start(c)) {
        return read_ident(tok, out, token_start_pos, token_start_line, token_start_col);
    }

    /* Symbol char => operator (no whitespace requirement) */
    if (is_symbol_char(c)) {
        return read_operator(tok, out, token_start_pos, token_start_line, token_start_col);
    }

    /* Anything else (shouldn't normally reach here with ASCII input) */
    snprintf(tok->error_msg, MAX_ERROR_MSG, "invalid character");
    set_error(tok, out, tok->error_msg, token_start_pos, token_start_line, token_start_col);
    return true;
}

bool tokenizer_reset(Tokenizer *tok) {
    if (!tok) {
        return false;
    }

    tok->pos = 0;
    tok->line = 1;
    tok->col = 1;
    tok->at_eof = false;
    tok->at_error = false;
    tok->error_msg[0] = '\0';

    return true;
}

const char *token_type_str(TokenType type) {
    switch (type) {
    case TOK_NUMBER:
        return "NUMBER";
    case TOK_STRING:
        return "STRING";
    case TOK_IDENT:
        return "IDENT";
    case TOK_OPERATOR:
        return "OPERATOR";
    case TOK_EOF:
        return "EOF";
    case TOK_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}
