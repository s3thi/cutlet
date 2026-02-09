/*
 * compiler.h - Cutlet bytecode compiler
 *
 * Walks an AST and emits bytecode into a Chunk. The compiler
 * handles all node types: literals, binary/unary operators,
 * variable declarations/assignments, function calls, blocks,
 * if/else, and logical short-circuit operators.
 *
 * Usage:
 *   CompileError err;
 *   Chunk *chunk = compile(ast_node, &err);
 *   if (!chunk) { handle err.message ... }
 *   // use chunk with VM
 *   chunk_free(chunk); free(chunk);
 */

#ifndef CUTLET_COMPILER_H
#define CUTLET_COMPILER_H

#include "chunk.h"
#include "parser.h"

/* Error information from the compiler. */
typedef struct {
    char message[256];
} CompileError;

/*
 * Compile an AST node tree into a bytecode Chunk.
 *
 * Returns a heap-allocated Chunk on success. The caller must
 * call chunk_free() then free() on the result.
 *
 * Returns NULL on error, populating err with the error message.
 */
Chunk *compile(const AstNode *node, CompileError *err);

#endif /* CUTLET_COMPILER_H */
