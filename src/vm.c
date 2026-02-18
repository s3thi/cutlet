/*
 * vm.c - Cutlet bytecode virtual machine
 *
 * Stack-based dispatch loop with call frames. Each opcode pops its
 * operands from the stack and pushes its result. The VM uses owned
 * Values on the stack — OP_POP calls value_free(), and OP_GET_GLOBAL
 * clones values from the global environment.
 *
 * Top-level code runs inside a "script" CallFrame (frame 0) whose
 * ObjFunction wraps the top-level Chunk. User function calls push
 * new frames; OP_RETURN pops them.
 */

#include "vm.h"
#include "runtime.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Stack operations ---- */

static void vm_reset_stack(VM *vm) {
    vm->stack_top = vm->stack;
    vm->frame_count = 0;
}

/* Returns false on stack overflow. Caller must handle the error. */
static bool vm_push(VM *vm, Value value) {
    if (vm->stack_top >= vm->stack + VM_STACK_MAX) {
        value_free(&value);
        return false;
    }
    *vm->stack_top++ = value;
    return true;
}

/* Returns false on stack underflow (bug in bytecode). */
static bool vm_pop(VM *vm, Value *out) {
    if (vm->stack_top <= vm->stack) {
        *out = (Value){0};
        return false;
    }
    *out = *--vm->stack_top;
    return true;
}

/* Returns false on stack underflow (not enough elements to peek). */
static bool vm_peek(VM *vm, int distance, Value *out) {
    if (vm->stack_top - 1 - distance < vm->stack) {
        *out = (Value){0};
        return false;
    }
    *out = vm->stack_top[-1 - distance];
    return true;
}

/* Free all values remaining on the stack. */
static void vm_free_stack(VM *vm) {
    while (vm->stack_top > vm->stack) {
        Value v;
        vm_pop(vm, &v);
        value_free(&v);
    }
}

/* ---- Byte reading helpers ----
 * Read from the current call frame's instruction pointer. */

static uint8_t read_byte(CallFrame *frame) { return *frame->ip++; }

static uint16_t read_short(CallFrame *frame) {
    uint8_t high = *frame->ip++;
    uint8_t low = *frame->ip++;
    return (uint16_t)((high << 8) | low);
}

/* ---- Runtime error helper ---- */

/*
 * Create an error Value, clean up the VM stack, and return it.
 * Uses varargs for printf-style formatting. Includes the source line
 * number of the instruction that caused the error.
 */
static Value vm_runtime_error(VM *vm, const char *fmt, ...) {
    /* Look up the line number from the current call frame.
     * frame->ip points to the next instruction, so subtract 1. */
    int line = 0;
    if (vm->frame_count > 0) {
        CallFrame *frame = &vm->frames[vm->frame_count - 1];
        Chunk *chunk = frame->function->chunk;
        if (frame->ip > chunk->code) {
            size_t offset = (size_t)(frame->ip - chunk->code - 1);
            if (offset < chunk->count) {
                line = chunk->lines[offset];
            }
        }
    }

    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap); // NOLINT(clang-analyzer-valist.Uninitialized)
    vm_free_stack(vm);

    if (line > 0) {
        return make_error("line %d: %s", line, msg);
    }
    return make_error("%s", msg);
}

/* ---- Equality check ---- */

static bool values_equal(const Value *a, const Value *b) {
    if (a->type != b->type)
        return false;
    switch (a->type) {
    case VAL_NUMBER:
        return a->number == b->number;
    case VAL_STRING:
        return strcmp(a->string, b->string) == 0;
    case VAL_BOOL:
        return a->boolean == b->boolean;
    case VAL_NOTHING:
        return true;
    case VAL_FUNCTION:
        /* Identity-based: only equal if pointing to the same ObjFunction. */
        return a->function == b->function;
    default:
        return false;
    }
}

/* ---- Ordered comparison ---- */

/*
 * Compare two values for ordering.
 * Returns -1, 0, or 1 via *result.
 * Returns true on success. On type error, returns false and sets *err_msg
 * to a static error message string. The caller handles creating the runtime
 * error and cleanup.
 */
static bool values_compare(const Value *a, const Value *b, int *result, const char **err_msg) {
    /* Check for nothing */
    if (a->type == VAL_NOTHING || b->type == VAL_NOTHING) {
        *err_msg = "cannot compare nothing";
        return false;
    }
    /* Must be same type */
    if (a->type != b->type) {
        *err_msg = "ordered comparison requires same types";
        return false;
    }
    /* Booleans can't be ordered */
    if (a->type == VAL_BOOL) {
        *err_msg = "ordered comparison not supported for booleans";
        return false;
    }

    if (a->type == VAL_NUMBER) {
        *result = (a->number > b->number) - (a->number < b->number);
        return true;
    }
    if (a->type == VAL_STRING) {
        *result = strcmp(a->string, b->string);
        return true;
    }

    *err_msg = "ordered comparison not supported for this type";
    return false;
}

/* ---- Native function implementations ---- */

/*
 * Native implementation of say(expr).
 * Formats the argument and writes it + newline via the EvalContext.
 */
static Value native_say(int argc, Value *args, EvalContext *ctx) {
    (void)argc; /* Arity is checked by the VM before calling. */

    if (!ctx->write_fn) {
        return make_error("no output writer available");
    }

    char *formatted = value_format(&args[0]);
    if (!formatted)
        return make_error("memory allocation failed");

    size_t flen = strlen(formatted);
    char *with_newline = realloc(formatted, flen + 2);
    if (!with_newline) {
        free(formatted);
        return make_error("memory allocation failed");
    }
    with_newline[flen] = '\n';
    with_newline[flen + 1] = '\0';
    ctx->write_fn(ctx->userdata, with_newline, flen + 1);
    free(with_newline);

    return make_nothing();
}

/*
 * Register built-in functions as native VAL_FUNCTION values in the
 * global variable environment. Called at the start of each vm_execute()
 * to ensure builtins are always available (even if a previous eval
 * overwrote a builtin name).
 */
static void register_builtins(void) {
    Value say_fn = make_native("say", 1, native_say);
    runtime_var_define("say", &say_fn);
    value_free(&say_fn); /* runtime_var_define clones it. */
}

/* ---- Type name helper ---- */

/* Return a human-readable type name for error messages. */
static const char *value_type_name(ValueType type) {
    switch (type) {
    case VAL_NUMBER:
        return "number";
    case VAL_STRING:
        return "string";
    case VAL_BOOL:
        return "boolean";
    case VAL_NOTHING:
        return "nothing";
    case VAL_FUNCTION:
        return "function";
    case VAL_ERROR:
        return "error";
    default:
        return "value";
    }
}

/* ---- Main dispatch loop ---- */

Value vm_execute(Chunk *chunk, EvalContext *ctx) {
    /* Ensure built-in functions are registered in the global environment.
     * Called every time so builtins are available even if a prior eval
     * overwrote a builtin name (e.g. "my say = 42"). */
    register_builtins();

    VM vm;
    vm.ctx = ctx;
    vm_reset_stack(&vm);

    /* Create a "script" ObjFunction that wraps the top-level chunk.
     * This lives on the C stack — it's only used for the duration of
     * this vm_execute() call. The chunk pointer is borrowed (not owned). */
    ObjFunction script_fn = {
        .name = NULL,
        .arity = 0,
        .params = NULL,
        .chunk = chunk,
        .native = NULL,
    };

    /* Push frame 0: the top-level script. */
    CallFrame *frame = &vm.frames[vm.frame_count++];
    frame->function = &script_fn;
    frame->ip = chunk->code;
    frame->slots = vm.stack;

    for (;;) {
        /* Current frame may change during execution (calls/returns). */
        frame = &vm.frames[vm.frame_count - 1];
        uint8_t instruction = read_byte(frame);
        switch ((OpCode)instruction) {

        case OP_CONSTANT: {
            uint8_t idx = read_byte(frame);
            Value v;
            if (!value_clone(&v, &frame->function->chunk->constants[idx])) {
                return vm_runtime_error(&vm, "memory allocation failed");
            }
            if (!vm_push(&vm, v)) {
                return vm_runtime_error(&vm, "stack overflow");
            }
            break;
        }

        case OP_TRUE:
            if (!vm_push(&vm, make_bool(true))) {
                return vm_runtime_error(&vm, "stack overflow");
            }
            break;

        case OP_FALSE:
            if (!vm_push(&vm, make_bool(false))) {
                return vm_runtime_error(&vm, "stack overflow");
            }
            break;

        case OP_NOTHING:
            if (!vm_push(&vm, make_nothing())) {
                return vm_runtime_error(&vm, "stack overflow");
            }
            break;

        case OP_ADD: {
            Value b, a;
            if (!vm_pop(&vm, &b)) {
                return vm_runtime_error(&vm, "stack underflow");
            }
            if (!vm_pop(&vm, &a)) {
                value_free(&b);
                return vm_runtime_error(&vm, "stack underflow");
            }
            if (a.type != VAL_NUMBER || b.type != VAL_NUMBER) {
                value_free(&a);
                value_free(&b);
                return vm_runtime_error(&vm, "arithmetic requires numbers");
            }
            if (!vm_push(&vm, make_number(a.number + b.number))) {
                return vm_runtime_error(&vm, "stack overflow");
            }
            break;
        }

        case OP_SUBTRACT: {
            Value b, a;
            if (!vm_pop(&vm, &b)) {
                return vm_runtime_error(&vm, "stack underflow");
            }
            if (!vm_pop(&vm, &a)) {
                value_free(&b);
                return vm_runtime_error(&vm, "stack underflow");
            }
            if (a.type != VAL_NUMBER || b.type != VAL_NUMBER) {
                value_free(&a);
                value_free(&b);
                return vm_runtime_error(&vm, "arithmetic requires numbers");
            }
            if (!vm_push(&vm, make_number(a.number - b.number))) {
                return vm_runtime_error(&vm, "stack overflow");
            }
            break;
        }

        case OP_MULTIPLY: {
            Value b, a;
            if (!vm_pop(&vm, &b)) {
                return vm_runtime_error(&vm, "stack underflow");
            }
            if (!vm_pop(&vm, &a)) {
                value_free(&b);
                return vm_runtime_error(&vm, "stack underflow");
            }
            if (a.type != VAL_NUMBER || b.type != VAL_NUMBER) {
                value_free(&a);
                value_free(&b);
                return vm_runtime_error(&vm, "arithmetic requires numbers");
            }
            if (!vm_push(&vm, make_number(a.number * b.number))) {
                return vm_runtime_error(&vm, "stack overflow");
            }
            break;
        }

        case OP_DIVIDE: {
            Value b, a;
            if (!vm_pop(&vm, &b)) {
                return vm_runtime_error(&vm, "stack underflow");
            }
            if (!vm_pop(&vm, &a)) {
                value_free(&b);
                return vm_runtime_error(&vm, "stack underflow");
            }
            if (a.type != VAL_NUMBER || b.type != VAL_NUMBER) {
                value_free(&a);
                value_free(&b);
                return vm_runtime_error(&vm, "arithmetic requires numbers");
            }
            if (b.number == 0) {
                value_free(&a);
                value_free(&b);
                return vm_runtime_error(&vm, "division by zero");
            }
            if (!vm_push(&vm, make_number(a.number / b.number))) {
                return vm_runtime_error(&vm, "stack overflow");
            }
            break;
        }

        case OP_MODULO: {
            Value b, a;
            if (!vm_pop(&vm, &b)) {
                return vm_runtime_error(&vm, "stack underflow");
            }
            if (!vm_pop(&vm, &a)) {
                value_free(&b);
                return vm_runtime_error(&vm, "stack underflow");
            }
            if (a.type != VAL_NUMBER || b.type != VAL_NUMBER) {
                value_free(&a);
                value_free(&b);
                return vm_runtime_error(&vm, "arithmetic requires numbers");
            }
            if (b.number == 0) {
                value_free(&a);
                value_free(&b);
                return vm_runtime_error(&vm, "modulo by zero");
            }
            /* Python-style modulo: result has the sign of the divisor.
             * Formula: a % b = a - b * floor(a / b) */
            double result = a.number - b.number * floor(a.number / b.number);
            if (!vm_push(&vm, make_number(result))) {
                return vm_runtime_error(&vm, "stack overflow");
            }
            break;
        }

        case OP_POWER: {
            Value b, a;
            if (!vm_pop(&vm, &b)) {
                return vm_runtime_error(&vm, "stack underflow");
            }
            if (!vm_pop(&vm, &a)) {
                value_free(&b);
                return vm_runtime_error(&vm, "stack underflow");
            }
            if (a.type != VAL_NUMBER || b.type != VAL_NUMBER) {
                value_free(&a);
                value_free(&b);
                return vm_runtime_error(&vm, "arithmetic requires numbers");
            }
            if (!vm_push(&vm, make_number(pow(a.number, b.number)))) {
                return vm_runtime_error(&vm, "stack overflow");
            }
            break;
        }

        case OP_CONCAT: {
            /* String concatenation: pop two values, coerce each to string
             * via value_format(), concatenate, and push the result. */
            Value b, a;
            if (!vm_pop(&vm, &b)) {
                return vm_runtime_error(&vm, "stack underflow");
            }
            if (!vm_pop(&vm, &a)) {
                value_free(&b);
                return vm_runtime_error(&vm, "stack underflow");
            }
            char *sa = value_format(&a);
            char *sb = value_format(&b);
            value_free(&a);
            value_free(&b);
            if (!sa || !sb) {
                free(sa);
                free(sb);
                return vm_runtime_error(&vm, "memory allocation failed");
            }
            size_t la = strlen(sa);
            size_t lb = strlen(sb);
            char *result = malloc(la + lb + 1);
            if (!result) {
                free(sa);
                free(sb);
                return vm_runtime_error(&vm, "memory allocation failed");
            }
            memcpy(result, sa, la);
            memcpy(result + la, sb, lb);
            result[la + lb] = '\0';
            free(sa);
            free(sb);
            if (!vm_push(&vm, make_string(result))) {
                return vm_runtime_error(&vm, "stack overflow");
            }
            break;
        }

        case OP_NEGATE: {
            Value a;
            if (!vm_pop(&vm, &a)) {
                return vm_runtime_error(&vm, "stack underflow");
            }
            if (a.type != VAL_NUMBER) {
                value_free(&a);
                return vm_runtime_error(&vm, "unary minus requires a number");
            }
            if (!vm_push(&vm, make_number(-a.number))) {
                return vm_runtime_error(&vm, "stack overflow");
            }
            break;
        }

        case OP_EQUAL: {
            Value b, a;
            if (!vm_pop(&vm, &b)) {
                return vm_runtime_error(&vm, "stack underflow");
            }
            if (!vm_pop(&vm, &a)) {
                value_free(&b);
                return vm_runtime_error(&vm, "stack underflow");
            }
            bool eq = values_equal(&a, &b);
            value_free(&a);
            value_free(&b);
            if (!vm_push(&vm, make_bool(eq))) {
                return vm_runtime_error(&vm, "stack overflow");
            }
            break;
        }

        case OP_NOT_EQUAL: {
            Value b, a;
            if (!vm_pop(&vm, &b)) {
                return vm_runtime_error(&vm, "stack underflow");
            }
            if (!vm_pop(&vm, &a)) {
                value_free(&b);
                return vm_runtime_error(&vm, "stack underflow");
            }
            bool eq = values_equal(&a, &b);
            value_free(&a);
            value_free(&b);
            if (!vm_push(&vm, make_bool(!eq))) {
                return vm_runtime_error(&vm, "stack overflow");
            }
            break;
        }

        case OP_LESS: {
            Value b, a;
            if (!vm_pop(&vm, &b)) {
                return vm_runtime_error(&vm, "stack underflow");
            }
            if (!vm_pop(&vm, &a)) {
                value_free(&b);
                return vm_runtime_error(&vm, "stack underflow");
            }
            int cmp;
            const char *err_msg;
            if (!values_compare(&a, &b, &cmp, &err_msg)) {
                value_free(&a);
                value_free(&b);
                return vm_runtime_error(&vm, "%s", err_msg);
            }
            value_free(&a);
            value_free(&b);
            if (!vm_push(&vm, make_bool(cmp < 0))) {
                return vm_runtime_error(&vm, "stack overflow");
            }
            break;
        }

        case OP_GREATER: {
            Value b, a;
            if (!vm_pop(&vm, &b)) {
                return vm_runtime_error(&vm, "stack underflow");
            }
            if (!vm_pop(&vm, &a)) {
                value_free(&b);
                return vm_runtime_error(&vm, "stack underflow");
            }
            int cmp;
            const char *err_msg;
            if (!values_compare(&a, &b, &cmp, &err_msg)) {
                value_free(&a);
                value_free(&b);
                return vm_runtime_error(&vm, "%s", err_msg);
            }
            value_free(&a);
            value_free(&b);
            if (!vm_push(&vm, make_bool(cmp > 0))) {
                return vm_runtime_error(&vm, "stack overflow");
            }
            break;
        }

        case OP_LESS_EQUAL: {
            Value b, a;
            if (!vm_pop(&vm, &b)) {
                return vm_runtime_error(&vm, "stack underflow");
            }
            if (!vm_pop(&vm, &a)) {
                value_free(&b);
                return vm_runtime_error(&vm, "stack underflow");
            }
            int cmp;
            const char *err_msg;
            if (!values_compare(&a, &b, &cmp, &err_msg)) {
                value_free(&a);
                value_free(&b);
                return vm_runtime_error(&vm, "%s", err_msg);
            }
            value_free(&a);
            value_free(&b);
            if (!vm_push(&vm, make_bool(cmp <= 0))) {
                return vm_runtime_error(&vm, "stack overflow");
            }
            break;
        }

        case OP_GREATER_EQUAL: {
            Value b, a;
            if (!vm_pop(&vm, &b)) {
                return vm_runtime_error(&vm, "stack underflow");
            }
            if (!vm_pop(&vm, &a)) {
                value_free(&b);
                return vm_runtime_error(&vm, "stack underflow");
            }
            int cmp;
            const char *err_msg;
            if (!values_compare(&a, &b, &cmp, &err_msg)) {
                value_free(&a);
                value_free(&b);
                return vm_runtime_error(&vm, "%s", err_msg);
            }
            value_free(&a);
            value_free(&b);
            if (!vm_push(&vm, make_bool(cmp >= 0))) {
                return vm_runtime_error(&vm, "stack overflow");
            }
            break;
        }

        case OP_NOT: {
            Value a;
            if (!vm_pop(&vm, &a)) {
                return vm_runtime_error(&vm, "stack underflow");
            }
            bool truthy = is_truthy(&a);
            value_free(&a);
            if (!vm_push(&vm, make_bool(!truthy))) {
                return vm_runtime_error(&vm, "stack overflow");
            }
            break;
        }

        case OP_DEFINE_GLOBAL: {
            uint8_t idx = read_byte(frame);
            const char *name = frame->function->chunk->constants[idx].string;
            /* Peek TOS (don't pop — the value stays as the expression result). */
            Value tos;
            if (!vm_peek(&vm, 0, &tos)) {
                return vm_runtime_error(&vm, "stack underflow");
            }
            RuntimeVarStatus status = runtime_var_define(name, &tos);
            if (status != RUNTIME_VAR_OK) {
                return vm_runtime_error(&vm, "memory allocation failed");
            }
            break;
        }

        case OP_GET_GLOBAL: {
            uint8_t idx = read_byte(frame);
            const char *name = frame->function->chunk->constants[idx].string;
            Value val = {0};
            RuntimeVarStatus status = runtime_var_get(name, &val);
            if (status == RUNTIME_VAR_NOT_FOUND) {
                return vm_runtime_error(&vm, "unknown variable '%s'", name);
            }
            if (status != RUNTIME_VAR_OK) {
                return vm_runtime_error(&vm, "memory allocation failed");
            }
            if (!vm_push(&vm, val)) {
                return vm_runtime_error(&vm, "stack overflow");
            }
            break;
        }

        case OP_SET_GLOBAL: {
            uint8_t idx = read_byte(frame);
            const char *name = frame->function->chunk->constants[idx].string;
            /* Peek TOS (don't pop — value stays as expression result). */
            Value tos;
            if (!vm_peek(&vm, 0, &tos)) {
                return vm_runtime_error(&vm, "stack underflow");
            }
            RuntimeVarStatus status = runtime_var_assign(name, &tos);
            if (status == RUNTIME_VAR_NOT_FOUND) {
                return vm_runtime_error(&vm, "undefined variable '%s'", name);
            }
            if (status != RUNTIME_VAR_OK) {
                return vm_runtime_error(&vm, "memory allocation failed");
            }
            break;
        }

        case OP_GET_LOCAL: {
            /* Read a local variable from the current call frame's stack window.
             * The slot index is relative to frame->slots (slot 0 = callee,
             * slot 1 = first param, etc.). */
            uint8_t slot = read_byte(frame);
            Value v;
            if (!value_clone(&v, &frame->slots[slot])) {
                return vm_runtime_error(&vm, "memory allocation failed");
            }
            if (!vm_push(&vm, v)) {
                return vm_runtime_error(&vm, "stack overflow");
            }
            break;
        }

        case OP_JUMP: {
            uint16_t offset = read_short(frame);
            frame->ip += offset;
            break;
        }

        case OP_JUMP_IF_FALSE: {
            uint16_t offset = read_short(frame);
            Value tos;
            if (!vm_peek(&vm, 0, &tos)) {
                return vm_runtime_error(&vm, "stack underflow");
            }
            if (!is_truthy(&tos)) {
                frame->ip += offset;
            }
            break;
        }

        case OP_JUMP_IF_TRUE: {
            uint16_t offset = read_short(frame);
            Value tos;
            if (!vm_peek(&vm, 0, &tos)) {
                return vm_runtime_error(&vm, "stack underflow");
            }
            if (is_truthy(&tos)) {
                frame->ip += offset;
            }
            break;
        }

        case OP_LOOP: {
            /* Backward jump: subtract offset from instruction pointer. */
            uint16_t offset = read_short(frame);
            frame->ip -= offset;
            break;
        }

        case OP_POP: {
            Value v;
            if (!vm_pop(&vm, &v)) {
                return vm_runtime_error(&vm, "stack underflow");
            }
            value_free(&v);
            break;
        }

        case OP_CALL: {
            uint8_t argc = read_byte(frame);

            /* Peek at the callee: it sits below the arguments on the stack.
             * vm_peek gives us a shallow copy — the function pointer is
             * shared with the stack Value. We must NOT free the peeked
             * copy; vm_free_stack (inside vm_runtime_error) handles it. */
            Value callee;
            if (!vm_peek(&vm, argc, &callee)) {
                return vm_runtime_error(&vm, "stack underflow");
            }

            /* Callee must be a function. Error paths let vm_runtime_error
             * call vm_free_stack to clean up the stack (args + callee). */
            if (callee.type != VAL_FUNCTION) {
                return vm_runtime_error(&vm, "cannot call %s", value_type_name(callee.type));
            }

            ObjFunction *fn = callee.function;

            /* Check arity. fn->name is still valid because the callee
             * Value is on the stack; vm_runtime_error formats the message
             * BEFORE vm_free_stack frees it. */
            if (argc != fn->arity) {
                return vm_runtime_error(&vm, "'%s' expects %d argument%s, got %d",
                                        fn->name ? fn->name : "<fn>", fn->arity,
                                        fn->arity == 1 ? "" : "s", argc);
            }

            if (fn->native) {
                /* Native function: pop args into temp array, call, push result. */
                Value args[256];
                for (int i = argc - 1; i >= 0; i--) {
                    vm_pop(&vm, &args[i]);
                }
                /* Pop the callee itself. */
                Value callee_val;
                vm_pop(&vm, &callee_val);

                /* Call the native. fn->native is read before callee_val
                 * is freed, so the pointer is still valid. */
                Value result = fn->native(argc, args, vm.ctx);

                /* Free the argument values and the callee. */
                for (int i = 0; i < argc; i++) {
                    value_free(&args[i]);
                }
                value_free(&callee_val);

                if (result.type == VAL_ERROR) {
                    vm_free_stack(&vm);
                    return result;
                }
                if (!vm_push(&vm, result)) {
                    return vm_runtime_error(&vm, "stack overflow");
                }
            } else {
                /* User-defined function: push a new call frame.
                 * The callee value sits at stack_top[-argc-1]; arguments
                 * are above it. The new frame's slots point to the callee
                 * slot (slot 0 = the function itself, not used yet but
                 * reserves the position for future local variable support). */
                if (vm.frame_count >= FRAMES_MAX) {
                    return vm_runtime_error(&vm, "stack overflow");
                }
                CallFrame *new_frame = &vm.frames[vm.frame_count++];
                new_frame->function = fn;
                new_frame->ip = fn->chunk->code;
                new_frame->slots = vm.stack_top - argc - 1;
            }
            break;
        }

        case OP_RETURN: {
            /* Pop the return value from the stack. */
            Value result;
            if (vm.stack_top > frame->slots) {
                vm_pop(&vm, &result);
            } else {
                result = make_nothing();
            }

            /* Pop the current call frame. */
            vm.frame_count--;

            if (vm.frame_count == 0) {
                /* Returning from the top-level script: end of program.
                 * Free any remaining values on the stack and return. */
                vm_free_stack(&vm);
                return result; // NOLINT(clang-analyzer-core.StackAddressEscape)
            }

            /* Returning from a user function: discard the called
             * function's stack window (callee + args + locals), then
             * push the return value for the caller. */
            while (vm.stack_top > frame->slots) {
                Value v;
                vm_pop(&vm, &v);
                value_free(&v);
            }

            /* Push the return value back onto the caller's stack. */
            if (!vm_push(&vm, result)) {
                return vm_runtime_error(&vm, "stack overflow");
            }
            break;
        }

        default:
            return vm_runtime_error(&vm, "unknown opcode %d", instruction);
        }
    }
}
