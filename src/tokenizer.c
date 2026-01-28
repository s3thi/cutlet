/*
 * tokenizer.c - Cutlet tokenizer implementation
 *
 * Implements the v0 tokenizer with:
 * - NUMBER: integer literals (digits only, no negatives)
 * - STRING: double-quoted strings (no escapes)
 * - IDENT: ASCII letter start, letters/digits continue, symbol sandwiches
 *          between ASCII letters are part of the identifier
 * - OPERATOR: symbol chars delimited by whitespace (or SOI/EOI)
 *
 * Key rules:
 * - Symbol char: anything that is not whitespace, ASCII letter, digit, or '"'
 * - In an identifier, symbols can appear between letters (sandwich).
 *   After a symbol run, the next char must be an ASCII letter (not digit).
 *   Before a symbol run, the previous char must be an ASCII letter (not digit).
 * - Operators are symbol runs preceded by whitespace/SOI and followed by whitespace/EOI.
 * - Numbers must be followed by whitespace or EOF.
 * - Strings must be followed by whitespace or EOF.
 * - Errors and EOF are sticky.
 */

#include "tokenizer.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

/* Maximum size for error messages and token value buffer */
#define MAX_ERROR_MSG 256
#define INITIAL_VALUE_BUF 64

/* Tokenizer state */
struct Tokenizer {
    const char *input;      /* Input string (not owned) */
    size_t input_len;       /* Length of input */
    size_t pos;             /* Current byte position (0-indexed) */
    size_t line;            /* Current line (1-indexed) */
    size_t col;             /* Current column (1-indexed) */

    /* Value buffer for token values */
    char *value_buf;
    size_t value_buf_size;

    /* Error message buffer */
    char error_msg[MAX_ERROR_MSG];

    /* Sticky state flags */
    bool at_eof;
    bool at_error;

    /* Whether whitespace (or SOI) preceded the current token position.
     * Set by skip_whitespace or at start of input. */
    bool had_whitespace;
};

/* ============================================================
 * Character classification helpers
 * ============================================================ */

static bool is_ascii_digit(char c) {
    return c >= '0' && c <= '9';
}

static bool is_ascii_letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/*
 * A symbol char is anything that is not whitespace, not ASCII letter,
 * not ASCII digit, and not '"'. This includes _, -, +, *, /, @, #, etc.
 * We only consider ASCII bytes here; non-ASCII (high bit set) bytes are
 * also treated as symbol chars for now (they'll typically be part of
 * UTF-8 sequences which are not valid identifier starts).
 */
static bool is_symbol_char(char c) {
    if (is_whitespace(c)) return false;
    if (is_ascii_letter(c)) return false;
    if (is_ascii_digit(c)) return false;
    if (c == '"') return false;
    if (c == '\0') return false;
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
    tok->had_whitespace = true;  /* SOI counts as whitespace */

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
static void set_error(Tokenizer *tok, Token *out, const char *msg, size_t error_pos, size_t error_line, size_t error_col) {
    tok->at_error = true;

    out->type = TOK_ERROR;
    out->value = msg;
    out->value_len = strlen(msg);
    out->pos = error_pos;
    out->line = error_line;
    out->col = error_col;
}

/*
 * Read a number token (digits only, no negatives).
 * Called when current char is a digit.
 */
static bool read_number(Tokenizer *tok, Token *out, size_t start_pos, size_t start_line, size_t start_col) {
    size_t value_start = tok->pos;

    /* Consume digits */
    while (tok->pos < tok->input_len && is_ascii_digit(tok->input[tok->pos])) {
        tok->pos++;
        tok->col++;
    }

    size_t value_len = tok->pos - value_start;

    /* After consuming digits, next char must be whitespace or EOF */
    if (tok->pos < tok->input_len && !is_whitespace(tok->input[tok->pos])) {
        snprintf(tok->error_msg, MAX_ERROR_MSG, "adjacent tokens without whitespace");
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
 */
static bool read_string(Tokenizer *tok, Token *out, size_t start_pos, size_t start_line, size_t start_col) {
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

            /* After closing quote, next char must be whitespace or EOF */
            if (tok->pos < tok->input_len && !is_whitespace(tok->input[tok->pos])) {
                snprintf(tok->error_msg, MAX_ERROR_MSG, "adjacent tokens without whitespace");
                set_error(tok, out, tok->error_msg, start_pos, start_line, start_col);
                return true;
            }

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
 * Called when current char is an ASCII letter.
 *
 * Identifier structure:
 * - Starts with ASCII letter
 * - Continues with letters and digits freely
 * - When a symbol char is encountered:
 *   - Previous char must have been an ASCII letter (not digit)
 *   - Consume the entire run of symbol chars
 *   - Next char must be an ASCII letter (not digit, not ws, not EOF)
 *   - If either check fails, emit error
 */
static bool read_ident(Tokenizer *tok, Token *out, size_t start_pos, size_t start_line, size_t start_col) {
    size_t value_start = tok->pos;

    /* Track what kind of char we last consumed (letter or digit)
     * to enforce the sandwich rule */
    char prev_kind = 'L'; /* 'L' = letter, 'D' = digit */

    /* Consume the first letter (already validated by caller) */
    tok->pos++;
    tok->col++;

    while (tok->pos < tok->input_len) {
        char c = tok->input[tok->pos];

        if (is_ascii_letter(c)) {
            prev_kind = 'L';
            tok->pos++;
            tok->col++;
            continue;
        }

        if (is_ascii_digit(c)) {
            prev_kind = 'D';
            tok->pos++;
            tok->col++;
            continue;
        }

        if (c == '"') {
            /* Quote after ident without whitespace => error */
            snprintf(tok->error_msg, MAX_ERROR_MSG, "adjacent tokens without whitespace");
            set_error(tok, out, tok->error_msg, start_pos, start_line, start_col);
            return true;
        }

        if (is_symbol_char(c)) {
            /* Symbol sandwich: previous must be letter */
            if (prev_kind != 'L') {
                snprintf(tok->error_msg, MAX_ERROR_MSG, "symbol after digit in identifier");
                set_error(tok, out, tok->error_msg, start_pos, start_line, start_col);
                return true;
            }

            /* Consume the entire run of symbol chars */
            while (tok->pos < tok->input_len && is_symbol_char(tok->input[tok->pos])) {
                tok->pos++;
                tok->col++;
            }

            /* After symbol run, next char must be an ASCII letter */
            if (tok->pos >= tok->input_len || !is_ascii_letter(tok->input[tok->pos])) {
                snprintf(tok->error_msg, MAX_ERROR_MSG, "symbol not followed by letter in identifier");
                set_error(tok, out, tok->error_msg, start_pos, start_line, start_col);
                return true;
            }

            /* The next iteration will consume the letter */
            continue;
        }

        /* Whitespace or other boundary - end of identifier */
        break;
    }

    size_t value_len = tok->pos - value_start;

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
 * Called when current char is a symbol char preceded by whitespace/SOI.
 * Consumes a run of symbol chars. Next char must be whitespace or EOF.
 */
static bool read_operator(Tokenizer *tok, Token *out, size_t start_pos, size_t start_line, size_t start_col) {
    size_t value_start = tok->pos;

    /* Consume run of symbol chars */
    while (tok->pos < tok->input_len && is_symbol_char(tok->input[tok->pos])) {
        tok->pos++;
        tok->col++;
    }

    size_t value_len = tok->pos - value_start;

    /* After symbol run, next must be whitespace or EOF */
    if (tok->pos < tok->input_len && !is_whitespace(tok->input[tok->pos])) {
        snprintf(tok->error_msg, MAX_ERROR_MSG, "operator not followed by whitespace");
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

    /* Skip whitespace and record whether we skipped any */
    size_t ws_skipped = skip_whitespace(tok);
    bool preceded_by_whitespace = tok->had_whitespace || ws_skipped > 0;

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

    /* After producing a token, the next call won't have SOI whitespace */
    tok->had_whitespace = false;

    /* String */
    if (c == '"') {
        return read_string(tok, out, token_start_pos, token_start_line, token_start_col);
    }

    /* Number (digit) */
    if (is_ascii_digit(c)) {
        return read_number(tok, out, token_start_pos, token_start_line, token_start_col);
    }

    /* Identifier (ASCII letter) */
    if (is_ascii_letter(c)) {
        return read_ident(tok, out, token_start_pos, token_start_line, token_start_col);
    }

    /* Symbol char: could be operator (if preceded by whitespace) or error */
    if (is_symbol_char(c)) {
        if (preceded_by_whitespace) {
            return read_operator(tok, out, token_start_pos, token_start_line, token_start_col);
        } else {
            /* Symbol not preceded by whitespace and not part of ident */
            snprintf(tok->error_msg, MAX_ERROR_MSG, "unexpected symbol character");
            set_error(tok, out, tok->error_msg, token_start_pos, token_start_line, token_start_col);
            return true;
        }
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
    tok->had_whitespace = true;  /* SOI counts as whitespace */

    return true;
}

const char *token_type_str(TokenType type) {
    switch (type) {
        case TOK_NUMBER:   return "NUMBER";
        case TOK_STRING:   return "STRING";
        case TOK_IDENT:    return "IDENT";
        case TOK_OPERATOR: return "OPERATOR";
        case TOK_EOF:      return "EOF";
        case TOK_ERROR:    return "ERROR";
        default:           return "UNKNOWN";
    }
}
