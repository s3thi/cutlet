/*
 * vm.h - Cutlet bytecode virtual machine
 *
 * Stack-based VM that executes bytecode from a Chunk.
 * The VM maintains a value stack, an instruction pointer,
 * and a reference to the EvalContext for built-in I/O.
 *
 * Global variables are stored externally in the runtime module
 * (not owned by the VM).
 */

#ifndef CUTLET_VM_H
#define CUTLET_VM_H

#include "chunk.h"
#include "value.h"

#define VM_STACK_MAX 256

typedef struct {
    Chunk *chunk;              /* Current bytecode chunk (not owned). */
    uint8_t *ip;               /* Instruction pointer into chunk->code. */
    Value stack[VM_STACK_MAX]; /* Value stack. */
    Value *stack_top;          /* Points one past the top of stack. */
    EvalContext *ctx;          /* Write callback for say() (not owned). */
} VM;

/*
 * Execute a bytecode chunk and return the result value.
 *
 * The chunk is not consumed; the caller still owns it.
 * ctx provides the write callback for built-in functions like say().
 *
 * On success, returns the value left on top of stack by OP_RETURN.
 * On error, returns a VAL_ERROR with an error message.
 * The caller must call value_free() on the result.
 */
Value vm_execute(Chunk *chunk, EvalContext *ctx);

#endif /* CUTLET_VM_H */
