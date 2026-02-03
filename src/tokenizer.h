/*
 * tokenizer.h - Cutlet tokenizer interface
 *
 * Token types supported in v0:
 * - NUMBER: integer only, digits only (0, 42)
 * - STRING: double-quoted, no escapes ("hi")
 * - IDENT: ASCII letter start, letters/digits continue, symbols sandwiched
 *          between letters are part of the identifier (hello+world, my_var)
 * - OPERATOR: symbol chars surrounded by whitespace/SOI/EOI (+, -, etc.)
 * - EOF: end of input
 * - ERROR: tokenization error with position info
 *
 * Symbol chars: anything that is not whitespace, ASCII letter, digit, or '"'.
 * Whitespace is ignored between tokens.
 */

#ifndef CUTLET_TOKENIZER_H
#define CUTLET_TOKENIZER_H

#include <stdbool.h>
#include <stddef.h>

/* Token types for the minimal v0 tokenizer */
typedef enum {
    TOK_NUMBER,   /* Integer literal (digits only) */
    TOK_STRING,   /* Double-quoted string */
    TOK_IDENT,    /* Identifier (ASCII letters, digits, symbol sandwiches) */
    TOK_OPERATOR, /* Operator (symbol chars delimited by whitespace) */
    TOK_NEWLINE,  /* Newline (\n, \r, or \r\n treated as single token) */
    TOK_EOF,      /* End of input */
    TOK_ERROR     /* Tokenization error */
} TokenType;

/*
 * Token structure
 *
 * For NUMBER: value points to the digit string
 * For STRING: value points to the string contents (without quotes)
 * For IDENT: value points to the identifier
 * For OPERATOR: value points to the operator symbol(s)
 * For ERROR: value points to the error message
 *
 * Position info is 0-indexed from start of input.
 * Line and column are 1-indexed for human-readable output.
 */
typedef struct {
    TokenType type;
    const char *value; /* Token value (owned by tokenizer) */
    size_t value_len;  /* Length of value */
    size_t pos;        /* 0-indexed position in input */
    size_t line;       /* 1-indexed line number */
    size_t col;        /* 1-indexed column number */
} Token;

/*
 * Tokenizer state
 *
 * Holds the input and current position. Created with tokenizer_create(),
 * freed with tokenizer_destroy().
 */
typedef struct Tokenizer Tokenizer;

/*
 * Create a new tokenizer for the given input string.
 * The input string must remain valid for the lifetime of the tokenizer.
 * Returns NULL on allocation failure.
 */
Tokenizer *tokenizer_create(const char *input);

/*
 * Destroy a tokenizer and free its resources.
 * Safe to call with NULL.
 */
void tokenizer_destroy(Tokenizer *tok);

/*
 * Get the next token from the tokenizer.
 *
 * Returns true on success, false on failure (invalid tokenizer).
 * The token is valid until the next call to tokenizer_next() or
 * tokenizer_destroy().
 *
 * After EOF is returned, subsequent calls continue to return EOF.
 * After ERROR is returned, subsequent calls continue to return ERROR.
 */
bool tokenizer_next(Tokenizer *tok, Token *out);

/*
 * Reset the tokenizer to the beginning of the input.
 * Returns true on success, false on failure (invalid tokenizer).
 */
bool tokenizer_reset(Tokenizer *tok);

/*
 * Convert a token type to a human-readable string.
 * Returns a static string, never NULL.
 */
const char *token_type_str(TokenType type);

#endif /* CUTLET_TOKENIZER_H */
