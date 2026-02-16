/*
 * compiler.c - Cutlet bytecode compiler
 *
 * Single-pass compiler that walks the AST and emits bytecode
 * into a Chunk. Jump instructions use forward patching: emit
 * a placeholder offset, then patch it once the target is known.
 */

#include "compiler.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Internal compiler state. */
typedef struct {
    Chunk *chunk;      /* The chunk being compiled into. */
    CompileError *err; /* Where to report errors. */
    bool had_error;    /* Set to true on first error. */
} Compiler;

/* ---- Helpers ---- */

/* Report a compile error. Only the first error is recorded. */
static void compiler_error(Compiler *c, const char *fmt, ...) {
    if (c->had_error)
        return;
    c->had_error = true;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(c->err->message, sizeof(c->err->message), fmt, ap);
    va_end(ap); // NOLINT(clang-analyzer-valist.Uninitialized)
}

/* Duplicate a string, setting compiler error on failure. Returns NULL on error. */
static char *compiler_strdup(Compiler *c, const char *s) {
    char *dup = strdup(s);
    if (!dup)
        compiler_error(c, "memory allocation failed");
    return dup;
}

/* Emit a single byte. Sets had_error on allocation failure. */
static void emit_byte(Compiler *c, uint8_t byte, int line) {
    if (c->had_error)
        return;
    if (!chunk_write(c->chunk, byte, line)) {
        compiler_error(c, "memory allocation failed");
    }
}

/* Emit two bytes. */
static void emit_bytes(Compiler *c, uint8_t b1, uint8_t b2, int line) {
    emit_byte(c, b1, line);
    emit_byte(c, b2, line);
}

/* Emit a constant instruction: OP_CONSTANT + index. */
static void emit_constant(Compiler *c, Value value, int line) {
    int idx = chunk_find_or_add_constant(c->chunk, value);
    if (idx < 0) {
        compiler_error(c, "too many constants in one chunk");
        return;
    }
    emit_bytes(c, OP_CONSTANT, (uint8_t)idx, line);
}

/*
 * Emit a jump instruction with a placeholder 2-byte offset.
 * Returns the offset of the placeholder bytes (for patching).
 */
static size_t emit_jump(Compiler *c, uint8_t op, int line) {
    emit_byte(c, op, line);
    emit_byte(c, 0xFF, line); /* placeholder high byte */
    emit_byte(c, 0xFF, line); /* placeholder low byte */
    return c->chunk->count - 2;
}

/*
 * Patch a previously emitted jump instruction.
 * offset is the location of the placeholder bytes.
 * The jump target is the current chunk->count (i.e. the next instruction).
 */
static void patch_jump(Compiler *c, size_t offset) {
    /* The jump offset is relative to the instruction AFTER the jump. */
    size_t jump = c->chunk->count - (offset + 2);
    if (jump > UINT16_MAX) {
        compiler_error(c, "jump offset too large");
        return;
    }
    c->chunk->code[offset] = (uint8_t)((jump >> 8) & 0xFF);
    c->chunk->code[offset + 1] = (uint8_t)(jump & 0xFF);
}

/* ---- AST node compilation ---- */

/* Forward declaration: compile any AST node. */
static void compile_node(Compiler *c, const AstNode *node);

/* Compile a number literal. */
static void compile_number(Compiler *c, const AstNode *node) {
    double n = strtod(node->value, NULL);
    emit_constant(c, make_number(n), (int)node->line);
}

/* Compile a string literal. */
static void compile_string(Compiler *c, const AstNode *node) {
    char *s = compiler_strdup(c, node->value);
    if (!s)
        return;
    emit_constant(c, make_string(s), (int)node->line);
}

/* Compile a boolean literal. */
static void compile_bool(Compiler *c, const AstNode *node) {
    if (strcmp(node->value, "true") == 0) {
        emit_byte(c, OP_TRUE, (int)node->line);
    } else {
        emit_byte(c, OP_FALSE, (int)node->line);
    }
}

/* Compile a nothing literal. */
static void compile_nothing(Compiler *c, const AstNode *node) {
    emit_byte(c, OP_NOTHING, (int)node->line);
}

/* Compile an identifier (variable read). */
static void compile_ident(Compiler *c, const AstNode *node) {
    char *name = compiler_strdup(c, node->value);
    if (!name)
        return;
    int idx = chunk_find_or_add_constant(c->chunk, make_string(name));
    if (idx < 0) {
        compiler_error(c, "too many constants");
        return;
    }
    emit_bytes(c, OP_GET_GLOBAL, (uint8_t)idx, (int)node->line);
}

/* Compile a binary operator expression. */
static void compile_binop(Compiler *c, const AstNode *node) {
    const char *op = node->value;
    int line = (int)node->line;

    /* Short-circuit: and */
    if (strcmp(op, "and") == 0) {
        compile_node(c, node->left);
        size_t end_jump = emit_jump(c, OP_JUMP_IF_FALSE, line);
        emit_byte(c, OP_POP, line); /* discard truthy left */
        compile_node(c, node->right);
        patch_jump(c, end_jump);
        return;
    }

    /* Short-circuit: or */
    if (strcmp(op, "or") == 0) {
        compile_node(c, node->left);
        size_t end_jump = emit_jump(c, OP_JUMP_IF_TRUE, line);
        emit_byte(c, OP_POP, line); /* discard falsy left */
        compile_node(c, node->right);
        patch_jump(c, end_jump);
        return;
    }

    /* Non-short-circuit: compile both operands then the operator. */
    compile_node(c, node->left);
    compile_node(c, node->right);

    if (strcmp(op, "+") == 0) {
        emit_byte(c, OP_ADD, line);
    } else if (strcmp(op, "-") == 0) {
        emit_byte(c, OP_SUBTRACT, line);
    } else if (strcmp(op, "*") == 0) {
        emit_byte(c, OP_MULTIPLY, line);
    } else if (strcmp(op, "/") == 0) {
        emit_byte(c, OP_DIVIDE, line);
    } else if (strcmp(op, "%") == 0) {
        emit_byte(c, OP_MODULO, line);
    } else if (strcmp(op, "**") == 0) {
        emit_byte(c, OP_POWER, line);
    } else if (strcmp(op, "==") == 0) {
        emit_byte(c, OP_EQUAL, line);
    } else if (strcmp(op, "!=") == 0) {
        emit_byte(c, OP_NOT_EQUAL, line);
    } else if (strcmp(op, "<") == 0) {
        emit_byte(c, OP_LESS, line);
    } else if (strcmp(op, ">") == 0) {
        emit_byte(c, OP_GREATER, line);
    } else if (strcmp(op, "<=") == 0) {
        emit_byte(c, OP_LESS_EQUAL, line);
    } else if (strcmp(op, ">=") == 0) {
        emit_byte(c, OP_GREATER_EQUAL, line);
    } else {
        compiler_error(c, "unknown operator '%s'", op);
    }
}

/* Compile a unary operator expression. */
static void compile_unary(Compiler *c, const AstNode *node) {
    compile_node(c, node->left);
    if (strcmp(node->value, "-") == 0) {
        emit_byte(c, OP_NEGATE, (int)node->line);
    } else if (strcmp(node->value, "not") == 0) {
        emit_byte(c, OP_NOT, (int)node->line);
    } else {
        compiler_error(c, "unknown unary operator '%s'", node->value);
    }
}

/* Compile a variable declaration: my name = expr */
static void compile_decl(Compiler *c, const AstNode *node) {
    compile_node(c, node->left); /* compile the RHS expression */
    char *name = compiler_strdup(c, node->value);
    if (!name)
        return;
    int idx = chunk_find_or_add_constant(c->chunk, make_string(name));
    if (idx < 0) {
        compiler_error(c, "too many constants");
        return;
    }
    emit_bytes(c, OP_DEFINE_GLOBAL, (uint8_t)idx, (int)node->line);
}

/* Compile an assignment: name = expr */
static void compile_assign(Compiler *c, const AstNode *node) {
    compile_node(c, node->left); /* compile the RHS expression */
    char *name = compiler_strdup(c, node->value);
    if (!name)
        return;
    int idx = chunk_find_or_add_constant(c->chunk, make_string(name));
    if (idx < 0) {
        compiler_error(c, "too many constants");
        return;
    }
    emit_bytes(c, OP_SET_GLOBAL, (uint8_t)idx, (int)node->line);
}

/* Compile a block: sequence of expressions. */
static void compile_block(Compiler *c, const AstNode *node) {
    if (node->child_count == 0) {
        emit_byte(c, OP_NOTHING, (int)node->line);
        return;
    }
    for (size_t i = 0; i < node->child_count; i++) {
        compile_node(c, node->children[i]);
        /* Pop all but the last value (the block's result). */
        if (i < node->child_count - 1) {
            emit_byte(c, OP_POP, (int)node->children[i]->line);
        }
    }
}

/* Compile a function call: name(arg1, arg2, ...) */
static void compile_call(Compiler *c, const AstNode *node) {
    /* Check argc limit before emitting anything. */
    if (node->child_count > 255) {
        compiler_error(c, "too many arguments (max 255)");
        return;
    }
    /* Compile all arguments (push them onto the stack). */
    for (size_t i = 0; i < node->child_count; i++) {
        compile_node(c, node->children[i]);
    }
    /* Emit OP_CALL with function name index and argc. */
    char *fn_name = compiler_strdup(c, node->value);
    if (!fn_name)
        return;
    int name_idx = chunk_find_or_add_constant(c->chunk, make_string(fn_name));
    if (name_idx < 0) {
        compiler_error(c, "too many constants");
        return;
    }
    int line = (int)node->line;
    emit_byte(c, OP_CALL, line);
    emit_byte(c, (uint8_t)name_idx, line);
    emit_byte(c, (uint8_t)node->child_count, line);
}

/*
 * Compile an if/else expression.
 *
 * children[0] = condition
 * children[1] = then-body
 * children[2] = else-body (optional)
 *
 * Always leaves exactly one value on the stack.
 */
static void compile_if(Compiler *c, const AstNode *node) {
    if (node->child_count < 2) {
        compiler_error(c, "malformed if expression");
        return;
    }

    int line = (int)node->line;

    /* Compile condition. */
    compile_node(c, node->children[0]);

    /* Jump to else if condition is false. */
    size_t else_jump = emit_jump(c, OP_JUMP_IF_FALSE, line);

    /* Pop the condition value (it was truthy). */
    emit_byte(c, OP_POP, line);

    /* Compile then-body. */
    compile_node(c, node->children[1]);

    /* Jump over else-body. */
    size_t end_jump = emit_jump(c, OP_JUMP, line);

    /* Else label: patch the else_jump to here. */
    patch_jump(c, else_jump);

    /* Pop the condition value (it was falsy). */
    emit_byte(c, OP_POP, line);

    if (node->child_count >= 3) {
        /* Compile else-body. */
        compile_node(c, node->children[2]);
    } else {
        /* No else clause: push nothing. */
        emit_byte(c, OP_NOTHING, line);
    }

    /* End label: patch the end_jump to here. */
    patch_jump(c, end_jump);
}

/* Compile any AST node by dispatching on its type. */
static void compile_node(Compiler *c, const AstNode *node) {
    if (c->had_error)
        return;
    if (!node) {
        compiler_error(c, "null AST node");
        return;
    }

    switch (node->type) {
    case AST_NUMBER:
        compile_number(c, node);
        break;
    case AST_STRING:
        compile_string(c, node);
        break;
    case AST_BOOL:
        compile_bool(c, node);
        break;
    case AST_NOTHING:
        compile_nothing(c, node);
        break;
    case AST_IDENT:
        compile_ident(c, node);
        break;
    case AST_BINOP:
        compile_binop(c, node);
        break;
    case AST_UNARY:
        compile_unary(c, node);
        break;
    case AST_DECL:
        compile_decl(c, node);
        break;
    case AST_ASSIGN:
        compile_assign(c, node);
        break;
    case AST_BLOCK:
        compile_block(c, node);
        break;
    case AST_CALL:
        compile_call(c, node);
        break;
    case AST_IF:
        compile_if(c, node);
        break;
    default:
        compiler_error(c, "unknown AST node type %d", node->type);
        break;
    }
}

Chunk *compile(const AstNode *node, CompileError *err) {
    Chunk *chunk = malloc(sizeof(Chunk));
    if (!chunk) {
        if (err)
            snprintf(err->message, sizeof(err->message), "out of memory");
        return NULL;
    }
    chunk_init(chunk);

    Compiler c = {.chunk = chunk, .err = err, .had_error = false};

    compile_node(&c, node);

    if (c.had_error) {
        chunk_free(chunk);
        free(chunk);
        return NULL;
    }

    /* Emit OP_RETURN to end execution. Use the last node's line. */
    emit_byte(&c, OP_RETURN, (int)node->line);

    return chunk;
}
