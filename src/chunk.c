/*
 * chunk.c - Bytecode chunk implementation
 *
 * Dynamic arrays for bytecode, constants, and line info.
 * All grow by doubling capacity when full.
 */

#include "chunk.h"
#include <stdarg.h>
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
    case OP_GET_LOCAL:
        return "OP_GET_LOCAL";
    case OP_SET_LOCAL:
        return "OP_SET_LOCAL";
    case OP_CALL:
        return "OP_CALL";
    case OP_CLOSURE:
        return "OP_CLOSURE";
    case OP_GET_UPVALUE:
        return "OP_GET_UPVALUE";
    case OP_SET_UPVALUE:
        return "OP_SET_UPVALUE";
    case OP_CLOSE_UPVALUE:
        return "OP_CLOSE_UPVALUE";
    case OP_ARRAY:
        return "OP_ARRAY";
    case OP_RETURN:
        return "OP_RETURN";
    default:
        return "OP_UNKNOWN";
    }
}

/* ============================================================
 * Dynamic buffer for building disassembly strings
 * ============================================================ */

/* Simple growable string buffer used by the disassembler. */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} DynBuf;

/* Initialize a dynamic buffer with an initial capacity. */
static bool dynbuf_init(DynBuf *b, size_t initial_cap) {
    b->data = malloc(initial_cap);
    if (!b->data)
        return false;
    b->data[0] = '\0';
    b->len = 0;
    b->cap = initial_cap;
    return true;
}

/* Append a formatted string to the buffer. Grows as needed. */
__attribute__((format(printf, 2, 3))) static bool dynbuf_printf(DynBuf *b, const char *fmt, ...) {
    va_list ap;

    /* First attempt: try writing into remaining space. */
    va_start(ap, fmt);
    size_t avail = b->cap - b->len;
    int n = vsnprintf(b->data + b->len, avail, fmt, ap);
    va_end(ap);

    if (n < 0)
        return false;

    /* If it fit, advance len and return. */
    if ((size_t)n < avail) {
        b->len += (size_t)n;
        return true;
    }

    /* Grow the buffer and retry. */
    size_t needed = b->len + (size_t)n + 1;
    size_t new_cap = b->cap;
    while (new_cap < needed)
        new_cap *= 2;
    char *new_data = realloc(b->data, new_cap);
    if (!new_data)
        return false;
    b->data = new_data;
    b->cap = new_cap;

    va_start(ap, fmt);
    vsnprintf(b->data + b->len, b->cap - b->len, fmt, ap);
    va_end(ap);
    b->len += (size_t)n;
    return true;
}

/* ============================================================
 * Buffer-based instruction disassembler
 * ============================================================ */

/*
 * Disassemble a single instruction starting at offset into the
 * dynamic buffer. Returns the offset of the next instruction.
 */
static size_t disassemble_instruction_to_buf(DynBuf *b, const Chunk *chunk, size_t offset) {
    dynbuf_printf(b, "%04zu ", offset);

    /* Print line number: show it if different from previous byte's line,
     * otherwise show "|" to indicate same line. */
    if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1]) {
        dynbuf_printf(b, "   | ");
    } else {
        dynbuf_printf(b, "%4d ", chunk->lines[offset]);
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
    case OP_CLOSE_UPVALUE:
    case OP_RETURN:
        dynbuf_printf(b, "%s\n", opcode_name((OpCode)instruction));
        return offset + 1;

    /* 1-byte constant index operand */
    case OP_CONSTANT:
    case OP_DEFINE_GLOBAL:
    case OP_GET_GLOBAL:
    case OP_SET_GLOBAL: {
        uint8_t idx = chunk->code[offset + 1];
        dynbuf_printf(b, "%-20s %4d '", opcode_name((OpCode)instruction), idx);
        if (idx < chunk->const_count) {
            char *s = value_format(&chunk->constants[idx]);
            if (s) {
                dynbuf_printf(b, "%s", s);
                free(s);
            }
        }
        dynbuf_printf(b, "'\n");
        return offset + 2;
    }

    /* 2-byte jump offset operand (forward jumps) */
    case OP_JUMP:
    case OP_JUMP_IF_FALSE:
    case OP_JUMP_IF_TRUE: {
        uint16_t jump = (uint16_t)((chunk->code[offset + 1] << 8) | chunk->code[offset + 2]);
        dynbuf_printf(b, "%-20s %4d -> %zu\n", opcode_name((OpCode)instruction), jump,
                      offset + 3 + (size_t)jump);
        return offset + 3;
    }

    /* 2-byte jump offset operand (backward jump for loops) */
    case OP_LOOP: {
        uint16_t jump = (uint16_t)((chunk->code[offset + 1] << 8) | chunk->code[offset + 2]);
        dynbuf_printf(b, "%-20s %4d -> %zu\n", opcode_name((OpCode)instruction), jump,
                      offset + 3 - (size_t)jump);
        return offset + 3;
    }

    /* OP_GET_LOCAL / OP_SET_LOCAL: 1-byte slot index */
    case OP_GET_LOCAL:
    case OP_SET_LOCAL: {
        uint8_t slot = chunk->code[offset + 1];
        dynbuf_printf(b, "%-20s slot=%d\n", opcode_name((OpCode)instruction), slot);
        return offset + 2;
    }

    /* OP_CALL: 1-byte argc (callee is on the stack) */
    case OP_CALL: {
        uint8_t argc = chunk->code[offset + 1];
        dynbuf_printf(b, "%-20s argc=%d\n", opcode_name((OpCode)instruction), argc);
        return offset + 2;
    }

    /* OP_ARRAY: 1-byte element count */
    case OP_ARRAY: {
        uint8_t count = chunk->code[offset + 1];
        dynbuf_printf(b, "%-20s count=%d\n", opcode_name((OpCode)instruction), count);
        return offset + 2;
    }

    /* OP_GET_UPVALUE / OP_SET_UPVALUE: 1-byte upvalue index */
    case OP_GET_UPVALUE:
    case OP_SET_UPVALUE: {
        uint8_t uv_idx = chunk->code[offset + 1];
        dynbuf_printf(b, "%-20s %4d\n", opcode_name((OpCode)instruction), uv_idx);
        return offset + 2;
    }

    /* OP_CLOSURE: 1-byte constant index, then N x (is_local, index) pairs.
     * N is determined by the ObjFunction's upvalue_count in the constant pool. */
    case OP_CLOSURE: {
        uint8_t const_idx = chunk->code[offset + 1];
        offset += 2;
        dynbuf_printf(b, "%-20s %4d '", opcode_name(OP_CLOSURE), const_idx);
        /* Show the function name from the constant pool. */
        if (const_idx < chunk->const_count) {
            char *s = value_format(&chunk->constants[const_idx]);
            if (s) {
                dynbuf_printf(b, "%s", s);
                free(s);
            }
        }
        dynbuf_printf(b, "'\n");
        /* Read upvalue descriptor pairs from the ObjFunction in the constant pool. */
        int uv_count = 0;
        if (const_idx < chunk->const_count && chunk->constants[const_idx].type == VAL_FUNCTION &&
            chunk->constants[const_idx].function != NULL) {
            uv_count = chunk->constants[const_idx].function->upvalue_count;
        }
        for (int i = 0; i < uv_count; i++) {
            uint8_t is_local = chunk->code[offset];
            uint8_t index = chunk->code[offset + 1];
            dynbuf_printf(b, "%04zu    |   %s %d\n", offset, is_local ? "local" : "upvalue", index);
            offset += 2;
        }
        return offset;
    }

    default:
        dynbuf_printf(b, "Unknown opcode %d\n", instruction);
        return offset + 1;
    }
}

/* ============================================================
 * Public disassembly API
 * ============================================================ */

char *chunk_disassemble_to_string(const Chunk *chunk, const char *name) {
    DynBuf b;
    if (!dynbuf_init(&b, 256))
        return NULL;

    dynbuf_printf(&b, "== %s ==\n", name);
    size_t offset = 0;
    while (offset < chunk->count) {
        offset = disassemble_instruction_to_buf(&b, chunk, offset);
    }

    /* Recursively disassemble any function constants' inner chunks. */
    for (size_t i = 0; i < chunk->const_count; i++) {
        if (chunk->constants[i].type == VAL_FUNCTION && chunk->constants[i].function != NULL &&
            chunk->constants[i].function->chunk != NULL) {
            ObjFunction *fn = chunk->constants[i].function;
            const char *label = fn->name ? fn->name : "<fn>";
            char *inner = chunk_disassemble_to_string(fn->chunk, label);
            if (inner) {
                dynbuf_printf(&b, "\n%s", inner);
                free(inner);
            }
        }
    }

    return b.data; /* Caller owns the buffer. */
}

void chunk_disassemble(const Chunk *chunk, const char *name) {
    char *s = chunk_disassemble_to_string(chunk, name);
    if (s) {
        fputs(s, stdout);
        free(s);
    }
}
