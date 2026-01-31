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
 * initialized exactly once, even under concurrent calls.  destroy is
 * a no-op — the lock lives for the process lifetime.
 */

#include "runtime.h"
#include <pthread.h>
#include <stdbool.h>

/* Global evaluation lock.  Write-locked for every eval; read-locks
 * reserved for future read-only introspection. */
static pthread_rwlock_t eval_lock;

/* pthread_once control for one-time initialization. */
static pthread_once_t runtime_once = PTHREAD_ONCE_INIT;

/* Set to true by runtime_init_impl if pthread_rwlock_init succeeds. */
static bool init_ok = false;

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

/* No-op: the eval lock lives for the process lifetime.  Calling this
 * is harmless and keeps existing call sites working. */
void runtime_destroy(void) {}

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
