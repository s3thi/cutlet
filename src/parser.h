/*
 * parser.h - Cutlet parser interface
 *
 * Parses expressions using a Pratt (precedence climbing) parser.
 * Supports binary operators (+, -, *, /, **), unary minus, assignment,
 * declarations ("my"), and parenthesized sub-expressions. Leaf nodes are
 * NUMBER, STRING, or IDENT.
 */

#ifndef CUTLET_PARSER_H
#define CUTLET_PARSER_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    AST_NUMBER,  /* numeric literal */
    AST_STRING,  /* string literal */
    AST_IDENT,   /* identifier */
    AST_BOOL,    /* boolean literal: true, false */
    AST_NOTHING, /* nothing literal (like nil/null in other languages) */
    AST_BINOP,   /* binary operator: +, -, *, /, **, ==, !=, <, >, <=, >=, and, or */
    AST_UNARY,   /* unary: - (minus), not */
    AST_DECL,    /* declaration: my name = expr */
    AST_ASSIGN,  /* assignment: name = expr */
    AST_BLOCK,   /* block: sequence of newline-separated expressions */
    AST_IF,      /* if expression: if cond then body [else body] end */
    AST_CALL,    /* function call: name(arg1, arg2, ...) */
} AstNodeType;

typedef struct AstNode {
    AstNodeType type;
    char *value;           /* literal value or operator string (owned) */
    struct AstNode *left;  /* left operand (or sole operand for unary) */
    struct AstNode *right; /* right operand (NULL for unary/leaf) */
    bool grouped;          /* true if the node originated from parentheses */

    /* For AST_BLOCK only: array of child expressions */
    struct AstNode **children; /* owned array of owned nodes */
    size_t child_count;        /* number of children */
} AstNode;

typedef struct {
    size_t line;
    size_t col;
    char message[256];
} ParseError;

/*
 * Parse an expression from input.
 * On success, sets *out to a newly allocated AST tree and returns true.
 * On failure, populates err and returns false.
 * The caller must free *out with ast_free().
 */
bool parser_parse(const char *input, AstNode **out, ParseError *err);

/*
 * Free an AstNode tree recursively. Safe to call with NULL.
 */
void ast_free(AstNode *node);

/*
 * Return a human-readable string for an AstNodeType.
 */
const char *ast_node_type_str(AstNodeType type);

/*
 * Format an AstNode tree as nested S-expression.
 * Leaf: "AST [TYPE value]"
 * Binop: "AST [BINOP op [left] [right]]"
 * Unary: "AST [UNARY op [operand]]"
 * Decl/Assign: "AST [DECL name [expr]]" or "AST [ASSIGN name [expr]]"
 * Returns a newly allocated string. Caller must free.
 */
char *ast_format(const AstNode *node);

/*
 * Check if input is a complete expression (for REPL multiline support).
 *
 * Returns true if:
 *   - Input parses successfully, OR
 *   - Input has a syntax error that cannot be fixed by adding more input
 *
 * Returns false if input is incomplete:
 *   - Unclosed if/end
 *   - Unmatched parentheses
 *   - Unterminated string
 *   - Trailing operator expecting more input
 *
 * This is used by the REPL client to determine whether to continue reading
 * input (incomplete) or send to the server (complete).
 */
bool parser_is_complete(const char *input);

#endif /* CUTLET_PARSER_H */
