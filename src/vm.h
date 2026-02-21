/*
 * vm.h - Cutlet bytecode virtual machine
 *
 * Stack-based VM that executes bytecode from a Chunk.
 * The VM maintains a value stack, a call frame stack, and a
 * reference to the EvalContext for built-in I/O.
 *
 * Each function call pushes a CallFrame that tracks the executing
 * function, instruction pointer, and stack window base. Top-level
 * code runs inside a "script" frame (frame 0).
 *
 * Global variables are stored externally in the runtime module
 * (not owned by the VM).
 */

#ifndef CUTLET_VM_H
#define CUTLET_VM_H

#include "chunk.h"
#include "value.h"

#define VM_STACK_MAX 256
#define FRAMES_MAX 64

/*
 * CallFrame - tracks one in-flight function invocation.
 *
 * closure: the ObjClosure being executed (not owned by the frame).
 *          For the top-level script, this points to a stack-allocated
 *          ObjClosure with 0 upvalues.
 * ip:      instruction pointer into closure->function->chunk->code.
 * slots:   pointer into the VM value stack marking the base of
 *          this frame's stack window (callee slot 0).
 */
typedef struct {
    ObjClosure *closure; /* The closure being executed. */
    uint8_t *ip;         /* Instruction pointer into function's chunk. */
    Value *slots;        /* Pointer into VM stack: base of this frame's window. */
} CallFrame;

typedef struct {
    CallFrame frames[FRAMES_MAX]; /* Call frame stack. */
    int frame_count;              /* Number of active call frames. */
    Value stack[VM_STACK_MAX];    /* Value stack. */
    Value *stack_top;             /* Points one past the top of stack. */
    EvalContext *ctx;             /* Write callback for say() (not owned). */
    ObjUpvalue *open_upvalues;    /* Linked list of open upvalues (sorted by slot address). */
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
