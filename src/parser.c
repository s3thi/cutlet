/*
 * parser.c - Cutlet Pratt parser implementation
 *
 * Implements a Pratt (precedence climbing) parser for expressions.
 * Precedence (low to high):
 *   0. assignment / declaration (=, "my") (right-associative)
 *   1. or (left-associative, keyword infix)
 *   2. and (left-associative, keyword infix)
 *   3. not (prefix, keyword)
 *   4. ==, !=, <, >, <=, >= (comparison, non-associative)
 *   5. +, - (binary, left-associative)
 *   6. *, / (left-associative)
 *   7. unary - (prefix)
 *   8. ** (right-associative)
 *
 * Grammar:
 *   expr     → assignment
 *   assignment → "my" IDENT "=" assignment
 *              | IDENT "=" assignment
 *              | pratt(0)
 *   atom     → NUMBER | STRING | IDENT | '(' expr ')' | '-' expr
 *   infix    → '+' | '-' | '*' | '/' | '**'
 */

#include "parser.h"
#include "tokenizer.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * Parser state: wraps the tokenizer with one-token lookahead
 * ============================================================ */

typedef struct {
    Tokenizer *tok;
    Token current; /* current lookahead token */
    bool has_error;
    ParseError *err; /* caller-provided error output (may be NULL) */
} Parser;

/*
 * Advance the parser to the next token.
 */
static void advance(Parser *p) { tokenizer_next(p->tok, &p->current); }

/*
 * Set a parse error. Only the first error is recorded (first error wins).
 * Subsequent calls are no-ops so that the original error context is preserved.
 */
static void parser_error(Parser *p, size_t line, size_t col, const char *fmt, ...) {
    if (p->has_error)
        return;
    p->has_error = true;
    if (!p->err)
        return;
    p->err->line = line;
    p->err->col = col;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(p->err->message, sizeof(p->err->message), fmt, ap);
    va_end(ap); // NOLINT(clang-analyzer-valist.Uninitialized)
}

/*
 * Check whether the current token matches a keyword literal.
 */
static bool token_is_keyword(const Token *t, const char *kw) {
    size_t kw_len = strlen(kw);
    return t->type == TOK_IDENT && t->value_len == kw_len && strncmp(t->value, kw, kw_len) == 0;
}

/* ============================================================
 * AST node constructors
 * ============================================================ */

/*
 * Create a leaf AST node (NUMBER, STRING, IDENT).
 */
static AstNode *make_leaf(AstNodeType type, const char *value, size_t value_len) {
    AstNode *node = malloc(sizeof(AstNode));
    if (!node)
        return NULL;
    node->type = type;
    node->value = malloc(value_len + 1);
    if (!node->value) {
        free(node);
        return NULL;
    }
    memcpy(node->value, value, value_len);
    node->value[value_len] = '\0';
    node->left = NULL;
    node->right = NULL;
    node->grouped = false;
    return node;
}

/*
 * Create a binary operator AST node.
 */
static AstNode *make_binop(const char *op, size_t op_len, AstNode *left, AstNode *right) {
    AstNode *node = malloc(sizeof(AstNode));
    if (!node)
        return NULL;
    node->type = AST_BINOP;
    node->value = malloc(op_len + 1);
    if (!node->value) {
        free(node);
        return NULL;
    }
    memcpy(node->value, op, op_len);
    node->value[op_len] = '\0';
    node->left = left;
    node->right = right;
    node->grouped = false;
    return node;
}

/*
 * Create a unary operator AST node.
 */
static AstNode *make_unary(const char *op, size_t op_len, AstNode *operand) {
    AstNode *node = malloc(sizeof(AstNode));
    if (!node)
        return NULL;
    node->type = AST_UNARY;
    node->value = malloc(op_len + 1);
    if (!node->value) {
        free(node);
        return NULL;
    }
    memcpy(node->value, op, op_len);
    node->value[op_len] = '\0';
    node->left = operand;
    node->right = NULL;
    node->grouped = false;
    return node;
}

/*
 * Create a declaration or assignment AST node.
 */
static AstNode *make_named(AstNodeType type, const char *name, size_t name_len, AstNode *expr) {
    AstNode *node = malloc(sizeof(AstNode));
    if (!node)
        return NULL;
    node->type = type;
    node->value = malloc(name_len + 1);
    if (!node->value) {
        free(node);
        return NULL;
    }
    memcpy(node->value, name, name_len);
    node->value[name_len] = '\0';
    node->left = expr;
    node->right = NULL;
    node->grouped = false;
    return node;
}

/* ============================================================
 * Operator precedence and associativity
 * ============================================================ */

/*
 * Precedence levels for infix operators (low to high):
 *   1: or (keyword, left-associative)
 *   2: and (keyword, left-associative)
 *   3: not (keyword, prefix — handled in parse_atom)
 *   4: comparison (==, !=, <, >, <=, >=) — non-associative
 *   5: +, - (left-associative)
 *   6: *, / (left-associative)
 *   7: unary - (prefix, handled in parse_atom)
 *   8: ** (right-associative)
 */
static int infix_precedence(const char *op, size_t len) {
    if (len == 1) {
        if (op[0] == '<' || op[0] == '>')
            return 4;
        if (op[0] == '+' || op[0] == '-')
            return 5;
        if (op[0] == '*' || op[0] == '/')
            return 6;
    }
    if (len == 2) {
        if ((op[0] == '=' && op[1] == '=') || (op[0] == '!' && op[1] == '=') ||
            (op[0] == '<' && op[1] == '=') || (op[0] == '>' && op[1] == '='))
            return 4;
        if (op[0] == '*' && op[1] == '*')
            return 8; /* ** is above unary (7) */
        if (op[0] == 'o' && op[1] == 'r')
            return 1;
    }
    if (len == 3 && op[0] == 'a' && op[1] == 'n' && op[2] == 'd')
        return 2;
    return 0; /* not a known infix operator */
}

/*
 * Check if a token is a keyword-based infix operator (and, or).
 * These tokenize as TOK_IDENT but act as infix operators in the parser.
 */
static bool is_keyword_infix(const Token *t) {
    return token_is_keyword(t, "and") || token_is_keyword(t, "or");
}

/*
 * Check if an operator is a comparison operator (non-associative).
 */
static bool is_comparison_op(const char *op, size_t len) {
    if (len == 1)
        return op[0] == '<' || op[0] == '>';
    if (len == 2)
        return (op[0] == '=' && op[1] == '=') || (op[0] == '!' && op[1] == '=') ||
               (op[0] == '<' && op[1] == '=') || (op[0] == '>' && op[1] == '=');
    return false;
}

/*
 * Check if an operator is right-associative.
 * Only ** is right-associative.
 */
static bool is_right_assoc(const char *op, size_t len) {
    return len == 2 && op[0] == '*' && op[1] == '*';
}

/*
 * Check if a keyword is reserved and cannot be used as a variable name.
 * This includes true, false, and, or, not.
 */
static bool is_reserved_keyword(const Token *t) {
    return token_is_keyword(t, "true") || token_is_keyword(t, "false") ||
           token_is_keyword(t, "and") || token_is_keyword(t, "or") || token_is_keyword(t, "not");
}

/* ============================================================
 * Pratt parser core
 * ============================================================ */

/* Forward declarations */
static AstNode *parse_assignment(Parser *p);
static AstNode *parse_expr(Parser *p, int min_prec);

/*
 * Parse an atom (prefix position).
 * atom → NUMBER | STRING | IDENT | '(' expr ')' | '-' expr
 */
static AstNode *parse_atom(Parser *p) {
    if (p->has_error)
        return NULL;

    Token t = p->current;

    /* Handle tokenizer errors */
    if (t.type == TOK_ERROR) {
        parser_error(p, t.line, t.col, "%.*s", (int)t.value_len, t.value);
        return NULL;
    }

    /* Handle EOF */
    if (t.type == TOK_EOF) {
        parser_error(p, t.line, t.col, "expected expression, got EOF");
        return NULL;
    }

    /* Number literal */
    if (t.type == TOK_NUMBER) {
        AstNode *node = make_leaf(AST_NUMBER, t.value, t.value_len);
        if (!node) {
            parser_error(p, t.line, t.col, "memory allocation failed");
            return NULL;
        }
        advance(p);
        return node;
    }

    /* String literal */
    if (t.type == TOK_STRING) {
        AstNode *node = make_leaf(AST_STRING, t.value, t.value_len);
        if (!node) {
            parser_error(p, t.line, t.col, "memory allocation failed");
            return NULL;
        }
        advance(p);
        return node;
    }

    /* Boolean literals: true, false */
    if (token_is_keyword(&t, "true") || token_is_keyword(&t, "false")) {
        AstNode *node = make_leaf(AST_BOOL, t.value, t.value_len);
        if (!node) {
            parser_error(p, t.line, t.col, "memory allocation failed");
            return NULL;
        }
        advance(p);
        return node;
    }

    /* "not" prefix operator: precedence 3, binds looser than comparisons */
    if (token_is_keyword(&t, "not")) {
        advance(p);
        /* Parse operand at precedence 3 so comparisons (prec 4) bind tighter */
        AstNode *operand = parse_expr(p, 3);
        if (!operand)
            return NULL;
        AstNode *node = make_unary("not", 3, operand);
        if (!node) {
            ast_free(operand);
            parser_error(p, t.line, t.col, "memory allocation failed");
            return NULL;
        }
        return node;
    }

    /* Reject other reserved keywords used as bare identifiers in expression position.
     * "and" and "or" are infix operators — seeing them in prefix position is an error. */
    if (token_is_keyword(&t, "and") || token_is_keyword(&t, "or")) {
        parser_error(p, t.line, t.col, "unexpected keyword '%.*s'", (int)t.value_len, t.value);
        return NULL;
    }

    /* Identifier */
    if (t.type == TOK_IDENT) {
        AstNode *node = make_leaf(AST_IDENT, t.value, t.value_len);
        if (!node) {
            parser_error(p, t.line, t.col, "memory allocation failed");
            return NULL;
        }
        advance(p);
        return node;
    }

    /* Operator: check for prefix operators */
    if (t.type == TOK_OPERATOR) {
        /* Unary minus */
        if (t.value_len == 1 && t.value[0] == '-') {
            advance(p);
            /* Unary minus has precedence 7 (between * and **) */
            AstNode *operand = parse_expr(p, 7);
            if (!operand)
                return NULL;
            AstNode *node = make_unary("-", 1, operand);
            if (!node) {
                ast_free(operand);
                parser_error(p, t.line, t.col, "memory allocation failed");
                return NULL;
            }
            return node;
        }

        /* Open parenthesis */
        if (t.value_len == 1 && t.value[0] == '(') {
            advance(p);
            AstNode *expr = parse_assignment(p);
            if (!expr)
                return NULL;

            /* Expect closing paren */
            if (p->current.type != TOK_OPERATOR || p->current.value_len != 1 ||
                p->current.value[0] != ')') {
                parser_error(p, p->current.line, p->current.col, "expected ')'");
                ast_free(expr);
                return NULL;
            }
            advance(p);
            expr->grouped = true;
            return expr;
        }

        /* Unknown prefix operator */
        parser_error(p, t.line, t.col, "unexpected operator '%.*s'", (int)t.value_len, t.value);
        return NULL;
    }

    /* Shouldn't reach here */
    parser_error(p, t.line, t.col, "unexpected token");
    return NULL;
}

/*
 * Parse an assignment or declaration expression.
 * assignment → "my" IDENT "=" assignment
 *            | IDENT "=" assignment
 *            | pratt(0)
 */
static AstNode *parse_assignment(Parser *p) {
    if (p->has_error)
        return NULL;

    if (token_is_keyword(&p->current, "my")) {
        Token kw = p->current;
        advance(p);

        /* Reject reserved keywords as variable names. */
        if (is_reserved_keyword(&p->current)) {
            parser_error(p, p->current.line, p->current.col,
                         "cannot declare keyword '%.*s' as variable", (int)p->current.value_len,
                         p->current.value);
            return NULL;
        }

        if (p->current.type != TOK_IDENT) {
            parser_error(p, p->current.line, p->current.col, "expected identifier after 'my'");
            return NULL;
        }

        Token name_tok = p->current;
        AstNode *decl = make_named(AST_DECL, name_tok.value, name_tok.value_len, NULL);
        if (!decl) {
            parser_error(p, kw.line, kw.col, "memory allocation failed");
            return NULL;
        }
        advance(p);

        if (p->current.type != TOK_OPERATOR || p->current.value_len != 1 ||
            p->current.value[0] != '=') {
            parser_error(p, p->current.line, p->current.col, "expected '=' after variable name");
            ast_free(decl);
            return NULL;
        }

        advance(p);
        AstNode *rhs = parse_assignment(p);
        if (!rhs) {
            ast_free(decl);
            return NULL;
        }
        decl->left = rhs;
        return decl;
    }

    AstNode *left = parse_expr(p, 0);
    if (!left)
        return NULL;

    if (p->current.type == TOK_OPERATOR && p->current.value_len == 1 &&
        p->current.value[0] == '=') {
        size_t op_line = p->current.line;
        size_t op_col = p->current.col;

        if (left->type != AST_IDENT || left->grouped) {
            parser_error(p, op_line, op_col, "invalid assignment target");
            ast_free(left);
            return NULL;
        }

        advance(p);
        AstNode *rhs = parse_assignment(p);
        if (!rhs) {
            ast_free(left);
            return NULL;
        }

        AstNode *assign = make_named(AST_ASSIGN, left->value, strlen(left->value), rhs);
        if (!assign) {
            ast_free(left);
            ast_free(rhs);
            parser_error(p, op_line, op_col, "memory allocation failed");
            return NULL;
        }
        ast_free(left);
        return assign;
    }

    return left;
}

/*
 * Parse an expression with Pratt precedence climbing.
 * min_prec: minimum precedence level to continue parsing infix ops.
 */
static AstNode *parse_expr(Parser *p, int min_prec) {
    AstNode *left = parse_atom(p);
    if (!left)
        return NULL;

    /* Loop: consume infix operators with sufficient precedence.
     * Handles both symbol operators (TOK_OPERATOR) and keyword operators
     * like "and"/"or" (TOK_IDENT that act as infix operators). */
    while (!p->has_error && (p->current.type == TOK_OPERATOR || is_keyword_infix(&p->current))) {
        const char *op_value = p->current.value;
        size_t op_len = p->current.value_len;
        size_t op_line = p->current.line;
        size_t op_col = p->current.col;
        int prec = infix_precedence(op_value, op_len);

        /* Not a known infix operator, or precedence too low → stop */
        if (prec == 0 || prec < min_prec)
            break;

        /* Save operator string before advance overwrites the token buffer */
        char op_buf[8];
        if (op_len >= sizeof(op_buf))
            break; /* operator too long — not one we recognize */
        memcpy(op_buf, op_value, op_len);
        op_buf[op_len] = '\0';

        /* For left-associative, require strictly greater precedence on right.
         * For right-associative, allow equal precedence on right. */
        int next_min_prec = is_right_assoc(op_buf, op_len) ? prec : prec + 1;

        advance(p);
        AstNode *right = parse_expr(p, next_min_prec);
        if (!right) {
            ast_free(left);
            return NULL;
        }

        AstNode *binop = make_binop(op_buf, op_len, left, right);
        if (!binop) {
            ast_free(left);
            ast_free(right);
            parser_error(p, op_line, op_col, "memory allocation failed");
            return NULL;
        }
        left = binop;

        /* Non-associative check: reject chained comparisons like 1 < 2 < 3 */
        if (is_comparison_op(op_buf, op_len) && p->current.type == TOK_OPERATOR &&
            is_comparison_op(p->current.value, p->current.value_len)) {
            parser_error(p, p->current.line, p->current.col,
                         "comparison operators cannot be chained");
            ast_free(left);
            return NULL;
        }
    }

    return left;
}

/* ============================================================
 * Public API
 * ============================================================ */

bool parser_parse(const char *input, AstNode **out, ParseError *err) {
    if (!out) {
        if (err) {
            err->line = 0;
            err->col = 0;
            snprintf(err->message, sizeof(err->message), "parser output pointer is NULL");
        }
        return false;
    }

    *out = NULL;

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

    Parser p = {.tok = tok, .has_error = false, .err = err};

    /* Prime the lookahead */
    advance(&p);

    /* Parse the full expression */
    AstNode *result = parse_assignment(&p);

    if (p.has_error) {
        ast_free(result);
        tokenizer_destroy(tok);
        return false;
    }

    /* Check for trailing tokens */
    if (p.current.type == TOK_ERROR) {
        parser_error(&p, p.current.line, p.current.col, "%.*s", (int)p.current.value_len,
                     p.current.value);
        ast_free(result);
        tokenizer_destroy(tok);
        return false;
    }

    if (p.current.type != TOK_EOF) {
        parser_error(&p, p.current.line, p.current.col, "unexpected extra token '%.*s'",
                     (int)p.current.value_len, p.current.value);
        ast_free(result);
        tokenizer_destroy(tok);
        return false;
    }

    tokenizer_destroy(tok);
    *out = result;
    return true;
}

void ast_free(AstNode *node) {
    if (!node)
        return;
    ast_free(node->left);
    ast_free(node->right);
    free(node->value);
    free(node);
}

const char *ast_node_type_str(AstNodeType type) {
    switch (type) {
    case AST_NUMBER:
        return "NUMBER";
    case AST_STRING:
        return "STRING";
    case AST_BOOL:
        return "BOOL";
    case AST_IDENT:
        return "IDENT";
    case AST_BINOP:
        return "BINOP";
    case AST_UNARY:
        return "UNARY";
    case AST_DECL:
        return "DECL";
    case AST_ASSIGN:
        return "ASSIGN";
    default:
        return "UNKNOWN";
    }
}

/*
 * Internal helper: format a node subtree (without "AST " prefix).
 * Returns a newly allocated string like "[BINOP + [NUMBER 1] [NUMBER 2]]".
 */
static char *ast_format_node(const AstNode *node) {
    if (!node)
        return NULL;

    const char *type_str = ast_node_type_str(node->type);

    if (node->type == AST_NUMBER || node->type == AST_STRING || node->type == AST_IDENT ||
        node->type == AST_BOOL) {
        /* Leaf node: [TYPE value] */
        size_t len = 1 + strlen(type_str) + 1 + strlen(node->value) + 1 + 1;
        char *buf = malloc(len);
        if (!buf)
            return NULL;
        snprintf(buf, len, "[%s %s]", type_str, node->value);
        return buf;
    }

    if (node->type == AST_BINOP) {
        /* Binary: [BINOP op left right] */
        char *left_str = ast_format_node(node->left);
        char *right_str = ast_format_node(node->right);
        if (!left_str || !right_str) {
            free(left_str);
            free(right_str);
            return NULL;
        }
        size_t len = 1 + strlen(type_str) + 1 + strlen(node->value) + 1 + strlen(left_str) + 1 +
                     strlen(right_str) + 1 + 1;
        char *buf = malloc(len);
        if (!buf) {
            free(left_str);
            free(right_str);
            return NULL;
        }
        snprintf(buf, len, "[%s %s %s %s]", type_str, node->value, left_str, right_str);
        free(left_str);
        free(right_str);
        return buf;
    }

    if (node->type == AST_UNARY) {
        /* Unary: [UNARY op operand] */
        char *operand_str = ast_format_node(node->left);
        if (!operand_str)
            return NULL;
        size_t len =
            1 + strlen(type_str) + 1 + strlen(node->value) + 1 + strlen(operand_str) + 1 + 1;
        char *buf = malloc(len);
        if (!buf) {
            free(operand_str);
            return NULL;
        }
        snprintf(buf, len, "[%s %s %s]", type_str, node->value, operand_str);
        free(operand_str);
        return buf;
    }

    if (node->type == AST_DECL || node->type == AST_ASSIGN) {
        /* Decl/Assign: [DECL name expr] or [ASSIGN name expr] */
        char *expr_str = ast_format_node(node->left);
        if (!expr_str)
            return NULL;
        size_t len = 1 + strlen(type_str) + 1 + strlen(node->value) + 1 + strlen(expr_str) + 1 + 1;
        char *buf = malloc(len);
        if (!buf) {
            free(expr_str);
            return NULL;
        }
        snprintf(buf, len, "[%s %s %s]", type_str, node->value, expr_str);
        free(expr_str);
        return buf;
    }

    return NULL;
}

char *ast_format(const AstNode *node) {
    if (!node)
        return NULL;

    char *node_str = ast_format_node(node);
    if (!node_str)
        return NULL;

    /* Prepend "AST " */
    size_t len = 4 + strlen(node_str) + 1;
    char *buf = malloc(len);
    if (!buf) {
        free(node_str);
        return NULL;
    }
    snprintf(buf, len, "AST %s", node_str);
    free(node_str);
    return buf;
}
