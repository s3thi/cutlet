/*
 * parser.h - Cutlet parser interface
 *
 * Parses a single-token expression (NUMBER, STRING, or IDENT) from input.
 * Operators are parse errors. Extra tokens after the first are parse errors.
 * Tokenizer errors propagate as parse errors.
 */

#ifndef CUTLET_PARSER_H
#define CUTLET_PARSER_H

#include <stdbool.h>
#include <stddef.h>

typedef enum { AST_NUMBER, AST_STRING, AST_IDENT } AstNodeType;

typedef struct {
    AstNodeType type;
    char *value; /* owned, malloc'd copy */
} AstNode;

typedef struct {
    size_t line;
    size_t col;
    char message[256];
} ParseError;

/*
 * Parse a single-token expression from input.
 * On success, sets *out to a newly allocated AstNode and returns true.
 * On failure, populates err and returns false.
 * The caller must free *out with ast_free().
 */
bool parser_parse_single(const char *input, AstNode **out, ParseError *err);

/*
 * Free an AstNode. Safe to call with NULL.
 */
void ast_free(AstNode *node);

/*
 * Return a human-readable string for an AstNodeType.
 */
const char *ast_node_type_str(AstNodeType type);

/*
 * Format an AstNode as "AST [TYPE value]".
 * Returns a newly allocated string. Caller must free.
 */
char *ast_format(const AstNode *node);

#endif /* CUTLET_PARSER_H */
