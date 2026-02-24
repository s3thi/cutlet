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
 *   5. ++ (concatenation, right-associative)
 *   6. +, - (binary, left-associative)
 *   7. *, /, % (left-associative)
 *   8. unary - (prefix)
 *   9. ** (right-associative)
 *
 * Grammar:
 *   expr     → assignment
 *   assignment → "my" IDENT "=" assignment
 *              | IDENT "=" assignment
 *              | pratt(0)
 *   atom     → NUMBER | STRING | IDENT | '(' expr ')' | '-' expr
 *   infix    → '+' | '-' | '*' | '/' | '%' | '**' | '++'
 */

#include "parser.h"
#include "ptr_array.h"
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
static AstNode *make_leaf(AstNodeType type, const char *value, size_t value_len, size_t line) {
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
    node->line = line;
    node->children = NULL;
    node->child_count = 0;
    node->params = NULL;
    node->param_count = 0;
    return node;
}

/*
 * Create a binary operator AST node.
 */
static AstNode *make_binop(const char *op, size_t op_len, AstNode *left, AstNode *right,
                           size_t line) {
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
    node->line = line;
    node->children = NULL;
    node->child_count = 0;
    node->params = NULL;
    node->param_count = 0;
    return node;
}

/*
 * Create a unary operator AST node.
 */
static AstNode *make_unary(const char *op, size_t op_len, AstNode *operand, size_t line) {
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
    node->line = line;
    node->children = NULL;
    node->child_count = 0;
    node->params = NULL;
    node->param_count = 0;
    return node;
}

/*
 * Create a declaration or assignment AST node.
 */
static AstNode *make_named(AstNodeType type, const char *name, size_t name_len, AstNode *expr,
                           size_t line) {
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
    node->line = line;
    node->children = NULL;
    node->child_count = 0;
    node->params = NULL;
    node->param_count = 0;
    return node;
}

/*
 * Create a block AST node containing an array of child expressions.
 * Takes ownership of the children array and all nodes within it.
 */
static AstNode *make_block(AstNode **children, size_t count, size_t line) {
    AstNode *node = malloc(sizeof(AstNode));
    if (!node)
        return NULL;
    node->type = AST_BLOCK;
    node->value = NULL;
    node->left = NULL;
    node->right = NULL;
    node->grouped = false;
    node->line = line;
    node->children = children;
    node->child_count = count;
    node->params = NULL;
    node->param_count = 0;
    return node;
}

/*
 * Create an if expression AST node.
 * Uses children array: children[0]=condition, children[1]=then-body,
 * children[2]=else-body (if has_else is true).
 * Takes ownership of condition, then_body, and else_body.
 */
static AstNode *make_if(AstNode *condition, AstNode *then_body, AstNode *else_body, size_t line) {
    AstNode *node = malloc(sizeof(AstNode));
    if (!node)
        return NULL;

    size_t count = else_body ? 3 : 2;
    AstNode **children = (AstNode **)malloc(count * sizeof(AstNode *));
    if (!children) {
        free(node);
        return NULL;
    }

    children[0] = condition;
    children[1] = then_body;
    if (else_body) {
        children[2] = else_body;
    }

    node->type = AST_IF;
    node->value = NULL;
    node->left = NULL;
    node->right = NULL;
    node->grouped = false;
    node->line = line;
    node->children = children;
    node->child_count = count;
    node->params = NULL;
    node->param_count = 0;
    return node;
}

/*
 * Create a function call AST node.
 * value = function name, children[0..n] = arguments.
 * Takes ownership of the name string (copied) and the children array.
 */
static AstNode *make_call(const char *name, size_t name_len, AstNode **args, size_t arg_count,
                          size_t line) {
    AstNode *node = malloc(sizeof(AstNode));
    if (!node)
        return NULL;
    node->type = AST_CALL;
    node->value = malloc(name_len + 1);
    if (!node->value) {
        free(node);
        return NULL;
    }
    memcpy(node->value, name, name_len);
    node->value[name_len] = '\0';
    node->left = NULL;
    node->right = NULL;
    node->grouped = false;
    node->line = line;
    node->children = args;
    node->child_count = arg_count;
    node->params = NULL;
    node->param_count = 0;
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
 *   5: ++ (concatenation, right-associative)
 *   6: +, - (left-associative)
 *   7: *, /, % (left-associative)
 *   8: unary - (prefix, handled in parse_atom)
 *   9: ** (right-associative)
 */
static int infix_precedence(const char *op, size_t len) {
    if (len == 1) {
        if (op[0] == '<' || op[0] == '>')
            return 4;
        if (op[0] == '+' || op[0] == '-')
            return 6;
        if (op[0] == '*' || op[0] == '/' || op[0] == '%')
            return 7;
    }
    if (len == 2) {
        if ((op[0] == '=' && op[1] == '=') || (op[0] == '!' && op[1] == '=') ||
            (op[0] == '<' && op[1] == '=') || (op[0] == '>' && op[1] == '='))
            return 4;
        if (op[0] == '+' && op[1] == '+')
            return 5; /* ++ (concatenation) */
        if (op[0] == '*' && op[1] == '*')
            return 9; /* ** is above unary (8) */
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
 * ** (exponentiation) and ++ (concatenation) are right-associative.
 */
static bool is_right_assoc(const char *op, size_t len) {
    return len == 2 && ((op[0] == '*' && op[1] == '*') || (op[0] == '+' && op[1] == '+'));
}

/*
 * Check if a keyword is reserved and cannot be used as a variable name.
 * This includes true, false, nothing, and, or, not, if, then, else, end.
 */
static bool is_reserved_keyword(const Token *t) {
    return token_is_keyword(t, "true") || token_is_keyword(t, "false") ||
           token_is_keyword(t, "nothing") || token_is_keyword(t, "and") ||
           token_is_keyword(t, "or") || token_is_keyword(t, "not") || token_is_keyword(t, "if") ||
           token_is_keyword(t, "then") || token_is_keyword(t, "else") ||
           token_is_keyword(t, "end") || token_is_keyword(t, "while") ||
           token_is_keyword(t, "do") || token_is_keyword(t, "break") ||
           token_is_keyword(t, "continue") || token_is_keyword(t, "return") ||
           token_is_keyword(t, "fn") || token_is_keyword(t, "is");
}

/* ============================================================
 * Pratt parser core
 * ============================================================ */

/* Forward declarations */
static AstNode *parse_assignment(Parser *p);
static AstNode *parse_expr(Parser *p, int min_prec);
static AstNode *parse_if(Parser *p);
static AstNode *parse_while(Parser *p);
static AstNode *parse_fn(Parser *p);
static void skip_newlines(Parser *p);
static void free_expr_array(PtrArray *arr);

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

    /* Handle unexpected newline in expression position */
    if (t.type == TOK_NEWLINE) {
        parser_error(p, t.line, t.col, "expected expression, got newline");
        return NULL;
    }

    /* Number literal */
    if (t.type == TOK_NUMBER) {
        AstNode *node = make_leaf(AST_NUMBER, t.value, t.value_len, t.line);
        if (!node) {
            parser_error(p, t.line, t.col, "memory allocation failed");
            return NULL;
        }
        advance(p);
        return node;
    }

    /* String literal */
    if (t.type == TOK_STRING) {
        AstNode *node = make_leaf(AST_STRING, t.value, t.value_len, t.line);
        if (!node) {
            parser_error(p, t.line, t.col, "memory allocation failed");
            return NULL;
        }
        advance(p);
        return node;
    }

    /* Boolean literals: true, false */
    if (token_is_keyword(&t, "true") || token_is_keyword(&t, "false")) {
        AstNode *node = make_leaf(AST_BOOL, t.value, t.value_len, t.line);
        if (!node) {
            parser_error(p, t.line, t.col, "memory allocation failed");
            return NULL;
        }
        advance(p);
        return node;
    }

    /* Nothing literal */
    if (token_is_keyword(&t, "nothing")) {
        AstNode *node = make_leaf(AST_NOTHING, t.value, t.value_len, t.line);
        if (!node) {
            parser_error(p, t.line, t.col, "memory allocation failed");
            return NULL;
        }
        advance(p);
        return node;
    }

    /* If expression */
    if (token_is_keyword(&t, "if")) {
        return parse_if(p);
    }

    /* While loop expression */
    if (token_is_keyword(&t, "while")) {
        return parse_while(p);
    }

    /* Function definition expression */
    if (token_is_keyword(&t, "fn")) {
        return parse_fn(p);
    }

    /* Break expression: break [value]
     * Parsed syntactically anywhere; the compiler enforces loop context.
     * Peek at the next token to decide whether break has a value:
     * if the next token can start an expression, parse the value. */
    if (token_is_keyword(&t, "break")) {
        advance(p);
        AstNode *value_node = NULL;

        /* Check if the next token can start an expression.
         * If it's end/else/then/do/EOF/NEWLINE or another non-expression token,
         * treat this as a bare break. */
        Token next = p->current;
        bool has_value = false;
        if (next.type == TOK_NUMBER || next.type == TOK_STRING ||
            (next.type == TOK_OPERATOR && next.value_len == 1 &&
             (next.value[0] == '(' || next.value[0] == '-' || next.value[0] == '['))) {
            has_value = true;
        } else if (next.type == TOK_IDENT) {
            /* Identifiers that start expressions: non-keyword idents,
             * plus keywords that are expression starters. */
            has_value = !token_is_keyword(&next, "end") && !token_is_keyword(&next, "else") &&
                        !token_is_keyword(&next, "then") && !token_is_keyword(&next, "do") &&
                        !token_is_keyword(&next, "and") && !token_is_keyword(&next, "or") &&
                        !token_is_keyword(&next, "break") && !token_is_keyword(&next, "continue") &&
                        !token_is_keyword(&next, "return");
        }

        if (has_value) {
            value_node = parse_assignment(p);
            if (!value_node)
                return NULL;
        }

        AstNode *node = malloc(sizeof(AstNode));
        if (!node) {
            ast_free(value_node);
            parser_error(p, t.line, t.col, "memory allocation failed");
            return NULL;
        }
        node->type = AST_BREAK;
        node->value = NULL;
        node->left = value_node;
        node->right = NULL;
        node->grouped = false;
        node->line = t.line;
        node->children = NULL;
        node->child_count = 0;
        node->params = NULL;
        node->param_count = 0;
        return node;
    }

    /* Continue expression: skip to next loop iteration.
     * Parsed syntactically anywhere; the compiler enforces loop context. */
    if (token_is_keyword(&t, "continue")) {
        advance(p);
        AstNode *node = malloc(sizeof(AstNode));
        if (!node) {
            parser_error(p, t.line, t.col, "memory allocation failed");
            return NULL;
        }
        node->type = AST_CONTINUE;
        node->value = NULL;
        node->left = NULL;
        node->right = NULL;
        node->grouped = false;
        node->line = t.line;
        node->children = NULL;
        node->child_count = 0;
        node->params = NULL;
        node->param_count = 0;
        return node;
    }

    /* Return expression: return [value]
     * Parsed syntactically anywhere; the compiler enforces function context.
     * Uses the same value-detection logic as break to decide whether
     * return has an accompanying expression. */
    if (token_is_keyword(&t, "return")) {
        advance(p);
        AstNode *value_node = NULL;

        /* Check if the next token can start an expression.
         * Same logic as break: if it's end/else/then/do/EOF/NEWLINE or
         * another non-expression token, treat this as a bare return. */
        Token next = p->current;
        bool has_value = false;
        if (next.type == TOK_NUMBER || next.type == TOK_STRING ||
            (next.type == TOK_OPERATOR && next.value_len == 1 &&
             (next.value[0] == '(' || next.value[0] == '-' || next.value[0] == '['))) {
            has_value = true;
        } else if (next.type == TOK_IDENT) {
            has_value = !token_is_keyword(&next, "end") && !token_is_keyword(&next, "else") &&
                        !token_is_keyword(&next, "then") && !token_is_keyword(&next, "do") &&
                        !token_is_keyword(&next, "and") && !token_is_keyword(&next, "or") &&
                        !token_is_keyword(&next, "break") && !token_is_keyword(&next, "continue") &&
                        !token_is_keyword(&next, "return");
        }

        if (has_value) {
            value_node = parse_assignment(p);
            if (!value_node)
                return NULL;
        }

        AstNode *node = malloc(sizeof(AstNode));
        if (!node) {
            ast_free(value_node);
            parser_error(p, t.line, t.col, "memory allocation failed");
            return NULL;
        }
        node->type = AST_RETURN;
        node->value = NULL;
        node->left = value_node;
        node->right = NULL;
        node->grouped = false;
        node->line = t.line;
        node->children = NULL;
        node->child_count = 0;
        node->params = NULL;
        node->param_count = 0;
        return node;
    }

    /* "not" prefix operator: precedence 3, binds looser than comparisons */
    if (token_is_keyword(&t, "not")) {
        advance(p);
        /* Parse operand at precedence 3 so comparisons (prec 4) bind tighter */
        AstNode *operand = parse_expr(p, 3);
        if (!operand)
            return NULL;
        AstNode *node = make_unary("not", 3, operand, t.line);
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

    /* Identifier — may be followed by '(' for a function call.
     * Token values are owned by the tokenizer and invalidated on advance(),
     * so we must save the identifier name before consuming any more tokens. */
    if (t.type == TOK_IDENT) {
        /* Save identifier name before advance invalidates t.value */
        char *saved_name = malloc(t.value_len + 1);
        if (!saved_name) {
            parser_error(p, t.line, t.col, "memory allocation failed");
            return NULL;
        }
        memcpy(saved_name, t.value, t.value_len);
        saved_name[t.value_len] = '\0';
        size_t saved_len = t.value_len;
        size_t saved_line = t.line;
        size_t saved_col = t.col;

        advance(p);

        /* Check if next token is '(' — if so, parse as function call */
        if (p->current.type == TOK_OPERATOR && p->current.value_len == 1 &&
            p->current.value[0] == '(') {
            advance(p); /* consume '(' */

            /* Parse comma-separated argument list */
            PtrArray args;
            if (!ptr_array_init(&args, 4)) {
                free(saved_name);
                parser_error(p, saved_line, saved_col, "memory allocation failed");
                return NULL;
            }

            /* Check for zero-arg call: immediate ')' */
            if (!(p->current.type == TOK_OPERATOR && p->current.value_len == 1 &&
                  p->current.value[0] == ')')) {
                /* Parse first argument */
                AstNode *arg = parse_assignment(p);
                if (!arg) {
                    free(saved_name);
                    ptr_array_destroy(&args);
                    return NULL;
                }
                if (!ptr_array_push(&args, arg)) {
                    ast_free(arg);
                    free(saved_name);
                    ptr_array_destroy(&args);
                    parser_error(p, saved_line, saved_col, "memory allocation failed");
                    return NULL;
                }

                /* Parse additional comma-separated arguments */
                while (p->current.type == TOK_OPERATOR && p->current.value_len == 1 &&
                       p->current.value[0] == ',') {
                    advance(p); /* consume ',' */
                    arg = parse_assignment(p);
                    if (!arg) {
                        free(saved_name);
                        free_expr_array(&args);
                        return NULL;
                    }
                    if (!ptr_array_push(&args, arg)) {
                        ast_free(arg);
                        free(saved_name);
                        free_expr_array(&args);
                        parser_error(p, saved_line, saved_col, "memory allocation failed");
                        return NULL;
                    }
                }
            }

            /* Expect closing ')' */
            if (p->current.type != TOK_OPERATOR || p->current.value_len != 1 ||
                p->current.value[0] != ')') {
                parser_error(p, p->current.line, p->current.col, "expected ')' after arguments");
                free(saved_name);
                free_expr_array(&args);
                return NULL;
            }
            advance(p); /* consume ')' */

            /* Build AST_CALL node — make_call takes ownership of saved_name's content */
            size_t arg_count = args.count;
            AstNode **children = NULL;
            if (arg_count > 0) {
                children = (AstNode **)ptr_array_release(&args);
            } else {
                ptr_array_destroy(&args);
            }

            AstNode *call = make_call(saved_name, saved_len, children, arg_count, saved_line);
            free(saved_name);
            if (!call) {
                for (size_t i = 0; i < arg_count; i++)
                    ast_free(children[i]);
                if (children)
                    ptr_array_free_raw((void *)children);
                parser_error(p, saved_line, saved_col, "memory allocation failed");
                return NULL;
            }
            return call;
        }

        /* Plain identifier (not a call) */
        AstNode *node = make_leaf(AST_IDENT, saved_name, saved_len, saved_line);
        free(saved_name);
        if (!node) {
            parser_error(p, saved_line, saved_col, "memory allocation failed");
            return NULL;
        }
        return node;
    }

    /* Operator: check for prefix operators */
    if (t.type == TOK_OPERATOR) {
        /* Unary minus */
        if (t.value_len == 1 && t.value[0] == '-') {
            advance(p);
            /* Unary minus has precedence 8 (between * and **) */
            AstNode *operand = parse_expr(p, 8);
            if (!operand)
                return NULL;
            AstNode *node = make_unary("-", 1, operand, t.line);
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

        /* Open bracket — array literal: [expr, expr, ...] */
        if (t.value_len == 1 && t.value[0] == '[') {
            advance(p);

            /* Collect element expressions into a PtrArray. */
            PtrArray elems;
            if (!ptr_array_init(&elems, 4)) {
                parser_error(p, t.line, t.col, "memory allocation failed");
                return NULL;
            }

            /* Skip newlines after opening '[' — allows multiline arrays. */
            skip_newlines(p);

            /* Check for empty array: immediate ']' */
            if (!(p->current.type == TOK_OPERATOR && p->current.value_len == 1 &&
                  p->current.value[0] == ']')) {
                /* Parse first element */
                AstNode *elem = parse_assignment(p);
                if (!elem) {
                    ptr_array_destroy(&elems);
                    return NULL;
                }
                if (!ptr_array_push(&elems, elem)) {
                    ast_free(elem);
                    ptr_array_destroy(&elems);
                    parser_error(p, t.line, t.col, "memory allocation failed");
                    return NULL;
                }

                /* Parse additional comma-separated elements */
                while (p->current.type == TOK_OPERATOR && p->current.value_len == 1 &&
                       p->current.value[0] == ',') {
                    advance(p); /* consume ',' */

                    /* Skip newlines after comma — allows multiline arrays. */
                    skip_newlines(p);

                    /* Allow trailing comma: if next token is ']', stop */
                    if (p->current.type == TOK_OPERATOR && p->current.value_len == 1 &&
                        p->current.value[0] == ']') {
                        break;
                    }

                    elem = parse_assignment(p);
                    if (!elem) {
                        free_expr_array(&elems);
                        return NULL;
                    }
                    if (!ptr_array_push(&elems, elem)) {
                        ast_free(elem);
                        free_expr_array(&elems);
                        parser_error(p, t.line, t.col, "memory allocation failed");
                        return NULL;
                    }
                }
            }

            /* Skip newlines before closing ']' — allows multiline arrays. */
            skip_newlines(p);

            /* Expect closing ']' */
            if (p->current.type != TOK_OPERATOR || p->current.value_len != 1 ||
                p->current.value[0] != ']') {
                parser_error(p, p->current.line, p->current.col, "expected ']'");
                free_expr_array(&elems);
                return NULL;
            }
            advance(p); /* consume ']' */

            /* Build AST_ARRAY node using children/child_count. */
            size_t elem_count = elems.count;
            AstNode **children = NULL;
            if (elem_count > 0) {
                children = (AstNode **)ptr_array_release(&elems);
            } else {
                ptr_array_destroy(&elems);
            }

            AstNode *node = malloc(sizeof(AstNode));
            if (!node) {
                for (size_t i = 0; i < elem_count; i++)
                    ast_free(children[i]);
                if (children)
                    ptr_array_free_raw((void *)children);
                parser_error(p, t.line, t.col, "memory allocation failed");
                return NULL;
            }
            node->type = AST_ARRAY;
            node->value = NULL;
            node->left = NULL;
            node->right = NULL;
            node->grouped = false;
            node->line = t.line;
            node->children = children;
            node->child_count = elem_count;
            node->params = NULL;
            node->param_count = 0;
            return node;
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
        AstNode *decl = make_named(AST_DECL, name_tok.value, name_tok.value_len, NULL, kw.line);
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

        /* Index assignment: expr[idx] = value → AST_INDEX_ASSIGN */
        if (left->type == AST_INDEX) {
            advance(p); /* consume '=' */
            AstNode *rhs = parse_assignment(p);
            if (!rhs) {
                ast_free(left);
                return NULL;
            }

            /* Build AST_INDEX_ASSIGN: left=array, right=index, children[0]=value.
             * We steal the array and index pointers from the AST_INDEX node. */
            AstNode *node = malloc(sizeof(AstNode));
            if (!node) {
                ast_free(left);
                ast_free(rhs);
                parser_error(p, op_line, op_col, "memory allocation failed");
                return NULL;
            }
            AstNode **children = (AstNode **)malloc(sizeof(AstNode *));
            if (!children) {
                free(node);
                ast_free(left);
                ast_free(rhs);
                parser_error(p, op_line, op_col, "memory allocation failed");
                return NULL;
            }
            children[0] = rhs;

            node->type = AST_INDEX_ASSIGN;
            node->value = NULL;
            node->left = left->left;   /* array expression (stolen from AST_INDEX) */
            node->right = left->right; /* index expression (stolen from AST_INDEX) */
            node->grouped = false;
            node->line = op_line;
            node->children = children;
            node->child_count = 1;
            node->params = NULL;
            node->param_count = 0;

            /* Free the original AST_INDEX shell without freeing left/right. */
            left->left = NULL;
            left->right = NULL;
            ast_free(left);

            return node;
        }

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

        AstNode *assign = make_named(AST_ASSIGN, left->value, strlen(left->value), rhs, op_line);
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
     * like "and"/"or" (TOK_IDENT that act as infix operators).
     * Also handles postfix '[' for array indexing (precedence 10). */
    while (!p->has_error && (p->current.type == TOK_OPERATOR || is_keyword_infix(&p->current))) {

        /* Postfix '[' — array indexing: expr[expr].
         * Precedence 10 (highest, above **). Left-associative so
         * xs[0][1] parses as (xs[0])[1]. */
        if (p->current.type == TOK_OPERATOR && p->current.value_len == 1 &&
            p->current.value[0] == '[') {
            /* Index precedence is 10; only parse if min_prec allows it. */
            if (10 < min_prec)
                break;

            size_t bracket_line = p->current.line;
            size_t bracket_col = p->current.col;
            advance(p); /* consume '[' */

            /* Parse the index expression (full precedence reset). */
            AstNode *index_expr = parse_assignment(p);
            if (!index_expr) {
                ast_free(left);
                return NULL;
            }

            /* Expect closing ']' */
            if (p->current.type != TOK_OPERATOR || p->current.value_len != 1 ||
                p->current.value[0] != ']') {
                parser_error(p, p->current.line, p->current.col, "expected ']' after index");
                ast_free(left);
                ast_free(index_expr);
                return NULL;
            }
            advance(p); /* consume ']' */

            /* Build AST_INDEX node: left=array, right=index. */
            AstNode *node = malloc(sizeof(AstNode));
            if (!node) {
                ast_free(left);
                ast_free(index_expr);
                parser_error(p, bracket_line, bracket_col, "memory allocation failed");
                return NULL;
            }
            node->type = AST_INDEX;
            node->value = NULL;
            node->left = left;
            node->right = index_expr;
            node->grouped = false;
            node->line = bracket_line;
            node->children = NULL;
            node->child_count = 0;
            node->params = NULL;
            node->param_count = 0;
            left = node;
            continue;
        }

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

        AstNode *binop = make_binop(op_buf, op_len, left, right, op_line);
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

/*
 * Parse an if expression.
 * Syntax: if condition then body [else body] end
 *
 * Special case: "else if" is treated as a single construct, so
 * "if a then b else if c then d else e end" only needs one "end".
 */
static AstNode *parse_if(Parser *p) {
    if (p->has_error)
        return NULL;

    Token if_tok = p->current;

    /* Consume 'if' keyword (already verified by caller) */
    advance(p);
    skip_newlines(p);

    /* Parse condition - stops at 'then' keyword */
    if (token_is_keyword(&p->current, "then")) {
        parser_error(p, p->current.line, p->current.col, "expected condition after 'if'");
        return NULL;
    }

    AstNode *condition = parse_assignment(p);
    if (!condition)
        return NULL;

    skip_newlines(p);

    /* Expect 'then' */
    if (!token_is_keyword(&p->current, "then")) {
        parser_error(p, p->current.line, p->current.col, "expected 'then' after condition");
        ast_free(condition);
        return NULL;
    }
    advance(p); /* consume 'then' */

    /* Single-line mode: no newline after 'then' means body is one expression
     * on the same line. 'end' is accepted but not required. */
    if (p->current.type != TOK_NEWLINE) {
        /* Guard against empty body (e.g., "if true then else") */
        if (token_is_keyword(&p->current, "else") || token_is_keyword(&p->current, "end")) {
            parser_error(p, p->current.line, p->current.col, "expected expression in 'then' body");
            ast_free(condition);
            return NULL;
        }

        /* Parse single then-body expression */
        AstNode *then_body = parse_assignment(p);
        if (!then_body) {
            ast_free(condition);
            return NULL;
        }

        /* Check for 'else' */
        AstNode *else_body = NULL;
        if (token_is_keyword(&p->current, "else")) {
            advance(p); /* consume 'else' */

            /* "else if" chains recursively */
            if (token_is_keyword(&p->current, "if")) {
                else_body = parse_if(p);
            } else {
                /* Single else-body expression */
                if (token_is_keyword(&p->current, "end")) {
                    parser_error(p, p->current.line, p->current.col,
                                 "expected expression in 'else' body");
                    ast_free(condition);
                    ast_free(then_body);
                    return NULL;
                }
                else_body = parse_assignment(p);
            }
            if (!else_body) {
                ast_free(condition);
                ast_free(then_body);
                return NULL;
            }
        }

        /* Optional 'end' — accepted but not required in single-line mode */
        if (token_is_keyword(&p->current, "end")) {
            advance(p);
        }

        AstNode *if_node = make_if(condition, then_body, else_body, if_tok.line);
        if (!if_node) {
            ast_free(condition);
            ast_free(then_body);
            ast_free(else_body);
            parser_error(p, if_tok.line, if_tok.col, "memory allocation failed");
            return NULL;
        }
        return if_node;
    }

    /* Multi-line mode: newline after 'then', collect body until 'else'/'end'.
     * 'end' is mandatory. */
    skip_newlines(p);

    /* Parse then-body: collect expressions until 'else' or 'end' */
    if (token_is_keyword(&p->current, "else") || token_is_keyword(&p->current, "end")) {
        parser_error(p, p->current.line, p->current.col, "expected expression in 'then' body");
        ast_free(condition);
        return NULL;
    }

    PtrArray then_exprs;
    if (!ptr_array_init(&then_exprs, 4)) {
        parser_error(p, if_tok.line, if_tok.col, "memory allocation failed");
        ast_free(condition);
        return NULL;
    }

    /* Parse first then-body expression */
    AstNode *expr = parse_assignment(p);
    if (!expr) {
        ast_free(condition);
        ptr_array_destroy(&then_exprs);
        return NULL;
    }
    ptr_array_push(&then_exprs, expr);

    /* Parse additional newline-separated expressions in then-body */
    while (!p->has_error && p->current.type == TOK_NEWLINE) {
        skip_newlines(p);
        if (token_is_keyword(&p->current, "else") || token_is_keyword(&p->current, "end"))
            break;
        if (p->current.type == TOK_EOF)
            break;

        expr = parse_assignment(p);
        if (!expr) {
            ast_free(condition);
            free_expr_array(&then_exprs);
            return NULL;
        }
        if (!ptr_array_push(&then_exprs, expr)) {
            ast_free(expr);
            ast_free(condition);
            free_expr_array(&then_exprs);
            parser_error(p, if_tok.line, if_tok.col, "memory allocation failed");
            return NULL;
        }
    }

    /* Build then-body: unwrap if single expression */
    AstNode *then_body;
    if (then_exprs.count == 1) {
        then_body = (AstNode *)then_exprs.items[0];
        ptr_array_destroy(&then_exprs);
    } else {
        size_t count = then_exprs.count;
        AstNode **children = (AstNode **)ptr_array_release(&then_exprs);
        then_body = make_block(children, count, if_tok.line);
        if (!then_body) {
            for (size_t i = 0; i < count; i++)
                ast_free(children[i]);
            ptr_array_free_raw((void *)children);
            ast_free(condition);
            parser_error(p, if_tok.line, if_tok.col, "memory allocation failed");
            return NULL;
        }
    }

    skip_newlines(p);

    /* Check for 'else' or 'end' */
    AstNode *else_body = NULL;

    if (token_is_keyword(&p->current, "else")) {
        advance(p);
        skip_newlines(p);

        /* Special case: "else if" - parse as nested if-expression */
        if (token_is_keyword(&p->current, "if")) {
            else_body = parse_if(p);
            if (!else_body) {
                ast_free(condition);
                ast_free(then_body);
                return NULL;
            }
            /* No 'end' needed for outer if - the inner if consumed it */
        } else {
            /* Regular else body */
            if (token_is_keyword(&p->current, "end")) {
                parser_error(p, p->current.line, p->current.col,
                             "expected expression in 'else' body");
                ast_free(condition);
                ast_free(then_body);
                return NULL;
            }

            PtrArray else_exprs;
            if (!ptr_array_init(&else_exprs, 4)) {
                parser_error(p, if_tok.line, if_tok.col, "memory allocation failed");
                ast_free(condition);
                ast_free(then_body);
                return NULL;
            }

            /* Parse first else-body expression */
            expr = parse_assignment(p);
            if (!expr) {
                ast_free(condition);
                ast_free(then_body);
                ptr_array_destroy(&else_exprs);
                return NULL;
            }
            ptr_array_push(&else_exprs, expr);

            /* Parse additional newline-separated expressions in else-body */
            while (!p->has_error && p->current.type == TOK_NEWLINE) {
                skip_newlines(p);
                if (token_is_keyword(&p->current, "end"))
                    break;
                if (p->current.type == TOK_EOF)
                    break;

                expr = parse_assignment(p);
                if (!expr) {
                    ast_free(condition);
                    ast_free(then_body);
                    free_expr_array(&else_exprs);
                    return NULL;
                }
                if (!ptr_array_push(&else_exprs, expr)) {
                    ast_free(expr);
                    ast_free(condition);
                    ast_free(then_body);
                    free_expr_array(&else_exprs);
                    parser_error(p, if_tok.line, if_tok.col, "memory allocation failed");
                    return NULL;
                }
            }

            /* Build else-body: unwrap if single expression */
            if (else_exprs.count == 1) {
                else_body = (AstNode *)else_exprs.items[0];
                ptr_array_destroy(&else_exprs);
            } else {
                size_t count = else_exprs.count;
                AstNode **children = (AstNode **)ptr_array_release(&else_exprs);
                else_body = make_block(children, count, if_tok.line);
                if (!else_body) {
                    for (size_t i = 0; i < count; i++)
                        ast_free(children[i]);
                    ptr_array_free_raw((void *)children);
                    ast_free(condition);
                    ast_free(then_body);
                    parser_error(p, if_tok.line, if_tok.col, "memory allocation failed");
                    return NULL;
                }
            }

            skip_newlines(p);

            /* Expect 'end' */
            if (!token_is_keyword(&p->current, "end")) {
                parser_error(p, p->current.line, p->current.col, "expected 'end' to close 'if'");
                ast_free(condition);
                ast_free(then_body);
                ast_free(else_body);
                return NULL;
            }
            advance(p);
        }
    } else if (token_is_keyword(&p->current, "end")) {
        /* No else clause */
        advance(p);
    } else {
        parser_error(p, p->current.line, p->current.col, "expected 'else' or 'end'");
        ast_free(condition);
        ast_free(then_body);
        return NULL;
    }

    AstNode *if_node = make_if(condition, then_body, else_body, if_tok.line);
    if (!if_node) {
        ast_free(condition);
        ast_free(then_body);
        ast_free(else_body);
        parser_error(p, if_tok.line, if_tok.col, "memory allocation failed");
        return NULL;
    }

    return if_node;
}

/*
 * Create a while loop AST node.
 * Uses children array: children[0]=condition, children[1]=body.
 * Takes ownership of condition and body.
 */
static AstNode *make_while(AstNode *condition, AstNode *body, size_t line) {
    AstNode *node = malloc(sizeof(AstNode));
    if (!node)
        return NULL;

    AstNode **children = (AstNode **)malloc(2 * sizeof(AstNode *));
    if (!children) {
        free(node);
        return NULL;
    }

    children[0] = condition;
    children[1] = body;

    node->type = AST_WHILE;
    node->value = NULL;
    node->left = NULL;
    node->right = NULL;
    node->grouped = false;
    node->line = line;
    node->children = children;
    node->child_count = 2;
    node->params = NULL;
    node->param_count = 0;
    return node;
}

/*
 * Parse a while loop expression.
 * Syntax: while condition do body end
 *
 * The body can contain multiple newline-separated expressions.
 * Returns the last body value as the loop result.
 */
static AstNode *parse_while(Parser *p) {
    if (p->has_error)
        return NULL;

    Token while_tok = p->current;

    /* Consume 'while' keyword (already verified by caller) */
    advance(p);
    skip_newlines(p);

    /* Parse condition - stops at 'do' keyword */
    if (token_is_keyword(&p->current, "do")) {
        parser_error(p, p->current.line, p->current.col, "expected condition after 'while'");
        return NULL;
    }

    AstNode *condition = parse_assignment(p);
    if (!condition)
        return NULL;

    skip_newlines(p);

    /* Expect 'do' */
    if (!token_is_keyword(&p->current, "do")) {
        parser_error(p, p->current.line, p->current.col, "expected 'do' after condition");
        ast_free(condition);
        return NULL;
    }
    advance(p); /* consume 'do' */

    /* Single-line mode: no newline after 'do' means body is one expression
     * on the same line. 'end' is accepted but not required. */
    if (p->current.type != TOK_NEWLINE) {
        if (token_is_keyword(&p->current, "end")) {
            parser_error(p, p->current.line, p->current.col, "expected expression in 'while' body");
            ast_free(condition);
            return NULL;
        }

        AstNode *body = parse_assignment(p);
        if (!body) {
            ast_free(condition);
            return NULL;
        }

        /* Optional 'end' */
        if (token_is_keyword(&p->current, "end")) {
            advance(p);
        }

        AstNode *while_node = make_while(condition, body, while_tok.line);
        if (!while_node) {
            ast_free(condition);
            ast_free(body);
            parser_error(p, while_tok.line, while_tok.col, "memory allocation failed");
            return NULL;
        }
        return while_node;
    }

    /* Multi-line mode: newline after 'do', collect body until 'end'.
     * 'end' is mandatory. */
    skip_newlines(p);

    /* Parse body: collect expressions until 'end' */
    if (token_is_keyword(&p->current, "end")) {
        parser_error(p, p->current.line, p->current.col, "expected expression in 'while' body");
        ast_free(condition);
        return NULL;
    }

    PtrArray body_exprs;
    if (!ptr_array_init(&body_exprs, 4)) {
        parser_error(p, while_tok.line, while_tok.col, "memory allocation failed");
        ast_free(condition);
        return NULL;
    }

    /* Parse first body expression */
    AstNode *expr = parse_assignment(p);
    if (!expr) {
        ast_free(condition);
        ptr_array_destroy(&body_exprs);
        return NULL;
    }
    ptr_array_push(&body_exprs, expr);

    /* Parse additional newline-separated expressions in body */
    while (!p->has_error && p->current.type == TOK_NEWLINE) {
        skip_newlines(p);
        if (token_is_keyword(&p->current, "end"))
            break;
        if (p->current.type == TOK_EOF)
            break;

        expr = parse_assignment(p);
        if (!expr) {
            ast_free(condition);
            free_expr_array(&body_exprs);
            return NULL;
        }
        if (!ptr_array_push(&body_exprs, expr)) {
            ast_free(expr);
            ast_free(condition);
            free_expr_array(&body_exprs);
            parser_error(p, while_tok.line, while_tok.col, "memory allocation failed");
            return NULL;
        }
    }

    /* Build body: unwrap if single expression */
    AstNode *body;
    if (body_exprs.count == 1) {
        body = (AstNode *)body_exprs.items[0];
        ptr_array_destroy(&body_exprs);
    } else {
        size_t count = body_exprs.count;
        AstNode **children = (AstNode **)ptr_array_release(&body_exprs);
        body = make_block(children, count, while_tok.line);
        if (!body) {
            for (size_t i = 0; i < count; i++)
                ast_free(children[i]);
            ptr_array_free_raw((void *)children);
            ast_free(condition);
            parser_error(p, while_tok.line, while_tok.col, "memory allocation failed");
            return NULL;
        }
    }

    skip_newlines(p);

    /* Expect 'end' */
    if (!token_is_keyword(&p->current, "end")) {
        parser_error(p, p->current.line, p->current.col, "expected 'end' to close 'while'");
        ast_free(condition);
        ast_free(body);
        return NULL;
    }
    advance(p);

    AstNode *while_node = make_while(condition, body, while_tok.line);
    if (!while_node) {
        ast_free(condition);
        ast_free(body);
        parser_error(p, while_tok.line, while_tok.col, "memory allocation failed");
        return NULL;
    }

    return while_node;
}

/*
 * Helper: free a params array (array of owned strings).
 */
static void free_params(char **params, size_t count) {
    if (!params)
        return;
    for (size_t i = 0; i < count; i++) {
        free(params[i]);
    }
    free((void *)params);
}

/*
 * Parse a function definition expression.
 * Syntax: fn name(param1, param2, ...) is body end     (named)
 *         fn(param1, param2, ...) is body end           (anonymous)
 *
 * Returns an AST_FUNCTION node with:
 *   value = function name (NULL for anonymous functions)
 *   params = array of parameter name strings
 *   param_count = number of parameters
 *   left = body expression (single expr or AST_BLOCK)
 */
static AstNode *parse_fn(Parser *p) {
    if (p->has_error)
        return NULL;

    Token fn_tok = p->current;

    /* Consume 'fn' keyword (already verified by caller) */
    advance(p);

    /* Check if the next token is '(' — if so, this is an anonymous function.
     * Otherwise, expect a function name (identifier). */
    char *fn_name = NULL;
    if (p->current.type == TOK_OPERATOR && p->current.value_len == 1 &&
        p->current.value[0] == '(') {
        /* Anonymous function: fn_name stays NULL, don't advance past '(' yet. */
    } else if (p->current.type == TOK_IDENT && !is_reserved_keyword(&p->current)) {
        /* Named function: save the name. */
        fn_name = malloc(p->current.value_len + 1);
        if (!fn_name) {
            parser_error(p, fn_tok.line, fn_tok.col, "memory allocation failed");
            return NULL;
        }
        memcpy(fn_name, p->current.value, p->current.value_len);
        fn_name[p->current.value_len] = '\0';
        advance(p);
    } else {
        parser_error(p, p->current.line, p->current.col,
                     "expected function name or '(' after 'fn'");
        return NULL;
    }

    /* Expect '(' */
    if (p->current.type != TOK_OPERATOR || p->current.value_len != 1 ||
        p->current.value[0] != '(') {
        parser_error(p, p->current.line, p->current.col, "expected '(' after function name");
        free(fn_name);
        return NULL;
    }
    advance(p); /* consume '(' */

    /* Note: for anonymous functions, fn_name is NULL and is intentionally
     * threaded through the rest of this function unchanged. */

    /* Parse comma-separated parameter names */
    PtrArray param_names;
    if (!ptr_array_init(&param_names, 4)) {
        parser_error(p, fn_tok.line, fn_tok.col, "memory allocation failed");
        free(fn_name);
        return NULL;
    }

    /* Check for zero-param: immediate ')' */
    if (!(p->current.type == TOK_OPERATOR && p->current.value_len == 1 &&
          p->current.value[0] == ')')) {
        /* Parse first parameter name */
        if (p->current.type != TOK_IDENT || is_reserved_keyword(&p->current)) {
            parser_error(p, p->current.line, p->current.col, "expected parameter name");
            free(fn_name);
            ptr_array_destroy(&param_names);
            return NULL;
        }
        char *param = malloc(p->current.value_len + 1);
        if (!param) {
            parser_error(p, fn_tok.line, fn_tok.col, "memory allocation failed");
            free(fn_name);
            ptr_array_destroy(&param_names);
            return NULL;
        }
        memcpy(param, p->current.value, p->current.value_len);
        param[p->current.value_len] = '\0';
        ptr_array_push(&param_names, param);
        advance(p);

        /* Parse additional comma-separated parameter names */
        while (p->current.type == TOK_OPERATOR && p->current.value_len == 1 &&
               p->current.value[0] == ',') {
            advance(p); /* consume ',' */
            if (p->current.type != TOK_IDENT || is_reserved_keyword(&p->current)) {
                parser_error(p, p->current.line, p->current.col, "expected parameter name");
                free(fn_name);
                free_params((char **)param_names.items, param_names.count);
                param_names.items = NULL;
                param_names.count = 0;
                ptr_array_destroy(&param_names);
                return NULL;
            }
            param = malloc(p->current.value_len + 1);
            if (!param) {
                parser_error(p, fn_tok.line, fn_tok.col, "memory allocation failed");
                free(fn_name);
                free_params((char **)param_names.items, param_names.count);
                param_names.items = NULL;
                param_names.count = 0;
                ptr_array_destroy(&param_names);
                return NULL;
            }
            memcpy(param, p->current.value, p->current.value_len);
            param[p->current.value_len] = '\0';
            ptr_array_push(&param_names, param);
            advance(p);
        }
    }

    /* Expect ')' */
    if (p->current.type != TOK_OPERATOR || p->current.value_len != 1 ||
        p->current.value[0] != ')') {
        parser_error(p, p->current.line, p->current.col, "expected ')' after parameters");
        free(fn_name);
        free_params((char **)param_names.items, param_names.count);
        param_names.items = NULL;
        param_names.count = 0;
        ptr_array_destroy(&param_names);
        return NULL;
    }
    advance(p); /* consume ')' */

    skip_newlines(p);

    /* Expect 'is' */
    if (!token_is_keyword(&p->current, "is")) {
        parser_error(p, p->current.line, p->current.col, "expected 'is' after parameters");
        free(fn_name);
        free_params((char **)param_names.items, param_names.count);
        param_names.items = NULL;
        param_names.count = 0;
        ptr_array_destroy(&param_names);
        return NULL;
    }
    advance(p); /* consume 'is' */

    /* Single-line mode: no newline after 'is' means body is one expression
     * on the same line. 'end' is accepted but not required. */
    if (p->current.type != TOK_NEWLINE) {
        if (token_is_keyword(&p->current, "end")) {
            parser_error(p, p->current.line, p->current.col,
                         "expected expression in function body");
            free(fn_name);
            free_params((char **)param_names.items, param_names.count);
            param_names.items = NULL;
            param_names.count = 0;
            ptr_array_destroy(&param_names);
            return NULL;
        }

        AstNode *body = parse_assignment(p);
        if (!body) {
            free(fn_name);
            free_params((char **)param_names.items, param_names.count);
            param_names.items = NULL;
            param_names.count = 0;
            ptr_array_destroy(&param_names);
            return NULL;
        }

        /* Optional 'end' */
        if (token_is_keyword(&p->current, "end")) {
            advance(p);
        }

        /* Build AST_FUNCTION node */
        AstNode *node = malloc(sizeof(AstNode));
        if (!node) {
            free(fn_name);
            free_params((char **)param_names.items, param_names.count);
            param_names.items = NULL;
            param_names.count = 0;
            ptr_array_destroy(&param_names);
            ast_free(body);
            parser_error(p, fn_tok.line, fn_tok.col, "memory allocation failed");
            return NULL;
        }

        size_t pc = param_names.count;
        char **params_arr = NULL;
        if (pc > 0) {
            params_arr = (char **)ptr_array_release(&param_names);
        } else {
            ptr_array_destroy(&param_names);
        }

        node->type = AST_FUNCTION;
        node->value = fn_name;
        node->left = body;
        node->right = NULL;
        node->grouped = false;
        node->line = fn_tok.line;
        node->children = NULL;
        node->child_count = 0;
        node->params = params_arr;
        node->param_count = pc;

        return node;
    }

    /* Multi-line mode: newline after 'is', collect body until 'end'.
     * 'end' is mandatory. */
    skip_newlines(p);

    /* Parse body: collect expressions until 'end' */
    if (token_is_keyword(&p->current, "end")) {
        parser_error(p, p->current.line, p->current.col, "expected expression in function body");
        free(fn_name);
        free_params((char **)param_names.items, param_names.count);
        param_names.items = NULL;
        param_names.count = 0;
        ptr_array_destroy(&param_names);
        return NULL;
    }

    PtrArray body_exprs;
    if (!ptr_array_init(&body_exprs, 4)) {
        parser_error(p, fn_tok.line, fn_tok.col, "memory allocation failed");
        free(fn_name);
        free_params((char **)param_names.items, param_names.count);
        param_names.items = NULL;
        param_names.count = 0;
        ptr_array_destroy(&param_names);
        return NULL;
    }

    /* Parse first body expression */
    AstNode *expr = parse_assignment(p);
    if (!expr) {
        free(fn_name);
        free_params((char **)param_names.items, param_names.count);
        param_names.items = NULL;
        param_names.count = 0;
        ptr_array_destroy(&param_names);
        ptr_array_destroy(&body_exprs);
        return NULL;
    }
    ptr_array_push(&body_exprs, expr);

    /* Parse additional newline-separated expressions in body */
    while (!p->has_error && p->current.type == TOK_NEWLINE) {
        skip_newlines(p);
        if (token_is_keyword(&p->current, "end"))
            break;
        if (p->current.type == TOK_EOF)
            break;

        expr = parse_assignment(p);
        if (!expr) {
            free(fn_name);
            free_params((char **)param_names.items, param_names.count);
            param_names.items = NULL;
            param_names.count = 0;
            ptr_array_destroy(&param_names);
            free_expr_array(&body_exprs);
            return NULL;
        }
        if (!ptr_array_push(&body_exprs, expr)) {
            ast_free(expr);
            free(fn_name);
            free_params((char **)param_names.items, param_names.count);
            param_names.items = NULL;
            param_names.count = 0;
            ptr_array_destroy(&param_names);
            free_expr_array(&body_exprs);
            parser_error(p, fn_tok.line, fn_tok.col, "memory allocation failed");
            return NULL;
        }
    }

    /* Build body: unwrap if single expression */
    AstNode *body;
    if (body_exprs.count == 1) {
        body = (AstNode *)body_exprs.items[0];
        ptr_array_destroy(&body_exprs);
    } else {
        size_t count = body_exprs.count;
        AstNode **children = (AstNode **)ptr_array_release(&body_exprs);
        body = make_block(children, count, fn_tok.line);
        if (!body) {
            for (size_t i = 0; i < count; i++)
                ast_free(children[i]);
            ptr_array_free_raw((void *)children);
            free(fn_name);
            free_params((char **)param_names.items, param_names.count);
            param_names.items = NULL;
            param_names.count = 0;
            ptr_array_destroy(&param_names);
            parser_error(p, fn_tok.line, fn_tok.col, "memory allocation failed");
            return NULL;
        }
    }

    skip_newlines(p);

    /* Expect 'end' */
    if (!token_is_keyword(&p->current, "end")) {
        parser_error(p, p->current.line, p->current.col, "expected 'end' to close 'fn'");
        free(fn_name);
        free_params((char **)param_names.items, param_names.count);
        param_names.items = NULL;
        param_names.count = 0;
        ptr_array_destroy(&param_names);
        ast_free(body);
        return NULL;
    }
    advance(p);

    /* Build AST_FUNCTION node */
    AstNode *node = malloc(sizeof(AstNode));
    if (!node) {
        free(fn_name);
        free_params((char **)param_names.items, param_names.count);
        param_names.items = NULL;
        param_names.count = 0;
        ptr_array_destroy(&param_names);
        ast_free(body);
        parser_error(p, fn_tok.line, fn_tok.col, "memory allocation failed");
        return NULL;
    }

    /* Transfer param_names ownership to the node */
    size_t pc = param_names.count;
    char **params_arr = NULL;
    if (pc > 0) {
        params_arr = (char **)ptr_array_release(&param_names);
    } else {
        ptr_array_destroy(&param_names);
    }

    node->type = AST_FUNCTION;
    node->value = fn_name; /* takes ownership */
    node->left = body;     /* body expression */
    node->right = NULL;
    node->grouped = false;
    node->line = fn_tok.line;
    node->children = NULL;
    node->child_count = 0;
    node->params = params_arr;
    node->param_count = pc;

    return node;
}

/* ============================================================
 * Public API
 * ============================================================ */

/*
 * Helper: skip any consecutive newline tokens.
 */
static void skip_newlines(Parser *p) {
    while (p->current.type == TOK_NEWLINE) {
        advance(p);
    }
}

/*
 * Helper: free all expressions in a PtrArray and destroy the array.
 */
static void free_expr_array(PtrArray *arr) {
    for (size_t i = 0; i < arr->count; i++) {
        ast_free((AstNode *)arr->items[i]);
    }
    ptr_array_destroy(arr);
}

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

    /* Skip leading newlines */
    skip_newlines(&p);

    /* Check for empty input (only newlines/whitespace) */
    if (p.current.type == TOK_EOF) {
        parser_error(&p, p.current.line, p.current.col, "expected expression, got EOF");
        tokenizer_destroy(tok);
        return false;
    }

    /* Parse newline-separated expressions into a dynamic array */
    PtrArray exprs;
    if (!ptr_array_init(&exprs, 4)) {
        parser_error(&p, 1, 1, "memory allocation failed");
        tokenizer_destroy(tok);
        return false;
    }

    /* Parse first expression */
    AstNode *expr = parse_assignment(&p);
    if (p.has_error || !expr) {
        ast_free(expr); /* Free expr if it was allocated before error */
        ptr_array_destroy(&exprs);
        tokenizer_destroy(tok);
        return false;
    }
    ptr_array_push(&exprs, expr);

    /* Parse additional newline-separated expressions */
    while (!p.has_error && p.current.type == TOK_NEWLINE) {
        /* Skip consecutive newlines (blank lines) */
        skip_newlines(&p);

        /* Check if we've reached EOF after newlines */
        if (p.current.type == TOK_EOF) {
            break;
        }

        /* Check for tokenizer error */
        if (p.current.type == TOK_ERROR) {
            parser_error(&p, p.current.line, p.current.col, "%.*s", (int)p.current.value_len,
                         p.current.value);
            free_expr_array(&exprs);
            tokenizer_destroy(tok);
            return false;
        }

        /* Parse next expression */
        expr = parse_assignment(&p);
        if (p.has_error || !expr) {
            ast_free(expr); /* Free expr if it was partially allocated before error */
            free_expr_array(&exprs);
            tokenizer_destroy(tok);
            return false;
        }

        /* Push to array (auto-grows if needed) */
        if (!ptr_array_push(&exprs, expr)) {
            ast_free(expr);
            free_expr_array(&exprs);
            parser_error(&p, 1, 1, "memory allocation failed");
            tokenizer_destroy(tok);
            return false;
        }
    }

    if (p.has_error) {
        free_expr_array(&exprs);
        tokenizer_destroy(tok);
        return false;
    }

    /* Check for trailing tokens (not newline, not EOF) */
    if (p.current.type == TOK_ERROR) {
        parser_error(&p, p.current.line, p.current.col, "%.*s", (int)p.current.value_len,
                     p.current.value);
        free_expr_array(&exprs);
        tokenizer_destroy(tok);
        return false;
    }

    if (p.current.type != TOK_EOF && p.current.type != TOK_NEWLINE) {
        parser_error(&p, p.current.line, p.current.col, "unexpected extra token '%.*s'",
                     (int)p.current.value_len, p.current.value);
        free_expr_array(&exprs);
        tokenizer_destroy(tok);
        return false;
    }

    tokenizer_destroy(tok);

    /* If only one expression, return it directly (no block wrapper) */
    if (exprs.count == 1) {
        *out = (AstNode *)exprs.items[0];
        ptr_array_destroy(&exprs);
        return true;
    }

    /* Multiple expressions: wrap in AST_BLOCK */
    size_t count = exprs.count;
    AstNode **children = (AstNode **)ptr_array_release(&exprs);
    /* Use line 1 for the top-level block. */
    AstNode *block = make_block(children, count, 1);
    if (!block) {
        /* On failure, free_expr_array can't be used since array was released */
        for (size_t i = 0; i < count; i++) {
            ast_free(children[i]);
        }
        ptr_array_free_raw((void *)children);
        if (err) {
            err->line = 1;
            err->col = 1;
            snprintf(err->message, sizeof(err->message), "memory allocation failed");
        }
        return false;
    }

    *out = block;
    return true;
}

void ast_free(AstNode *node) {
    if (!node)
        return;
    ast_free(node->left);
    ast_free(node->right);
    /* Free children array for AST_BLOCK nodes */
    if (node->children) {
        for (size_t i = 0; i < node->child_count; i++) {
            ast_free(node->children[i]);
        }
        ptr_array_free_raw((void *)node->children);
    }
    /* Free params array for AST_FUNCTION nodes */
    if (node->params) {
        for (size_t i = 0; i < node->param_count; i++) {
            free(node->params[i]);
        }
        free((void *)node->params);
    }
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
    case AST_NOTHING:
        return "NOTHING";
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
    case AST_BLOCK:
        return "BLOCK";
    case AST_IF:
        return "IF";
    case AST_CALL:
        return "CALL";
    case AST_WHILE:
        return "WHILE";
    case AST_BREAK:
        return "BREAK";
    case AST_CONTINUE:
        return "CONTINUE";
    case AST_RETURN:
        return "RETURN";
    case AST_FUNCTION:
        return "FN";
    case AST_ARRAY:
        return "ARRAY";
    case AST_INDEX:
        return "INDEX";
    case AST_INDEX_ASSIGN:
        return "INDEX_ASSIGN";
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
        node->type == AST_BOOL || node->type == AST_NOTHING) {
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

    if (node->type == AST_BLOCK) {
        /* Block: [BLOCK child1 child2 ...] */
        /* First, format all children and calculate total length */
        PtrArray child_strs;
        if (!ptr_array_init(&child_strs, node->child_count))
            return NULL;

        size_t total_len = 1 + strlen(type_str); /* "[BLOCK" */
        for (size_t i = 0; i < node->child_count; i++) {
            char *str = ast_format_node(node->children[i]);
            if (!str) {
                /* Free already-allocated strings */
                for (size_t j = 0; j < child_strs.count; j++) {
                    free(child_strs.items[j]);
                }
                ptr_array_destroy(&child_strs);
                return NULL;
            }
            ptr_array_push(&child_strs, str);
            total_len += 1 + strlen(str); /* " child" */
        }
        total_len += 1 + 1; /* "]" + null terminator */

        char *buf = malloc(total_len);
        if (!buf) {
            for (size_t i = 0; i < child_strs.count; i++) {
                free(child_strs.items[i]);
            }
            ptr_array_destroy(&child_strs);
            return NULL;
        }

        /* Build the string */
        size_t pos = 0;
        pos += (size_t)snprintf(buf + pos, total_len - pos, "[%s", type_str);
        for (size_t i = 0; i < child_strs.count; i++) {
            pos += (size_t)snprintf(buf + pos, total_len - pos, " %s", (char *)child_strs.items[i]);
            free(child_strs.items[i]);
        }
        snprintf(buf + pos, total_len - pos, "]");
        ptr_array_destroy(&child_strs);
        return buf;
    }

    if (node->type == AST_CALL) {
        /* Call: [CALL name arg1 arg2 ...]
         * value = function name
         * children[0..n] = arguments */

        /* Calculate total length: "[CALL name" + args + "]" */
        PtrArray arg_strs;
        if (!ptr_array_init(&arg_strs, node->child_count))
            return NULL;

        size_t total_len = 1 + strlen(type_str) + 1 + strlen(node->value); /* "[CALL name" */
        for (size_t i = 0; i < node->child_count; i++) {
            char *str = ast_format_node(node->children[i]);
            if (!str) {
                for (size_t j = 0; j < arg_strs.count; j++) {
                    free(arg_strs.items[j]);
                }
                ptr_array_destroy(&arg_strs);
                return NULL;
            }
            ptr_array_push(&arg_strs, str);
            total_len += 1 + strlen(str); /* " arg" */
        }
        total_len += 1 + 1; /* "]" + null terminator */

        char *buf = malloc(total_len);
        if (!buf) {
            for (size_t i = 0; i < arg_strs.count; i++) {
                free(arg_strs.items[i]);
            }
            ptr_array_destroy(&arg_strs);
            return NULL;
        }

        /* Build the string */
        size_t pos = 0;
        pos += (size_t)snprintf(buf + pos, total_len - pos, "[%s %s", type_str, node->value);
        for (size_t i = 0; i < arg_strs.count; i++) {
            pos += (size_t)snprintf(buf + pos, total_len - pos, " %s", (char *)arg_strs.items[i]);
            free(arg_strs.items[i]);
        }
        snprintf(buf + pos, total_len - pos, "]");
        ptr_array_destroy(&arg_strs);
        return buf;
    }

    if (node->type == AST_IF) {
        /* If: [IF cond then-body [else-body]]
         * children[0] = condition
         * children[1] = then-body
         * children[2] = else-body (optional) */
        if (node->child_count < 2)
            return NULL;

        char *cond_str = ast_format_node(node->children[0]);
        char *then_str = ast_format_node(node->children[1]);
        if (!cond_str || !then_str) {
            free(cond_str);
            free(then_str);
            return NULL;
        }

        char *else_str = NULL;
        if (node->child_count >= 3) {
            else_str = ast_format_node(node->children[2]);
            if (!else_str) {
                free(cond_str);
                free(then_str);
                return NULL;
            }
        }

        size_t len = 1 + strlen(type_str) + 1 + strlen(cond_str) + 1 + strlen(then_str);
        if (else_str) {
            len += 1 + strlen(else_str);
        }
        len += 1 + 1; /* "]" + null terminator */

        char *buf = malloc(len);
        if (!buf) {
            free(cond_str);
            free(then_str);
            free(else_str);
            return NULL;
        }

        if (else_str) {
            snprintf(buf, len, "[%s %s %s %s]", type_str, cond_str, then_str, else_str);
        } else {
            snprintf(buf, len, "[%s %s %s]", type_str, cond_str, then_str);
        }

        free(cond_str);
        free(then_str);
        free(else_str);
        return buf;
    }

    if (node->type == AST_WHILE) {
        /* While: [WHILE cond body]
         * children[0] = condition
         * children[1] = body */
        if (node->child_count < 2)
            return NULL;

        char *cond_str = ast_format_node(node->children[0]);
        char *body_str = ast_format_node(node->children[1]);
        if (!cond_str || !body_str) {
            free(cond_str);
            free(body_str);
            return NULL;
        }

        size_t len = 1 + strlen(type_str) + 1 + strlen(cond_str) + 1 + strlen(body_str) + 1 + 1;
        char *buf = malloc(len);
        if (!buf) {
            free(cond_str);
            free(body_str);
            return NULL;
        }

        snprintf(buf, len, "[%s %s %s]", type_str, cond_str, body_str);
        free(cond_str);
        free(body_str);
        return buf;
    }

    if (node->type == AST_BREAK) {
        /* Break: [BREAK] or [BREAK expr] */
        if (node->left) {
            char *val_str = ast_format_node(node->left);
            if (!val_str)
                return NULL;
            size_t len = 1 + strlen(type_str) + 1 + strlen(val_str) + 1 + 1;
            char *buf = malloc(len);
            if (!buf) {
                free(val_str);
                return NULL;
            }
            snprintf(buf, len, "[%s %s]", type_str, val_str);
            free(val_str);
            return buf;
        }
        /* Bare break: [BREAK] */
        size_t len = 1 + strlen(type_str) + 1 + 1;
        char *buf = malloc(len);
        if (!buf)
            return NULL;
        snprintf(buf, len, "[%s]", type_str);
        return buf;
    }

    if (node->type == AST_CONTINUE) {
        /* Continue: [CONTINUE] */
        size_t len = 1 + strlen(type_str) + 1 + 1;
        char *buf = malloc(len);
        if (!buf)
            return NULL;
        snprintf(buf, len, "[%s]", type_str);
        return buf;
    }

    if (node->type == AST_RETURN) {
        /* Return: [RETURN] or [RETURN expr] */
        if (node->left) {
            char *val_str = ast_format_node(node->left);
            if (!val_str)
                return NULL;
            size_t len = 1 + strlen(type_str) + 1 + strlen(val_str) + 1 + 1;
            char *buf = malloc(len);
            if (!buf) {
                free(val_str);
                return NULL;
            }
            snprintf(buf, len, "[%s %s]", type_str, val_str);
            free(val_str);
            return buf;
        }
        /* Bare return: [RETURN] */
        size_t len = 1 + strlen(type_str) + 1 + 1;
        char *buf = malloc(len);
        if (!buf)
            return NULL;
        snprintf(buf, len, "[%s]", type_str);
        return buf;
    }

    if (node->type == AST_FUNCTION) {
        /* Function: [FN name(a, b) body] for named, [FN (a, b) body] for anonymous.
         * value = function name (NULL for anonymous)
         * params = parameter name strings
         * left = body expression */
        char *body_str = ast_format_node(node->left);
        if (!body_str)
            return NULL;

        /* Build params string: "a, b" or "" */
        size_t params_len = 0;
        for (size_t i = 0; i < node->param_count; i++) {
            params_len += strlen(node->params[i]);
            if (i > 0)
                params_len += 2; /* ", " */
        }

        /* Total: "[FN name(params) body]" or "[FN (params) body]"
         * Named:    "[" + type + " " + name + "(" + params + ") " + body + "]" + NUL
         * Anonymous: "[" + type + " " + "(" + params + ") " + body + "]" + NUL */
        size_t name_len = node->value ? strlen(node->value) : 0;
        size_t len =
            1 + strlen(type_str) + 1 + name_len + 1 + params_len + 2 + strlen(body_str) + 1 + 1;
        char *buf = malloc(len);
        if (!buf) {
            free(body_str);
            return NULL;
        }

        size_t pos = 0;
        if (node->value) {
            pos += (size_t)snprintf(buf + pos, len - pos, "[%s %s(", type_str, node->value);
        } else {
            pos += (size_t)snprintf(buf + pos, len - pos, "[%s (", type_str);
        }
        for (size_t i = 0; i < node->param_count; i++) {
            if (i > 0)
                pos += (size_t)snprintf(buf + pos, len - pos, ", ");
            pos += (size_t)snprintf(buf + pos, len - pos, "%s", node->params[i]);
        }
        snprintf(buf + pos, len - pos, ") %s]", body_str);
        free(body_str);
        return buf;
    }

    if (node->type == AST_INDEX) {
        /* Index read: [INDEX [array] [index]] */
        char *arr_str = ast_format_node(node->left);
        char *idx_str = ast_format_node(node->right);
        if (!arr_str || !idx_str) {
            free(arr_str);
            free(idx_str);
            return NULL;
        }
        size_t len = 1 + strlen(type_str) + 1 + strlen(arr_str) + 1 + strlen(idx_str) + 1 + 1;
        char *buf = malloc(len);
        if (!buf) {
            free(arr_str);
            free(idx_str);
            return NULL;
        }
        snprintf(buf, len, "[%s %s %s]", type_str, arr_str, idx_str);
        free(arr_str);
        free(idx_str);
        return buf;
    }

    if (node->type == AST_INDEX_ASSIGN) {
        /* Index write: [INDEX_ASSIGN [array] [index] [value]]
         * left = array expr, right = index expr, children[0] = value expr */
        char *arr_str = ast_format_node(node->left);
        char *idx_str = ast_format_node(node->right);
        char *val_str = (node->child_count > 0) ? ast_format_node(node->children[0]) : NULL;
        if (!arr_str || !idx_str || !val_str) {
            free(arr_str);
            free(idx_str);
            free(val_str);
            return NULL;
        }
        size_t len = 1 + strlen(type_str) + 1 + strlen(arr_str) + 1 + strlen(idx_str) + 1 +
                     strlen(val_str) + 1 + 1;
        char *buf = malloc(len);
        if (!buf) {
            free(arr_str);
            free(idx_str);
            free(val_str);
            return NULL;
        }
        snprintf(buf, len, "[%s %s %s %s]", type_str, arr_str, idx_str, val_str);
        free(arr_str);
        free(idx_str);
        free(val_str);
        return buf;
    }

    if (node->type == AST_ARRAY) {
        /* Array: [ARRAY elem1 elem2 ...] or [ARRAY] for empty.
         * children[0..n] = element expressions. */
        if (node->child_count == 0) {
            size_t len = 1 + strlen(type_str) + 1 + 1;
            char *buf = malloc(len);
            if (!buf)
                return NULL;
            snprintf(buf, len, "[%s]", type_str);
            return buf;
        }

        /* Format all children and calculate total length. */
        PtrArray child_strs;
        if (!ptr_array_init(&child_strs, node->child_count))
            return NULL;

        size_t total_len = 1 + strlen(type_str); /* "[ARRAY" */
        for (size_t i = 0; i < node->child_count; i++) {
            char *str = ast_format_node(node->children[i]);
            if (!str) {
                for (size_t j = 0; j < child_strs.count; j++)
                    free(child_strs.items[j]);
                ptr_array_destroy(&child_strs);
                return NULL;
            }
            ptr_array_push(&child_strs, str);
            total_len += 1 + strlen(str); /* " elem" */
        }
        total_len += 1 + 1; /* "]" + NUL */

        char *buf = malloc(total_len);
        if (!buf) {
            for (size_t i = 0; i < child_strs.count; i++)
                free(child_strs.items[i]);
            ptr_array_destroy(&child_strs);
            return NULL;
        }

        size_t pos = 0;
        pos += (size_t)snprintf(buf + pos, total_len - pos, "[%s", type_str);
        for (size_t i = 0; i < child_strs.count; i++) {
            pos += (size_t)snprintf(buf + pos, total_len - pos, " %s", (char *)child_strs.items[i]);
            free(child_strs.items[i]);
        }
        snprintf(buf + pos, total_len - pos, "]");
        ptr_array_destroy(&child_strs);
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

/*
 * Check if input is a complete expression (for REPL multiline support).
 *
 * Returns true if input parses successfully or has a "real" syntax error.
 * Returns false if input is incomplete and could be completed by adding more.
 *
 * Incomplete patterns detected:
 *   - Unterminated string (tokenizer error)
 *   - Expected expression at EOF (trailing operator, empty after keyword)
 *   - Expected ')' (unclosed parenthesis)
 *   - Expected 'then', 'end', 'else' (unclosed if/then/else)
 */
/*
 * Helper: check if string contains only whitespace (spaces, tabs, newlines)
 * and/or comments (# to end of line). This ensures that comment-only input
 * like "# hello" is treated as complete (empty), not sent to the parser
 * where it would hit EOF and produce a misleading "incomplete" classification.
 */
static bool is_whitespace_only(const char *s) {
    while (*s) {
        if (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') {
            s++;
        } else if (*s == '#') {
            /* Skip comment: consume everything until newline or end of string */
            s++;
            while (*s && *s != '\n' && *s != '\r') {
                s++;
            }
        } else {
            return false;
        }
    }
    return true;
}

bool parser_is_complete(const char *input) {
    /* NULL or empty input is considered complete (empty is an error, but not incomplete) */
    if (!input || *input == '\0') {
        return true;
    }

    /* Whitespace-only input is also complete (empty error, not incomplete) */
    if (is_whitespace_only(input)) {
        return true;
    }

    /* Try to parse the input */
    AstNode *node = NULL;
    ParseError err = {0};
    bool parsed = parser_parse(input, &node, &err);

    if (parsed) {
        /* Parsed successfully - complete */
        ast_free(node);
        return true;
    }

    /* Parse failed - check if it's an incomplete input or a real error.
     *
     * Incomplete indicators (could be fixed by adding more input):
     * - "unterminated string" - needs closing quote
     * - "expected expression, got EOF" - needs more expression (trailing op)
     * - "expected ')'" - needs closing paren
     * - "expected 'then'" at EOF - needs then keyword
     * - "expected 'end'" at EOF - needs end keyword
     * - "expected 'else' or 'end'" - needs else/end
     * - "expected expression in '...' body" at EOF - needs body
     *
     * Real errors (can't be fixed by adding more):
     * - "unexpected extra token" - too much input
     * - "expected condition after 'if'" - missing required part, not at EOF
     * - Other structural errors
     */

    const char *msg = err.message;

    /* Unterminated string is always incomplete */
    if (strstr(msg, "unterminated string") != NULL) {
        return false;
    }

    /* "expected expression, got EOF" means we need more input */
    if (strcmp(msg, "expected expression, got EOF") == 0) {
        return false;
    }

    /* "expected expression, got newline" at the end of input means incomplete */
    if (strcmp(msg, "expected expression, got newline") == 0) {
        return false;
    }

    /* Check for "expected '...'" patterns that indicate unclosed constructs.
     * These are incomplete only if we're expecting something that can be added.
     * We distinguish by checking if the message suggests EOF was reached. */
    if (strstr(msg, "expected ')'") != NULL) {
        /* Unclosed parenthesis - incomplete */
        return false;
    }

    if (strstr(msg, "expected ']'") != NULL) {
        /* Unclosed array bracket - incomplete */
        return false;
    }

    if (strstr(msg, "expected 'then'") != NULL) {
        /* if without then - incomplete */
        return false;
    }

    if (strstr(msg, "expected 'end'") != NULL) {
        /* if/else/while without end - incomplete */
        return false;
    }

    if (strstr(msg, "expected 'do'") != NULL) {
        /* while without do - incomplete */
        return false;
    }

    if (strstr(msg, "expected 'is'") != NULL) {
        /* fn without is - incomplete */
        return false;
    }

    if (strstr(msg, "expected 'else' or 'end'") != NULL) {
        /* if body without else/end - incomplete */
        return false;
    }

    /* "expected expression in '...' body" means empty body - but we need to
     * distinguish between "if true then else" (real error) and "if true then"
     * followed by EOF (incomplete). The former produces this error when 'else'
     * is the next token; the latter produces "expected expression, got EOF". */

    /* Default: treat as complete (real syntax error) */
    return true;
}
