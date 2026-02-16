/*
 * chunk.c - Bytecode chunk implementation
 *
 * Dynamic arrays for bytecode, constants, and line info.
 * All grow by doubling capacity when full.
 */

#include "chunk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Initial capacity for dynamic arrays. */
#define INITIAL_CAPACITY 8

void chunk_init(Chunk *chunk) {
    chunk->code = NULL;
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->constants = NULL;
    chunk->const_count = 0;
    chunk->const_capacity = 0;
    chunk->lines = NULL;
}

void chunk_free(Chunk *chunk) {
    free(chunk->code);
    /* Free owned values in the constants pool. */
    for (size_t i = 0; i < chunk->const_count; i++) {
        value_free(&chunk->constants[i]);
    }
    free(chunk->constants);
    free(chunk->lines);
    chunk_init(chunk); /* Reset to clean state. */
}

bool chunk_write(Chunk *chunk, uint8_t byte, int line) {
    if (chunk->count >= chunk->capacity) {
        size_t new_cap =
            chunk->capacity < INITIAL_CAPACITY ? INITIAL_CAPACITY : chunk->capacity * 2;
        uint8_t *new_code = realloc(chunk->code, new_cap * sizeof(uint8_t));
        if (!new_code)
            return false;
        chunk->code = new_code;
        int *new_lines = realloc(chunk->lines, new_cap * sizeof(int));
        if (!new_lines) {
            /* code was already reallocated (can't undo) but capacity stays at the
             * old value, so count will never advance past lines' allocation.
             * The next chunk_write call will retry the lines realloc. */
            chunk->code = new_code;
            return false;
        }
        chunk->lines = new_lines;
        chunk->capacity = new_cap;
    }
    chunk->code[chunk->count] = byte;
    chunk->lines[chunk->count] = line;
    chunk->count++;
    return true;
}

int chunk_add_constant(Chunk *chunk, Value value) {
    /* Limit to 256 constants (1-byte index). */
    if (chunk->const_count >= 256) {
        value_free(&value);
        return -1;
    }
    if (chunk->const_count >= chunk->const_capacity) {
        size_t new_cap =
            chunk->const_capacity < INITIAL_CAPACITY ? INITIAL_CAPACITY : chunk->const_capacity * 2;
        Value *new_constants = realloc(chunk->constants, new_cap * sizeof(Value));
        if (!new_constants) {
            value_free(&value);
            return -1;
        }
        chunk->constants = new_constants;
        chunk->const_capacity = new_cap;
    }
    chunk->constants[chunk->const_count] = value;
    return (int)chunk->const_count++;
}

int chunk_find_or_add_constant(Chunk *chunk, Value value) {
    /* Search for an existing identical constant. */
    for (size_t i = 0; i < chunk->const_count; i++) {
        if (chunk->constants[i].type == value.type) {
            if (value.type == VAL_STRING && strcmp(chunk->constants[i].string, value.string) == 0) {
                value_free(&value); /* Don't need the duplicate. */
                return (int)i;
            }
            if (value.type == VAL_NUMBER && chunk->constants[i].number == value.number) {
                return (int)i;
            }
        }
    }
    return chunk_add_constant(chunk, value);
}

const char *opcode_name(OpCode op) {
    switch (op) {
    case OP_CONSTANT:
        return "OP_CONSTANT";
    case OP_TRUE:
        return "OP_TRUE";
    case OP_FALSE:
        return "OP_FALSE";
    case OP_NOTHING:
        return "OP_NOTHING";
    case OP_ADD:
        return "OP_ADD";
    case OP_SUBTRACT:
        return "OP_SUBTRACT";
    case OP_MULTIPLY:
        return "OP_MULTIPLY";
    case OP_DIVIDE:
        return "OP_DIVIDE";
    case OP_MODULO:
        return "OP_MODULO";
    case OP_POWER:
        return "OP_POWER";
    case OP_NEGATE:
        return "OP_NEGATE";
    case OP_CONCAT:
        return "OP_CONCAT";
    case OP_EQUAL:
        return "OP_EQUAL";
    case OP_NOT_EQUAL:
        return "OP_NOT_EQUAL";
    case OP_LESS:
        return "OP_LESS";
    case OP_GREATER:
        return "OP_GREATER";
    case OP_LESS_EQUAL:
        return "OP_LESS_EQUAL";
    case OP_GREATER_EQUAL:
        return "OP_GREATER_EQUAL";
    case OP_NOT:
        return "OP_NOT";
    case OP_DEFINE_GLOBAL:
        return "OP_DEFINE_GLOBAL";
    case OP_GET_GLOBAL:
        return "OP_GET_GLOBAL";
    case OP_SET_GLOBAL:
        return "OP_SET_GLOBAL";
    case OP_JUMP:
        return "OP_JUMP";
    case OP_JUMP_IF_FALSE:
        return "OP_JUMP_IF_FALSE";
    case OP_JUMP_IF_TRUE:
        return "OP_JUMP_IF_TRUE";
    case OP_LOOP:
        return "OP_LOOP";
    case OP_POP:
        return "OP_POP";
    case OP_CALL:
        return "OP_CALL";
    case OP_RETURN:
        return "OP_RETURN";
    default:
        return "OP_UNKNOWN";
    }
}

/*
 * Disassemble a single instruction starting at offset.
 * Returns the offset of the next instruction.
 */
static size_t disassemble_instruction(const Chunk *chunk, size_t offset) {
    printf("%04zu ", offset);

    /* Print line number: show it if different from previous byte's line,
     * otherwise show "|" to indicate same line. */
    if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1]) {
        printf("   | ");
    } else {
        printf("%4d ", chunk->lines[offset]);
    }

    uint8_t instruction = chunk->code[offset];
    switch ((OpCode)instruction) {
    /* Simple (no operand) instructions */
    case OP_TRUE:
    case OP_FALSE:
    case OP_NOTHING:
    case OP_ADD:
    case OP_SUBTRACT:
    case OP_MULTIPLY:
    case OP_DIVIDE:
    case OP_MODULO:
    case OP_POWER:
    case OP_NEGATE:
    case OP_CONCAT:
    case OP_EQUAL:
    case OP_NOT_EQUAL:
    case OP_LESS:
    case OP_GREATER:
    case OP_LESS_EQUAL:
    case OP_GREATER_EQUAL:
    case OP_NOT:
    case OP_POP:
    case OP_RETURN:
        printf("%s\n", opcode_name((OpCode)instruction));
        return offset + 1;

    /* 1-byte constant index operand */
    case OP_CONSTANT:
    case OP_DEFINE_GLOBAL:
    case OP_GET_GLOBAL:
    case OP_SET_GLOBAL: {
        uint8_t idx = chunk->code[offset + 1];
        printf("%-20s %4d '", opcode_name((OpCode)instruction), idx);
        if (idx < chunk->const_count) {
            char *s = value_format(&chunk->constants[idx]);
            if (s) {
                printf("%s", s);
                free(s);
            }
        }
        printf("'\n");
        return offset + 2;
    }

    /* 2-byte jump offset operand (forward jumps) */
    case OP_JUMP:
    case OP_JUMP_IF_FALSE:
    case OP_JUMP_IF_TRUE: {
        uint16_t jump = (uint16_t)((chunk->code[offset + 1] << 8) | chunk->code[offset + 2]);
        printf("%-20s %4d -> %zu\n", opcode_name((OpCode)instruction), jump,
               offset + 3 + (size_t)jump);
        return offset + 3;
    }

    /* 2-byte jump offset operand (backward jump for loops) */
    case OP_LOOP: {
        uint16_t jump = (uint16_t)((chunk->code[offset + 1] << 8) | chunk->code[offset + 2]);
        printf("%-20s %4d -> %zu\n", opcode_name((OpCode)instruction), jump,
               offset + 3 - (size_t)jump);
        return offset + 3;
    }

    /* OP_CALL: 1-byte name index + 1-byte argc */
    case OP_CALL: {
        uint8_t name_idx = chunk->code[offset + 1];
        uint8_t argc = chunk->code[offset + 2];
        printf("%-20s %4d '", opcode_name((OpCode)instruction), name_idx);
        if (name_idx < chunk->const_count) {
            char *s = value_format(&chunk->constants[name_idx]);
            if (s) {
                printf("%s", s);
                free(s);
            }
        }
        printf("' argc=%d\n", argc);
        return offset + 3;
    }

    default:
        printf("Unknown opcode %d\n", instruction);
        return offset + 1;
    }
}

void chunk_disassemble(const Chunk *chunk, const char *name) {
    printf("== %s ==\n", name);
    size_t offset = 0;
    while (offset < chunk->count) {
        offset = disassemble_instruction(chunk, offset);
    }
}
