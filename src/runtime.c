/*
 * runtime.c - Cutlet runtime environment
 *
 * Implements the global evaluation lock that serializes all evaluation.
 * See runtime.h for the lock hierarchy and design rationale.
 *
 * Implementation uses pthread_rwlock_t so we can later allow read-only
 * access without changing the lock primitive.  For now every acquisition
 * is a write lock.
 *
 * Thread safety: init uses pthread_once to guarantee the rwlock is
 * initialized exactly once, even under concurrent calls.  destroy
 * clears the variable environment; the lock lives for the process lifetime.
 */

#include "runtime.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* Global evaluation lock.  Write-locked for every eval; read-locks
 * reserved for future read-only introspection. */
static pthread_rwlock_t eval_lock;

/* pthread_once control for one-time initialization. */
static pthread_once_t runtime_once = PTHREAD_ONCE_INIT;

/* Set to true by runtime_init_impl if pthread_rwlock_init succeeds. */
static bool init_ok = false;

/* Simple variable environment (linked list).  Access is serialized by the
 * global eval lock; do not call these helpers without holding the lock. */
typedef struct RuntimeVar {
    char *name;
    Value value;
    struct RuntimeVar *next;
} RuntimeVar;

static RuntimeVar *runtime_vars = NULL;

static void runtime_vars_clear(void);

/* Test-only hook pointers (only compiled in when CUTLET_TESTING is defined). */
#ifdef CUTLET_TESTING
void (*runtime_test_on_lock_enter)(void) = NULL;
void (*runtime_test_on_lock_exit)(void) = NULL;
#endif

/* Called exactly once by pthread_once. */
static void runtime_init_impl(void) {
    if (pthread_rwlock_init(&eval_lock, NULL) == 0)
        init_ok = true;
}

bool runtime_init(void) {
    pthread_once(&runtime_once, runtime_init_impl);
    return init_ok;
}

/* Clears the variable environment. The eval lock lives for the process
 * lifetime, so this remains safe to call any time. */
void runtime_destroy(void) { runtime_vars_clear(); }

static void runtime_vars_clear(void) {
    RuntimeVar *cur = runtime_vars;
    while (cur) {
        RuntimeVar *next = cur->next;
        free(cur->name);
        value_free(&cur->value);
        free(cur);
        cur = next;
    }
    runtime_vars = NULL;
}

void runtime_eval_lock(void) {
    /* Lazy init: safe to call even if runtime_init() was never called
     * explicitly.  pthread_once guarantees this is a no-op after the
     * first successful init. */
    runtime_init();
    pthread_rwlock_wrlock(&eval_lock);
#ifdef CUTLET_TESTING
    /* Hook fires after lock is held so tests can detect overlap. */
    if (runtime_test_on_lock_enter)
        runtime_test_on_lock_enter();
#endif
}

void runtime_eval_unlock(void) {
    /* No lazy init needed here — unlock is only valid after a
     * successful lock, which already called runtime_init(). */
#ifdef CUTLET_TESTING
    /* Hook fires before lock is released so tests can detect overlap. */
    if (runtime_test_on_lock_exit)
        runtime_test_on_lock_exit();
#endif
    pthread_rwlock_unlock(&eval_lock);
}

static RuntimeVar *runtime_var_find(const char *name) {
    for (RuntimeVar *cur = runtime_vars; cur; cur = cur->next) {
        if (strcmp(cur->name, name) == 0)
            return cur;
    }
    return NULL;
}

static bool value_clone(Value *out, const Value *src) {
    if (!out || !src)
        return false;
    *out = *src;
    if (src->type == VAL_STRING || src->type == VAL_ERROR) {
        const char *s = src->string ? src->string : "";
        out->string = strdup(s);
        if (!out->string)
            return false;
    } else {
        out->string = NULL;
    }
    return true;
}

RuntimeVarStatus runtime_var_get(const char *name, Value *out) {
    runtime_init();
    RuntimeVar *var = runtime_var_find(name);
    if (!var)
        return RUNTIME_VAR_NOT_FOUND;
    if (!value_clone(out, &var->value))
        return RUNTIME_VAR_OOM;
    return RUNTIME_VAR_OK;
}

RuntimeVarStatus runtime_var_define(const char *name, const Value *value) {
    runtime_init();
    RuntimeVar *var = runtime_var_find(name);
    Value cloned;
    if (!value_clone(&cloned, value))
        return RUNTIME_VAR_OOM;

    if (var) {
        value_free(&var->value);
        var->value = cloned;
        return RUNTIME_VAR_OK;
    }

    RuntimeVar *entry = malloc(sizeof(RuntimeVar));
    if (!entry) {
        value_free(&cloned);
        return RUNTIME_VAR_OOM;
    }
    entry->name = strdup(name);
    if (!entry->name) {
        value_free(&cloned);
        free(entry);
        return RUNTIME_VAR_OOM;
    }
    entry->value = cloned;
    entry->next = runtime_vars;
    runtime_vars = entry;
    return RUNTIME_VAR_OK;
}

RuntimeVarStatus runtime_var_assign(const char *name, const Value *value) {
    runtime_init();
    RuntimeVar *var = runtime_var_find(name);
    if (!var)
        return RUNTIME_VAR_NOT_FOUND;

    Value cloned;
    if (!value_clone(&cloned, value))
        return RUNTIME_VAR_OOM;

    value_free(&var->value);
    var->value = cloned;
    return RUNTIME_VAR_OK;
}
