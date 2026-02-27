/*
 * runtime.c - Cutlet runtime environment
 *
 * Implements the global evaluation lock that serializes all evaluation.
 * See runtime.h for the lock hierarchy and design rationale.
 *
 * Variable storage uses an open-addressing hash table with FNV-1a hash
 * and linear probing. Auto-resizes at 75% load factor. O(1) average
 * lookup instead of O(n) linked list scan.
 *
 * Thread safety: init uses pthread_once to guarantee the rwlock is
 * initialized exactly once, even under concurrent calls. destroy
 * clears the variable environment; the lock lives for the process lifetime.
 */

#include "runtime.h"
#include "gc.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Global evaluation lock. Write-locked for every eval; read-locks
 * reserved for future read-only introspection. */
static pthread_rwlock_t eval_lock;

/* pthread_once control for one-time initialization. */
static pthread_once_t runtime_once = PTHREAD_ONCE_INIT;

/* Set to true by runtime_init_impl if pthread_rwlock_init succeeds. */
static bool init_ok = false;

/* ============================================================
 * Hash table for global variables
 * ============================================================ */

/* Each entry in the hash table. An entry is "occupied" when name != NULL. */
typedef struct {
    char *name;  /* Owned. NULL means empty slot. */
    Value value; /* Owned. */
} VarEntry;

/* Initial table capacity (must be a power of 2). */
#define VAR_TABLE_INIT_CAP 16

/* Load factor threshold for resizing (75%). */
#define VAR_TABLE_MAX_LOAD 0.75

static VarEntry *var_table = NULL;
static size_t var_table_cap = 0;   /* Current capacity (always power of 2). */
static size_t var_table_count = 0; /* Number of occupied entries. */

/*
 * FNV-1a hash for a null-terminated string.
 * Fast, simple, good distribution for short identifier strings.
 */
static uint32_t fnv1a(const char *key) {
    uint32_t hash = 2166136261u;
    for (const char *p = key; *p; p++) {
        hash ^= (uint8_t)*p;
        hash *= 16777619u;
    }
    return hash;
}

/*
 * Find the slot for a given key, or the first empty slot.
 * Uses linear probing. Table must not be full (ensured by load factor).
 */
static size_t var_table_find_slot(VarEntry *table, size_t cap, const char *name) {
    uint32_t hash = fnv1a(name);
    size_t idx = hash & (cap - 1); /* cap is always power of 2 */
    for (;;) {
        if (table[idx].name == NULL)
            return idx; /* Empty slot */
        if (strcmp(table[idx].name, name) == 0)
            return idx; /* Found existing entry */
        idx = (idx + 1) & (cap - 1);
    }
}

/*
 * Resize the hash table to new_cap. Rehashes all existing entries.
 * Returns true on success, false on OOM.
 */
static bool var_table_resize(size_t new_cap) {
    VarEntry *new_table = calloc(new_cap, sizeof(VarEntry));
    if (!new_table)
        return false;

    /* Rehash all existing entries into the new table. */
    for (size_t i = 0; i < var_table_cap; i++) {
        if (var_table[i].name != NULL) {
            size_t slot = var_table_find_slot(new_table, new_cap, var_table[i].name);
            new_table[slot] = var_table[i]; /* Move ownership */
        }
    }

    free(var_table);
    var_table = new_table;
    var_table_cap = new_cap;
    /* count stays the same */
    return true;
}

/* Clear all entries in the variable table.
 * value_free() is a no-op for GC-managed types (just nulls pointers).
 * The actual GC-managed objects are freed later by gc_free_all(). */
static void var_table_clear(void) {
    for (size_t i = 0; i < var_table_cap; i++) {
        if (var_table[i].name != NULL) {
            free(var_table[i].name);
            value_free(&var_table[i].value);
            var_table[i].name = NULL;
        }
    }
    free(var_table);
    var_table = NULL;
    var_table_cap = 0;
    var_table_count = 0;
}

/* Ensure the table is initialized and has room for at least one more entry. */
static bool var_table_ensure_capacity(void) {
    if (var_table == NULL) {
        var_table = calloc(VAR_TABLE_INIT_CAP, sizeof(VarEntry));
        if (!var_table)
            return false;
        var_table_cap = VAR_TABLE_INIT_CAP;
        return true;
    }
    /* Resize if at or above 75% load. */
    if ((double)(var_table_count + 1) > (double)var_table_cap * VAR_TABLE_MAX_LOAD) {
        return var_table_resize(var_table_cap * 2);
    }
    return true;
}

/*
 * Find an existing variable by name.
 * Returns a pointer to the entry if found, NULL otherwise.
 */
static VarEntry *var_table_find(const char *name) {
    if (var_table == NULL || var_table_count == 0)
        return NULL;
    size_t slot = var_table_find_slot(var_table, var_table_cap, name);
    if (var_table[slot].name == NULL)
        return NULL;
    return &var_table[slot];
}

/* Test-only hook pointers (only compiled in when CUTLET_TESTING is defined). */
#ifdef CUTLET_TESTING
void (*runtime_test_on_lock_enter)(void) = NULL;
void (*runtime_test_on_lock_exit)(void) = NULL;
#endif

/* Called exactly once by pthread_once. */
static void runtime_init_impl(void) {
    if (pthread_rwlock_init(&eval_lock, NULL) == 0) {
        init_ok = true;
        /* Initialize GC state after the lock is ready.
         * GC state is protected by eval_lock (no separate sync needed). */
        gc_init();
    }
}

bool runtime_init(void) {
    pthread_once(&runtime_once, runtime_init_impl);
    return init_ok;
}

/* Clears the variable environment and sweeps remaining GC objects.
 * var_table_clear() frees Value wrappers in globals (nulling pointers).
 * gc_free_all() then frees all GC-managed objects and their contents.
 * The eval lock lives for the process lifetime. */
void runtime_destroy(void) {
    var_table_clear();
    gc_free_all();
}

/*
 * Mark all Values in the global variable table as GC roots.
 *
 * Iterates every occupied entry (name != NULL) in var_table and
 * calls gc_mark_value() on its stored Value. This ensures that
 * heap objects referenced by globals are not collected during GC.
 *
 * Called by gc_mark_roots() during the mark phase.
 */
void runtime_mark_globals(void) {
    for (size_t i = 0; i < var_table_cap; i++) {
        if (var_table[i].name != NULL)
            gc_mark_value(&var_table[i].value);
    }
}

void runtime_eval_lock(void) {
    /* Lazy init: safe to call even if runtime_init() was never called
     * explicitly. pthread_once guarantees this is a no-op after the
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

/* Look up a global variable by name.
 * Returns a shallow copy of the stored Value via value_clone().
 * For GC-managed types this is just a pointer copy — the GC keeps
 * the object alive. Only VAL_ERROR requires a deep copy (strdup). */
RuntimeVarStatus runtime_var_get(const char *name, Value *out) {
    runtime_init();
    VarEntry *entry = var_table_find(name);
    if (!entry)
        return RUNTIME_VAR_NOT_FOUND;
    if (!value_clone(out, &entry->value))
        return RUNTIME_VAR_OOM;
    return RUNTIME_VAR_OK;
}

/* Define (or redefine) a global variable.
 * Stores a shallow copy of the Value via value_clone(). For GC-managed
 * types this is just a pointer copy — the GC keeps the object alive.
 * value_free() on the old value is a no-op for GC types (just nulls
 * pointers); only VAL_ERROR needs actual cleanup (freeing the message). */
RuntimeVarStatus runtime_var_define(const char *name, const Value *value) {
    runtime_init();

    /* Check if already exists (overwrite). */
    VarEntry *entry = var_table_find(name);
    if (entry) {
        /* Free old value (no-op for GC types), then shallow-copy new value. */
        value_free(&entry->value);
        if (!value_clone(&entry->value, value))
            return RUNTIME_VAR_OOM;
        return RUNTIME_VAR_OK;
    }

    /* New entry: ensure capacity. */
    if (!var_table_ensure_capacity())
        return RUNTIME_VAR_OOM;

    char *name_dup = strdup(name);
    if (!name_dup)
        return RUNTIME_VAR_OOM;

    size_t slot = var_table_find_slot(var_table, var_table_cap, name);
    var_table[slot].name = name_dup;
    if (!value_clone(&var_table[slot].value, value)) {
        free(name_dup);
        var_table[slot].name = NULL;
        return RUNTIME_VAR_OOM;
    }
    var_table_count++;
    return RUNTIME_VAR_OK;
}

/* Assign a new value to an existing global variable.
 * Same simplification as runtime_var_define(): value_free() is a no-op
 * for GC types, and value_clone() is a shallow pointer copy. */
RuntimeVarStatus runtime_var_assign(const char *name, const Value *value) {
    runtime_init();
    VarEntry *entry = var_table_find(name);
    if (!entry)
        return RUNTIME_VAR_NOT_FOUND;

    /* Free old value (no-op for GC types), then shallow-copy new value. */
    value_free(&entry->value);
    if (!value_clone(&entry->value, value))
        return RUNTIME_VAR_OOM;
    return RUNTIME_VAR_OK;
}
