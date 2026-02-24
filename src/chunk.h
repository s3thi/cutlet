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
 * - OP_CALL has a 1-byte argc operand (callee is on the stack).
 * - OP_JUMP, OP_JUMP_IF_FALSE, OP_JUMP_IF_TRUE have a 2-byte offset.
 * - OP_CLOSURE has a 1-byte constant index, then N x (is_local, index)
 *   pairs where N is the function's upvalue_count.
 * - OP_GET_UPVALUE, OP_SET_UPVALUE have a 1-byte upvalue index.
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

    /* Local variables (stack-slot based, used inside functions) */
    OP_GET_LOCAL, /* Operand: 1-byte slot index. Push clone of frame->slots[slot]. */
    OP_SET_LOCAL, /* Operand: 1-byte slot index. Peek TOS, update frame->slots[slot]. */

    /* Function calls (stack-based: callee is on the stack) */
    OP_CALL, /* Operand: 1-byte argc. Callee at stack_top[-argc-1]. */

    /* Closure opcodes */
    OP_CLOSURE,       /* Create closure from constant pool ObjFunction.
                       * Operand: 1-byte constant index, then N x (1-byte is_local, 1-byte index)
                       * where N = fn->upvalue_count. Push 1 closure. */
    OP_GET_UPVALUE,   /* Read captured variable. Operand: 1-byte upvalue index. Push 1. */
    OP_SET_UPVALUE,   /* Write captured variable. Operand: 1-byte upvalue index. Peek TOS. */
    OP_CLOSE_UPVALUE, /* Close the topmost open upvalue at TOS slot. Pop 1. */

    /* Array operations */
    OP_ARRAY,     /* Build array from top N stack values. Operand: 1-byte count */
    OP_INDEX_GET, /* Read array[index]. Pop 2 (array, index), push 1. */
    OP_INDEX_SET, /* Write array[index] = value. Pop 3 (array, index, value), push 1 (value). */

    /* Meta-operator: array reduction/fold and vectorization */
    OP_REDUCE,    /* Fold array with built-in operator. Operand: 1-byte op (OpCode value).
                   * Pop 1 (array), push 1 (result). */
    OP_VECTORIZE, /* Element-wise operation on arrays. Operand: 1-byte op (OpCode value).
                   * Pop 2 (left, right), push 1 (result array).
                   * Supports array-array, array-scalar, and scalar-array. */

    /* Custom function reduction/vectorization — call user function in a loop.
     * Unlike OP_REDUCE/OP_VECTORIZE which use a built-in op byte, these
     * expect the callable (closure or native) to already be on the stack. */
    OP_REDUCE_CALL,    /* Fold array with a callable. Stack: [fn, array] → [result].
                        * Calls fn(acc, elem) for each element left-to-right. */
    OP_VECTORIZE_CALL, /* Element-wise with a callable. Stack: [fn, left, right] → [result].
                        * Calls fn(left[i], right[i]) for each element.
                        * Supports array-array, array-scalar, scalar-array. */

    /* Logical op-byte constants used as OP_REDUCE/OP_VECTORIZE operands only.
     * These are not standalone VM opcodes — they encode @and / @or
     * reduction semantics with short-circuit behavior. */
    OP_AND, /* @and: left fold, short-circuit on falsy */
    OP_OR,  /* @or: left fold, short-circuit on truthy */

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
