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

#include <stdbool.h>

/*
 * Initialize the global runtime.
 * Must be called once before any evaluation.
 * Returns true on success, false on failure.
 */
bool runtime_init(void);

/*
 * Destroy the global runtime.
 * Must be called once after all evaluation is done.
 * Safe to call even if runtime_init() was never called.
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
 * Test-only hooks.  When CUTLET_TESTING is defined, the runtime calls
 * these function pointers (if non-NULL) on lock enter and exit.
 * Tests can set them to track concurrent access.
 */
#ifdef CUTLET_TESTING
extern void (*runtime_test_on_lock_enter)(void);
extern void (*runtime_test_on_lock_exit)(void);
#endif

#endif /* CUTLET_RUNTIME_H */
