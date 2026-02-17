/*
 * chunk.h - Bytecode chunk for the Cutlet VM
 *
 * A Chunk holds a sequence of bytecode instructions, a constants pool
 * (for numbers, strings, and identifier names), and per-byte source
 * line information for error reporting.
 *
 * Opcodes use a simple encoding:
 * - Most opcodes are a single byte.
 * - OP_CONSTANT, OP_DEFINE_GLOBAL, OP_GET_GLOBAL, OP_SET_GLOBAL have
 *   a 1-byte constant index operand.
 * - OP_CALL has a 1-byte constant index (name) + 1-byte argc.
 * - OP_JUMP, OP_JUMP_IF_FALSE, OP_JUMP_IF_TRUE have a 2-byte offset.
 */

#ifndef CUTLET_CHUNK_H
#define CUTLET_CHUNK_H

#include "value.h"
#include <stddef.h>
#include <stdint.h>

/* Bytecode opcodes for the Cutlet VM. */
typedef enum {
    /* Constants & literals */
    OP_CONSTANT, /* Push constant from pool. Operand: 1-byte index */
    OP_TRUE,     /* Push boolean true */
    OP_FALSE,    /* Push boolean false */
    OP_NOTHING,  /* Push nothing */

    /* Arithmetic (pop 2, push 1) */
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_MODULO,
    OP_POWER,
    OP_NEGATE, /* Unary minus (pop 1, push 1) */

    /* String concatenation (pop 2, push 1 string) */
    OP_CONCAT,

    /* Comparison (pop 2, push 1 bool) */
    OP_EQUAL,
    OP_NOT_EQUAL,
    OP_LESS,
    OP_GREATER,
    OP_LESS_EQUAL,
    OP_GREATER_EQUAL,

    /* Logical */
    OP_NOT, /* Pop 1, push bool */

    /* Variables (name-based globals) */
    OP_DEFINE_GLOBAL, /* Operand: constant index (name). Peek TOS, store clone. */
    OP_GET_GLOBAL,    /* Operand: constant index (name). Push clone. */
    OP_SET_GLOBAL,    /* Operand: constant index (name). Peek TOS, update. */

    /* Control flow */
    OP_JUMP,          /* Unconditional jump. Operand: 2-byte offset */
    OP_JUMP_IF_FALSE, /* Jump if TOS is falsy (does NOT pop) */
    OP_JUMP_IF_TRUE,  /* Jump if TOS is truthy (does NOT pop) */
    OP_LOOP,          /* Backward jump. Operand: 2-byte offset to subtract from IP */

    /* Stack management */
    OP_POP, /* Discard TOS */

    /* Built-in function calls */
    OP_CALL, /* Operand: constant index (fn name), 1-byte argc */

    /* End of program */
    OP_RETURN, /* End execution. TOS is the result. */
} OpCode;

/*
 * A chunk of bytecode: the unit of compilation.
 *
 * code:       dynamic array of bytecode bytes
 * constants:  pool of constant values (numbers, strings, identifiers)
 * lines:      source line number per bytecode byte (for error reporting)
 */
typedef struct Chunk {
    uint8_t *code;
    size_t count;
    size_t capacity;

    Value *constants;
    size_t const_count;
    size_t const_capacity;

    int *lines; /* Source line number per bytecode byte */
} Chunk;

/* Initialize a chunk to empty state. */
void chunk_init(Chunk *chunk);

/* Free all memory owned by a chunk (code, constants, lines). */
void chunk_free(Chunk *chunk);

/* Append a byte to the chunk's bytecode array.
 * line is the source line number for error reporting.
 * Returns true on success, false on allocation failure. */
bool chunk_write(Chunk *chunk, uint8_t byte, int line);

/*
 * Add a constant value to the chunk's constant pool.
 * The chunk takes ownership of any heap data in the value.
 * Returns the index of the constant, or -1 on overflow (>255 constants).
 */
int chunk_add_constant(Chunk *chunk, Value value);

/*
 * Find an existing constant in the pool that matches value, or add a new one.
 * Returns the index, or -1 on overflow.
 * For strings, compares by content. For numbers, compares by value.
 * If a match is found, the duplicate value is freed.
 */
int chunk_find_or_add_constant(Chunk *chunk, Value value);

/*
 * Print a human-readable disassembly of the chunk to stdout.
 * name is a label for the chunk (e.g. "main", "test").
 */
void chunk_disassemble(const Chunk *chunk, const char *name);

/*
 * Return a heap-allocated string with a human-readable disassembly
 * of the chunk. Format matches chunk_disassemble() output.
 * The string starts with "== name ==\n" followed by one line per
 * instruction. Caller must free the returned string.
 * Returns NULL on allocation failure.
 */
char *chunk_disassemble_to_string(const Chunk *chunk, const char *name);

/*
 * Return a human-readable name for an opcode.
 */
const char *opcode_name(OpCode op);

#endif /* CUTLET_CHUNK_H */
