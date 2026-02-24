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
 * ObjMap creation and formatting tests
 * ============================================================ */

TEST(test_empty_map_format) {
    /* Create an empty ObjMap, wrap in Value, format → "{}". */
    ObjMap *m = obj_map_new();
    ASSERT(m != NULL, "obj_map_new should succeed");
    ASSERT(m->count == 0, "empty map count should be 0");
    ASSERT(m->refcount == 1, "initial refcount should be 1");

    Value v = make_map(m);
    ASSERT(v.type == VAL_MAP, "type should be VAL_MAP");

    char *fmt = value_format(&v);
    ASSERT(fmt != NULL, "format should return non-NULL");
    ASSERT(strcmp(fmt, "{}") == 0, "empty map should format as {}");
    free(fmt);

    value_free(&v);
    PASS();
}

TEST(test_string_key_map_format) {
    /* Create a map with string keys {a: 1, b: 2}, format → "{a: 1, b: 2}". */
    ObjMap *m = obj_map_new();
    Value k1 = make_string(strdup("a"));
    Value v1 = make_number(1);
    obj_map_set(m, &k1, &v1);
    value_free(&k1);
    value_free(&v1);

    Value k2 = make_string(strdup("b"));
    Value v2 = make_number(2);
    obj_map_set(m, &k2, &v2);
    value_free(&k2);
    value_free(&v2);

    Value mv = make_map(m);
    char *fmt = value_format(&mv);
    ASSERT(fmt != NULL, "format should return non-NULL");
    ASSERT(strcmp(fmt, "{a: 1, b: 2}") == 0, "should format as {a: 1, b: 2}");
    free(fmt);

    value_free(&mv);
    PASS();
}

TEST(test_map_get_existing) {
    /* obj_map_get for existing key returns pointer to value. */
    ObjMap *m = obj_map_new();
    Value k = make_string(strdup("x"));
    Value v = make_number(42);
    obj_map_set(m, &k, &v);
    value_free(&k);
    value_free(&v);

    Value lookup_key = make_string(strdup("x"));
    Value *result = obj_map_get(m, &lookup_key);
    ASSERT(result != NULL, "should find existing key");
    ASSERT(result->type == VAL_NUMBER, "value should be number");
    ASSERT(result->number == 42, "value should be 42");
    value_free(&lookup_key);

    Value mv = make_map(m);
    value_free(&mv);
    PASS();
}

TEST(test_map_get_missing) {
    /* obj_map_get for missing key returns NULL. */
    ObjMap *m = obj_map_new();
    Value k = make_string(strdup("a"));
    Value v = make_number(1);
    obj_map_set(m, &k, &v);
    value_free(&k);
    value_free(&v);

    Value missing = make_string(strdup("z"));
    Value *result = obj_map_get(m, &missing);
    ASSERT(result == NULL, "should return NULL for missing key");
    value_free(&missing);

    Value mv = make_map(m);
    value_free(&mv);
    PASS();
}

TEST(test_map_set_duplicate) {
    /* obj_map_set with duplicate key updates the value. */
    ObjMap *m = obj_map_new();
    Value k1 = make_string(strdup("a"));
    Value v1 = make_number(1);
    obj_map_set(m, &k1, &v1);
    value_free(&k1);
    value_free(&v1);

    /* Update same key with new value. */
    Value k2 = make_string(strdup("a"));
    Value v2 = make_number(99);
    obj_map_set(m, &k2, &v2);
    value_free(&k2);
    value_free(&v2);

    ASSERT(m->count == 1, "count should still be 1 after duplicate key");

    Value lookup = make_string(strdup("a"));
    Value *result = obj_map_get(m, &lookup);
    ASSERT(result != NULL, "should find key");
    ASSERT(result->number == 99, "value should be updated to 99");
    value_free(&lookup);

    Value mv = make_map(m);
    value_free(&mv);
    PASS();
}

TEST(test_map_has) {
    /* obj_map_has returns true/false correctly. */
    ObjMap *m = obj_map_new();
    Value k = make_string(strdup("a"));
    Value v = make_number(1);
    obj_map_set(m, &k, &v);
    value_free(&k);
    value_free(&v);

    Value lookup_yes = make_string(strdup("a"));
    ASSERT(obj_map_has(m, &lookup_yes) == true, "should have key 'a'");
    value_free(&lookup_yes);

    Value lookup_no = make_string(strdup("b"));
    ASSERT(obj_map_has(m, &lookup_no) == false, "should not have key 'b'");
    value_free(&lookup_no);

    Value mv = make_map(m);
    value_free(&mv);
    PASS();
}

/* ============================================================
 * Map clone and refcount tests
 * ============================================================ */

TEST(test_map_clone_refcount) {
    /* Clone a VAL_MAP → refcount is 2. Free clone → refcount is 1. */
    ObjMap *m = obj_map_new();
    Value k = make_string(strdup("x"));
    Value v = make_number(42);
    obj_map_set(m, &k, &v);
    value_free(&k);
    value_free(&v);

    Value mv = make_map(m);
    ASSERT(m->refcount == 1, "initial refcount should be 1");

    Value cloned;
    bool ok = value_clone(&cloned, &mv);
    ASSERT(ok, "clone should succeed");
    ASSERT(cloned.type == VAL_MAP, "cloned type should be VAL_MAP");
    ASSERT(cloned.map == m, "cloned should share same ObjMap");
    ASSERT(m->refcount == 2, "refcount should be 2 after clone");

    value_free(&cloned);
    ASSERT(m->refcount == 1, "refcount should be 1 after freeing clone");

    value_free(&mv);
    PASS();
}

/* ============================================================
 * Map truthiness tests
 * ============================================================ */

TEST(test_map_truthy_nonempty) {
    /* Non-empty maps are truthy. */
    ObjMap *m = obj_map_new();
    Value k = make_string(strdup("a"));
    Value v = make_number(1);
    obj_map_set(m, &k, &v);
    value_free(&k);
    value_free(&v);

    Value mv = make_map(m);
    ASSERT(is_truthy(&mv) == true, "non-empty map should be truthy");
    value_free(&mv);
    PASS();
}

TEST(test_map_truthy_empty) {
    /* Empty maps are falsy. */
    ObjMap *m = obj_map_new();
    Value mv = make_map(m);
    ASSERT(is_truthy(&mv) == false, "empty map should be falsy");
    value_free(&mv);
    PASS();
}

/* ============================================================
 * Map copy-on-write (ensure_owned) tests
 * ============================================================ */

TEST(test_map_ensure_owned_refcount_one) {
    /* obj_map_ensure_owned with refcount 1 → no-op. */
    ObjMap *m = obj_map_new();
    Value k = make_string(strdup("a"));
    Value v = make_number(10);
    obj_map_set(m, &k, &v);
    value_free(&k);
    value_free(&v);

    Value mv = make_map(m);
    ASSERT(m->refcount == 1, "refcount should be 1");

    ObjMap *before = mv.map;
    obj_map_ensure_owned(&mv);
    ASSERT(mv.map == before, "should be same pointer (no-op)");
    ASSERT(mv.map->refcount == 1, "refcount should still be 1");

    value_free(&mv);
    PASS();
}

TEST(test_map_ensure_owned_refcount_two) {
    /* obj_map_ensure_owned with refcount 2 → deep clone, original refcount drops to 1. */
    ObjMap *m = obj_map_new();
    Value k = make_string(strdup("a"));
    Value v = make_number(10);
    obj_map_set(m, &k, &v);
    value_free(&k);
    value_free(&v);

    Value k2 = make_string(strdup("b"));
    Value v2 = make_number(20);
    obj_map_set(m, &k2, &v2);
    value_free(&k2);
    value_free(&v2);

    Value mv1 = make_map(m);
    Value mv2;
    value_clone(&mv2, &mv1);
    ASSERT(m->refcount == 2, "refcount should be 2 after clone");

    obj_map_ensure_owned(&mv2);
    ASSERT(mv2.map != m, "mv2 should point to a new ObjMap");
    ASSERT(mv2.map->refcount == 1, "new map refcount should be 1");
    ASSERT(m->refcount == 1, "original refcount should drop to 1");

    /* Values should be independent copies. */
    ASSERT(mv2.map->count == 2, "new map should have 2 entries");

    value_free(&mv1);
    value_free(&mv2);
    PASS();
}

/* ============================================================
 * value_equal tests (extracted from VM)
 * ============================================================ */

TEST(test_value_equal_numbers) {
    Value a = make_number(42);
    Value b = make_number(42);
    ASSERT(value_equal(&a, &b) == true, "42 == 42");

    Value c = make_number(43);
    ASSERT(value_equal(&a, &c) == false, "42 != 43");
    PASS();
}

TEST(test_value_equal_strings) {
    Value a = make_string(strdup("hello"));
    Value b = make_string(strdup("hello"));
    ASSERT(value_equal(&a, &b) == true, "hello == hello");

    Value c = make_string(strdup("world"));
    ASSERT(value_equal(&a, &c) == false, "hello != world");

    value_free(&a);
    value_free(&b);
    value_free(&c);
    PASS();
}

TEST(test_value_equal_bools) {
    Value t1 = make_bool(true);
    Value t2 = make_bool(true);
    ASSERT(value_equal(&t1, &t2) == true, "true == true");

    Value f = make_bool(false);
    ASSERT(value_equal(&t1, &f) == false, "true != false");
    PASS();
}

TEST(test_value_equal_nothing) {
    Value a = make_nothing();
    Value b = make_nothing();
    ASSERT(value_equal(&a, &b) == true, "nothing == nothing");
    PASS();
}

TEST(test_value_equal_different_types) {
    Value n = make_number(1);
    Value s = make_string(strdup("1"));
    ASSERT(value_equal(&n, &s) == false, "number != string");
    value_free(&s);
    PASS();
}

/* ============================================================
 * Mixed-type key map formatting
 * ============================================================ */

TEST(test_mixed_key_map_format) {
    /* Map with number and bool keys: {1: one, true: yes}. */
    ObjMap *m = obj_map_new();
    Value k1 = make_number(1);
    Value v1 = make_string(strdup("one"));
    obj_map_set(m, &k1, &v1);
    value_free(&k1);
    value_free(&v1);

    Value k2 = make_bool(true);
    Value v2 = make_string(strdup("yes"));
    obj_map_set(m, &k2, &v2);
    value_free(&k2);
    value_free(&v2);

    Value mv = make_map(m);
    char *fmt = value_format(&mv);
    ASSERT(fmt != NULL, "format should return non-NULL");
    ASSERT(strcmp(fmt, "{1: one, true: yes}") == 0, "should format as {1: one, true: yes}");
    free(fmt);

    value_free(&mv);
    PASS();
}

/* ============================================================
 * Deep clone map test
 * ============================================================ */

TEST(test_obj_map_clone_deep) {
    /* Deep clone creates an independent copy. */
    ObjMap *m = obj_map_new();
    Value k = make_string(strdup("hello"));
    Value v = make_number(99);
    obj_map_set(m, &k, &v);
    value_free(&k);
    value_free(&v);

    ObjMap *clone = obj_map_clone_deep(m);
    ASSERT(clone != NULL, "clone should succeed");
    ASSERT(clone != m, "clone should be a different pointer");
    ASSERT(clone->refcount == 1, "clone refcount should be 1");
    ASSERT(clone->count == 1, "clone should have 1 entry");

    /* Verify the key-value pair was cloned. */
    Value lookup = make_string(strdup("hello"));
    Value *result = obj_map_get(clone, &lookup);
    ASSERT(result != NULL, "should find key in clone");
    ASSERT(result->number == 99, "value should be 99");
    value_free(&lookup);

    /* Keys should be independent copies. */
    ASSERT(clone->entries[0].key.string != m->entries[0].key.string,
           "key strings should be independent copies");

    Value mv1 = make_map(m);
    Value mv2 = make_map(clone);
    value_free(&mv1);
    value_free(&mv2);
    PASS();
}

/* ============================================================
 * Main
 * ============================================================ */

int main(void) {
    printf("Running value tests...\n\n");

    printf("Array formatting:\n");
    RUN_TEST(test_empty_array_format);
    RUN_TEST(test_number_array_format);
    RUN_TEST(test_mixed_array_format);
    RUN_TEST(test_nested_array_format);

    printf("\nArray clone and refcount:\n");
    RUN_TEST(test_array_clone_refcount);

    printf("\nArray truthiness:\n");
    RUN_TEST(test_array_truthy_nonempty);
    RUN_TEST(test_array_truthy_empty);

    printf("\nArray copy-on-write (ensure_owned):\n");
    RUN_TEST(test_ensure_owned_refcount_one);
    RUN_TEST(test_ensure_owned_refcount_two);

    printf("\nArray deep clone:\n");
    RUN_TEST(test_obj_array_clone_deep);

    printf("\nMap formatting:\n");
    RUN_TEST(test_empty_map_format);
    RUN_TEST(test_string_key_map_format);
    RUN_TEST(test_mixed_key_map_format);

    printf("\nMap get/set/has:\n");
    RUN_TEST(test_map_get_existing);
    RUN_TEST(test_map_get_missing);
    RUN_TEST(test_map_set_duplicate);
    RUN_TEST(test_map_has);

    printf("\nMap clone and refcount:\n");
    RUN_TEST(test_map_clone_refcount);

    printf("\nMap truthiness:\n");
    RUN_TEST(test_map_truthy_nonempty);
    RUN_TEST(test_map_truthy_empty);

    printf("\nMap copy-on-write (ensure_owned):\n");
    RUN_TEST(test_map_ensure_owned_refcount_one);
    RUN_TEST(test_map_ensure_owned_refcount_two);

    printf("\nMap deep clone:\n");
    RUN_TEST(test_obj_map_clone_deep);

    printf("\nvalue_equal:\n");
    RUN_TEST(test_value_equal_numbers);
    RUN_TEST(test_value_equal_strings);
    RUN_TEST(test_value_equal_bools);
    RUN_TEST(test_value_equal_nothing);
    RUN_TEST(test_value_equal_different_types);

    printf("\n========================================\n");
    printf("Tests run: %d\n", tests_run);
    printf("Passed:    %d\n", tests_passed);
    printf("Failed:    %d\n", tests_failed);
    printf("========================================\n");

    return tests_failed > 0 ? 1 : 0;
}
