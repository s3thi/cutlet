/*
 * vm.c - Cutlet bytecode virtual machine
 *
 * Stack-based dispatch loop. Each opcode pops its operands from
 * the stack and pushes its result. The VM uses owned Values on
 * the stack — OP_POP calls value_free(), and OP_GET_GLOBAL clones
 * values from the global environment.
 */

#include "vm.h"
#include "runtime.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Stack operations ---- */

static void vm_reset_stack(VM *vm) { vm->stack_top = vm->stack; }

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

/* ---- Byte reading helpers ---- */

static uint8_t read_byte(VM *vm) { return *vm->ip++; }

static uint16_t read_short(VM *vm) {
    uint8_t high = *vm->ip++;
    uint8_t low = *vm->ip++;
    return (uint16_t)((high << 8) | low);
}

/* ---- Runtime error helper ---- */

/*
 * Create an error Value, clean up the VM stack, and return it.
 * Uses varargs for printf-style formatting. Includes the source line
 * number of the instruction that caused the error.
 */
static Value vm_runtime_error(VM *vm, const char *fmt, ...) {
    /* Look up the line number of the instruction that just executed.
     * vm->ip points to the next instruction, so subtract 1. */
    int line = 0;
    if (vm->ip > vm->chunk->code) {
        size_t offset = (size_t)(vm->ip - vm->chunk->code - 1);
        if (offset < vm->chunk->count) {
            line = vm->chunk->lines[offset];
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

/* ---- Built-in function dispatch ---- */

/*
 * Execute a built-in function call.
 * fn_name is the function name string from the constant pool.
 * argc is the number of arguments on the stack (TOS is last arg).
 * Returns a Value result. On error returns VAL_ERROR.
 */
static Value call_builtin(VM *vm, const char *fn_name, int argc) {
    if (strcmp(fn_name, "say") == 0) {
        if (argc != 1) {
            /* Pop all arguments to clean up. */
            for (int i = 0; i < argc; i++) {
                Value v;
                vm_pop(vm, &v);
                value_free(&v);
            }
            return make_error("say() expects 1 argument, got %d", argc);
        }

        Value arg;
        vm_pop(vm, &arg);

        if (!vm->ctx->write_fn) {
            value_free(&arg);
            return make_error("no output writer available");
        }

        char *formatted = value_format(&arg);
        value_free(&arg);
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
        vm->ctx->write_fn(vm->ctx->userdata, with_newline, flen + 1);
        free(with_newline);

        return make_nothing();
    }

    /* Unknown function: pop all arguments. */
    for (int i = 0; i < argc; i++) {
        Value v;
        vm_pop(vm, &v);
        value_free(&v);
    }
    return make_error("unknown function '%s'", fn_name);
}

/* ---- Main dispatch loop ---- */

Value vm_execute(Chunk *chunk, EvalContext *ctx) {
    VM vm;
    vm.chunk = chunk;
    vm.ip = chunk->code;
    vm.ctx = ctx;
    vm_reset_stack(&vm);

    for (;;) {
        uint8_t instruction = read_byte(&vm);
        switch ((OpCode)instruction) {

        case OP_CONSTANT: {
            uint8_t idx = read_byte(&vm);
            Value v;
            if (!value_clone(&v, &chunk->constants[idx])) {
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
            uint8_t idx = read_byte(&vm);
            const char *name = chunk->constants[idx].string;
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
            uint8_t idx = read_byte(&vm);
            const char *name = chunk->constants[idx].string;
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
            uint8_t idx = read_byte(&vm);
            const char *name = chunk->constants[idx].string;
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

        case OP_JUMP: {
            uint16_t offset = read_short(&vm);
            vm.ip += offset;
            break;
        }

        case OP_JUMP_IF_FALSE: {
            uint16_t offset = read_short(&vm);
            Value tos;
            if (!vm_peek(&vm, 0, &tos)) {
                return vm_runtime_error(&vm, "stack underflow");
            }
            if (!is_truthy(&tos)) {
                vm.ip += offset;
            }
            break;
        }

        case OP_JUMP_IF_TRUE: {
            uint16_t offset = read_short(&vm);
            Value tos;
            if (!vm_peek(&vm, 0, &tos)) {
                return vm_runtime_error(&vm, "stack underflow");
            }
            if (is_truthy(&tos)) {
                vm.ip += offset;
            }
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
            uint8_t name_idx = read_byte(&vm);
            uint8_t argc = read_byte(&vm);
            const char *fn_name = chunk->constants[name_idx].string;
            Value result = call_builtin(&vm, fn_name, argc);
            if (result.type == VAL_ERROR) {
                vm_free_stack(&vm);
                return result;
            }
            if (!vm_push(&vm, result)) {
                return vm_runtime_error(&vm, "stack overflow");
            }
            break;
        }

        case OP_RETURN: {
            /* Pop the result from the stack. */
            Value result;
            if (vm.stack_top > vm.stack) {
                vm_pop(&vm, &result);
            } else {
                result = make_nothing();
            }
            /* Free any remaining values on the stack. */
            vm_free_stack(&vm);
            return result; // NOLINT(clang-analyzer-core.StackAddressEscape)
        }

        default:
            return vm_runtime_error(&vm, "unknown opcode %d", instruction);
        }
    }
}
