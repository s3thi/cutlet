/*
 * test_gc.c - Unit tests for GC infrastructure and mark phase (gc.h / gc.c)
 *
 * Tests the object allocation tracking list, gc_init, gc_alloc,
 * gc_free_object, gc_free_all, and the mark phase (gc_mark_object,
 * gc_mark_value, gc_collect with mark-then-clear semantics).
 *
 * These tests exercise the GC allocation primitives and marking
 * in isolation — objects are allocated via gc_alloc() and their
 * fields are set up manually for targeted testing.
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
 * test_gc_mark_simple_object: mark a single object, verify flag
 * ============================================================ */

TEST(test_gc_mark_simple_object) {
    gc_init();

    /* Allocate an ObjArray via gc_alloc. */
    ObjArray *arr = (ObjArray *)gc_alloc(OBJ_ARRAY, sizeof(ObjArray));
    ASSERT(arr != NULL, "gc_alloc should return non-NULL");
    ASSERT(arr->obj.is_marked == false, "is_marked should start false");

    /* Mark the object and verify the flag is set. */
    gc_mark_object((Obj *)arr);
    ASSERT(arr->obj.is_marked == true, "is_marked should be true after gc_mark_object");

    gc_free_all();
    PASS();
}

/* ============================================================
 * test_gc_mark_clears_after_collect: collect resets all marks
 *
 * gc_collect() marks reachable objects then clears all marks
 * (since there's no sweep yet). With no VM active, no objects
 * are reachable, so all marks should be false after collect.
 * ============================================================ */

TEST(test_gc_mark_clears_after_collect) {
    gc_init();

    /* Allocate several objects of different types. */
    ObjArray *arr = (ObjArray *)gc_alloc(OBJ_ARRAY, sizeof(ObjArray));
    ObjFunction *fn = (ObjFunction *)gc_alloc(OBJ_FUNCTION, sizeof(ObjFunction));
    ObjMap *m = (ObjMap *)gc_alloc(OBJ_MAP, sizeof(ObjMap));

    ASSERT(arr != NULL, "arr alloc should succeed");
    ASSERT(fn != NULL, "fn alloc should succeed");
    ASSERT(m != NULL, "map alloc should succeed");

    /* Manually set marks to true to simulate a previous partial mark. */
    arr->obj.is_marked = true;
    fn->obj.is_marked = true;
    m->obj.is_marked = true;

    /* gc_collect() should mark roots (none active) then clear all marks. */
    gc_collect();

    ASSERT(arr->obj.is_marked == false, "arr is_marked should be false after collect");
    ASSERT(fn->obj.is_marked == false, "fn is_marked should be false after collect");
    ASSERT(m->obj.is_marked == false, "map is_marked should be false after collect");

    gc_free_all();
    PASS();
}

/* ============================================================
 * test_gc_mark_reachable_from_array: marking an array traces
 * its element Values (e.g. a VAL_CLOSURE inside the array).
 *
 * Creates an ObjArray containing a VAL_CLOSURE value. Marks the
 * array. Verifies that the array, the closure, and the closure's
 * underlying function are all marked.
 * ============================================================ */

TEST(test_gc_mark_reachable_from_array) {
    gc_init();

    /* Allocate a function and a closure wrapping it. */
    ObjFunction *fn = (ObjFunction *)gc_alloc(OBJ_FUNCTION, sizeof(ObjFunction));
    ASSERT(fn != NULL, "fn alloc should succeed");
    fn->refcount = 1;
    fn->upvalue_count = 0;

    ObjClosure *cl = (ObjClosure *)gc_alloc(OBJ_CLOSURE, sizeof(ObjClosure));
    ASSERT(cl != NULL, "closure alloc should succeed");
    cl->refcount = 1;
    cl->function = fn;
    cl->upvalues = NULL;
    cl->upvalue_count = 0;

    /* Allocate an ObjArray and add the closure as an element. */
    ObjArray *arr = (ObjArray *)gc_alloc(OBJ_ARRAY, sizeof(ObjArray));
    ASSERT(arr != NULL, "arr alloc should succeed");
    arr->data = malloc(sizeof(Value));
    ASSERT(arr->data != NULL, "data alloc should succeed");
    arr->count = 1;
    arr->capacity = 1;
    /* Manually construct a VAL_CLOSURE value pointing to cl.
     * We use memset to zero-init, then set the relevant fields. */
    memset(&arr->data[0], 0, sizeof(Value));
    arr->data[0].type = VAL_CLOSURE;
    arr->data[0].closure = cl;

    /* Mark the array — should recursively mark the closure and function. */
    gc_mark_object((Obj *)arr);

    ASSERT(arr->obj.is_marked == true, "array should be marked");
    ASSERT(cl->obj.is_marked == true, "closure inside array should be marked");
    ASSERT(fn->obj.is_marked == true, "function inside closure should be marked");

    /* Clean up: free data array manually, then gc_free_all frees the objects.
     * We must zero out arr->data first so gc_free_all doesn't try to
     * value_free the closure element (which would decrement refcount). */
    memset(&arr->data[0], 0, sizeof(Value));
    free(arr->data);
    arr->data = NULL;
    arr->count = 0;

    gc_free_all();
    PASS();
}

/* ============================================================
 * test_gc_mark_reachable_from_map: marking a map traces its
 * key and value entries.
 *
 * Creates an ObjMap with a string key and closure value. Marks
 * the map. Verifies the map, the string, and the closure are
 * all marked.
 * ============================================================ */

TEST(test_gc_mark_reachable_from_map) {
    gc_init();

    /* Allocate a string key. */
    ObjString *key_str = obj_string_new("hello", 5);
    ASSERT(key_str != NULL, "string alloc should succeed");

    /* Allocate a function and closure for the map value. */
    ObjFunction *fn = (ObjFunction *)gc_alloc(OBJ_FUNCTION, sizeof(ObjFunction));
    ASSERT(fn != NULL, "fn alloc should succeed");
    fn->refcount = 1;
    fn->upvalue_count = 0;

    ObjClosure *cl = (ObjClosure *)gc_alloc(OBJ_CLOSURE, sizeof(ObjClosure));
    ASSERT(cl != NULL, "closure alloc should succeed");
    cl->refcount = 1;
    cl->function = fn;
    cl->upvalues = NULL;
    cl->upvalue_count = 0;

    /* Allocate an ObjMap and add one entry: string key -> closure value. */
    ObjMap *m = (ObjMap *)gc_alloc(OBJ_MAP, sizeof(ObjMap));
    ASSERT(m != NULL, "map alloc should succeed");
    m->entries = malloc(sizeof(MapEntry));
    ASSERT(m->entries != NULL, "entries alloc should succeed");
    m->count = 1;
    m->capacity = 1;

    /* Construct the key Value (VAL_STRING). */
    memset(&m->entries[0].key, 0, sizeof(Value));
    m->entries[0].key.type = VAL_STRING;
    m->entries[0].key.string = key_str;

    /* Construct the value Value (VAL_CLOSURE). */
    memset(&m->entries[0].value, 0, sizeof(Value));
    m->entries[0].value.type = VAL_CLOSURE;
    m->entries[0].value.closure = cl;

    /* Mark the map — should recursively mark key string and value closure. */
    gc_mark_object((Obj *)m);

    ASSERT(m->obj.is_marked == true, "map should be marked");
    ASSERT(key_str->obj.is_marked == true, "string key should be marked");
    ASSERT(cl->obj.is_marked == true, "closure value should be marked");

    /* Clean up: zero out entries to prevent gc_free_all from
     * value_free-ing the references (which would affect refcounts). */
    memset(&m->entries[0].key, 0, sizeof(Value));
    memset(&m->entries[0].value, 0, sizeof(Value));
    free(m->entries);
    m->entries = NULL;
    m->count = 0;

    gc_free_all();
    PASS();
}

/* ============================================================
 * test_gc_mark_closure_traces_function_and_upvalues:
 * marking a closure traces its function and upvalue array,
 * including closed-over values.
 *
 * Creates a closure with one upvalue that is closed (location
 * points to &closed). The closed value is a VAL_ARRAY pointing
 * to an ObjArray. Marks the closure. Verifies the closure,
 * function, upvalue, and the ObjArray in the closed value are
 * all marked.
 * ============================================================ */

TEST(test_gc_mark_closure_traces_function_and_upvalues) {
    gc_init();

    /* Allocate a function. */
    ObjFunction *fn = (ObjFunction *)gc_alloc(OBJ_FUNCTION, sizeof(ObjFunction));
    ASSERT(fn != NULL, "fn alloc should succeed");
    fn->refcount = 1;
    fn->upvalue_count = 1;

    /* Allocate an ObjArray that will be the closed-over value. */
    ObjArray *closed_arr = (ObjArray *)gc_alloc(OBJ_ARRAY, sizeof(ObjArray));
    ASSERT(closed_arr != NULL, "closed_arr alloc should succeed");
    closed_arr->refcount = 1;

    /* Allocate an upvalue and close it with the array value. */
    ObjUpvalue *uv = (ObjUpvalue *)gc_alloc(OBJ_UPVALUE, sizeof(ObjUpvalue));
    ASSERT(uv != NULL, "upvalue alloc should succeed");
    uv->refcount = 1;
    /* Set up the closed value as a VAL_ARRAY. */
    memset(&uv->closed, 0, sizeof(Value));
    uv->closed.type = VAL_ARRAY;
    uv->closed.array = closed_arr;
    /* Mark the upvalue as closed: location points to &closed. */
    uv->location = &uv->closed;

    /* Allocate a closure wrapping the function, with one upvalue. */
    ObjClosure *cl = (ObjClosure *)gc_alloc(OBJ_CLOSURE, sizeof(ObjClosure));
    ASSERT(cl != NULL, "closure alloc should succeed");
    cl->refcount = 1;
    cl->function = fn;
    cl->upvalue_count = 1;
    cl->upvalues = malloc(sizeof(ObjUpvalue *));
    ASSERT(cl->upvalues != NULL, "upvalues array alloc should succeed");
    cl->upvalues[0] = uv;

    /* Mark the closure — should trace function, upvalue, and closed value. */
    gc_mark_object((Obj *)cl);

    ASSERT(cl->obj.is_marked == true, "closure should be marked");
    ASSERT(fn->obj.is_marked == true, "function should be marked");
    ASSERT(uv->obj.is_marked == true, "upvalue should be marked");
    ASSERT(closed_arr->obj.is_marked == true, "closed-over array should be marked");

    /* Clean up: zero out the closed value so gc_free_all doesn't
     * try to value_free the array reference. Free the upvalues array. */
    memset(&uv->closed, 0, sizeof(Value));
    free((void *)cl->upvalues);
    cl->upvalues = NULL;

    gc_free_all();
    PASS();
}

/* ============================================================
 * test_gc_stress_mode: allocate objects and call gc_collect
 * to verify no crashes or assertion failures.
 *
 * This test exercises the gc_collect path to ensure it doesn't
 * crash when there's no VM active (no roots to mark). Under
 * GC_STRESS (compile-time flag), gc_collect is called on every
 * gc_alloc — this test verifies that the basic code path works
 * even without that flag, by calling gc_collect explicitly.
 * ============================================================ */

TEST(test_gc_stress_mode) {
    gc_init();

    /* Allocate a mix of object types. */
    ObjArray *arr = (ObjArray *)gc_alloc(OBJ_ARRAY, sizeof(ObjArray));
    ObjFunction *fn = (ObjFunction *)gc_alloc(OBJ_FUNCTION, sizeof(ObjFunction));
    ObjMap *m = (ObjMap *)gc_alloc(OBJ_MAP, sizeof(ObjMap));
    ObjClosure *cl = (ObjClosure *)gc_alloc(OBJ_CLOSURE, sizeof(ObjClosure));
    ObjUpvalue *uv = (ObjUpvalue *)gc_alloc(OBJ_UPVALUE, sizeof(ObjUpvalue));

    ASSERT(arr != NULL, "arr alloc should succeed");
    ASSERT(fn != NULL, "fn alloc should succeed");
    ASSERT(m != NULL, "map alloc should succeed");
    ASSERT(cl != NULL, "closure alloc should succeed");
    ASSERT(uv != NULL, "upvalue alloc should succeed");

    /* Call gc_collect — should mark roots (none) and clear all marks.
     * The key assertion is that this doesn't crash or trigger any
     * assertion failures. */
    gc_collect();

    /* All objects should still exist (no sweep yet) and marks cleared. */
    ASSERT(count_gc_objects() == 5, "all 5 objects should still exist");
    ASSERT(arr->obj.is_marked == false, "arr mark should be cleared");
    ASSERT(fn->obj.is_marked == false, "fn mark should be cleared");
    ASSERT(m->obj.is_marked == false, "map mark should be cleared");
    ASSERT(cl->obj.is_marked == false, "closure mark should be cleared");
    ASSERT(uv->obj.is_marked == false, "upvalue mark should be cleared");

    /* Allocate more objects after collect — verify no corruption. */
    ObjArray *arr2 = (ObjArray *)gc_alloc(OBJ_ARRAY, sizeof(ObjArray));
    ASSERT(arr2 != NULL, "post-collect alloc should succeed");
    ASSERT(count_gc_objects() == 6, "should have 6 objects total");

    gc_free_all();
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

    printf("\nGC mark phase:\n");
    RUN_TEST(test_gc_mark_simple_object);
    RUN_TEST(test_gc_mark_clears_after_collect);
    RUN_TEST(test_gc_mark_reachable_from_array);
    RUN_TEST(test_gc_mark_reachable_from_map);
    RUN_TEST(test_gc_mark_closure_traces_function_and_upvalues);
    RUN_TEST(test_gc_stress_mode);

    printf("\n========================================\n");
    printf("Tests run: %d\n", tests_run);
    printf("Passed:    %d\n", tests_passed);
    printf("Failed:    %d\n", tests_failed);
    printf("========================================\n");

    return tests_failed > 0 ? 1 : 0;
}
