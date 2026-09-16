#include "g.h"
#include "astc.h"
#include "astg.h"
#include "buildfe.h"
#include <assert.h>

typedef struct math_object {
    id base;
    int bias;
} math_object;
typedef void (*math_method)(ptr self, int a, int b, int *ret);

static void domath(ptr self, int a, int b, int *ret) {
    math_object *object = self;
    *ret = a + b + object->bias;
}
static func math_lookup(str name) {
    static const u8 selector[] = {'d', 'o', 'm', 'a', 't', 'h'};
    if (name.len == sizeof(selector) && memcmp(name.data, selector, sizeof(selector)) == 0)
        return (func)domath;
    return (func)0;
}
static const class math_descriptor = {math_lookup};
const class *runtime_math_class = &math_descriptor;

static int cleanup_calls;
static ptr expected_payload;
static rc *released_box;
static void cleanup(ptr data) {
    assert(data == expected_payload);
    assert(released_box->count == 0 && released_box->data == NULL);
    assert(rc_release(released_box, cleanup) == G_INVALID);
    ++cleanup_calls;
}
static void test_strings_dispatch(void) {
    char terminated[] = "Hello";
    char no_null[] = {'H', 'e', 'l', 'l', 'o'};
    cstr c = terminated;
    str s = {(u8 *)no_null, sizeof(no_null)};
    u8 selector[] = {'d', 'o', 'm', 'a', 't', 'h'};
    str name = {selector, sizeof(selector)};
    math_object object = {{runtime_math_class}, 7};
    int result = 0;
    func method = id_lookup(&object.base, name);
    assert(strlen(c) == 5 && sizeof(terminated) == 6);
    assert(s.len == 5 && sizeof(no_null) == 5);
    assert(method);
    ((math_method)method)(&object, 2, 3, &result);
    assert(result == 12);
    name.len = 3;
    assert(id_lookup(&object.base, name) == (func)0);
    name.data = NULL;
    assert(id_lookup(&object.base, name) == (func)0);
    assert(id_lookup(NULL, s) == (func)0);
    object.base.cls = NULL;
    assert(id_lookup(&object.base, s) == (func)0);
}
static void test_reference_counts(void) {
    rc box = {0};
    int plain = 1;
    math_object object = {{runtime_math_class}, 0};
    assert(rc_count(NULL) == 0 && !rc_is_object(NULL));
    assert(rc_box(NULL, &plain, 0) == G_INVALID);
    assert(rc_box(&box, NULL, 0) == G_INVALID);
    assert(rc_retain(&box) == G_INVALID);
    assert(rc_release(&box, cleanup) == G_INVALID);
    for (u8 object_mode = 0; object_mode < 2; ++object_mode) {
        ptr payload = object_mode ? (ptr)&object : (ptr)&plain;
        expected_payload = payload;
        released_box = &box;
        assert(rc_box(&box, payload, object_mode) == G_OK);
        assert(box.count == (object_mode ? -1 : 1));
        assert(rc_is_object(&box) == object_mode && rc_count(&box) == 1);
        assert(rc_box(&box, &plain, 0) == G_INVALID);
        assert(rc_retain(&box) == G_OK && rc_count(&box) == 2);
        assert(box.count == (object_mode ? -2 : 2));
        assert(rc_release(&box, NULL) == G_INVALID && rc_count(&box) == 2);
        assert(rc_release(&box, cleanup) == G_OK && rc_count(&box) == 1);
        assert(cleanup_calls == object_mode);
        assert(rc_release(&box, cleanup) == G_OK);
        assert(cleanup_calls == object_mode + 1 && rc_count(&box) == 0);
        assert(rc_release(&box, cleanup) == G_INVALID);
    }
    assert(rc_box(&box, &plain, 0) == G_OK);
    box.count = INTPTR_MAX;
    assert(rc_retain(&box) == G_OVERFLOW && box.count == INTPTR_MAX);
    assert(rc_release(&box, cleanup) == G_OK && box.count == INTPTR_MAX - 1);
    box.count = 1;
    expected_payload = &plain;
    assert(rc_release(&box, cleanup) == G_OK);

    assert(rc_box(&box, &object, 1) == G_OK);
    box.count = INTPTR_MIN + 1;
    assert(rc_retain(&box) == G_OK && box.count == INTPTR_MIN);
    assert(rc_count(&box) == (usize)INTPTR_MAX + 1);
    assert(rc_retain(&box) == G_OVERFLOW && box.count == INTPTR_MIN);
    assert(rc_release(&box, cleanup) == G_OK && box.count == INTPTR_MIN + 1);
    box.count = -1;
    expected_payload = &object;
    assert(rc_release(&box, cleanup) == G_OK);
    assert(cleanup_calls == 4);
}
static void test_count_len(void) {
    f64 values[10] = {0};
    g_array array;
    g_vec vector;
    assert(g_array_init(&array, values, 10, sizeof(f64)) == G_OK);
    assert(array.count == 10 && array.len == sizeof(f64));
    assert(array.count * array.len == sizeof(values));
    assert(g_vec_init(&vector, sizeof(f64), NULL) == G_OK);
    assert(g_vec_push(&vector, &values[0]) == G_OK);
    assert(vector.count == 1 && vector.len == sizeof(f64));
    g_vec_destroy(&vector);
}
int main(void) {
    test_strings_dispatch();
    test_reference_counts();
    test_count_len();
    return 0;
}
