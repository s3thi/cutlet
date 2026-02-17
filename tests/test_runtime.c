/*
 * test_runtime.c - Tests for the runtime evaluation lock
 *
 * Proves that concurrent calls to repl_eval_line() are serialized
 * by the global eval lock.
 *
 * Strategy:
 * - Use runtime_test_on_lock_enter/exit hooks (CUTLET_TESTING) to
 *   maintain an atomic counter of threads inside the critical section.
 * - Spawn multiple threads that each call repl_eval_line() many times.
 * - Assert the counter never exceeds 1 (no overlap).
 * - Secondary tests do the same for eval with AST and mixed modes.
 */

#include "../src/repl.h"
#include "../src/runtime.h"
#include "../src/value.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* No-op EvalContext for tests that don't need say() output. */
static EvalContext noop_ctx = {.write_fn = NULL, .userdata = NULL};

/* ============================================================
 * Simple test harness (same as other test files)
 * ============================================================ */

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)

#define RUN_TEST(name)                                                                             \
    do {                                                                                           \
        tests_run++;                                                                               \
        printf("  %-55s ", #name);                                                                 \
        fflush(stdout);                                                                            \
        name();                                                                                    \
    } while (0)

#define ASSERT(cond, msg)                                                                          \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL\n    %s:%d: %s\n", __FILE__, __LINE__, msg);                              \
            tests_failed++;                                                                        \
            return;                                                                                \
        }                                                                                          \
    } while (0)

#define PASS()                                                                                     \
    do {                                                                                           \
        printf("PASS\n");                                                                          \
        tests_passed++;                                                                            \
    } while (0)

/* ============================================================
 * Shared state for overlap detection
 * ============================================================ */

/* Counts how many threads are currently inside the critical section. */
static atomic_int in_critical = 0;

/* Set to 1 if overlap is ever detected. */
static atomic_int overlap_detected = 0;

/* Counts total lock acquisitions (must be > 0 to prove locking happened). */
static atomic_int lock_enter_count = 0;

static void hook_lock_enter(void) {
    atomic_fetch_add(&lock_enter_count, 1);
    int prev = atomic_fetch_add(&in_critical, 1);
    if (prev != 0) {
        atomic_store(&overlap_detected, 1);
    }
}

static void hook_lock_exit(void) { atomic_fetch_sub(&in_critical, 1); }

/* ============================================================
 * Test: runtime_init and runtime_destroy basics
 * ============================================================ */

TEST(test_runtime_init_destroy) {
    ASSERT(runtime_init(), "runtime_init should succeed");
    runtime_destroy();
    /* Double destroy should be safe */
    runtime_destroy();
    PASS();
}

TEST(test_runtime_double_init) {
    ASSERT(runtime_init(), "first init should succeed");
    ASSERT(runtime_init(), "second init should succeed (idempotent)");
    runtime_destroy();
    PASS();
}

#define NUM_THREADS 4
#define ITERS_PER_THREAD 200

/* ============================================================
 * Test: concurrent runtime_init from multiple threads
 * ============================================================ */

static atomic_int init_failures = 0;

static void *thread_runtime_init(void *arg) {
    (void)arg;
    for (int i = 0; i < 100; i++) {
        if (!runtime_init()) {
            atomic_fetch_add(&init_failures, 1);
        }
    }
    return NULL;
}

TEST(test_concurrent_runtime_init) {
    atomic_store(&init_failures, 0);

    pthread_t threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        int rc = pthread_create(&threads[i], NULL, thread_runtime_init, NULL);
        ASSERT(rc == 0, "pthread_create");
    }
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    ASSERT(atomic_load(&init_failures) == 0, "All concurrent runtime_init calls should succeed");

    /* Verify the lock actually works after concurrent init */
    runtime_eval_lock();
    runtime_eval_unlock();

    PASS();
}

/* ============================================================
 * Test: serialized eval via repl_format_line
 * ============================================================ */

static void *thread_eval_line(void *arg) {
    (void)arg;
    for (int i = 0; i < ITERS_PER_THREAD; i++) {
        ReplResult r = repl_eval_line("42", false, false, false, &noop_ctx);
        repl_result_free(&r);
    }
    return NULL;
}

TEST(test_serialized_eval_line) {
    /* Reset overlap detection state */
    atomic_store(&in_critical, 0);
    atomic_store(&overlap_detected, 0);
    atomic_store(&lock_enter_count, 0);

    ASSERT(runtime_init(), "runtime_init");

    /* Install hooks */
    runtime_test_on_lock_enter = hook_lock_enter;
    runtime_test_on_lock_exit = hook_lock_exit;

    pthread_t threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        int rc = pthread_create(&threads[i], NULL, thread_eval_line, NULL);
        ASSERT(rc == 0, "pthread_create");
    }
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    /* Clear hooks before assertions */
    runtime_test_on_lock_enter = NULL;
    runtime_test_on_lock_exit = NULL;

    ASSERT(atomic_load(&lock_enter_count) == NUM_THREADS * ITERS_PER_THREAD,
           "Lock must be acquired for every repl_eval_line call");
    ASSERT(!atomic_load(&overlap_detected),
           "No two threads should be in the critical section simultaneously (eval_line)");

    runtime_destroy();
    PASS();
}

/* ============================================================
 * Test: serialized eval via repl_format_line_ast
 * ============================================================ */

static void *thread_eval_line_with_ast(void *arg) {
    (void)arg;
    for (int i = 0; i < ITERS_PER_THREAD; i++) {
        ReplResult r = repl_eval_line("42", false, true, false, &noop_ctx);
        repl_result_free(&r);
    }
    return NULL;
}

TEST(test_serialized_eval_line_with_ast) {
    atomic_store(&in_critical, 0);
    atomic_store(&overlap_detected, 0);
    atomic_store(&lock_enter_count, 0);

    ASSERT(runtime_init(), "runtime_init");

    runtime_test_on_lock_enter = hook_lock_enter;
    runtime_test_on_lock_exit = hook_lock_exit;

    pthread_t threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        int rc = pthread_create(&threads[i], NULL, thread_eval_line_with_ast, NULL);
        ASSERT(rc == 0, "pthread_create");
    }
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    runtime_test_on_lock_enter = NULL;
    runtime_test_on_lock_exit = NULL;

    ASSERT(atomic_load(&lock_enter_count) == NUM_THREADS * ITERS_PER_THREAD,
           "Lock must be acquired for every repl_eval_line (ast) call");
    ASSERT(!atomic_load(&overlap_detected),
           "No two threads should be in the critical section simultaneously (eval_line_ast)");

    runtime_destroy();
    PASS();
}

/* ============================================================
 * Test: mixed format_line and format_line_ast concurrently
 * ============================================================ */

static void *thread_mixed_even(void *arg) {
    (void)arg;
    for (int i = 0; i < ITERS_PER_THREAD; i++) {
        ReplResult r = repl_eval_line("hello", false, false, false, &noop_ctx);
        repl_result_free(&r);
    }
    return NULL;
}

static void *thread_mixed_odd(void *arg) {
    (void)arg;
    for (int i = 0; i < ITERS_PER_THREAD; i++) {
        ReplResult r = repl_eval_line("hello", false, true, false, &noop_ctx);
        repl_result_free(&r);
    }
    return NULL;
}

TEST(test_serialized_eval_mixed) {
    atomic_store(&in_critical, 0);
    atomic_store(&overlap_detected, 0);
    atomic_store(&lock_enter_count, 0);

    ASSERT(runtime_init(), "runtime_init");

    runtime_test_on_lock_enter = hook_lock_enter;
    runtime_test_on_lock_exit = hook_lock_exit;

    pthread_t threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        void *(*fn)(void *) = (i % 2 == 0) ? thread_mixed_even : thread_mixed_odd;
        int rc = pthread_create(&threads[i], NULL, fn, NULL);
        ASSERT(rc == 0, "pthread_create");
    }
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    runtime_test_on_lock_enter = NULL;
    runtime_test_on_lock_exit = NULL;

    ASSERT(atomic_load(&lock_enter_count) == NUM_THREADS * ITERS_PER_THREAD,
           "Lock must be acquired for every eval call (mixed)");
    ASSERT(!atomic_load(&overlap_detected),
           "No two threads should be in the critical section simultaneously (mixed)");

    runtime_destroy();
    PASS();
}

/* ============================================================
 * Test: results remain correct under concurrency
 * ============================================================ */

static atomic_int result_errors = 0;

static void *thread_check_result(void *arg) {
    (void)arg;
    for (int i = 0; i < ITERS_PER_THREAD; i++) {
        ReplResult r = repl_eval_line("42", false, false, false, &noop_ctx);
        if (!r.ok || !r.value || strcmp(r.value, "42") != 0) {
            atomic_fetch_add(&result_errors, 1);
        }
        repl_result_free(&r);
    }
    return NULL;
}

TEST(test_results_correct_under_concurrency) {
    atomic_store(&result_errors, 0);

    ASSERT(runtime_init(), "runtime_init");

    pthread_t threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        int rc = pthread_create(&threads[i], NULL, thread_check_result, NULL);
        ASSERT(rc == 0, "pthread_create");
    }
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    ASSERT(atomic_load(&result_errors) == 0,
           "All results should be correct under concurrent access");

    runtime_destroy();
    PASS();
}

/* ============================================================
 * main
 * ============================================================ */

int main(void) {
    printf("runtime tests:\n");

    RUN_TEST(test_runtime_init_destroy);
    RUN_TEST(test_runtime_double_init);
    RUN_TEST(test_concurrent_runtime_init);
    RUN_TEST(test_serialized_eval_line);
    RUN_TEST(test_serialized_eval_line_with_ast);
    RUN_TEST(test_serialized_eval_mixed);
    RUN_TEST(test_results_correct_under_concurrency);

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf("%d FAILED\n", tests_failed);
        return 1;
    }
    return 0;
}
