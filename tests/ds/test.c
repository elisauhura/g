#include "g.h"
#include "astc.h"
#include "astg.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct test_allocator { usize calls, fail_at, live; } test_allocator;
static void *test_alloc(void *context, usize bytes) {
    test_allocator *a = (test_allocator *)context;
    void *p;
    ++a->calls;
    if (a->fail_at && a->calls == a->fail_at) return NULL;
    p = malloc((size_t)bytes);
    if (p) ++a->live;
    return p;
}
static void test_free(void *context, void *p) {
    test_allocator *a = (test_allocator *)context;
    assert(p && a->live);
    --a->live;
    free(p);
}
static g_allocator allocator_for(test_allocator *a) {
    g_allocator result = {a, test_alloc, test_free};
    return result;
}
static u64 collide(const void *key, usize size, void *context) {
    (void)key; (void)size; (void)context;
    return 1;
}
static void test_arrays_vectors(void) {
    int storage[] = {1, 2, 3}, value, i;
    g_array array;
    g_vec vector;
    test_allocator state = {0};
    g_allocator a = allocator_for(&state);
    assert(g_array_init(&array, storage, 3, sizeof(int)) == G_OK);
    assert(*(const int *)g_array_at_const(&array, 1) == 2);
    assert(g_array_at(&array, 3) == NULL);
    assert(g_array_init(&array, NULL, 1, sizeof(int)) == G_INVALID);
    assert(g_array_init(&array, storage, (usize)SIZE_MAX, 2) == G_OVERFLOW);
    assert(g_vec_init(&vector, sizeof(int), &a) == G_OK);
    assert(g_vec_pop(&vector, NULL) == G_NOT_FOUND);
    for (i = 0; i < 4; ++i) assert(g_vec_push(&vector, &i) == G_OK);
    /* Internal source survives reallocation. */
    assert(g_vec_push(&vector, g_vec_at(&vector, 2)) == G_OK);
    assert(*(int *)g_vec_at(&vector, 4) == 2);
    for (i = 5; i < 1000; ++i) assert(g_vec_push(&vector, &i) == G_OK);
    assert(g_vec_at(&vector, vector.len) == NULL);
    for (i = 999; i >= 5; --i) {
        assert(g_vec_pop(&vector, &value) == G_OK);
        assert(value == i);
    }
    assert(g_vec_reserve(&vector, (usize)SIZE_MAX) == G_OVERFLOW);
    state.fail_at = state.calls + 1;
    assert(g_vec_reserve(&vector, vector.capacity + 1) == G_OUT_OF_MEMORY);
    assert(vector.len == 5 && *(int *)g_vec_at(&vector, 4) == 2);
    g_vec_clear(&vector);
    assert(vector.len == 0 && vector.capacity > 0);
    g_vec_destroy(&vector);
    assert(state.live == 0);

    state.fail_at = 0;
    assert(g_vec_init(&vector, sizeof(int), &a) == G_OK);
    for (i = 0; i < 4; ++i) assert(g_vec_push(&vector, &i) == G_OK);
    state.fail_at = state.calls + 1;
    assert(g_vec_push(&vector, &i) == G_OUT_OF_MEMORY);
    assert(vector.len == 4 && *(int *)g_vec_at(&vector, 3) == 3);
    g_vec_destroy(&vector);
    assert(state.live == 0);
}
static void test_rings(void) {
    int storage[3], i, value;
    g_ring ring;
    test_allocator state = {0};
    g_allocator a = allocator_for(&state);
    assert(g_ring_init_fixed(&ring, storage, 3, sizeof(int)) == G_OK);
    for (i = 0; i < 3; ++i) assert(g_ring_push_back(&ring, &i) == G_OK);
    assert(g_ring_push_back(&ring, &i) == G_FULL);
    assert(g_ring_pop_front(&ring, &value) == G_OK && value == 0);
    assert(g_ring_push_back(&ring, &i) == G_OK);
    assert(g_ring_pop_back(&ring, &value) == G_OK && value == 3);
    assert(g_ring_pop_front(&ring, &value) == G_OK && value == 1);
    assert(g_ring_pop_front(&ring, &value) == G_OK && value == 2);
    assert(g_ring_pop_back(&ring, &value) == G_NOT_FOUND);
    g_ring_destroy(&ring); /* Must not free stack storage. */
    assert(g_ring_init_fixed(&ring, NULL, 0, sizeof(int)) == G_OK);
    assert(g_ring_push_back(&ring, &value) == G_FULL);
    g_ring_destroy(&ring);

    assert(g_ring_init_dynamic(&ring, sizeof(int), &a) == G_OK);
    for (i = 0; i < 4; ++i) assert(g_ring_push_back(&ring, &i) == G_OK);
    assert(g_ring_pop_front(&ring, &value) == G_OK && value == 0);
    assert(g_ring_pop_front(&ring, &value) == G_OK && value == 1);
    for (i = 4; i < 6; ++i) assert(g_ring_push_back(&ring, &i) == G_OK);
    state.fail_at = state.calls + 1;
    assert(g_ring_push_back(&ring, &i) == G_OUT_OF_MEMORY);
    assert(ring.len == 4 && *(int *)g_ring_at(&ring, 0) == 2);
    state.fail_at = 0;
    /* Growth from a wrapped ring, with an internal source. */
    assert(g_ring_push_back(&ring, g_ring_at(&ring, 1)) == G_OK);
    for (i = 2; i < 6; ++i) {
        assert(g_ring_pop_front(&ring, &value) == G_OK);
        assert(value == i);
    }
    assert(g_ring_pop_back(&ring, &value) == G_OK && value == 3);
    assert(g_ring_at(&ring, 0) == NULL);
    for (i = 0; i < 1000; ++i) assert(g_ring_push_back(&ring, &i) == G_OK);
    for (i = 999; i >= 0; --i) {
        assert(g_ring_pop_back(&ring, &value) == G_OK);
        assert(value == i);
    }
    g_ring_clear(&ring);
    g_ring_destroy(&ring);
    assert(state.live == 0);
}
static void test_dict_set(void) {
    g_dict dict;
    g_set set;
    int i, value;
    test_allocator state = {0};
    g_allocator a = allocator_for(&state);
    assert(g_dict_init(&dict, sizeof(int), sizeof(int), &a, collide, NULL, NULL) == G_INVALID);
    assert(g_dict_init(&dict, sizeof(int), sizeof(int), &a, collide, g_ds_equal, NULL) == G_OK);
    for (i = 0; i < 200; ++i) {
        value = i * 7;
        assert(g_dict_put(&dict, &i, &value) == G_OK);
    }
    assert(dict.len == 200);
    for (i = 0; i < 200; ++i)
        assert(*(const int *)g_dict_get_const(&dict, &i) == i * 7);
    i = 12; value = 900;
    assert(g_dict_put(&dict, &i, &value) == G_OK && dict.len == 200);
    assert(*(int *)g_dict_get(&dict, &i) == 900);
    for (i = 0; i < 200; i += 2) assert(g_dict_remove(&dict, &i) == G_OK);
    for (i = 0; i < 200; ++i) assert(g_dict_contains(&dict, &i) == (i % 2 != 0));
    i = 1000;
    assert(g_dict_get(&dict, &i) == NULL);
    assert(g_dict_remove(&dict, &i) == G_NOT_FOUND);
    g_dict_clear(&dict);
    assert(dict.len == 0);
    assert(g_dict_put(&dict, &i, &i) == G_OK);
    g_dict_destroy(&dict);
    assert(state.live == 0);

    assert(g_set_init(&set, sizeof(int), &a, NULL, NULL, NULL) == G_OK);
    for (i = 0; i < 100; ++i) {
        assert(g_set_add(&set, &i) == G_OK);
        assert(g_set_add(&set, &i) == G_OK);
    }
    assert(g_set_len(&set) == 100);
    for (i = 0; i < 100; ++i) {
        assert(g_set_contains(&set, &i));
        assert(g_set_remove(&set, &i) == G_OK);
        assert(!g_set_contains(&set, &i));
    }
    g_set_clear(&set);
    g_set_destroy(&set);
    assert(state.live == 0);
}
static void test_dict_failures(void) {
    usize failure;
    for (failure = 1; failure <= 4; ++failure) {
        g_dict dict;
        test_allocator state = {0};
        g_allocator a = allocator_for(&state);
        int i, value = 99;
        usize live;
        assert(g_dict_init(&dict, sizeof(int), sizeof(int), &a, NULL, NULL, NULL) == G_OK);
        for (i = 0; i < 6; ++i) assert(g_dict_put(&dict, &i, &i) == G_OK);
        live = state.live;
        state.fail_at = state.calls + failure;
        assert(g_dict_put(&dict, &value, &value) == G_OUT_OF_MEMORY);
        assert(dict.len == 6 && state.live == live);
        assert(!g_dict_contains(&dict, &value));
        for (i = 0; i < 6; ++i) assert(*(int *)g_dict_get(&dict, &i) == i);
        state.fail_at = 0;
        assert(g_dict_put(&dict, &value, &value) == G_OK);
        g_dict_destroy(&dict);
        assert(state.live == 0);
    }
}
static void test_default_allocator(void) {
    g_vec vector;
#ifdef G_NO_STDLIB_ALLOCATOR
    assert(g_vec_init(&vector, sizeof(int), NULL) == G_INVALID);
#else
    g_dict dict;
    g_ring ring;
    int key = 1, value = 42, out = 0;
    assert(g_vec_init(&vector, sizeof(int), NULL) == G_OK);
    assert(g_vec_push(&vector, &value) == G_OK);
    assert(g_vec_pop(&vector, &out) == G_OK && out == value);
    g_vec_destroy(&vector);
    assert(g_ring_init_dynamic(&ring, sizeof(int), NULL) == G_OK);
    assert(g_ring_push_back(&ring, &value) == G_OK);
    assert(g_ring_pop_front(&ring, &out) == G_OK && out == value);
    g_ring_destroy(&ring);
    assert(g_dict_init(&dict, sizeof(int), sizeof(int), NULL, NULL, NULL, NULL) == G_OK);
    assert(g_dict_put(&dict, &key, &value) == G_OK);
    assert(*(int *)g_dict_get(&dict, &key) == value);
    g_dict_destroy(&dict);
#endif
}

int main(void) {
    test_default_allocator();
    test_arrays_vectors();
    test_rings();
    test_dict_set();
    test_dict_failures();
    puts("Collection tests passed.");
    return 0;
}
