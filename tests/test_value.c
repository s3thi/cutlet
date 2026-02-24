/*
 * test_value.c - Tests for ObjArray and VAL_ARRAY value type
 *
 * Tests array creation, formatting, cloning (refcounting),
 * truthiness, and copy-on-write via obj_array_ensure_owned.
 */

#include "../src/value.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * Simple test harness (same pattern as other test files)
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
 * ObjArray creation and formatting tests
 * ============================================================ */

TEST(test_empty_array_format) {
    /* Create an empty ObjArray, wrap in Value, format → "[]". */
    ObjArray *arr = obj_array_new();
    ASSERT(arr != NULL, "obj_array_new should succeed");
    ASSERT(arr->count == 0, "empty array count should be 0");
    ASSERT(arr->refcount == 1, "initial refcount should be 1");

    Value v = make_array(arr);
    ASSERT(v.type == VAL_ARRAY, "type should be VAL_ARRAY");

    char *fmt = value_format(&v);
    ASSERT(fmt != NULL, "format should return non-NULL");
    ASSERT(strcmp(fmt, "[]") == 0, "empty array should format as []");
    free(fmt);

    value_free(&v);
    PASS();
}

TEST(test_number_array_format) {
    /* Create [1, 2, 3], format → "[1, 2, 3]". */
    ObjArray *arr = obj_array_new();
    obj_array_push(arr, make_number(1));
    obj_array_push(arr, make_number(2));
    obj_array_push(arr, make_number(3));

    Value v = make_array(arr);
    char *fmt = value_format(&v);
    ASSERT(fmt != NULL, "format should return non-NULL");
    ASSERT(strcmp(fmt, "[1, 2, 3]") == 0, "should format as [1, 2, 3]");
    free(fmt);

    value_free(&v);
    PASS();
}

TEST(test_mixed_array_format) {
    /* Create ["a", true, nothing], format → "[a, true, nothing]". */
    ObjArray *arr = obj_array_new();
    obj_array_push(arr, make_string(strdup("a")));
    obj_array_push(arr, make_bool(true));
    obj_array_push(arr, make_nothing());

    Value v = make_array(arr);
    char *fmt = value_format(&v);
    ASSERT(fmt != NULL, "format should return non-NULL");
    ASSERT(strcmp(fmt, "[a, true, nothing]") == 0, "should format as [a, true, nothing]");
    free(fmt);

    value_free(&v);
    PASS();
}

TEST(test_nested_array_format) {
    /* Create [[1, 2], [3]], format → "[[1, 2], [3]]". */
    ObjArray *inner1 = obj_array_new();
    obj_array_push(inner1, make_number(1));
    obj_array_push(inner1, make_number(2));

    ObjArray *inner2 = obj_array_new();
    obj_array_push(inner2, make_number(3));

    ObjArray *outer = obj_array_new();
    obj_array_push(outer, make_array(inner1));
    obj_array_push(outer, make_array(inner2));

    Value v = make_array(outer);
    char *fmt = value_format(&v);
    ASSERT(fmt != NULL, "format should return non-NULL");
    ASSERT(strcmp(fmt, "[[1, 2], [3]]") == 0, "should format as [[1, 2], [3]]");
    free(fmt);

    value_free(&v);
    PASS();
}

/* ============================================================
 * Clone and refcount tests
 * ============================================================ */

TEST(test_array_clone_refcount) {
    /* Clone a VAL_ARRAY → refcount is 2. Free clone → refcount is 1. */
    ObjArray *arr = obj_array_new();
    obj_array_push(arr, make_number(42));

    Value v = make_array(arr);
    ASSERT(arr->refcount == 1, "initial refcount should be 1");

    Value cloned;
    bool ok = value_clone(&cloned, &v);
    ASSERT(ok, "clone should succeed");
    ASSERT(cloned.type == VAL_ARRAY, "cloned type should be VAL_ARRAY");
    ASSERT(cloned.array == arr, "cloned should share same ObjArray");
    ASSERT(arr->refcount == 2, "refcount should be 2 after clone");

    value_free(&cloned);
    ASSERT(arr->refcount == 1, "refcount should be 1 after freeing clone");

    value_free(&v);
    /* After freeing original, arr is freed (refcount 0). Can't check further. */
    PASS();
}

/* ============================================================
 * Truthiness tests
 * ============================================================ */

TEST(test_array_truthy_nonempty) {
    /* Non-empty arrays are truthy. */
    ObjArray *arr = obj_array_new();
    obj_array_push(arr, make_number(1));
    Value v = make_array(arr);
    ASSERT(is_truthy(&v) == true, "non-empty array should be truthy");
    value_free(&v);
    PASS();
}

TEST(test_array_truthy_empty) {
    /* Empty arrays are falsy. */
    ObjArray *arr = obj_array_new();
    Value v = make_array(arr);
    ASSERT(is_truthy(&v) == false, "empty array should be falsy");
    value_free(&v);
    PASS();
}

/* ============================================================
 * Copy-on-write (ensure_owned) tests
 * ============================================================ */

TEST(test_ensure_owned_refcount_one) {
    /* obj_array_ensure_owned with refcount 1 → no-op. */
    ObjArray *arr = obj_array_new();
    obj_array_push(arr, make_number(10));
    Value v = make_array(arr);
    ASSERT(arr->refcount == 1, "refcount should be 1");

    ObjArray *before = v.array;
    obj_array_ensure_owned(&v);
    ASSERT(v.array == before, "should be same pointer (no-op)");
    ASSERT(v.array->refcount == 1, "refcount should still be 1");

    value_free(&v);
    PASS();
}

TEST(test_ensure_owned_refcount_two) {
    /* obj_array_ensure_owned with refcount 2 → deep clone, original refcount drops to 1. */
    ObjArray *arr = obj_array_new();
    obj_array_push(arr, make_number(10));
    obj_array_push(arr, make_number(20));

    Value v1 = make_array(arr);
    Value v2;
    value_clone(&v2, &v1);
    ASSERT(arr->refcount == 2, "refcount should be 2 after clone");

    obj_array_ensure_owned(&v2);
    ASSERT(v2.array != arr, "v2 should point to a new ObjArray");
    ASSERT(v2.array->refcount == 1, "new array refcount should be 1");
    ASSERT(arr->refcount == 1, "original refcount should drop to 1");

    /* Values should be independent copies. */
    ASSERT(v2.array->count == 2, "new array should have 2 elements");
    ASSERT(v2.array->data[0].number == 10, "first element should be 10");
    ASSERT(v2.array->data[1].number == 20, "second element should be 20");

    value_free(&v1);
    value_free(&v2);
    PASS();
}

/* ============================================================
 * obj_array_clone_deep test
 * ============================================================ */

TEST(test_obj_array_clone_deep) {
    /* Deep clone creates an independent copy. */
    ObjArray *arr = obj_array_new();
    obj_array_push(arr, make_string(strdup("hello")));
    obj_array_push(arr, make_number(99));

    ObjArray *clone = obj_array_clone_deep(arr);
    ASSERT(clone != NULL, "clone should succeed");
    ASSERT(clone != arr, "clone should be a different pointer");
    ASSERT(clone->refcount == 1, "clone refcount should be 1");
    ASSERT(clone->count == 2, "clone should have 2 elements");
    ASSERT(clone->data[0].type == VAL_STRING, "first element should be string");
    ASSERT(strcmp(clone->data[0].string, "hello") == 0, "first element value");
    ASSERT(clone->data[1].number == 99, "second element value");

    /* Modifying clone's string shouldn't affect original. */
    ASSERT(clone->data[0].string != arr->data[0].string, "strings should be independent copies");

    /* Clean up: free both arrays manually. */
    Value v1 = make_array(arr);
    Value v2 = make_array(clone);
    value_free(&v1);
    value_free(&v2);
    PASS();
}

/* ============================================================
 * Main
 * ============================================================ */

int main(void) {
    printf("Running value (array) tests...\n\n");

    printf("Array formatting:\n");
    RUN_TEST(test_empty_array_format);
    RUN_TEST(test_number_array_format);
    RUN_TEST(test_mixed_array_format);
    RUN_TEST(test_nested_array_format);

    printf("\nClone and refcount:\n");
    RUN_TEST(test_array_clone_refcount);

    printf("\nTruthiness:\n");
    RUN_TEST(test_array_truthy_nonempty);
    RUN_TEST(test_array_truthy_empty);

    printf("\nCopy-on-write (ensure_owned):\n");
    RUN_TEST(test_ensure_owned_refcount_one);
    RUN_TEST(test_ensure_owned_refcount_two);

    printf("\nDeep clone:\n");
    RUN_TEST(test_obj_array_clone_deep);

    printf("\n========================================\n");
    printf("Tests run: %d\n", tests_run);
    printf("Passed:    %d\n", tests_passed);
    printf("Failed:    %d\n", tests_failed);
    printf("========================================\n");

    return tests_failed > 0 ? 1 : 0;
}
