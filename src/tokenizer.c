/*
 * tokenizer.c - Cutlet tokenizer implementation
 *
 * Implements a minimal v0 tokenizer with:
 * - NUMBER: integer literals (positive, negative, zero)
 * - STRING: double-quoted strings (no escapes)
 * - IDENT: identifiers with Unicode support and kebab-case
 *
 * Key rules (per PLAN.md):
 * - Tokens must be separated by whitespace or EOF
 * - Adjacent tokens without whitespace are errors
 * - '-' is part of a number only when immediately followed by a digit
 * - '-' in the middle of an identifier is valid (kebab-case)
 * - Errors and EOF are sticky (return same on subsequent calls)
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

    /* Last token info for adjacent token detection */
    TokenType last_token_type;
    size_t last_token_end_pos;  /* Position right after last token */
};

/* ============================================================
 * UTF-8 decoding helpers
 * ============================================================ */

/*
 * Decode a UTF-8 codepoint from the input at the given position.
 * Returns the codepoint value and sets *bytes_consumed to the number
 * of bytes used. Returns 0xFFFFFFFF on invalid UTF-8.
 */
static uint32_t utf8_decode(const char *s, size_t len, size_t pos, size_t *bytes_consumed) {
    if (pos >= len) {
        *bytes_consumed = 0;
        return 0xFFFFFFFF;
    }

    unsigned char c = (unsigned char)s[pos];

    /* ASCII */
    if (c < 0x80) {
        *bytes_consumed = 1;
        return c;
    }

    /* 2-byte sequence: 110xxxxx 10xxxxxx */
    if ((c & 0xE0) == 0xC0) {
        if (pos + 1 >= len) {
            *bytes_consumed = 1;
            return 0xFFFFFFFF;
        }
        unsigned char c2 = (unsigned char)s[pos + 1];
        if ((c2 & 0xC0) != 0x80) {
            *bytes_consumed = 1;
            return 0xFFFFFFFF;
        }
        *bytes_consumed = 2;
        return ((uint32_t)(c & 0x1F) << 6) | (c2 & 0x3F);
    }

    /* 3-byte sequence: 1110xxxx 10xxxxxx 10xxxxxx */
    if ((c & 0xF0) == 0xE0) {
        if (pos + 2 >= len) {
            *bytes_consumed = 1;
            return 0xFFFFFFFF;
        }
        unsigned char c2 = (unsigned char)s[pos + 1];
        unsigned char c3 = (unsigned char)s[pos + 2];
        if ((c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) {
            *bytes_consumed = 1;
            return 0xFFFFFFFF;
        }
        *bytes_consumed = 3;
        return ((uint32_t)(c & 0x0F) << 12) | ((uint32_t)(c2 & 0x3F) << 6) | (c3 & 0x3F);
    }

    /* 4-byte sequence: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
    if ((c & 0xF8) == 0xF0) {
        if (pos + 3 >= len) {
            *bytes_consumed = 1;
            return 0xFFFFFFFF;
        }
        unsigned char c2 = (unsigned char)s[pos + 1];
        unsigned char c3 = (unsigned char)s[pos + 2];
        unsigned char c4 = (unsigned char)s[pos + 3];
        if ((c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80 || (c4 & 0xC0) != 0x80) {
            *bytes_consumed = 1;
            return 0xFFFFFFFF;
        }
        *bytes_consumed = 4;
        return ((uint32_t)(c & 0x07) << 18) | ((uint32_t)(c2 & 0x3F) << 12) |
               ((uint32_t)(c3 & 0x3F) << 6) | (c4 & 0x3F);
    }

    /* Invalid leading byte */
    *bytes_consumed = 1;
    return 0xFFFFFFFF;
}

/* ============================================================
 * Character classification helpers
 * ============================================================ */

static bool is_ascii_digit(uint32_t cp) {
    return cp >= '0' && cp <= '9';
}

static bool is_ascii_letter(uint32_t cp) {
    return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z');
}

/*
 * Check if a codepoint is a valid identifier start character.
 * Per PLAN.md: any Unicode letter/mark/connector or '_'
 *
 * We use a simplified check:
 * - ASCII letters (a-z, A-Z)
 * - Underscore (_)
 * - Unicode letters (Latin Extended, Devanagari, Gurmukhi, Cyrillic, Greek, etc.)
 *
 * This is a simplified Unicode letter check that covers common scripts.
 */
static bool is_ident_start(uint32_t cp) {
    /* ASCII letter or underscore */
    if (is_ascii_letter(cp) || cp == '_') {
        return true;
    }

    /* Extended Latin (Latin-1 Supplement, Latin Extended-A, Latin Extended-B) */
    if (cp >= 0x00C0 && cp <= 0x024F) {
        /* Skip non-letters in this range */
        if (cp == 0x00D7 || cp == 0x00F7) return false;  /* multiplication/division */
        return true;
    }

    /* Greek and Coptic */
    if (cp >= 0x0370 && cp <= 0x03FF) {
        return true;
    }

    /* Cyrillic */
    if (cp >= 0x0400 && cp <= 0x04FF) {
        return true;
    }

    /* Devanagari */
    if (cp >= 0x0900 && cp <= 0x097F) {
        return true;
    }

    /* Gurmukhi */
    if (cp >= 0x0A00 && cp <= 0x0A7F) {
        return true;
    }

    /* Bengali */
    if (cp >= 0x0980 && cp <= 0x09FF) {
        return true;
    }

    /* Tamil */
    if (cp >= 0x0B80 && cp <= 0x0BFF) {
        return true;
    }

    /* Telugu */
    if (cp >= 0x0C00 && cp <= 0x0C7F) {
        return true;
    }

    /* Kannada */
    if (cp >= 0x0C80 && cp <= 0x0CFF) {
        return true;
    }

    /* Malayalam */
    if (cp >= 0x0D00 && cp <= 0x0D7F) {
        return true;
    }

    /* Thai */
    if (cp >= 0x0E00 && cp <= 0x0E7F) {
        return true;
    }

    /* Arabic */
    if (cp >= 0x0600 && cp <= 0x06FF) {
        return true;
    }

    /* Hebrew */
    if (cp >= 0x0590 && cp <= 0x05FF) {
        return true;
    }

    /* CJK Unified Ideographs (common subset) */
    if (cp >= 0x4E00 && cp <= 0x9FFF) {
        return true;
    }

    /* Hiragana */
    if (cp >= 0x3040 && cp <= 0x309F) {
        return true;
    }

    /* Katakana */
    if (cp >= 0x30A0 && cp <= 0x30FF) {
        return true;
    }

    /* Hangul Syllables */
    if (cp >= 0xAC00 && cp <= 0xD7AF) {
        return true;
    }

    return false;
}

/*
 * Check if a codepoint is a valid identifier continue character.
 * Per PLAN.md: start chars + digits + '-' (for kebab-case)
 */
static bool is_ident_continue(uint32_t cp) {
    return is_ident_start(cp) || is_ascii_digit(cp) || cp == '-';
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
    tok->last_token_type = TOK_EOF;  /* No previous token */
    tok->last_token_end_pos = 0;

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
static void set_error(Tokenizer *tok, Token *out, const char *msg, size_t error_pos) {
    tok->at_error = true;

    /* Store error position info relative to where the error occurred */
    out->type = TOK_ERROR;
    out->value = msg;
    out->value_len = strlen(msg);
    out->pos = error_pos;
    /* Note: line and col were captured before attempting to tokenize */
}

/*
 * Read a number token (integers, possibly negative).
 * Called when we've seen a digit or '-' followed by digit.
 */
static bool read_number(Tokenizer *tok, Token *out, size_t start_pos, size_t start_line, size_t start_col, bool negative) {
    size_t value_start = tok->pos;

    if (negative) {
        /* Already validated that '-' is followed by digit, include it */
        value_start = tok->pos - 1;
    }

    /* Consume digits */
    while (tok->pos < tok->input_len && is_ascii_digit((unsigned char)tok->input[tok->pos])) {
        tok->pos++;
        tok->col++;
    }

    size_t value_len = tok->pos - value_start;

    /* Check for adjacent token without whitespace */
    if (tok->pos < tok->input_len) {
        size_t bytes;
        uint32_t cp = utf8_decode(tok->input, tok->input_len, tok->pos, &bytes);

        /* Number followed by identifier start or quote is an error */
        if (is_ident_start(cp) || cp == '"') {
            snprintf(tok->error_msg, MAX_ERROR_MSG, "adjacent tokens without whitespace");
            out->line = start_line;
            out->col = start_col;
            set_error(tok, out, tok->error_msg, start_pos);
            return true;
        }
    }

    /* Copy value to buffer */
    if (!ensure_value_buf(tok, value_len + 1)) {
        snprintf(tok->error_msg, MAX_ERROR_MSG, "memory allocation failed");
        set_error(tok, out, tok->error_msg, start_pos);
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

    tok->last_token_type = TOK_NUMBER;
    tok->last_token_end_pos = tok->pos;

    return true;
}

/*
 * Read a string token (double-quoted, no escapes).
 * Called when we've seen an opening '"'.
 */
static bool read_string(Tokenizer *tok, Token *out, size_t start_pos, size_t start_line, size_t start_col) {
    /* Skip opening quote (already at it) */
    tok->pos++;
    tok->col++;

    size_t content_start = tok->pos;

    /* Find closing quote, error on newline or EOF */
    while (tok->pos < tok->input_len) {
        char c = tok->input[tok->pos];

        if (c == '"') {
            /* Found closing quote */
            size_t content_len = tok->pos - content_start;

            /* Skip closing quote */
            tok->pos++;
            tok->col++;

            /* Check for adjacent token without whitespace */
            if (tok->pos < tok->input_len) {
                size_t bytes;
                uint32_t cp = utf8_decode(tok->input, tok->input_len, tok->pos, &bytes);

                /* String followed by identifier start, digit, or quote is an error */
                if (is_ident_start(cp) || is_ascii_digit(cp) || cp == '"' || cp == '-') {
                    /* '-' after string could be start of negative number or error */
                    /* Check if it's followed by a digit */
                    if (cp == '-') {
                        if (tok->pos + 1 < tok->input_len && is_ascii_digit((unsigned char)tok->input[tok->pos + 1])) {
                            /* It's a negative number, which is adjacent */
                            snprintf(tok->error_msg, MAX_ERROR_MSG, "adjacent tokens without whitespace");
                            out->line = start_line;
                            out->col = start_col;
                            /* Return the string, then error on next call */
                        } else {
                            /* It's not a valid token start after '-', will be caught later */
                        }
                    }

                    if (is_ident_start(cp) || is_ascii_digit(cp) || cp == '"') {
                        /* Copy string content first */
                        if (!ensure_value_buf(tok, content_len + 1)) {
                            snprintf(tok->error_msg, MAX_ERROR_MSG, "memory allocation failed");
                            set_error(tok, out, tok->error_msg, start_pos);
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

                        tok->last_token_type = TOK_STRING;
                        tok->last_token_end_pos = tok->pos;

                        return true;
                    }
                }
            }

            /* Copy content to buffer */
            if (!ensure_value_buf(tok, content_len + 1)) {
                snprintf(tok->error_msg, MAX_ERROR_MSG, "memory allocation failed");
                set_error(tok, out, tok->error_msg, start_pos);
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

            tok->last_token_type = TOK_STRING;
            tok->last_token_end_pos = tok->pos;

            return true;
        }

        if (c == '\n' || c == '\r') {
            /* String cannot span lines (no escapes in v0) */
            snprintf(tok->error_msg, MAX_ERROR_MSG, "unterminated string");
            out->line = start_line;
            out->col = start_col;
            set_error(tok, out, tok->error_msg, start_pos);
            return true;
        }

        tok->pos++;
        tok->col++;
    }

    /* Reached EOF without closing quote */
    snprintf(tok->error_msg, MAX_ERROR_MSG, "unterminated string");
    out->line = start_line;
    out->col = start_col;
    set_error(tok, out, tok->error_msg, start_pos);
    return true;
}

/*
 * Read an identifier token.
 * Called when we've seen an identifier start character.
 * Supports kebab-case (hyphens in the middle, not at start/end).
 */
static bool read_ident(Tokenizer *tok, Token *out, size_t start_pos, size_t start_line, size_t start_col) {
    size_t value_start = tok->pos;

    /* Consume identifier characters */
    while (tok->pos < tok->input_len) {
        size_t bytes;
        uint32_t cp = utf8_decode(tok->input, tok->input_len, tok->pos, &bytes);

        if (cp == 0xFFFFFFFF) {
            /* Invalid UTF-8 */
            break;
        }

        if (!is_ident_continue(cp)) {
            break;
        }

        /* Handle hyphen: must not be at end */
        if (cp == '-') {
            /* Peek ahead to see if there's a valid continue char after */
            size_t next_pos = tok->pos + bytes;
            if (next_pos >= tok->input_len) {
                /* Hyphen at end of input - error */
                snprintf(tok->error_msg, MAX_ERROR_MSG, "identifier cannot end with hyphen");
                out->line = start_line;
                out->col = start_col;
                set_error(tok, out, tok->error_msg, start_pos);
                return true;
            }

            size_t next_bytes;
            uint32_t next_cp = utf8_decode(tok->input, tok->input_len, next_pos, &next_bytes);

            /* Hyphen must be followed by letter, digit, or underscore (not another hyphen) */
            if (!is_ident_start(next_cp) && !is_ascii_digit(next_cp)) {
                /* Hyphen followed by invalid char - error */
                snprintf(tok->error_msg, MAX_ERROR_MSG, "identifier cannot end with hyphen");
                out->line = start_line;
                out->col = start_col;
                set_error(tok, out, tok->error_msg, start_pos);
                return true;
            }
        }

        tok->pos += bytes;
        tok->col++;  /* Simplified: count each codepoint as one column */
    }

    size_t value_len = tok->pos - value_start;

    /* Check for adjacent token without whitespace */
    if (tok->pos < tok->input_len) {
        char c = tok->input[tok->pos];

        /* Identifier followed by quote is an error */
        if (c == '"') {
            snprintf(tok->error_msg, MAX_ERROR_MSG, "adjacent tokens without whitespace");
            out->line = start_line;
            out->col = start_col;
            set_error(tok, out, tok->error_msg, start_pos);
            return true;
        }
    }

    /* Copy value to buffer */
    if (!ensure_value_buf(tok, value_len + 1)) {
        snprintf(tok->error_msg, MAX_ERROR_MSG, "memory allocation failed");
        set_error(tok, out, tok->error_msg, start_pos);
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

    tok->last_token_type = TOK_IDENT;
    tok->last_token_end_pos = tok->pos;

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

    /* Check for adjacent token error after string */
    if (tok->last_token_type == TOK_STRING && tok->pos == tok->last_token_end_pos) {
        /* We're right after a string, check what's next */
        if (tok->pos < tok->input_len) {
            size_t bytes;
            uint32_t cp = utf8_decode(tok->input, tok->input_len, tok->pos, &bytes);

            if (is_ident_start(cp) || is_ascii_digit(cp) || cp == '"') {
                snprintf(tok->error_msg, MAX_ERROR_MSG, "adjacent tokens without whitespace");
                out->pos = tok->pos;
                out->line = tok->line;
                out->col = tok->col;
                set_error(tok, out, tok->error_msg, tok->pos);
                return true;
            }

            /* Check for negative number adjacent to string */
            if (cp == '-' && tok->pos + 1 < tok->input_len &&
                is_ascii_digit((unsigned char)tok->input[tok->pos + 1])) {
                snprintf(tok->error_msg, MAX_ERROR_MSG, "adjacent tokens without whitespace");
                out->pos = tok->pos;
                out->line = tok->line;
                out->col = tok->col;
                set_error(tok, out, tok->error_msg, tok->pos);
                return true;
            }
        }
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

    /* Peek at current character */
    size_t bytes;
    uint32_t cp = utf8_decode(tok->input, tok->input_len, tok->pos, &bytes);

    if (cp == 0xFFFFFFFF) {
        snprintf(tok->error_msg, MAX_ERROR_MSG, "invalid UTF-8 encoding");
        out->line = token_start_line;
        out->col = token_start_col;
        set_error(tok, out, tok->error_msg, token_start_pos);
        return true;
    }

    /* String */
    if (cp == '"') {
        return read_string(tok, out, token_start_pos, token_start_line, token_start_col);
    }

    /* Number (digit or minus followed by digit) */
    if (is_ascii_digit(cp)) {
        return read_number(tok, out, token_start_pos, token_start_line, token_start_col, false);
    }

    if (cp == '-') {
        /* Check if followed by digit */
        if (tok->pos + 1 < tok->input_len && is_ascii_digit((unsigned char)tok->input[tok->pos + 1])) {
            tok->pos++;
            tok->col++;
            return read_number(tok, out, token_start_pos, token_start_line, token_start_col, true);
        } else {
            /* Lone minus or minus followed by non-digit is an error */
            snprintf(tok->error_msg, MAX_ERROR_MSG, "invalid character '-'");
            out->line = token_start_line;
            out->col = token_start_col;
            set_error(tok, out, tok->error_msg, token_start_pos);
            return true;
        }
    }

    /* Identifier */
    if (is_ident_start(cp)) {
        return read_ident(tok, out, token_start_pos, token_start_line, token_start_col);
    }

    /* Invalid character */
    snprintf(tok->error_msg, MAX_ERROR_MSG, "invalid character");
    out->line = token_start_line;
    out->col = token_start_col;
    set_error(tok, out, tok->error_msg, token_start_pos);
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
    tok->last_token_type = TOK_EOF;
    tok->last_token_end_pos = 0;

    return true;
}

const char *token_type_str(TokenType type) {
    switch (type) {
        case TOK_NUMBER: return "NUMBER";
        case TOK_STRING: return "STRING";
        case TOK_IDENT:  return "IDENT";
        case TOK_EOF:    return "EOF";
        case TOK_ERROR:  return "ERROR";
        default:         return "UNKNOWN";
    }
}
