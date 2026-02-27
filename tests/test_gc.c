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
#include "../src/runtime.h"
#include "../src/value.h"
#include "../src/vm.h"
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
    gc_suppress();
    ASSERT(gc_get_objects() == NULL, "object list should be NULL after init");
    ASSERT(gc_get_bytes_allocated() == 0, "bytes_allocated should be 0 after init");
    PASS();
}

/* ============================================================
 * test_gc_alloc_single: allocate one object, verify tracking
 * ============================================================ */

TEST(test_gc_alloc_single) {
    gc_init();
    gc_suppress();

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
    gc_suppress();

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
    gc_suppress();

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
    gc_suppress();

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
    gc_suppress();

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
 * test_gc_mark_clears_after_collect: gc_collect frees
 * unreachable objects.
 *
 * gc_collect() marks reachable objects then sweeps: unmarked
 * (unreachable) objects are freed and marks on survivors are
 * cleared. With no VM active and no globals, all objects are
 * unreachable and should be swept.
 *
 * Note: previously this test verified that marks were cleared
 * (before sweep existed). Now it verifies that unreachable
 * objects are freed by collect.
 * ============================================================ */

TEST(test_gc_mark_clears_after_collect) {
    /* Use runtime_init so gc_collect can call runtime_mark_globals. */
    runtime_init();
    gc_suppress();
    gc_set_vm(NULL);

    /* Allocate several objects of different types — none are rooted. */
    ObjArray *arr = (ObjArray *)gc_alloc(OBJ_ARRAY, sizeof(ObjArray));
    ObjFunction *fn = (ObjFunction *)gc_alloc(OBJ_FUNCTION, sizeof(ObjFunction));
    ObjMap *m = (ObjMap *)gc_alloc(OBJ_MAP, sizeof(ObjMap));

    ASSERT(arr != NULL, "arr alloc should succeed");
    ASSERT(fn != NULL, "fn alloc should succeed");
    ASSERT(m != NULL, "map alloc should succeed");
    ASSERT(count_gc_objects() == 3, "should have 3 objects before collect");

    /* gc_collect() marks roots (none) then sweeps — all 3 unreachable
     * objects should be freed. Do not access arr/fn/m after this. */
    gc_unsuppress();
    gc_collect();

    ASSERT(count_gc_objects() == 0, "all unreachable objects should be freed after collect");
    ASSERT(gc_get_bytes_allocated() == 0, "bytes_allocated should be 0 after sweeping all");

    runtime_destroy();
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
    gc_suppress();

    /* Allocate a function and a closure wrapping it. */
    ObjFunction *fn = (ObjFunction *)gc_alloc(OBJ_FUNCTION, sizeof(ObjFunction));
    ASSERT(fn != NULL, "fn alloc should succeed");

    fn->upvalue_count = 0;

    ObjClosure *cl = (ObjClosure *)gc_alloc(OBJ_CLOSURE, sizeof(ObjClosure));
    ASSERT(cl != NULL, "closure alloc should succeed");

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
     * value_free the closure element. */
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
    gc_suppress();

    /* Allocate a string key. */
    ObjString *key_str = obj_string_new("hello", 5);
    ASSERT(key_str != NULL, "string alloc should succeed");

    /* Allocate a function and closure for the map value. */
    ObjFunction *fn = (ObjFunction *)gc_alloc(OBJ_FUNCTION, sizeof(ObjFunction));
    ASSERT(fn != NULL, "fn alloc should succeed");

    fn->upvalue_count = 0;

    ObjClosure *cl = (ObjClosure *)gc_alloc(OBJ_CLOSURE, sizeof(ObjClosure));
    ASSERT(cl != NULL, "closure alloc should succeed");

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
     * value_free-ing the references. */
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
    gc_suppress();

    /* Allocate a function. */
    ObjFunction *fn = (ObjFunction *)gc_alloc(OBJ_FUNCTION, sizeof(ObjFunction));
    ASSERT(fn != NULL, "fn alloc should succeed");

    fn->upvalue_count = 1;

    /* Allocate an ObjArray that will be the closed-over value. */
    ObjArray *closed_arr = (ObjArray *)gc_alloc(OBJ_ARRAY, sizeof(ObjArray));
    ASSERT(closed_arr != NULL, "closed_arr alloc should succeed");

    /* Allocate an upvalue and close it with the array value. */
    ObjUpvalue *uv = (ObjUpvalue *)gc_alloc(OBJ_UPVALUE, sizeof(ObjUpvalue));
    ASSERT(uv != NULL, "upvalue alloc should succeed");

    /* Set up the closed value as a VAL_ARRAY. */
    memset(&uv->closed, 0, sizeof(Value));
    uv->closed.type = VAL_ARRAY;
    uv->closed.array = closed_arr;
    /* Mark the upvalue as closed: location points to &closed. */
    uv->location = &uv->closed;

    /* Allocate a closure wrapping the function, with one upvalue. */
    ObjClosure *cl = (ObjClosure *)gc_alloc(OBJ_CLOSURE, sizeof(ObjClosure));
    ASSERT(cl != NULL, "closure alloc should succeed");

    cl->function = fn;
    cl->upvalue_count = 1;
    cl->upvalues = (ObjUpvalue **)malloc(sizeof(ObjUpvalue *));
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
 *
 * With sweep active, unreachable objects are freed by collect.
 * ============================================================ */

TEST(test_gc_stress_mode) {
    /* Use runtime_init so gc_collect can call runtime_mark_globals. */
    runtime_init();
    gc_suppress();
    gc_set_vm(NULL);

    /* Allocate a mix of object types — none are rooted. */
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
    ASSERT(count_gc_objects() == 5, "should have 5 objects before collect");

    /* Call gc_collect — should mark roots (none) and sweep all.
     * The key assertion is that this doesn't crash or trigger any
     * assertion failures. All unreachable objects are freed. */
    gc_unsuppress();
    gc_collect();

    /* All objects were unreachable, so all should be freed. */
    ASSERT(count_gc_objects() == 0, "all unreachable objects should be swept");

    /* Re-suppress so the post-collect allocation isn't immediately
     * swept by GC_STRESS before we can inspect it. */
    gc_suppress();

    /* Allocate more objects after collect — verify no corruption. */
    ObjArray *arr2 = (ObjArray *)gc_alloc(OBJ_ARRAY, sizeof(ObjArray));
    ASSERT(arr2 != NULL, "post-collect alloc should succeed");
    ASSERT(count_gc_objects() == 1, "should have 1 object after post-collect alloc");

    runtime_destroy();
    PASS();
}

/* ============================================================
 * test_gc_set_vm_null_safe: gc_mark_roots with no VM active
 * should not crash. With gc_vm == NULL, only globals are
 * marked. Unreachable objects are swept.
 * ============================================================ */

TEST(test_gc_set_vm_null_safe) {
    /* Use runtime_init so gc_collect can call runtime_mark_globals. */
    runtime_init();
    gc_suppress();

    /* Ensure gc_vm is NULL (default after gc_init or explicit set). */
    gc_set_vm(NULL);

    /* Allocate an object — it's unreachable (no VM, no globals). */
    ObjArray *arr = (ObjArray *)gc_alloc(OBJ_ARRAY, sizeof(ObjArray));
    ASSERT(arr != NULL, "arr alloc should succeed");
    ASSERT(count_gc_objects() == 1, "should have 1 object before collect");

    /* gc_collect calls gc_mark_roots (no-op when gc_vm is NULL and
     * no globals), then sweeps. The unreachable object should be freed.
     * Should not crash. */
    gc_unsuppress();
    gc_collect();

    /* Object was unreachable — it should be freed by sweep. */
    ASSERT(count_gc_objects() == 0, "unreachable object should be freed after collect");

    runtime_destroy();
    PASS();
}

/* ============================================================
 * test_gc_mark_roots_marks_stack: set up a mock VM with values
 * on the stack and verify gc_mark_roots marks them.
 *
 * Manually constructs a VM with a stack containing an ObjArray
 * value. Calls gc_mark_roots (via gc_collect) and verifies the
 * array is marked (then cleared by gc_collect's clear phase).
 * Since gc_collect clears marks, we call gc_mark_roots directly
 * to inspect the marks before clearing.
 * ============================================================ */

TEST(test_gc_mark_roots_marks_stack) {
    gc_init();
    gc_suppress();

    /* Create a GC-tracked array to put on the VM stack. */
    ObjArray *arr = (ObjArray *)gc_alloc(OBJ_ARRAY, sizeof(ObjArray));
    ASSERT(arr != NULL, "arr alloc should succeed");

    /* Set up a minimal VM with the array value on the stack. */
    VM vm;
    memset(&vm, 0, sizeof(VM));
    vm.stack_top = vm.stack;
    vm.frame_count = 0;
    vm.open_upvalues = NULL;

    /* Push a VAL_ARRAY onto the stack. */
    memset(&vm.stack[0], 0, sizeof(Value));
    vm.stack[0].type = VAL_ARRAY;
    vm.stack[0].array = arr;
    vm.stack_top = &vm.stack[1];

    /* Register the VM and call gc_mark_roots directly. */
    gc_set_vm(&vm);
    gc_unsuppress();
    gc_mark_roots();

    /* The array should be marked because it's on the VM stack. */
    ASSERT(arr->obj.is_marked == true, "stack array should be marked by gc_mark_roots");

    /* Clean up: unregister VM, zero out stack to prevent double-free. */
    gc_set_vm(NULL);
    memset(&vm.stack[0], 0, sizeof(Value));
    gc_free_all();
    PASS();
}

/* ============================================================
 * test_gc_mark_roots_marks_frame_closures: set up a mock VM
 * with a call frame closure and verify it gets marked.
 * ============================================================ */

TEST(test_gc_mark_roots_marks_frame_closures) {
    gc_init();
    gc_suppress();

    /* Create a GC-tracked function and closure. */
    ObjFunction *fn = (ObjFunction *)gc_alloc(OBJ_FUNCTION, sizeof(ObjFunction));
    ASSERT(fn != NULL, "fn alloc should succeed");

    fn->upvalue_count = 0;

    ObjClosure *cl = (ObjClosure *)gc_alloc(OBJ_CLOSURE, sizeof(ObjClosure));
    ASSERT(cl != NULL, "closure alloc should succeed");

    cl->function = fn;
    cl->upvalues = NULL;
    cl->upvalue_count = 0;

    /* Set up a minimal VM with one call frame using the closure. */
    VM vm;
    memset(&vm, 0, sizeof(VM));
    vm.stack_top = vm.stack;
    vm.open_upvalues = NULL;
    vm.frame_count = 1;
    vm.frames[0].closure = cl;
    vm.frames[0].ip = NULL;
    vm.frames[0].slots = vm.stack;

    /* Register the VM and call gc_mark_roots directly. */
    gc_set_vm(&vm);
    gc_unsuppress();
    gc_mark_roots();

    /* The closure and its function should be marked. */
    ASSERT(cl->obj.is_marked == true, "frame closure should be marked");
    ASSERT(fn->obj.is_marked == true, "closure's function should be marked");

    /* Clean up. */
    gc_set_vm(NULL);
    gc_free_all();
    PASS();
}

/* ============================================================
 * test_gc_mark_roots_marks_open_upvalues: set up a mock VM
 * with an open upvalue linked list and verify it gets marked.
 * ============================================================ */

TEST(test_gc_mark_roots_marks_open_upvalues) {
    gc_init();
    gc_suppress();

    /* Create a GC-tracked upvalue. */
    ObjUpvalue *uv = (ObjUpvalue *)gc_alloc(OBJ_UPVALUE, sizeof(ObjUpvalue));
    ASSERT(uv != NULL, "upvalue alloc should succeed");

    uv->next = NULL;
    /* Mark it as open: location points to some stack slot (not &closed). */
    memset(&uv->closed, 0, sizeof(Value));
    uv->location = &uv->closed; /* Reuse closed for simplicity — the key
                                 * thing is it's in the open upvalue list. */

    /* Actually, to make it an open upvalue, location should NOT point
     * to &closed. Let's use a stack value instead. */
    Value stack_val = {0};
    uv->location = &stack_val;

    /* Set up a minimal VM with the upvalue in the open list. */
    VM vm;
    memset(&vm, 0, sizeof(VM));
    vm.stack_top = vm.stack;
    vm.frame_count = 0;
    vm.open_upvalues = uv;

    /* Register the VM and call gc_mark_roots directly. */
    gc_set_vm(&vm);
    gc_unsuppress();
    gc_mark_roots();

    /* The upvalue should be marked because it's in the open upvalue list. */
    ASSERT(uv->obj.is_marked == true, "open upvalue should be marked");

    /* Clean up. */
    gc_set_vm(NULL);
    gc_free_all();
    PASS();
}

/* ============================================================
 * test_gc_mark_roots_marks_globals: define a global variable
 * holding a GC-tracked object, then verify gc_mark_roots marks
 * it via runtime_mark_globals.
 *
 * With no VM active, globals are the only root set. After
 * gc_mark_roots, the object stored in the global should be
 * marked. After cleanup via runtime_destroy, the global is
 * removed and the object list is freed.
 * ============================================================ */

TEST(test_gc_mark_roots_marks_globals) {
    /* runtime_init calls gc_init internally. */
    runtime_init();
    gc_suppress();

    /* Ensure no VM is active — only globals should be roots. */
    gc_set_vm(NULL);

    /* Allocate a GC-tracked array. */
    ObjArray *arr = (ObjArray *)gc_alloc(OBJ_ARRAY, sizeof(ObjArray));
    ASSERT(arr != NULL, "arr alloc should succeed");

    /* Build a VAL_ARRAY value referencing the GC-tracked object. */
    Value val;
    memset(&val, 0, sizeof(Value));
    val.type = VAL_ARRAY;
    val.array = arr;

    /* Store the value in a global variable. runtime_var_define
     * clones the value (shallow copy — same pointer). */
    RuntimeVarStatus st = runtime_var_define("test_arr", &val);
    ASSERT(st == RUNTIME_VAR_OK, "runtime_var_define should succeed");

    /* The clone inside var_table also points to arr (shared pointer).
     * Call gc_mark_roots — runtime_mark_globals should mark arr. */
    gc_unsuppress();
    gc_mark_roots();

    ASSERT(arr->obj.is_marked == true, "global variable's array should be marked by gc_mark_roots");

    /* Clean up: runtime_destroy clears var_table and calls gc_free_all.
     * We need to decrement our local reference first. */
    arr->obj.is_marked = false; /* Reset mark so gc_free_all is clean. */
    runtime_destroy();
    PASS();
}

/* ============================================================
 * test_gc_mark_native_function_null_chunk: native ObjFunctions
 * have chunk == NULL. gc_mark_object must handle this by
 * skipping the constant pool iteration. Verify no crash.
 * ============================================================ */

TEST(test_gc_mark_native_function_null_chunk) {
    gc_init();
    gc_suppress();

    /* Allocate an ObjFunction and simulate a native function
     * (chunk == NULL, native != NULL). gc_alloc calloc-zeroes
     * all fields, so chunk is already NULL. */
    ObjFunction *fn = (ObjFunction *)gc_alloc(OBJ_FUNCTION, sizeof(ObjFunction));
    ASSERT(fn != NULL, "fn alloc should succeed");

    fn->chunk = NULL; /* Explicitly: this is a native function. */

    /* Mark the native function — should not crash or dereference
     * fn->chunk. The OBJ_FUNCTION case in gc_mark_object guards
     * with `if (fn->chunk != NULL)`. */
    gc_mark_object((Obj *)fn);
    ASSERT(fn->obj.is_marked == true, "native function should be marked");

    gc_free_all();
    PASS();
}

/* ============================================================
 * test_gc_mark_stack_allocated_script_objects: verify that
 * stack-allocated ObjFunction and ObjClosure (as used by
 * vm_execute's frame 0) can be safely marked via gc_mark_roots.
 *
 * These objects are NOT on the GC object list. gc_mark_object
 * sets is_marked on them (harmless) and traces their children.
 * The key correctness requirement is that the .obj.type field
 * is set correctly so gc_mark_object dispatches to the right
 * case (OBJ_CLOSURE, not OBJ_FUNCTION).
 * ============================================================ */

TEST(test_gc_mark_stack_allocated_script_objects) {
    gc_init();
    gc_suppress();

    /* Simulate the stack-allocated script objects from vm_execute.
     * The key difference from heap-allocated objects: these are NOT
     * on the GC list (gc_alloc was not called). */
    ObjFunction script_fn = {
        .obj = {.type = OBJ_FUNCTION},
        .name = NULL,
        .arity = 0,
        .upvalue_count = 0,
        .params = NULL,
        .chunk = NULL, /* Simulating: no constants to trace. */
        .native = NULL,
    };

    ObjClosure script_closure = {
        .obj = {.type = OBJ_CLOSURE},
        .function = &script_fn,
        .upvalues = NULL,
        .upvalue_count = 0,
    };

    /* Set up a minimal VM with frame 0 pointing to the stack closure. */
    VM vm;
    memset(&vm, 0, sizeof(VM));
    vm.stack_top = vm.stack;
    vm.open_upvalues = NULL;
    vm.frame_count = 1;
    vm.frames[0].closure = &script_closure;
    vm.frames[0].ip = NULL;
    vm.frames[0].slots = vm.stack;

    /* Register the VM and mark roots. */
    gc_set_vm(&vm);
    gc_unsuppress();
    gc_mark_roots();

    /* The stack-allocated closure and function should be marked.
     * Since they're not on the GC list, they won't be cleared by
     * gc_collect's clear phase — we check marks directly. */
    ASSERT(script_closure.obj.is_marked == true, "stack-allocated script_closure should be marked");
    ASSERT(script_fn.obj.is_marked == true,
           "stack-allocated script_fn should be marked (via closure tracing)");

    /* Verify they are NOT in the GC object list. */
    ASSERT(!obj_in_list((Obj *)&script_closure), "script_closure should NOT be in the GC list");
    ASSERT(!obj_in_list((Obj *)&script_fn), "script_fn should NOT be in the GC list");

    gc_set_vm(NULL);
    gc_free_all();
    PASS();
}

/* ============================================================
 * test_gc_sweep_frees_unmarked: allocate 3 objects, mark one,
 * call gc_sweep(). Verify only the marked object remains.
 * ============================================================ */

TEST(test_gc_sweep_frees_unmarked) {
    gc_init();
    gc_suppress();

    /* Allocate three objects. */
    ObjArray *a1 = (ObjArray *)gc_alloc(OBJ_ARRAY, sizeof(ObjArray));
    ObjArray *a2 = (ObjArray *)gc_alloc(OBJ_ARRAY, sizeof(ObjArray));
    ObjArray *a3 = (ObjArray *)gc_alloc(OBJ_ARRAY, sizeof(ObjArray));
    ASSERT(a1 != NULL, "a1 alloc should succeed");
    ASSERT(a2 != NULL, "a2 alloc should succeed");
    ASSERT(a3 != NULL, "a3 alloc should succeed");
    ASSERT(count_gc_objects() == 3, "should have 3 objects before sweep");

    /* Mark only a2 as reachable. */
    a2->obj.is_marked = true;

    /* Sweep should free a1 and a3 (unmarked), keeping a2. */
    gc_unsuppress();
    gc_sweep();

    ASSERT(count_gc_objects() == 1, "only 1 object should remain after sweep");
    ASSERT(obj_in_list((Obj *)a2), "marked object (a2) should survive sweep");
    ASSERT(!obj_in_list((Obj *)a1), "unmarked object (a1) should be freed by sweep");
    ASSERT(!obj_in_list((Obj *)a3), "unmarked object (a3) should be freed by sweep");

    /* bytes_allocated should reflect only the surviving object. */
    ASSERT(gc_get_bytes_allocated() == sizeof(ObjArray),
           "bytes_allocated should equal one ObjArray after sweep");

    gc_free_all();
    PASS();
}

/* ============================================================
 * test_gc_sweep_clears_marks: allocate and mark objects, sweep,
 * then verify surviving objects have is_marked == false.
 * ============================================================ */

TEST(test_gc_sweep_clears_marks) {
    gc_init();
    gc_suppress();

    /* Allocate two objects and mark both. */
    ObjArray *a1 = (ObjArray *)gc_alloc(OBJ_ARRAY, sizeof(ObjArray));
    ObjArray *a2 = (ObjArray *)gc_alloc(OBJ_ARRAY, sizeof(ObjArray));
    ASSERT(a1 != NULL, "a1 alloc should succeed");
    ASSERT(a2 != NULL, "a2 alloc should succeed");

    a1->obj.is_marked = true;
    a2->obj.is_marked = true;

    /* Sweep: both are marked, so both survive. But marks should be
     * cleared on surviving objects for the next collection cycle. */
    gc_unsuppress();
    gc_sweep();

    ASSERT(count_gc_objects() == 2, "both marked objects should survive sweep");
    ASSERT(a1->obj.is_marked == false, "a1 is_marked should be cleared after sweep");
    ASSERT(a2->obj.is_marked == false, "a2 is_marked should be cleared after sweep");

    gc_free_all();
    PASS();
}

/* ============================================================
 * test_gc_collect_full_cycle: set up runtime with a global
 * variable referencing one object. Allocate several objects,
 * some reachable from the global, some not. Call gc_collect().
 * Verify unreachable objects are freed, reachable ones survive.
 * ============================================================ */

TEST(test_gc_collect_full_cycle) {
    /* runtime_init calls gc_init internally. */
    runtime_init();
    gc_suppress();

    /* Ensure no VM is active — globals are the only root set. */
    gc_set_vm(NULL);

    /* Allocate a GC-tracked array that will be reachable via a global. */
    ObjArray *reachable = (ObjArray *)gc_alloc(OBJ_ARRAY, sizeof(ObjArray));
    ASSERT(reachable != NULL, "reachable alloc should succeed");

    /* Store the reachable array in a global variable. */
    Value val;
    memset(&val, 0, sizeof(Value));
    val.type = VAL_ARRAY;
    val.array = reachable;
    RuntimeVarStatus st = runtime_var_define("keep_me", &val);
    ASSERT(st == RUNTIME_VAR_OK, "runtime_var_define should succeed");

    /* Allocate unreachable objects (not stored in any root). */
    ObjArray *unreachable1 = (ObjArray *)gc_alloc(OBJ_ARRAY, sizeof(ObjArray));
    ObjArray *unreachable2 = (ObjArray *)gc_alloc(OBJ_ARRAY, sizeof(ObjArray));
    ASSERT(unreachable1 != NULL, "unreachable1 alloc should succeed");
    ASSERT(unreachable2 != NULL, "unreachable2 alloc should succeed");

    int before_count = count_gc_objects();
    ASSERT(before_count >= 3, "should have at least 3 objects before collect");

    /* Run a full GC cycle. With sweep implemented, unreachable objects
     * should be freed. Without sweep, all objects remain. */
    gc_unsuppress();
    gc_collect();

    /* After a full collect cycle, reachable object must survive. */
    ASSERT(obj_in_list((Obj *)reachable), "reachable object should survive gc_collect");

    /* Unreachable objects should be freed by sweep. If sweep is a stub,
     * they will still be present — this test detects that failure. */
    ASSERT(!obj_in_list((Obj *)unreachable1), "unreachable1 should be freed by gc_collect");
    ASSERT(!obj_in_list((Obj *)unreachable2), "unreachable2 should be freed by gc_collect");

    runtime_destroy();
    PASS();
}

/* ============================================================
 * test_gc_auto_trigger: set next_gc to a low value, allocate
 * objects until a collection is triggered. Verify that
 * bytes_allocated decreased (objects were freed).
 * ============================================================ */

TEST(test_gc_auto_trigger) {
    /* Use the runtime so gc_collect can call runtime_mark_globals. */
    runtime_init();
    gc_set_vm(NULL);

    /* Set a very low threshold so the next allocation triggers GC. */
    gc_set_next_gc(1);

    /* Record bytes before allocations. */
    size_t bytes_before = gc_get_bytes_allocated();

    /* Allocate several unreachable objects. Each gc_alloc checks
     * bytes_allocated > next_gc and calls gc_collect() if true.
     * Once sweep is implemented, unreachable objects will be freed
     * during these triggered collections.
     * GC must NOT be suppressed here — the whole point is auto-triggering. */
    gc_unsuppress();
    for (int i = 0; i < 10; i++) {
        gc_alloc(OBJ_ARRAY, sizeof(ObjArray));
    }

    size_t bytes_after = gc_get_bytes_allocated();

    /* If auto-triggering + sweep work, bytes_allocated should be
     * less than 10 * sizeof(ObjArray) because intermediate objects
     * were collected. With the stub sweep, all 10 survive and
     * bytes_after will equal bytes_before + 10 * sizeof(ObjArray).
     * We test that automatic collection reduced the total. */
    size_t max_expected = bytes_before + 10 * sizeof(ObjArray);
    ASSERT(bytes_after < max_expected,
           "auto-triggered GC should reduce bytes_allocated below maximum");

    runtime_destroy();
    PASS();
}

/* ============================================================
 * test_gc_threshold_grows: after a collection, verify that
 * next_gc is larger than before (proportional to surviving bytes).
 * ============================================================ */

TEST(test_gc_threshold_grows) {
    /* Use the runtime so gc_collect can call runtime_mark_globals. */
    runtime_init();
    gc_suppress();
    gc_set_vm(NULL);

    /* Allocate a reachable object (stored in a global). */
    ObjArray *arr = (ObjArray *)gc_alloc(OBJ_ARRAY, sizeof(ObjArray));
    ASSERT(arr != NULL, "arr alloc should succeed");

    Value val;
    memset(&val, 0, sizeof(Value));
    val.type = VAL_ARRAY;
    val.array = arr;
    RuntimeVarStatus st = runtime_var_define("threshold_test", &val);
    ASSERT(st == RUNTIME_VAR_OK, "runtime_var_define should succeed");

    /* Set next_gc to a small value so we can observe it grow. */
    gc_set_next_gc(1);
    size_t next_gc_before = gc_get_next_gc();

    /* Run a collection. After sweep, next_gc should be updated
     * to a value proportional to surviving bytes. */
    gc_unsuppress();
    gc_collect();

    size_t next_gc_after = gc_get_next_gc();

    /* next_gc should grow after collection to at least the minimum
     * threshold or proportional to surviving bytes. With the stub
     * sweep, gc_collect doesn't update next_gc, so this will fail. */
    ASSERT(next_gc_after > next_gc_before, "next_gc should grow after a collection cycle");

    runtime_destroy();
    PASS();
}

/* ============================================================
 * String interning tests
 *
 * These tests verify the string interning semantics described in
 * the gc-sweep plan (step 5-6). Since interning isn't implemented
 * yet, most will FAIL — they're written ahead of implementation.
 *
 * String interning guarantees: two calls to obj_string_new() with
 * the same content return the same ObjString* pointer. This enables
 * O(1) pointer equality for strings and eliminates duplicate storage.
 * ============================================================ */

/* test_intern_same_content_same_pointer: two obj_string_new calls
 * with identical content should return the same ObjString pointer
 * (once interning is implemented). */
TEST(test_intern_same_content_same_pointer) {
    gc_init();
    gc_suppress();

    ObjString *s1 = obj_string_new("hello", 5);
    ObjString *s2 = obj_string_new("hello", 5);
    ASSERT(s1 != NULL, "s1 should be non-NULL");
    ASSERT(s2 != NULL, "s2 should be non-NULL");

    /* With interning, both should be the exact same pointer. */
    ASSERT(s1 == s2, "same content should yield same ObjString pointer");

    gc_free_all();
    PASS();
}

/* test_intern_different_content_different_pointer: two obj_string_new
 * calls with different content should return different ObjString
 * pointers. */
TEST(test_intern_different_content_different_pointer) {
    gc_init();
    gc_suppress();

    ObjString *s1 = obj_string_new("hello", 5);
    ObjString *s2 = obj_string_new("world", 5);
    ASSERT(s1 != NULL, "s1 should be non-NULL");
    ASSERT(s2 != NULL, "s2 should be non-NULL");

    /* Different content must always produce different pointers. */
    ASSERT(s1 != s2, "different content should yield different ObjString pointers");

    gc_free_all();
    PASS();
}

/* test_intern_string_take_deduplicates: obj_string_take with content
 * matching an existing interned string should free the input buffer
 * and return the existing ObjString pointer.
 *
 * We can't directly verify the buffer was freed, but we verify that
 * the returned pointer matches the pre-existing interned string. */
TEST(test_intern_string_take_deduplicates) {
    gc_init();
    gc_suppress();

    /* Create the first string via obj_string_new (becomes interned). */
    ObjString *s1 = obj_string_new("dedup", 5);
    ASSERT(s1 != NULL, "s1 should be non-NULL");

    /* Allocate a buffer with matching content and call obj_string_take.
     * If interning works, this buffer should be freed and s1 returned. */
    char *buf = strdup("dedup");
    ASSERT(buf != NULL, "strdup should succeed");
    ObjString *s2 = obj_string_take(buf, 5);
    ASSERT(s2 != NULL, "s2 should be non-NULL");

    /* With interning, obj_string_take should return the existing string. */
    ASSERT(s1 == s2, "obj_string_take should return existing interned string");

    gc_free_all();
    PASS();
}

/* test_intern_value_equal_pointer_compare: two VAL_STRING values
 * with the same content should be equal via value_equal(), and
 * with interning they should share the same ObjString pointer
 * (enabling O(1) pointer comparison). */
TEST(test_intern_value_equal_pointer_compare) {
    gc_init();
    gc_suppress();

    /* Create two string values with the same content. */
    Value v1 = make_string_copy("compare", 7);
    Value v2 = make_string_copy("compare", 7);
    ASSERT(v1.type == VAL_STRING, "v1 should be VAL_STRING");
    ASSERT(v2.type == VAL_STRING, "v2 should be VAL_STRING");

    /* value_equal should return true (works even without interning
     * because of the strcmp fallback, but with interning the pointer
     * comparison fast path handles it). */
    ASSERT(value_equal(&v1, &v2), "same-content strings should be value_equal");

    /* With interning, the ObjString pointers should be identical —
     * this is the key invariant that makes pointer comparison valid. */
    ASSERT(v1.string == v2.string, "same-content string Values should share ObjString pointer");

    gc_free_all();
    PASS();
}

/* test_intern_dead_strings_swept: create a string, make it
 * unreachable, trigger GC. After sweep, the string should be
 * removed from the intern table. Creating a new string with the
 * same content should produce a *new* ObjString, not the old one.
 *
 * Requires runtime_init() because gc_collect calls runtime_mark_globals. */
TEST(test_intern_dead_strings_swept) {
    /* runtime_init calls gc_init internally. */
    runtime_init();
    gc_suppress();
    gc_set_vm(NULL);

    /* Create a string — it's only referenced by our local pointer. */
    ObjString *s1 = obj_string_new("ephemeral", 9);
    ASSERT(s1 != NULL, "s1 should be non-NULL");

    /* Make the string unreachable: we don't store it in any root.
     * Force a GC cycle — sweep should free the unreachable string
     * and remove it from the intern table. */
    gc_unsuppress();
    gc_collect();

    /* Re-suppress GC so subsequent allocations aren't swept by
     * GC_STRESS before we can inspect them. */
    gc_suppress();

    /* After sweep, s1 is dangling — do NOT dereference it.
     *
     * Create a new string with the same content. If the intern table
     * was properly cleaned, this should be a fresh ObjString (not
     * a dangling pointer to the freed s1). */
    ObjString *s2 = obj_string_new("ephemeral", 9);
    ASSERT(s2 != NULL, "s2 should be non-NULL");

    /* Verify the new string is valid and usable. If the intern table
     * still held a dead entry, gc_intern_find would return the freed
     * pointer — a use-after-free bug. The content and length checks
     * catch this: freed memory would not contain valid data.
     *
     * Note: we intentionally do NOT assert s2 != s1.  The allocator
     * may legitimately reuse the same address for a fresh allocation
     * after s1 was freed, which is fine as long as the intern table
     * was properly cleaned. */
    ASSERT(strcmp(s2->chars, "ephemeral") == 0, "new string should have correct content");
    ASSERT(s2->length == 9, "new string should have correct length");

    runtime_destroy();
    PASS();
}

/* test_intern_clone_shares_pointer: value_clone of a VAL_STRING
 * should return a Value pointing to the same ObjString pointer
 * (no new allocation needed — interned strings are shared).
 *
 * This test will fail until step 6 changes value_clone to share
 * the pointer instead of deep-copying. */
TEST(test_intern_clone_shares_pointer) {
    gc_init();
    gc_suppress();

    /* Create a string value. */
    Value src = make_string_copy("shared", 6);
    ASSERT(src.type == VAL_STRING, "src should be VAL_STRING");
    ASSERT(src.string != NULL, "src.string should be non-NULL");

    /* Clone the value. */
    Value cloned;
    memset(&cloned, 0, sizeof(Value));
    bool ok = value_clone(&cloned, &src);
    ASSERT(ok, "value_clone should succeed");
    ASSERT(cloned.type == VAL_STRING, "cloned should be VAL_STRING");
    ASSERT(cloned.string != NULL, "cloned.string should be non-NULL");

    /* With interning, value_clone should share the pointer — no
     * new ObjString allocation. Before step 6, value_clone deep-copies
     * so this assertion will fail. */
    ASSERT(src.string == cloned.string, "value_clone of VAL_STRING should share ObjString pointer");

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

    printf("\nGC root marking:\n");
    RUN_TEST(test_gc_set_vm_null_safe);
    RUN_TEST(test_gc_mark_roots_marks_stack);
    RUN_TEST(test_gc_mark_roots_marks_frame_closures);
    RUN_TEST(test_gc_mark_roots_marks_open_upvalues);
    RUN_TEST(test_gc_mark_roots_marks_globals);

    printf("\nGC edge cases:\n");
    RUN_TEST(test_gc_mark_native_function_null_chunk);
    RUN_TEST(test_gc_mark_stack_allocated_script_objects);

    printf("\nGC sweep phase:\n");
    RUN_TEST(test_gc_sweep_frees_unmarked);
    RUN_TEST(test_gc_sweep_clears_marks);
    RUN_TEST(test_gc_collect_full_cycle);
    RUN_TEST(test_gc_auto_trigger);
    RUN_TEST(test_gc_threshold_grows);

    printf("\nString interning:\n");
    RUN_TEST(test_intern_same_content_same_pointer);
    RUN_TEST(test_intern_different_content_different_pointer);
    RUN_TEST(test_intern_string_take_deduplicates);
    RUN_TEST(test_intern_value_equal_pointer_compare);
    RUN_TEST(test_intern_dead_strings_swept);
    RUN_TEST(test_intern_clone_shares_pointer);

    printf("\n========================================\n");
    printf("Tests run: %d\n", tests_run);
    printf("Passed:    %d\n", tests_passed);
    printf("Failed:    %d\n", tests_failed);
    printf("========================================\n");

    return tests_failed > 0 ? 1 : 0;
}
