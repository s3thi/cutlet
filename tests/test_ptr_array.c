/*
 * test_ptr_array.c - Tests for the PtrArray abstraction
 */

#include "../src/ptr_array.h"
#include <stdio.h>
#include <stdlib.h>

/* ============================================================
 * Simple test harness
 * ============================================================ */

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)

#define RUN_TEST(name)                                                                             \
    do {                                                                                           \
        tests_run++;                                                                               \
        printf("  %-50s ", #name);                                                                 \
        fflush(stdout);                                                                            \
        name();                                                                                    \
    } while (0)

#define ASSERT(cond, msg)                                                                          \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL\n");                                                                      \
            printf("    Assertion failed: %s\n", msg);                                             \
            printf("    At %s:%d\n", __FILE__, __LINE__);                                          \
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
 * PtrArray init tests
 * ============================================================ */

TEST(test_init_with_capacity) {
    PtrArray arr;
    ASSERT(ptr_array_init(&arr, 4), "should init with capacity 4");
    ASSERT(arr.items != NULL, "items should be allocated");
    ASSERT(arr.count == 0, "count should be 0");
    ASSERT(arr.capacity == 4, "capacity should be 4");
    ptr_array_destroy(&arr);
    PASS();
}

TEST(test_init_with_zero_capacity) {
    PtrArray arr;
    ASSERT(ptr_array_init(&arr, 0), "should init with capacity 0");
    ASSERT(arr.items == NULL, "items should be NULL");
    ASSERT(arr.count == 0, "count should be 0");
    ASSERT(arr.capacity == 0, "capacity should be 0");
    ptr_array_destroy(&arr);
    PASS();
}

TEST(test_init_null_array) {
    ASSERT(!ptr_array_init(NULL, 4), "should fail with NULL array");
    PASS();
}

/* ============================================================
 * PtrArray push tests
 * ============================================================ */

TEST(test_push_single) {
    PtrArray arr;
    ptr_array_init(&arr, 4);
    int value = 42;
    ASSERT(ptr_array_push(&arr, &value), "should push element");
    ASSERT(arr.count == 1, "count should be 1");
    ASSERT(arr.items[0] == &value, "element should be stored");
    ptr_array_destroy(&arr);
    PASS();
}

TEST(test_push_multiple) {
    PtrArray arr;
    ptr_array_init(&arr, 4);
    int values[3] = {1, 2, 3};
    ASSERT(ptr_array_push(&arr, &values[0]), "push 1");
    ASSERT(ptr_array_push(&arr, &values[1]), "push 2");
    ASSERT(ptr_array_push(&arr, &values[2]), "push 3");
    ASSERT(arr.count == 3, "count should be 3");
    ASSERT(arr.items[0] == &values[0], "element 0");
    ASSERT(arr.items[1] == &values[1], "element 1");
    ASSERT(arr.items[2] == &values[2], "element 2");
    ptr_array_destroy(&arr);
    PASS();
}

TEST(test_push_auto_grow) {
    PtrArray arr;
    ptr_array_init(&arr, 2);
    int values[5] = {1, 2, 3, 4, 5};
    for (int i = 0; i < 5; i++) {
        ASSERT(ptr_array_push(&arr, &values[i]), "push should succeed");
    }
    ASSERT(arr.count == 5, "count should be 5");
    ASSERT(arr.capacity >= 5, "capacity should have grown");
    for (int i = 0; i < 5; i++) {
        ASSERT(arr.items[i] == &values[i], "element should be correct");
    }
    ptr_array_destroy(&arr);
    PASS();
}

TEST(test_push_from_zero_capacity) {
    PtrArray arr;
    ptr_array_init(&arr, 0);
    int value = 42;
    ASSERT(ptr_array_push(&arr, &value), "push should auto-allocate");
    ASSERT(arr.count == 1, "count should be 1");
    ASSERT(arr.capacity >= 1, "capacity should have been allocated");
    ASSERT(arr.items[0] == &value, "element should be stored");
    ptr_array_destroy(&arr);
    PASS();
}

TEST(test_push_null_array) {
    int value = 42;
    ASSERT(!ptr_array_push(NULL, &value), "should fail with NULL array");
    PASS();
}

/* ============================================================
 * PtrArray destroy tests
 * ============================================================ */

TEST(test_destroy) {
    PtrArray arr;
    ptr_array_init(&arr, 4);
    int value = 42;
    ptr_array_push(&arr, &value);
    ptr_array_destroy(&arr);
    ASSERT(arr.items == NULL, "items should be NULL");
    ASSERT(arr.count == 0, "count should be 0");
    ASSERT(arr.capacity == 0, "capacity should be 0");
    PASS();
}

TEST(test_destroy_null_array) {
    /* Should not crash */
    ptr_array_destroy(NULL);
    PASS();
}

TEST(test_destroy_empty_array) {
    PtrArray arr;
    ptr_array_init(&arr, 0);
    ptr_array_destroy(&arr);
    ASSERT(arr.items == NULL, "items should be NULL");
    ASSERT(arr.count == 0, "count should be 0");
    ASSERT(arr.capacity == 0, "capacity should be 0");
    PASS();
}

/* ============================================================
 * PtrArray release tests
 * ============================================================ */

TEST(test_release) {
    PtrArray arr;
    ptr_array_init(&arr, 4);
    int values[2] = {1, 2};
    ptr_array_push(&arr, &values[0]);
    ptr_array_push(&arr, &values[1]);

    void **raw = ptr_array_release(&arr);
    ASSERT(raw != NULL, "released array should not be NULL");
    ASSERT(raw[0] == &values[0], "element 0 preserved");
    ASSERT(raw[1] == &values[1], "element 1 preserved");

    /* Array should be reset */
    ASSERT(arr.items == NULL, "items should be NULL after release");
    ASSERT(arr.count == 0, "count should be 0 after release");
    ASSERT(arr.capacity == 0, "capacity should be 0 after release");

    /* Clean up */
    ptr_array_free_raw((void *)raw);
    PASS();
}

TEST(test_release_empty) {
    PtrArray arr;
    ptr_array_init(&arr, 0);
    void **raw = ptr_array_release(&arr);
    ASSERT(raw == NULL, "released empty array should be NULL");
    ASSERT(arr.items == NULL, "items should be NULL");
    PASS();
}

TEST(test_release_null_array) {
    void **raw = ptr_array_release(NULL);
    ASSERT(raw == NULL, "release NULL should return NULL");
    PASS();
}

/* ============================================================
 * Main
 * ============================================================ */

int main(void) {
    printf("PtrArray tests:\n");

    /* Init tests */
    RUN_TEST(test_init_with_capacity);
    RUN_TEST(test_init_with_zero_capacity);
    RUN_TEST(test_init_null_array);

    /* Push tests */
    RUN_TEST(test_push_single);
    RUN_TEST(test_push_multiple);
    RUN_TEST(test_push_auto_grow);
    RUN_TEST(test_push_from_zero_capacity);
    RUN_TEST(test_push_null_array);

    /* Destroy tests */
    RUN_TEST(test_destroy);
    RUN_TEST(test_destroy_null_array);
    RUN_TEST(test_destroy_empty_array);

    /* Release tests */
    RUN_TEST(test_release);
    RUN_TEST(test_release_empty);
    RUN_TEST(test_release_null_array);

    printf("\n%d tests: %d passed, %d failed\n", tests_run, tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
