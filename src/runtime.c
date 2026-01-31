/*
 * runtime.c - Cutlet runtime environment
 *
 * Implements the global evaluation lock that serializes all evaluation.
 * See runtime.h for the lock hierarchy and design rationale.
 *
 * Implementation uses pthread_rwlock_t so we can later allow read-only
 * access without changing the lock primitive.  For now every acquisition
 * is a write lock.
 */

#include "runtime.h"
#include <pthread.h>
#include <stdbool.h>

/* Global evaluation lock.  Write-locked for every eval; read-locks
 * reserved for future read-only introspection. */
static pthread_rwlock_t eval_lock;

/* Track whether the runtime has been initialized so destroy is safe
 * even without a prior init. */
static bool initialized = false;

/* Test-only hook pointers (only compiled in when CUTLET_TESTING is defined). */
#ifdef CUTLET_TESTING
void (*runtime_test_on_lock_enter)(void) = NULL;
void (*runtime_test_on_lock_exit)(void) = NULL;
#endif

bool runtime_init(void) {
    if (initialized)
        return true;
    if (pthread_rwlock_init(&eval_lock, NULL) != 0)
        return false;
    initialized = true;
    return true;
}

void runtime_destroy(void) {
    if (!initialized)
        return;
    pthread_rwlock_destroy(&eval_lock);
    initialized = false;
}

void runtime_eval_lock(void) {
    pthread_rwlock_wrlock(&eval_lock);
#ifdef CUTLET_TESTING
    /* Hook fires after lock is held so tests can detect overlap. */
    if (runtime_test_on_lock_enter)
        runtime_test_on_lock_enter();
#endif
}

void runtime_eval_unlock(void) {
#ifdef CUTLET_TESTING
    /* Hook fires before lock is released so tests can detect overlap. */
    if (runtime_test_on_lock_exit)
        runtime_test_on_lock_exit();
#endif
    pthread_rwlock_unlock(&eval_lock);
}
