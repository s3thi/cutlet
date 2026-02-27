/*
 * test_gc.c - Unit tests for GC infrastructure (gc.h / gc.c)
 *
 * Tests the object allocation tracking list, gc_init, gc_alloc,
 * gc_free_object, and gc_free_all. These tests exercise the GC
 * allocation primitives in isolation — objects are allocated via
 * gc_alloc() and their fields are left zero-initialized (from calloc).
 *
 * Note: The Obj header is not yet embedded in the existing Obj*
 * structs (that happens in Step 3). These tests verify the GC
 * list mechanics independently.
 */

#include "../src/gc.h"
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
 * Helper: count objects in the GC list
 * ============================================================ */

static int count_gc_objects(void) {
    int count = 0;
    Obj *obj = gc_get_objects();
    while (obj) {
        count++;
        obj = obj->next;
    }
    return count;
}

/* Helper: check if a specific Obj pointer is in the GC list. */
static bool obj_in_list(Obj *target) {
    Obj *obj = gc_get_objects();
    while (obj) {
        if (obj == target)
            return true;
        obj = obj->next;
    }
    return false;
}

/* ============================================================
 * test_gc_init: verify gc_init resets state
 * ============================================================ */

TEST(test_gc_init) {
    gc_init();
    ASSERT(gc_get_objects() == NULL, "object list should be NULL after init");
    ASSERT(gc_get_bytes_allocated() == 0, "bytes_allocated should be 0 after init");
    PASS();
}

/* ============================================================
 * test_gc_alloc_single: allocate one object, verify tracking
 * ============================================================ */

TEST(test_gc_alloc_single) {
    gc_init();

    /* Allocate an OBJ_ARRAY-sized object. */
    void *ptr = gc_alloc(OBJ_ARRAY, sizeof(ObjArray));
    ASSERT(ptr != NULL, "gc_alloc should return non-NULL");

    /* Verify the Obj header is properly initialized. */
    Obj *obj = (Obj *)ptr;
    ASSERT(obj->type == OBJ_ARRAY, "type should be OBJ_ARRAY");
    ASSERT(obj->is_marked == false, "is_marked should be false");
    ASSERT(obj->alloc_size == sizeof(ObjArray), "alloc_size should match");

    /* Verify it appears in the object list. */
    ASSERT(gc_get_objects() == obj, "object should be the list head");
    ASSERT(count_gc_objects() == 1, "list should have exactly 1 object");

    /* Verify bytes_allocated was updated. */
    ASSERT(gc_get_bytes_allocated() == sizeof(ObjArray), "bytes_allocated should match alloc size");

    /* Clean up. */
    gc_free_all();
    PASS();
}

/* ============================================================
 * test_gc_alloc_multiple: allocate 3 objects, verify all tracked
 * ============================================================ */

TEST(test_gc_alloc_multiple) {
    gc_init();

    /* Allocate three objects of different types. */
    void *p1 = gc_alloc(OBJ_FUNCTION, sizeof(ObjFunction));
    void *p2 = gc_alloc(OBJ_ARRAY, sizeof(ObjArray));
    void *p3 = gc_alloc(OBJ_MAP, sizeof(ObjMap));

    ASSERT(p1 != NULL, "first alloc should succeed");
    ASSERT(p2 != NULL, "second alloc should succeed");
    ASSERT(p3 != NULL, "third alloc should succeed");

    /* Verify types. */
    ASSERT(OBJ_TYPE(p1) == OBJ_FUNCTION, "p1 type should be OBJ_FUNCTION");
    ASSERT(OBJ_TYPE(p2) == OBJ_ARRAY, "p2 type should be OBJ_ARRAY");
    ASSERT(OBJ_TYPE(p3) == OBJ_MAP, "p3 type should be OBJ_MAP");

    /* All three should appear in the list. */
    ASSERT(count_gc_objects() == 3, "list should have exactly 3 objects");
    ASSERT(obj_in_list((Obj *)p1), "p1 should be in the list");
    ASSERT(obj_in_list((Obj *)p2), "p2 should be in the list");
    ASSERT(obj_in_list((Obj *)p3), "p3 should be in the list");

    /* Verify bytes_allocated is the sum of all three. */
    size_t expected = sizeof(ObjFunction) + sizeof(ObjArray) + sizeof(ObjMap);
    ASSERT(gc_get_bytes_allocated() == expected, "bytes_allocated should be sum of allocs");

    /* The most recently allocated object should be the list head (prepend). */
    ASSERT(gc_get_objects() == (Obj *)p3, "last alloc should be list head");

    gc_free_all();
    PASS();
}

/* ============================================================
 * test_gc_free_object: allocate one, free it, verify list empty
 * ============================================================ */

TEST(test_gc_free_object) {
    gc_init();

    void *ptr = gc_alloc(OBJ_ARRAY, sizeof(ObjArray));
    ASSERT(ptr != NULL, "alloc should succeed");
    ASSERT(count_gc_objects() == 1, "list should have 1 object before free");

    gc_free_object((Obj *)ptr);

    ASSERT(gc_get_objects() == NULL, "list should be empty after free");
    ASSERT(count_gc_objects() == 0, "count should be 0 after free");
    ASSERT(gc_get_bytes_allocated() == 0, "bytes_allocated should be 0 after free");

    PASS();
}

/* ============================================================
 * test_gc_free_all: allocate several, free all, verify cleanup
 * ============================================================ */

TEST(test_gc_free_all) {
    gc_init();

    /* Allocate several objects. */
    gc_alloc(OBJ_FUNCTION, sizeof(ObjFunction));
    gc_alloc(OBJ_CLOSURE, sizeof(ObjClosure));
    gc_alloc(OBJ_ARRAY, sizeof(ObjArray));
    gc_alloc(OBJ_MAP, sizeof(ObjMap));
    gc_alloc(OBJ_UPVALUE, sizeof(ObjUpvalue));

    ASSERT(count_gc_objects() == 5, "list should have 5 objects");
    ASSERT(gc_get_bytes_allocated() > 0, "bytes_allocated should be > 0");

    gc_free_all();

    ASSERT(gc_get_objects() == NULL, "list should be NULL after free_all");
    ASSERT(count_gc_objects() == 0, "count should be 0 after free_all");
    ASSERT(gc_get_bytes_allocated() == 0, "bytes_allocated should be 0 after free_all");

    PASS();
}

/* ============================================================
 * Main
 * ============================================================ */

int main(void) {
    printf("Running GC infrastructure tests...\n\n");

    printf("GC init:\n");
    RUN_TEST(test_gc_init);

    printf("\nGC allocation:\n");
    RUN_TEST(test_gc_alloc_single);
    RUN_TEST(test_gc_alloc_multiple);

    printf("\nGC free:\n");
    RUN_TEST(test_gc_free_object);
    RUN_TEST(test_gc_free_all);

    printf("\n========================================\n");
    printf("Tests run: %d\n", tests_run);
    printf("Passed:    %d\n", tests_passed);
    printf("Failed:    %d\n", tests_failed);
    printf("========================================\n");

    return tests_failed > 0 ? 1 : 0;
}
