/*
 * runtime.h - Cutlet runtime environment
 *
 * Provides the global runtime state, including the evaluation lock that
 * serializes all evaluation across threads.
 *
 * Lock hierarchy (top to bottom):
 *   1. Global eval lock  (this module)
 *   2. Namespace locks   (future)
 *   3. Object locks      (future)
 *   4. IO locks          (future)
 *
 * Currently only the global eval lock exists.  It is taken as a
 * **write** lock for every evaluation so that concurrent REPL clients
 * never evaluate in parallel.  When we later need read-only access
 * (e.g. inspecting values without mutation) we can take a read lock
 * instead without changing call sites.
 *
 * Thread safety: all functions in this module are thread-safe.
 */

#ifndef CUTLET_RUNTIME_H
#define CUTLET_RUNTIME_H

#include "eval.h"
#include <stdbool.h>

/*
 * Initialize the global runtime.
 * Must be called once before any evaluation.
 * Returns true on success, false on failure.
 */
bool runtime_init(void);

/*
 * Clears the variable environment. The global eval lock lives for the
 * process lifetime, so this is otherwise a no-op.
 * Retained for API compatibility; safe to call at any time.
 */
void runtime_destroy(void);

/*
 * Acquire the global evaluation write lock.
 * Blocks until the lock is available.
 */
void runtime_eval_lock(void);

/*
 * Release the global evaluation write lock.
 */
void runtime_eval_unlock(void);

/*
 * Variable environment status codes.
 */
typedef enum {
    RUNTIME_VAR_OK = 0,
    RUNTIME_VAR_NOT_FOUND,
    RUNTIME_VAR_OOM,
} RuntimeVarStatus;

/*
 * Read a variable value into out (owned fields are allocated).
 * Returns RUNTIME_VAR_NOT_FOUND if the name is not bound.
 *
 * Callers must hold the global eval lock for thread safety.
 */
RuntimeVarStatus runtime_var_get(const char *name, Value *out);

/*
 * Define or overwrite a variable binding.
 * Returns RUNTIME_VAR_OK or RUNTIME_VAR_OOM.
 *
 * Callers must hold the global eval lock for thread safety.
 */
RuntimeVarStatus runtime_var_define(const char *name, const Value *value);

/*
 * Assign to an existing variable binding.
 * Returns RUNTIME_VAR_NOT_FOUND if the name is not bound.
 *
 * Callers must hold the global eval lock for thread safety.
 */
RuntimeVarStatus runtime_var_assign(const char *name, const Value *value);

/*
 * Test-only hooks.  When CUTLET_TESTING is defined, the runtime calls
 * these function pointers (if non-NULL) on lock enter and exit.
 * Tests can set them to track concurrent access.
 */
#ifdef CUTLET_TESTING
extern void (*runtime_test_on_lock_enter)(void);
extern void (*runtime_test_on_lock_exit)(void);
#endif

#endif /* CUTLET_RUNTIME_H */
