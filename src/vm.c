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
        Chunk *chunk = frame->closure->function->chunk;
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
    case VAL_CLOSURE:
        /* Identity-based: only equal if pointing to the same ObjClosure. */
        return a->closure == b->closure;
    case VAL_ARRAY: {
        /* Structural equality: same length and all elements pairwise equal. */
        const ObjArray *aa = a->array;
        const ObjArray *ba = b->array;
        if (aa == ba)
            return true; /* Same backing store — trivially equal. */
        if (aa->count != ba->count)
            return false;
        for (size_t i = 0; i < aa->count; i++) {
            if (!values_equal(&aa->data[i], &ba->data[i]))
                return false;
        }
        return true;
    }
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

/* str(x) — convert any value to its string representation via
 * value_format(). This is the explicit conversion counterpart to
 * the implicit coercion that say() does internally. */
static Value native_str(int argc, Value *args, EvalContext *ctx) {
    (void)argc; /* Arity is checked by the VM before calling. */
    (void)ctx;

    char *formatted = value_format(&args[0]);
    if (!formatted)
        return make_error("memory allocation failed");

    return make_string(formatted);
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

    Value str_fn = make_native("str", 1, native_str);
    runtime_var_define("str", &str_fn);
    value_free(&str_fn);
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
    case VAL_CLOSURE:
        return "function";
    case VAL_ARRAY:
        return "array";
    case VAL_ERROR:
        return "error";
    default:
        return "value";
    }
}

/* ---- Upvalue capture helper ----
 *
 * Find or create an ObjUpvalue for the given stack slot.
 * Walks the VM's open-upvalue linked list (sorted by slot address,
 * descending) looking for an existing upvalue pointing to `slot`.
 * If found, increments its refcount and returns it.
 * If not found, creates a new ObjUpvalue and inserts it into the
 * list at the correct position (maintaining descending order).
 * Returns NULL on allocation failure.
 */
static ObjUpvalue *capture_upvalue(VM *vm, Value *slot) {
    ObjUpvalue *prev = NULL;
    ObjUpvalue *curr = vm->open_upvalues;

    /* Walk the list until we find an upvalue at or below our slot.
     * The list is sorted by slot address descending (higher addresses first). */
    while (curr != NULL && curr->location > slot) {
        prev = curr;
        curr = curr->next;
    }

    /* If we found an existing upvalue for this exact slot, reuse it. */
    if (curr != NULL && curr->location == slot) {
        curr->refcount++;
        return curr;
    }

    /* Create a new upvalue for this slot. */
    ObjUpvalue *uv = obj_upvalue_new(slot);
    if (!uv)
        return NULL;

    /* Insert into the linked list between prev and curr. */
    uv->next = curr;
    if (prev == NULL) {
        vm->open_upvalues = uv;
    } else {
        prev->next = uv;
    }

    return uv;
}

/* ---- Close upvalues ----
 *
 * Close all open upvalues whose stack location is at or above `last`.
 * "Closing" an upvalue means copying the value from its stack slot into
 * the upvalue's `closed` field, then redirecting `location` to point to
 * `&closed` instead of the (soon-to-be-reclaimed) stack slot.
 *
 * Called from OP_RETURN (to close all upvalues in the returning frame's
 * stack window) and OP_CLOSE_UPVALUE (to close a single local before
 * popping it).
 *
 * The open_upvalues list is sorted by slot address descending, so we
 * walk from the head and stop once we reach a location below `last`.
 */
static void close_upvalues(VM *vm, Value *last) {
    while (vm->open_upvalues != NULL && vm->open_upvalues->location >= last) {
        ObjUpvalue *uv = vm->open_upvalues;
        /* Copy the stack value into the upvalue's closed field. */
        uv->closed = *uv->location;
        /* Redirect location to the upvalue's own closed field. */
        uv->location = &uv->closed;
        /* Remove from the open list. */
        vm->open_upvalues = uv->next;
        uv->next = NULL;
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
    vm.open_upvalues = NULL;
    vm_reset_stack(&vm);

    /* Create a "script" ObjFunction that wraps the top-level chunk.
     * This lives on the C stack — it's only used for the duration of
     * this vm_execute() call. The chunk pointer is borrowed (not owned). */
    ObjFunction script_fn = {
        .refcount = 1,
        .name = NULL,
        .arity = 0,
        .upvalue_count = 0,
        .params = NULL,
        .chunk = chunk,
        .native = NULL,
    };

    /* Wrap the script function in a stack-allocated ObjClosure with
     * 0 upvalues. No heap allocation needed — this closure lives on
     * the C stack alongside script_fn for the duration of vm_execute(). */
    ObjClosure script_closure = {
        .refcount = 1,
        .function = &script_fn,
        .upvalues = NULL,
        .upvalue_count = 0,
    };

    /* Push frame 0: the top-level script. */
    CallFrame *frame = &vm.frames[vm.frame_count++];
    frame->closure = &script_closure;
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
            if (!value_clone(&v, &frame->closure->function->chunk->constants[idx])) {
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
            /* Concatenation operator (++).
             * Supports two modes:
             *   - array ++ array  → new array with elements from both
             *   - string ++ string → new concatenated string
             * Mixed types (one array, one non-array) produce a runtime error.
             * Use str() for explicit string conversion of non-string values. */
            Value b, a;
            if (!vm_pop(&vm, &b)) {
                return vm_runtime_error(&vm, "stack underflow");
            }
            if (!vm_pop(&vm, &a)) {
                value_free(&b);
                return vm_runtime_error(&vm, "stack underflow");
            }

            /* Array concatenation: both operands must be arrays. */
            if (a.type == VAL_ARRAY && b.type == VAL_ARRAY) {
                ObjArray *la = a.array;
                ObjArray *lb = b.array;
                ObjArray *result = obj_array_new();
                if (!result) {
                    value_free(&a);
                    value_free(&b);
                    return vm_runtime_error(&vm, "memory allocation failed");
                }
                /* Append all elements from left array, then right array. */
                for (size_t i = 0; i < la->count; i++) {
                    Value elem;
                    if (!value_clone(&elem, &la->data[i])) {
                        value_free(&a);
                        value_free(&b);
                        /* Free partially-built result array. */
                        Value tmp = make_array(result);
                        value_free(&tmp);
                        return vm_runtime_error(&vm, "memory allocation failed");
                    }
                    obj_array_push(result, elem);
                }
                for (size_t i = 0; i < lb->count; i++) {
                    Value elem;
                    if (!value_clone(&elem, &lb->data[i])) {
                        value_free(&a);
                        value_free(&b);
                        Value tmp = make_array(result);
                        value_free(&tmp);
                        return vm_runtime_error(&vm, "memory allocation failed");
                    }
                    obj_array_push(result, elem);
                }
                value_free(&a);
                value_free(&b);
                if (!vm_push(&vm, make_array(result))) {
                    return vm_runtime_error(&vm, "stack overflow");
                }
                break;
            }

            /* Mixed array/non-array is an error. */
            if (a.type == VAL_ARRAY || b.type == VAL_ARRAY) {
                const char *ta = value_type_name(a.type);
                const char *tb = value_type_name(b.type);
                value_free(&a);
                value_free(&b);
                return vm_runtime_error(&vm, "cannot concatenate %s with %s", ta, tb);
            }

            /* String concatenation: both operands must be strings. */
            if (a.type != VAL_STRING || b.type != VAL_STRING) {
                const char *ta = value_type_name(a.type);
                const char *tb = value_type_name(b.type);
                value_free(&a);
                value_free(&b);
                return vm_runtime_error(&vm, "++ requires strings, got %s and %s", ta, tb);
            }
            size_t sla = strlen(a.string);
            size_t slb = strlen(b.string);
            char *sresult = malloc(sla + slb + 1);
            if (!sresult) {
                value_free(&a);
                value_free(&b);
                return vm_runtime_error(&vm, "memory allocation failed");
            }
            memcpy(sresult, a.string, sla);
            memcpy(sresult + sla, b.string, slb);
            sresult[sla + slb] = '\0';
            value_free(&a);
            value_free(&b);
            if (!vm_push(&vm, make_string(sresult))) {
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
            const char *name = frame->closure->function->chunk->constants[idx].string;
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
            const char *name = frame->closure->function->chunk->constants[idx].string;
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
            const char *name = frame->closure->function->chunk->constants[idx].string;
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

        case OP_SET_LOCAL: {
            /* Write TOS into a local variable slot without popping.
             * The old slot value is freed, then TOS is cloned into it.
             * TOS stays on the stack as the expression result. */
            uint8_t slot = read_byte(frame);
            Value tos;
            if (!vm_peek(&vm, 0, &tos)) {
                return vm_runtime_error(&vm, "stack underflow");
            }
            Value cloned;
            if (!value_clone(&cloned, &tos)) {
                return vm_runtime_error(&vm, "memory allocation failed");
            }
            value_free(&frame->slots[slot]);
            frame->slots[slot] = cloned;
            break;
        }

        case OP_GET_UPVALUE: {
            /* Read a captured variable through upvalue indirection.
             * The 1-byte operand indexes into the current closure's
             * upvalue array. The upvalue's location pointer points to
             * either a live stack slot (open) or the upvalue's own
             * closed field (closed). Clone the value and push it. */
            uint8_t slot = read_byte(frame);
            if (!frame->closure->upvalues) {
                return vm_runtime_error(&vm, "no upvalues in current closure");
            }
            ObjUpvalue *uv = frame->closure->upvalues[slot];
            Value v;
            if (!value_clone(&v, uv->location)) {
                return vm_runtime_error(&vm, "memory allocation failed");
            }
            if (!vm_push(&vm, v)) {
                return vm_runtime_error(&vm, "stack overflow");
            }
            break;
        }

        case OP_SET_UPVALUE: {
            /* Write TOS into a captured variable through upvalue indirection.
             * Like OP_SET_LOCAL, TOS stays on the stack as the expression result.
             * The old value at the upvalue's location is freed, then TOS is
             * cloned into it. */
            uint8_t slot = read_byte(frame);
            if (!frame->closure->upvalues) {
                return vm_runtime_error(&vm, "no upvalues in current closure");
            }
            ObjUpvalue *uv = frame->closure->upvalues[slot];
            Value tos;
            if (!vm_peek(&vm, 0, &tos)) {
                return vm_runtime_error(&vm, "stack underflow");
            }
            Value cloned;
            if (!value_clone(&cloned, &tos)) {
                return vm_runtime_error(&vm, "memory allocation failed");
            }
            value_free(uv->location);
            *uv->location = cloned;
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

        case OP_CLOSE_UPVALUE: {
            /* Close the upvalue pointing at TOS, then pop and free the
             * value (same effect as OP_POP but closes first). Used by
             * block scoping to close captured locals before they leave
             * scope. */
            close_upvalues(&vm, vm.stack_top - 1);
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

            if (callee.type == VAL_FUNCTION) {
                /* VAL_FUNCTION is only used for native functions (say, str, etc.).
                 * User-defined functions are always VAL_CLOSURE (created by OP_CLOSURE). */
                ObjFunction *fn = callee.function;

                if (argc != fn->arity) {
                    return vm_runtime_error(&vm, "'%s' expects %d argument%s, got %d",
                                            fn->name ? fn->name : "<fn>", fn->arity,
                                            fn->arity == 1 ? "" : "s", argc);
                }

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
            } else if (callee.type == VAL_CLOSURE) {
                /* User-defined function via closure. The closure is
                 * already on the stack in the callee slot. */
                ObjClosure *cl = callee.closure;
                ObjFunction *fn = cl->function;

                if (argc != fn->arity) {
                    return vm_runtime_error(&vm, "'%s' expects %d argument%s, got %d",
                                            fn->name ? fn->name : "<fn>", fn->arity,
                                            fn->arity == 1 ? "" : "s", argc);
                }

                if (vm.frame_count >= FRAMES_MAX) {
                    return vm_runtime_error(&vm, "stack overflow");
                }
                CallFrame *new_frame = &vm.frames[vm.frame_count++];
                new_frame->closure = cl;
                new_frame->ip = fn->chunk->code;
                new_frame->slots = vm.stack_top - argc - 1;
            } else {
                return vm_runtime_error(&vm, "cannot call %s", value_type_name(callee.type));
            }
            break;
        }

        case OP_CLOSURE: {
            /* Create an ObjClosure from a constant pool ObjFunction.
             * The constant index points to a VAL_FUNCTION in the pool.
             * After the constant index, there are fn->upvalue_count pairs
             * of (is_local, index) bytes describing captured variables.
             * For now, upvalue_count is always 0 (no captures yet). */
            uint8_t idx = read_byte(frame);
            ObjFunction *fn = frame->closure->function->chunk->constants[idx].function;
            ObjClosure *cl = obj_closure_new(fn, fn->upvalue_count);
            if (!cl) {
                return vm_runtime_error(&vm, "memory allocation failed");
            }
            /* Read upvalue descriptors and populate the closure's upvalue array.
             * Each descriptor is a pair of (is_local, index) bytes:
             * - is_local=1: capture a local from the enclosing frame's stack slot.
             * - is_local=0: copy an upvalue from the enclosing closure's upvalue array.
             * Currently upvalue_count is always 0 (capture emitted in closures-capture). */
            for (int i = 0; i < fn->upvalue_count; i++) {
                uint8_t is_local = read_byte(frame);
                uint8_t index = read_byte(frame);
                if (is_local) {
                    cl->upvalues[i] = capture_upvalue(&vm, &frame->slots[index]);
                } else {
                    /* Copy an upvalue from the enclosing closure.
                     * The enclosing closure must have upvalues for this path. */
                    if (frame->closure->upvalues != NULL) {
                        cl->upvalues[i] = frame->closure->upvalues[index];
                        if (cl->upvalues[i])
                            cl->upvalues[i]->refcount++;
                    } else {
                        cl->upvalues[i] = NULL;
                    }
                }
                if (!cl->upvalues[i]) {
                    /* Free the partially-constructed closure on failure. */
                    value_free(&(Value){.type = VAL_CLOSURE, .closure = cl});
                    return vm_runtime_error(&vm, "memory allocation failed");
                }
            }
            if (!vm_push(&vm, make_closure(cl))) {
                return vm_runtime_error(&vm, "stack overflow");
            }
            break;
        }

        case OP_ARRAY: {
            /* Build an array from the top N values on the stack.
             * The first element was pushed first (deepest on stack),
             * so we pop in reverse and insert back-to-front. */
            uint8_t count = read_byte(frame);
            ObjArray *arr = obj_array_new();
            if (!arr) {
                return vm_runtime_error(&vm, "out of memory");
            }

            /* Pre-grow the array to the correct size. Pop values into
             * a temporary buffer, then push them in element order. */
            Value temp[256];
            for (int i = count - 1; i >= 0; i--) {
                if (!vm_pop(&vm, &temp[i])) {
                    /* Free already-popped values */
                    for (int j = i + 1; j < count; j++)
                        value_free(&temp[j]);
                    free(arr->data);
                    free(arr);
                    return vm_runtime_error(&vm, "stack underflow");
                }
            }
            /* Append elements in order (first element = index 0). */
            for (int i = 0; i < count; i++) {
                obj_array_push(arr, temp[i]); /* takes ownership */
            }

            if (!vm_push(&vm, make_array(arr))) {
                return vm_runtime_error(&vm, "stack overflow");
            }
            break;
        }

        case OP_INDEX_GET: {
            /* Read array[index]: pop index, pop array, push element.
             * Negative indices wrap from end (e.g. -1 = last element).
             * Non-integer or out-of-bounds indices produce runtime errors. */
            Value idx_val, arr_val;
            if (!vm_pop(&vm, &idx_val)) {
                return vm_runtime_error(&vm, "stack underflow");
            }
            if (!vm_pop(&vm, &arr_val)) {
                value_free(&idx_val);
                return vm_runtime_error(&vm, "stack underflow");
            }
            if (arr_val.type != VAL_ARRAY) {
                const char *t = value_type_name(arr_val.type);
                value_free(&arr_val);
                value_free(&idx_val);
                return vm_runtime_error(&vm, "cannot index %s", t);
            }
            if (idx_val.type != VAL_NUMBER) {
                value_free(&arr_val);
                value_free(&idx_val);
                return vm_runtime_error(&vm, "index must be an integer");
            }
            double raw_idx = idx_val.number;
            /* Check that the index is an integer. */
            if (raw_idx != floor(raw_idx)) {
                value_free(&arr_val);
                value_free(&idx_val);
                return vm_runtime_error(&vm, "index must be an integer");
            }
            long long int_idx = (long long)raw_idx;
            ObjArray *arr = arr_val.array;
            /* Handle negative indices (wrap from end). */
            if (int_idx < 0)
                int_idx += (long long)arr->count;
            if (int_idx < 0 || (size_t)int_idx >= arr->count) {
                value_free(&arr_val);
                value_free(&idx_val);
                return vm_runtime_error(&vm, "index out of bounds");
            }
            /* Clone the element at the index and push it. */
            Value result;
            if (!value_clone(&result, &arr->data[(size_t)int_idx])) {
                value_free(&arr_val);
                value_free(&idx_val);
                return vm_runtime_error(&vm, "memory allocation failed");
            }
            value_free(&arr_val);
            value_free(&idx_val);
            if (!vm_push(&vm, result)) {
                return vm_runtime_error(&vm, "stack overflow");
            }
            break;
        }

        case OP_INDEX_SET: {
            /* Write array[index] = value.
             *
             * Stack in:  [..., value, array, index]   (TOS = index)
             * Pop index, pop array. Peek value (now at TOS).
             * COW the array if refcount > 1, set element, push modified array.
             * Stack out: [..., value, modified_array]
             *
             * The compiler follows this with SET_GLOBAL/LOCAL to write the
             * modified array back to the source variable, then OP_POP to
             * discard the modified array, leaving value as the result. */
            Value idx_val, arr_val;
            if (!vm_pop(&vm, &idx_val)) {
                return vm_runtime_error(&vm, "stack underflow");
            }
            if (!vm_pop(&vm, &arr_val)) {
                value_free(&idx_val);
                return vm_runtime_error(&vm, "stack underflow");
            }
            /* Peek at the value (now at TOS after popping index and array). */
            Value val;
            if (!vm_peek(&vm, 0, &val)) {
                value_free(&arr_val);
                value_free(&idx_val);
                return vm_runtime_error(&vm, "stack underflow");
            }
            if (arr_val.type != VAL_ARRAY) {
                const char *t = value_type_name(arr_val.type);
                value_free(&arr_val);
                value_free(&idx_val);
                return vm_runtime_error(&vm, "cannot index %s", t);
            }
            if (idx_val.type != VAL_NUMBER) {
                value_free(&arr_val);
                value_free(&idx_val);
                return vm_runtime_error(&vm, "index must be an integer");
            }
            double raw_idx = idx_val.number;
            if (raw_idx != floor(raw_idx)) {
                value_free(&arr_val);
                value_free(&idx_val);
                return vm_runtime_error(&vm, "index must be an integer");
            }
            long long int_idx = (long long)raw_idx;
            /* Handle negative indices. */
            if (int_idx < 0)
                int_idx += (long long)arr_val.array->count;
            if (int_idx < 0 || (size_t)int_idx >= arr_val.array->count) {
                value_free(&arr_val);
                value_free(&idx_val);
                return vm_runtime_error(&vm, "index out of bounds");
            }
            /* COW: ensure we own the backing store before mutating. */
            obj_array_ensure_owned(&arr_val);
            /* Free old element and clone the new value into the slot. */
            value_free(&arr_val.array->data[(size_t)int_idx]);
            Value cloned_val;
            if (!value_clone(&cloned_val, &val)) {
                value_free(&arr_val);
                value_free(&idx_val);
                return vm_runtime_error(&vm, "memory allocation failed");
            }
            arr_val.array->data[(size_t)int_idx] = cloned_val;
            value_free(&idx_val);
            /* Push the modified array on top of the value. */
            if (!vm_push(&vm, arr_val)) {
                return vm_runtime_error(&vm, "stack overflow");
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

            /* Close any open upvalues pointing into this frame's stack
             * window before we pop those slots. This moves captured
             * values from the stack into the upvalue's closed field,
             * so closures that outlive this call frame still work. */
            close_upvalues(&vm, frame->slots);

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
