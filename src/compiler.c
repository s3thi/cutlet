/*
 * compiler.c - Cutlet bytecode compiler
 *
 * Single-pass compiler that walks the AST and emits bytecode
 * into a Chunk. Jump instructions use forward patching: emit
 * a placeholder offset, then patch it once the target is known.
 */

#include "compiler.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Loop context: tracks state for break/continue compilation.
 * Each while loop pushes a LoopContext; nested loops form a stack
 * via the `enclosing` pointer.
 */
typedef struct LoopContext {
    size_t loop_start;     /* Bytecode offset of LOOP_START (for continue and OP_LOOP). */
    size_t *break_jumps;   /* Dynamic array of forward-jump offsets to patch at BREAK_TARGET. */
    size_t break_count;    /* Number of recorded break jumps. */
    size_t break_capacity; /* Capacity of break_jumps array. */
    int scope_depth;       /* Scope depth at loop entry (before body's begin_scope). */
    struct LoopContext *enclosing; /* Enclosing loop context (for nested loops). */
} LoopContext;

/*
 * Compilation context: distinguishes top-level script code from
 * function body code. In function context, identifiers are resolved
 * against the locals array before falling back to globals.
 */
typedef enum {
    COMPILE_SCRIPT,   /* Top-level code — all variables are global. */
    COMPILE_FUNCTION, /* Inside a function body — has local slots. */
} CompileContext;

/*
 * Local variable entry: tracks a named local in the compiler's
 * locals array. Each local occupies one stack slot in the call
 * frame at runtime.
 */
typedef struct {
    const char *name; /* Variable name (points into AST — not owned). */
    int length;       /* Length of the name string. */
    int depth;        /* Scope depth at which this local was declared. */
} Local;

/* Maximum number of local variables per function (1-byte slot index). */
#define LOCALS_MAX 256

/* Internal compiler state. */
typedef struct {
    Chunk *chunk;              /* The chunk being compiled into. */
    CompileError *err;         /* Where to report errors. */
    bool had_error;            /* Set to true on first error. */
    LoopContext *current_loop; /* Current innermost loop context (NULL if not in a loop). */
    CompileContext context;    /* Script vs function body. */
    Local locals[LOCALS_MAX];  /* Local variable slots (params, then declarations). */
    int local_count;           /* Number of locals currently in scope. */
    int scope_depth;           /* Current scope nesting depth (0=script, 1=function body). */
} Compiler;

/* ---- Helpers ---- */

/* Report a compile error. Only the first error is recorded. */
static void compiler_error(Compiler *c, const char *fmt, ...) {
    if (c->had_error)
        return;
    c->had_error = true;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(c->err->message, sizeof(c->err->message), fmt, ap);
    va_end(ap); // NOLINT(clang-analyzer-valist.Uninitialized)
}

/* Duplicate a string, setting compiler error on failure. Returns NULL on error. */
static char *compiler_strdup(Compiler *c, const char *s) {
    char *dup = strdup(s);
    if (!dup)
        compiler_error(c, "memory allocation failed");
    return dup;
}

/* Emit a single byte. Sets had_error on allocation failure. */
static void emit_byte(Compiler *c, uint8_t byte, int line) {
    if (c->had_error)
        return;
    if (!chunk_write(c->chunk, byte, line)) {
        compiler_error(c, "memory allocation failed");
    }
}

/* Emit two bytes. */
static void emit_bytes(Compiler *c, uint8_t b1, uint8_t b2, int line) {
    emit_byte(c, b1, line);
    emit_byte(c, b2, line);
}

/* Emit a constant instruction: OP_CONSTANT + index. */
static void emit_constant(Compiler *c, Value value, int line) {
    int idx = chunk_find_or_add_constant(c->chunk, value);
    if (idx < 0) {
        compiler_error(c, "too many constants in one chunk");
        return;
    }
    emit_bytes(c, OP_CONSTANT, (uint8_t)idx, line);
}

/*
 * Emit a jump instruction with a placeholder 2-byte offset.
 * Returns the offset of the placeholder bytes (for patching).
 */
static size_t emit_jump(Compiler *c, uint8_t op, int line) {
    emit_byte(c, op, line);
    emit_byte(c, 0xFF, line); /* placeholder high byte */
    emit_byte(c, 0xFF, line); /* placeholder low byte */
    return c->chunk->count - 2;
}

/*
 * Patch a previously emitted jump instruction.
 * offset is the location of the placeholder bytes.
 * The jump target is the current chunk->count (i.e. the next instruction).
 */
static void patch_jump(Compiler *c, size_t offset) {
    /* The jump offset is relative to the instruction AFTER the jump. */
    size_t jump = c->chunk->count - (offset + 2);
    if (jump > UINT16_MAX) {
        compiler_error(c, "jump offset too large");
        return;
    }
    c->chunk->code[offset] = (uint8_t)((jump >> 8) & 0xFF);
    c->chunk->code[offset + 1] = (uint8_t)(jump & 0xFF);
}

/*
 * Emit a backward jump (OP_LOOP) to loop_start.
 * The offset is calculated as the distance from the current position
 * back to loop_start, including the 3 bytes for the OP_LOOP instruction itself.
 */
static void emit_loop(Compiler *c, size_t loop_start, int line) {
    emit_byte(c, OP_LOOP, line);
    /* +2 for the two offset bytes we're about to emit */
    size_t offset = c->chunk->count - loop_start + 2;
    if (offset > UINT16_MAX) {
        compiler_error(c, "loop body too large");
        return;
    }
    emit_byte(c, (uint8_t)((offset >> 8) & 0xFF), line);
    emit_byte(c, (uint8_t)(offset & 0xFF), line);
}

/* ---- Scope management ---- */

/*
 * Begin a new scope. Increments scope_depth so that locals declared
 * hereafter are tagged with the new depth. Used by if/while bodies.
 */
static void begin_scope(Compiler *c) { c->scope_depth++; }

/*
 * End the current scope. Emits bytecode to clean up locals declared
 * in the ending scope while preserving the block's result value (TOS).
 *
 * Cleanup strategy (expression-based blocks leave one result on stack):
 *   1. OP_SET_LOCAL [first_departing_slot] — save result into the first
 *      local's position.
 *   2. OP_POP x N — pop the result copy plus the remaining N-1 locals.
 *   3. Result now occupies the first departing slot, which is TOS.
 *
 * If the scope declares 0 locals, no cleanup is needed.
 */
static void end_scope(Compiler *c, int line) {
    /* Count locals at the current depth (scan backwards). */
    int n = 0;
    while (n < c->local_count && c->locals[c->local_count - 1 - n].depth == c->scope_depth) {
        n++;
    }

    if (n > 0) {
        int first_slot = c->local_count - n;

        /* Save the block result (TOS) into the first departing local's slot. */
        emit_bytes(c, OP_SET_LOCAL, (uint8_t)first_slot, line);

        /* Pop the result copy and all remaining departing locals. */
        for (int i = 0; i < n; i++) {
            emit_byte(c, OP_POP, line);
        }

        c->local_count = first_slot;
    }

    c->scope_depth--;
}

/* ---- AST node compilation ---- */

/* Forward declaration: compile any AST node. */
static void compile_node(Compiler *c, const AstNode *node);

/* Compile a number literal. */
static void compile_number(Compiler *c, const AstNode *node) {
    double n = strtod(node->value, NULL);
    emit_constant(c, make_number(n), (int)node->line);
}

/* Compile a string literal. */
static void compile_string(Compiler *c, const AstNode *node) {
    char *s = compiler_strdup(c, node->value);
    if (!s)
        return;
    emit_constant(c, make_string(s), (int)node->line);
}

/* Compile a boolean literal. */
static void compile_bool(Compiler *c, const AstNode *node) {
    if (strcmp(node->value, "true") == 0) {
        emit_byte(c, OP_TRUE, (int)node->line);
    } else {
        emit_byte(c, OP_FALSE, (int)node->line);
    }
}

/* Compile a nothing literal. */
static void compile_nothing(Compiler *c, const AstNode *node) {
    emit_byte(c, OP_NOTHING, (int)node->line);
}

/*
 * Resolve a name against the compiler's locals array.
 * Returns the slot index if found, or -1 if not a local.
 */
static int resolve_local(Compiler *c, const char *name) {
    int len = (int)strlen(name);
    /* Search backwards so inner-scoped locals shadow outer ones. */
    for (int i = c->local_count - 1; i >= 0; i--) {
        if (c->locals[i].length == len && memcmp(c->locals[i].name, name, (size_t)len) == 0) {
            return i;
        }
    }
    return -1;
}

/* Compile an identifier (variable read).
 * In function context, check locals first (emit OP_GET_LOCAL).
 * Fall back to OP_GET_GLOBAL for globals or script context. */
static void compile_ident(Compiler *c, const AstNode *node) {
    /* In function context, try resolving as a local variable. */
    if (c->context == COMPILE_FUNCTION) {
        int slot = resolve_local(c, node->value);
        if (slot >= 0) {
            emit_bytes(c, OP_GET_LOCAL, (uint8_t)slot, (int)node->line);
            return;
        }
    }

    /* Fall back to global variable lookup. */
    char *name = compiler_strdup(c, node->value);
    if (!name)
        return;
    int idx = chunk_find_or_add_constant(c->chunk, make_string(name));
    if (idx < 0) {
        compiler_error(c, "too many constants");
        return;
    }
    emit_bytes(c, OP_GET_GLOBAL, (uint8_t)idx, (int)node->line);
}

/* Compile a binary operator expression. */
static void compile_binop(Compiler *c, const AstNode *node) {
    const char *op = node->value;
    int line = (int)node->line;

    /* Short-circuit: and */
    if (strcmp(op, "and") == 0) {
        compile_node(c, node->left);
        size_t end_jump = emit_jump(c, OP_JUMP_IF_FALSE, line);
        emit_byte(c, OP_POP, line); /* discard truthy left */
        compile_node(c, node->right);
        patch_jump(c, end_jump);
        return;
    }

    /* Short-circuit: or */
    if (strcmp(op, "or") == 0) {
        compile_node(c, node->left);
        size_t end_jump = emit_jump(c, OP_JUMP_IF_TRUE, line);
        emit_byte(c, OP_POP, line); /* discard falsy left */
        compile_node(c, node->right);
        patch_jump(c, end_jump);
        return;
    }

    /* Non-short-circuit: compile both operands then the operator. */
    compile_node(c, node->left);
    compile_node(c, node->right);

    if (strcmp(op, "+") == 0) {
        emit_byte(c, OP_ADD, line);
    } else if (strcmp(op, "-") == 0) {
        emit_byte(c, OP_SUBTRACT, line);
    } else if (strcmp(op, "*") == 0) {
        emit_byte(c, OP_MULTIPLY, line);
    } else if (strcmp(op, "/") == 0) {
        emit_byte(c, OP_DIVIDE, line);
    } else if (strcmp(op, "%") == 0) {
        emit_byte(c, OP_MODULO, line);
    } else if (strcmp(op, "**") == 0) {
        emit_byte(c, OP_POWER, line);
    } else if (strcmp(op, "++") == 0) {
        emit_byte(c, OP_CONCAT, line);
    } else if (strcmp(op, "==") == 0) {
        emit_byte(c, OP_EQUAL, line);
    } else if (strcmp(op, "!=") == 0) {
        emit_byte(c, OP_NOT_EQUAL, line);
    } else if (strcmp(op, "<") == 0) {
        emit_byte(c, OP_LESS, line);
    } else if (strcmp(op, ">") == 0) {
        emit_byte(c, OP_GREATER, line);
    } else if (strcmp(op, "<=") == 0) {
        emit_byte(c, OP_LESS_EQUAL, line);
    } else if (strcmp(op, ">=") == 0) {
        emit_byte(c, OP_GREATER_EQUAL, line);
    } else {
        compiler_error(c, "unknown operator '%s'", op);
    }
}

/* Compile a unary operator expression. */
static void compile_unary(Compiler *c, const AstNode *node) {
    compile_node(c, node->left);
    if (strcmp(node->value, "-") == 0) {
        emit_byte(c, OP_NEGATE, (int)node->line);
    } else if (strcmp(node->value, "not") == 0) {
        emit_byte(c, OP_NOT, (int)node->line);
    } else {
        compiler_error(c, "unknown unary operator '%s'", node->value);
    }
}

/* Compile a variable declaration: my name = expr
 *
 * In function context, the RHS value lands on the stack at the next
 * local slot.  We register the local, then emit OP_GET_LOCAL to push
 * a clone as the expression result.  This way compile_block's OP_POP
 * removes the clone while the local stays at its fixed slot.
 *
 * In script context, we use OP_DEFINE_GLOBAL as before. */
static void compile_decl(Compiler *c, const AstNode *node) {
    if (c->context == COMPILE_FUNCTION) {
        /* Compile RHS first — value lands at stack position = local_count. */
        compile_node(c, node->left);

        /* Register the new local variable at the current slot. */
        int slot = c->local_count;
        if (c->local_count >= LOCALS_MAX) {
            compiler_error(c, "too many local variables");
            return;
        }
        c->locals[c->local_count++] = (Local){
            .name = node->value, .length = (int)strlen(node->value), .depth = c->scope_depth};

        /* Push a clone of the local as the expression result.
         * compile_block will OP_POP this clone; the local stays. */
        emit_bytes(c, OP_GET_LOCAL, (uint8_t)slot, (int)node->line);
        return;
    }

    /* Script context: global variable declaration. */
    compile_node(c, node->left); /* compile the RHS expression */
    char *name = compiler_strdup(c, node->value);
    if (!name)
        return;
    int idx = chunk_find_or_add_constant(c->chunk, make_string(name));
    if (idx < 0) {
        compiler_error(c, "too many constants");
        return;
    }
    emit_bytes(c, OP_DEFINE_GLOBAL, (uint8_t)idx, (int)node->line);
}

/* Compile an assignment: name = expr
 *
 * In function context, check if the name resolves to a local variable
 * (parameter or prior `my` declaration).  If so, emit OP_SET_LOCAL
 * which writes TOS into the slot without popping (value stays as the
 * expression result).  Otherwise fall back to OP_SET_GLOBAL. */
static void compile_assign(Compiler *c, const AstNode *node) {
    compile_node(c, node->left); /* compile the RHS expression */

    /* In function context, try resolving as a local variable. */
    if (c->context == COMPILE_FUNCTION) {
        int slot = resolve_local(c, node->value);
        if (slot >= 0) {
            emit_bytes(c, OP_SET_LOCAL, (uint8_t)slot, (int)node->line);
            return;
        }
    }

    /* Fall back to global variable assignment. */
    char *name = compiler_strdup(c, node->value);
    if (!name)
        return;
    int idx = chunk_find_or_add_constant(c->chunk, make_string(name));
    if (idx < 0) {
        compiler_error(c, "too many constants");
        return;
    }
    emit_bytes(c, OP_SET_GLOBAL, (uint8_t)idx, (int)node->line);
}

/* Compile a block: sequence of expressions. */
static void compile_block(Compiler *c, const AstNode *node) {
    if (node->child_count == 0) {
        emit_byte(c, OP_NOTHING, (int)node->line);
        return;
    }
    for (size_t i = 0; i < node->child_count; i++) {
        compile_node(c, node->children[i]);
        /* Pop all but the last value (the block's result). */
        if (i < node->child_count - 1) {
            emit_byte(c, OP_POP, (int)node->children[i]->line);
        }
    }
}

/*
 * Compile a function call: name(arg1, arg2, ...)
 *
 * Stack-based call convention: push the callee, then push each argument,
 * then emit OP_CALL [argc]. The VM reads the callee from the stack at
 * runtime. In function context, the callee is resolved locally first
 * (OP_GET_LOCAL) before falling back to OP_GET_GLOBAL, matching the
 * pattern used by compile_ident().
 */
static void compile_call(Compiler *c, const AstNode *node) {
    /* Check argc limit before emitting anything. */
    if (node->child_count > 255) {
        compiler_error(c, "too many arguments (max 255)");
        return;
    }
    int line = (int)node->line;

    /* Push the callee: in function context, try local resolution first. */
    if (c->context == COMPILE_FUNCTION) {
        int slot = resolve_local(c, node->value);
        if (slot >= 0) {
            emit_bytes(c, OP_GET_LOCAL, (uint8_t)slot, line);
            goto args;
        }
    }

    /* Fall back to global variable lookup for the callee. */
    {
        char *fn_name = compiler_strdup(c, node->value);
        if (!fn_name)
            return;
        int name_idx = chunk_find_or_add_constant(c->chunk, make_string(fn_name));
        if (name_idx < 0) {
            compiler_error(c, "too many constants");
            return;
        }
        emit_bytes(c, OP_GET_GLOBAL, (uint8_t)name_idx, line);
    }

args:

    /* Compile all arguments (push them onto the stack after the callee). */
    for (size_t i = 0; i < node->child_count; i++) {
        compile_node(c, node->children[i]);
    }

    /* Emit OP_CALL with argc only. The callee is already on the stack. */
    emit_bytes(c, OP_CALL, (uint8_t)node->child_count, line);
}

/*
 * Compile an if/else expression.
 *
 * children[0] = condition
 * children[1] = then-body
 * children[2] = else-body (optional)
 *
 * Always leaves exactly one value on the stack.
 */
static void compile_if(Compiler *c, const AstNode *node) {
    if (node->child_count < 2) {
        compiler_error(c, "malformed if expression");
        return;
    }

    int line = (int)node->line;

    /* Compile condition. */
    compile_node(c, node->children[0]);

    /* Jump to else if condition is false. */
    size_t else_jump = emit_jump(c, OP_JUMP_IF_FALSE, line);

    /* Pop the condition value (it was truthy). */
    emit_byte(c, OP_POP, line);

    /* Compile then-body with block scoping (function context only). */
    if (c->context == COMPILE_FUNCTION)
        begin_scope(c);
    compile_node(c, node->children[1]);
    if (c->context == COMPILE_FUNCTION)
        end_scope(c, line);

    /* Jump over else-body. */
    size_t end_jump = emit_jump(c, OP_JUMP, line);

    /* Else label: patch the else_jump to here. */
    patch_jump(c, else_jump);

    /* Pop the condition value (it was falsy). */
    emit_byte(c, OP_POP, line);

    if (node->child_count >= 3) {
        /* Compile else-body with block scoping (function context only). */
        if (c->context == COMPILE_FUNCTION)
            begin_scope(c);
        compile_node(c, node->children[2]);
        if (c->context == COMPILE_FUNCTION)
            end_scope(c, line);
    } else {
        /* No else clause: push nothing. */
        emit_byte(c, OP_NOTHING, line);
    }

    /* End label: patch the end_jump to here. */
    patch_jump(c, end_jump);
}

/*
 * Compile a while loop expression.
 *
 * children[0] = condition
 * children[1] = body
 *
 * Bytecode layout (accumulator pattern):
 *   OP_NOTHING              ; initial accumulator (result if loop never runs)
 *   LOOP_START:
 *     <condition>           ; push condition value
 *     OP_JUMP_IF_FALSE → LOOP_EXIT
 *     OP_POP                ; pop truthy condition
 *     OP_POP                ; pop old accumulator
 *     <body>                ; push new accumulator (body result)
 *     OP_LOOP → LOOP_START  ; backward jump
 *   LOOP_EXIT:
 *     OP_POP                ; pop falsy condition
 *                           ; accumulator is TOS (the loop result)
 */
static void compile_while(Compiler *c, const AstNode *node) {
    if (node->child_count < 2) {
        compiler_error(c, "malformed while expression");
        return;
    }

    int line = (int)node->line;

    /* Push initial accumulator (nothing — result if loop never executes). */
    emit_byte(c, OP_NOTHING, line);

    /* LOOP_START: remember this position for the backward jump. */
    size_t loop_start = c->chunk->count;

    /* Set up loop context for break/continue.
     * scope_depth is recorded BEFORE begin_scope so that break/continue
     * can clean up all locals declared inside the loop body. */
    LoopContext loop_ctx = {
        .loop_start = loop_start,
        .break_jumps = NULL,
        .break_count = 0,
        .break_capacity = 0,
        .scope_depth = c->scope_depth,
        .enclosing = c->current_loop,
    };
    c->current_loop = &loop_ctx;

    /* Compile condition. */
    compile_node(c, node->children[0]);

    /* If condition is false, jump to LOOP_EXIT. */
    size_t exit_jump = emit_jump(c, OP_JUMP_IF_FALSE, line);

    /* Condition was truthy: pop condition, pop old accumulator. */
    emit_byte(c, OP_POP, line);
    emit_byte(c, OP_POP, line);

    /* Compile body with block scoping (function context only). */
    if (c->context == COMPILE_FUNCTION)
        begin_scope(c);
    compile_node(c, node->children[1]);
    if (c->context == COMPILE_FUNCTION)
        end_scope(c, line);

    /* Jump back to LOOP_START. */
    emit_loop(c, loop_start, line);

    /* LOOP_EXIT: patch the forward jump to here. */
    patch_jump(c, exit_jump);

    /* Pop the falsy condition. Accumulator is now TOS. */
    emit_byte(c, OP_POP, line);

    /* BREAK_TARGET: patch all break jumps to here.
     * At this point the stack has the accumulator at TOS, which is
     * either the last body value (normal exit) or the break value. */
    for (size_t i = 0; i < loop_ctx.break_count; i++) {
        patch_jump(c, loop_ctx.break_jumps[i]);
    }
    free(loop_ctx.break_jumps);

    /* Restore enclosing loop context. */
    c->current_loop = loop_ctx.enclosing;
}

/*
 * Compile a break expression.
 *
 * break [expr]: compile the value (or OP_NOTHING for bare break), then
 * emit a forward OP_JUMP whose offset will be patched to BREAK_TARGET
 * after the loop body. The break value becomes the loop's result.
 */
static void compile_break(Compiler *c, const AstNode *node) {
    if (!c->current_loop) {
        compiler_error(c, "'break' outside of loop");
        return;
    }

    int line = (int)node->line;

    /* Compile the break value, or push nothing for bare break.
     * The value is compiled BEFORE cleanup so it can reference
     * block-scoped locals that are about to be popped. */
    if (node->left) {
        compile_node(c, node->left);
    } else {
        emit_byte(c, OP_NOTHING, line);
    }

    /* Clean up block-scoped locals (those deeper than the loop's entry scope).
     * Uses the same save-and-pop trick as end_scope: save the break value
     * into the first departing local's slot, pop N times, result is at TOS. */
    if (c->context == COMPILE_FUNCTION) {
        int n = 0;
        while (n < c->local_count &&
               c->locals[c->local_count - 1 - n].depth > c->current_loop->scope_depth) {
            n++;
        }
        if (n > 0) {
            int first_slot = c->local_count - n;
            emit_bytes(c, OP_SET_LOCAL, (uint8_t)first_slot, line);
            for (int i = 0; i < n; i++) {
                emit_byte(c, OP_POP, line);
            }
        }
    }

    /* Emit a forward jump to be patched to BREAK_TARGET. */
    size_t jump_offset = emit_jump(c, OP_JUMP, line);

    /* Record this jump in the current loop's break_jumps array. */
    LoopContext *loop = c->current_loop;
    if (loop->break_count >= loop->break_capacity) {
        size_t new_cap = loop->break_capacity == 0 ? 4 : loop->break_capacity * 2;
        size_t *new_arr = realloc(loop->break_jumps, new_cap * sizeof(size_t));
        if (!new_arr) {
            compiler_error(c, "memory allocation failed");
            return;
        }
        loop->break_jumps = new_arr;
        loop->break_capacity = new_cap;
    }
    loop->break_jumps[loop->break_count++] = jump_offset;
}

/*
 * Compile a continue expression.
 *
 * continue: push nothing as the new accumulator (abandoning the current
 * iteration's value), then jump backward to LOOP_START.
 */
static void compile_continue(Compiler *c, const AstNode *node) {
    if (!c->current_loop) {
        compiler_error(c, "'continue' outside of loop");
        return;
    }

    int line = (int)node->line;

    /* Clean up block-scoped locals before jumping back to LOOP_START.
     * No value to preserve, so just emit OP_POP for each departing local. */
    if (c->context == COMPILE_FUNCTION) {
        int n = 0;
        while (n < c->local_count &&
               c->locals[c->local_count - 1 - n].depth > c->current_loop->scope_depth) {
            n++;
        }
        for (int i = 0; i < n; i++) {
            emit_byte(c, OP_POP, line);
        }
    }

    /* Push nothing as the new accumulator for this iteration. */
    emit_byte(c, OP_NOTHING, line);

    /* Jump backward to LOOP_START. */
    emit_loop(c, c->current_loop->loop_start, line);
}

/*
 * Compile a function definition: fn name(params) is body end
 *
 * Creates a new Compiler with a fresh Chunk for the function body,
 * compiles the body into it, emits OP_RETURN. Wraps the resulting
 * Chunk in an ObjFunction constant in the enclosing Chunk. Emits
 * OP_CONSTANT + OP_DEFINE_GLOBAL to bind the function to a global.
 *
 * The function value becomes the expression result (stays on stack
 * after OP_DEFINE_GLOBAL, which peeks rather than pops).
 */
static void compile_function(Compiler *c, const AstNode *node) {
    int line = (int)node->line;

    /* Create a new Chunk for the function body. */
    Chunk *body_chunk = malloc(sizeof(Chunk));
    if (!body_chunk) {
        compiler_error(c, "memory allocation failed");
        return;
    }
    chunk_init(body_chunk);

    /* Set up a temporary Compiler for the function body.
     * Function context enables local variable resolution. */
    CompileError body_err;
    Compiler body_compiler = {
        .chunk = body_chunk,
        .err = &body_err,
        .had_error = false,
        .current_loop = NULL,
        .context = COMPILE_FUNCTION,
        .local_count = 0,
        .scope_depth = 1, /* Function body starts at scope depth 1. */
    };

    /* Reserve slot 0 for the callee (the function value itself).
     * This matches the call frame layout where slots[0] = callee. */
    body_compiler.locals[body_compiler.local_count++] =
        (Local){.name = "", .length = 0, .depth = 1};

    /* Add each parameter as a local variable (slots 1..arity).
     * Parameters live at the function body scope (depth 1). */
    for (size_t i = 0; i < node->param_count; i++) {
        if (body_compiler.local_count >= LOCALS_MAX) {
            compiler_error(c, "too many local variables");
            chunk_free(body_chunk);
            free(body_chunk);
            return;
        }
        body_compiler.locals[body_compiler.local_count++] =
            (Local){.name = node->params[i], .length = (int)strlen(node->params[i]), .depth = 1};
    }

    /* Compile the body (node->left holds the body expression). */
    compile_node(&body_compiler, node->left);

    if (body_compiler.had_error) {
        compiler_error(c, "%s", body_err.message);
        chunk_free(body_chunk);
        free(body_chunk);
        return;
    }

    /* Emit OP_RETURN at the end of the function body. */
    emit_byte(&body_compiler, OP_RETURN, line);

    /* Build the ObjFunction.
     * For anonymous functions (node->value == NULL), fn->name is NULL. */
    ObjFunction *fn = calloc(1, sizeof(ObjFunction));
    if (!fn) {
        compiler_error(c, "memory allocation failed");
        chunk_free(body_chunk);
        free(body_chunk);
        return;
    }
    if (node->value) {
        fn->name = compiler_strdup(c, node->value);
        if (!fn->name) {
            chunk_free(body_chunk);
            free(body_chunk);
            free(fn);
            return;
        }
    } else {
        fn->name = NULL;
    }
    fn->arity = (int)node->param_count;
    fn->chunk = body_chunk;
    fn->native = NULL;

    /* Deep-copy parameter names into the ObjFunction. */
    if (node->param_count > 0) {
        fn->params = (char **)calloc(node->param_count, sizeof(char *));
        if (!fn->params) {
            compiler_error(c, "memory allocation failed");
            free(fn->name);
            chunk_free(body_chunk);
            free(body_chunk);
            free(fn);
            return;
        }
        for (size_t i = 0; i < node->param_count; i++) {
            fn->params[i] = compiler_strdup(c, node->params[i]);
            if (!fn->params[i]) {
                /* compiler_strdup already set the error. Clean up. */
                for (size_t j = 0; j < i; j++)
                    free(fn->params[j]);
                free((void *)fn->params);
                free(fn->name);
                chunk_free(body_chunk);
                free(body_chunk);
                free(fn);
                return;
            }
        }
    }

    /* Add the function as a constant in the enclosing chunk and emit
     * OP_CLOSURE to wrap it in a closure at runtime. The constant pool
     * stores the raw ObjFunction; the VM's OP_CLOSURE handler creates
     * an ObjClosure from it. */
    Value fn_val = make_function(fn);
    int fn_idx = chunk_find_or_add_constant(c->chunk, fn_val);
    if (fn_idx < 0) {
        compiler_error(c, "too many constants in one chunk");
        return;
    }
    emit_bytes(c, OP_CLOSURE, (uint8_t)fn_idx, line);
    /* Emit 0 upvalue descriptor pairs (no captures yet). When upvalue
     * capture is implemented, each captured variable will add a
     * (is_local, index) pair here. */

    /* For named functions, bind the name. In function context, register
     * as a local variable (lexical scoping). In script context, define
     * as a global. */
    if (node->value) {
        if (c->context == COMPILE_FUNCTION) {
            /* Same pattern as compile_decl in COMPILE_FUNCTION:
             * the OP_CLOSURE already placed the closure value at
             * the local_count position on the stack. Register the
             * local, then push a clone as the expression result. */
            int slot = c->local_count;
            if (c->local_count >= LOCALS_MAX) {
                compiler_error(c, "too many local variables");
                return;
            }
            c->locals[c->local_count++] = (Local){
                .name = node->value,
                .length = (int)strlen(node->value),
                .depth = c->scope_depth,
            };
            emit_bytes(c, OP_GET_LOCAL, (uint8_t)slot, line);
        } else {
            /* Script context: global variable binding. */
            char *name = compiler_strdup(c, node->value);
            if (!name)
                return;
            int name_idx = chunk_find_or_add_constant(c->chunk, make_string(name));
            if (name_idx < 0) {
                compiler_error(c, "too many constants");
                return;
            }
            emit_bytes(c, OP_DEFINE_GLOBAL, (uint8_t)name_idx, line);
        }
    }
}

/* Compile any AST node by dispatching on its type. */
static void compile_node(Compiler *c, const AstNode *node) {
    if (c->had_error)
        return;
    if (!node) {
        compiler_error(c, "null AST node");
        return;
    }

    switch (node->type) {
    case AST_NUMBER:
        compile_number(c, node);
        break;
    case AST_STRING:
        compile_string(c, node);
        break;
    case AST_BOOL:
        compile_bool(c, node);
        break;
    case AST_NOTHING:
        compile_nothing(c, node);
        break;
    case AST_IDENT:
        compile_ident(c, node);
        break;
    case AST_BINOP:
        compile_binop(c, node);
        break;
    case AST_UNARY:
        compile_unary(c, node);
        break;
    case AST_DECL:
        compile_decl(c, node);
        break;
    case AST_ASSIGN:
        compile_assign(c, node);
        break;
    case AST_BLOCK:
        compile_block(c, node);
        break;
    case AST_CALL:
        compile_call(c, node);
        break;
    case AST_IF:
        compile_if(c, node);
        break;
    case AST_WHILE:
        compile_while(c, node);
        break;
    case AST_BREAK:
        compile_break(c, node);
        break;
    case AST_CONTINUE:
        compile_continue(c, node);
        break;
    case AST_FUNCTION:
        compile_function(c, node);
        break;
    default:
        compiler_error(c, "unknown AST node type %d", node->type);
        break;
    }
}

Chunk *compile(const AstNode *node, CompileError *err) {
    Chunk *chunk = malloc(sizeof(Chunk));
    if (!chunk) {
        if (err)
            snprintf(err->message, sizeof(err->message), "out of memory");
        return NULL;
    }
    chunk_init(chunk);

    Compiler c = {
        .chunk = chunk,
        .err = err,
        .had_error = false,
        .current_loop = NULL,
        .context = COMPILE_SCRIPT,
        .local_count = 0,
        .scope_depth = 0, /* Script context starts at scope depth 0. */
    };

    compile_node(&c, node);

    if (c.had_error) {
        chunk_free(chunk);
        free(chunk);
        return NULL;
    }

    /* Emit OP_RETURN to end execution. Use the last node's line. */
    emit_byte(&c, OP_RETURN, (int)node->line);

    return chunk;
}
