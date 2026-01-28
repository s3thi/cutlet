/*
 * tokenizer.c - Cutlet tokenizer implementation
 *
 * STUB: This is a placeholder that returns failure for all operations.
 * Tests should fail until the real implementation is written.
 */

#include "tokenizer.h"
#include <stdlib.h>

/* Tokenizer state - stub, no actual implementation yet */
struct Tokenizer {
    const char *input;
    size_t pos;
};

Tokenizer *tokenizer_create(const char *input) {
    (void)input;
    /* STUB: Return NULL to indicate failure */
    return NULL;
}

void tokenizer_destroy(Tokenizer *tok) {
    (void)tok;
    /* STUB: Nothing to free */
}

bool tokenizer_next(Tokenizer *tok, Token *out) {
    (void)tok;
    (void)out;
    /* STUB: Return false to indicate failure */
    return false;
}

bool tokenizer_reset(Tokenizer *tok) {
    (void)tok;
    /* STUB: Return false to indicate failure */
    return false;
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
