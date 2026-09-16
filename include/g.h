#pragma once

#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

/* Pointer-sized unsigned and signed integers. */
typedef uintptr_t usize;
typedef intptr_t isize;

typedef void *ptr;

typedef float f32;
typedef double f64;

/* f16 requires a backend-specific half-precision floating-point type. */

/*
 * Runtime collections. C arrays ([]T and size-hinted arrays) remain native C.
 * g_array is a borrowed length-bearing view; g_vec owns growable storage.
 * g_ring supports FIFO with pop_front and FILO with pop_back. A fixed ring
 * borrows supplied storage; a dynamic ring owns storage through its allocator.
 * g_dict/g_set use separate chaining, with copied keys and values.
 *
 * Initialize before use and destroy owned containers exactly once. Reinitializing
 * a live container leaks storage. Owning containers must not be copied by value.
 * Copies are shallow: no element destructor, refcount, or pool action is implied.
 * Mutating operations invalidate pointers into storage when it grows or is freed.
 * Containers are not thread-safe. Bytewise keys must have stable representations
 * (including padding); use paired hash/equal callbacks for semantic comparison.
 * Allocators must return storage suitably aligned for ordinary C types. Over-
 * aligned element types require separate support. Sizes must be sizeof(T).
 */
#include <stddef.h>
#include <string.h>
#ifndef G_NO_STDLIB_ALLOCATOR
#include <stdlib.h>
#endif

typedef enum g_result {
    G_OK,
    G_INVALID,
    G_RANGE,
    G_FULL,
    G_NOT_FOUND,
    G_OVERFLOW,
    G_OUT_OF_MEMORY
} g_result;

typedef struct g_allocator {
    void *context;
    void *(*allocate)(void *context, usize bytes);
    void (*deallocate)(void *context, void *memory);
} g_allocator;

#ifndef G_NO_STDLIB_ALLOCATOR
static inline void *g_ds_allocate(void *context, usize bytes) {
    (void)context;
    return malloc((size_t)bytes);
}
static inline void g_ds_deallocate(void *context, void *memory) {
    (void)context;
    free(memory);
}
#endif

/* With G_NO_STDLIB_ALLOCATOR, callers must provide a non-null allocator. */
static inline g_result g_ds_allocator(g_allocator *out, const g_allocator *in) {
    if (in) {
        if (!in->allocate || !in->deallocate) return G_INVALID;
        *out = *in;
        return G_OK;
    }
#ifndef G_NO_STDLIB_ALLOCATOR
    out->context = NULL;
    out->allocate = g_ds_allocate;
    out->deallocate = g_ds_deallocate;
    return G_OK;
#else
    (void)out;
    return G_INVALID;
#endif
}

static inline u8 g_ds_bytes(usize count, usize size, usize *bytes) {
    if (size && count > (usize)SIZE_MAX / size) return 0;
    *bytes = count * size;
    return 1;
}
static inline void g_ds_free(g_allocator *a, void *p) {
    if (p) a->deallocate(a->context, p);
}
static inline g_result g_ds_capacity(usize current, usize needed, usize size,
                                    usize *capacity) {
    usize limit = size ? (usize)SIZE_MAX / size : 0;
    usize next = current ? current : 4;
    if (!size || needed > limit) return G_OVERFLOW;
    if (next > limit) next = limit;
    while (next < needed) {
        if (next > limit / 2) { next = needed; break; }
        next *= 2;
    }
    *capacity = next;
    return G_OK;
}

typedef struct g_array {
    void *data;
    usize len;
    usize element_size;
} g_array;

static inline g_result g_array_init(g_array *a, void *data, usize len, usize size) {
    usize bytes;
    if (!a || !size || (len && !data)) return G_INVALID;
    if (!g_ds_bytes(len, size, &bytes)) return G_OVERFLOW;
    a->data = data;
    a->len = len;
    a->element_size = size;
    return G_OK;
}
static inline void *g_array_at(g_array *a, usize index) {
    return a && index < a->len ? (u8 *)a->data + index * a->element_size : NULL;
}
static inline const void *g_array_at_const(const g_array *a, usize index) {
    return a && index < a->len ? (const u8 *)a->data + index * a->element_size : NULL;
}

typedef struct g_vec {
    void *data;
    usize len;
    usize capacity;
    usize element_size;
    g_allocator allocator;
} g_vec;

static inline g_result g_vec_init(g_vec *v, usize size, const g_allocator *a) {
    g_vec next = {0};
    g_result r;
    if (!v || !size) return G_INVALID;
    r = g_ds_allocator(&next.allocator, a);
    if (r != G_OK) return r;
    next.element_size = size;
    *v = next;
    return G_OK;
}
static inline void g_vec_destroy(g_vec *v) {
    if (!v) return;
    g_ds_free(&v->allocator, v->data);
    memset(v, 0, sizeof(*v));
}
static inline g_result g_vec_reserve(g_vec *v, usize capacity) {
    usize bytes;
    void *data;
    if (!v || !v->element_size) return G_INVALID;
    if (capacity <= v->capacity) return G_OK;
    if (!g_ds_bytes(capacity, v->element_size, &bytes)) return G_OVERFLOW;
    data = v->allocator.allocate(v->allocator.context, bytes);
    if (!data) return G_OUT_OF_MEMORY;
    if (v->len) memcpy(data, v->data, v->len * v->element_size);
    g_ds_free(&v->allocator, v->data);
    v->data = data;
    v->capacity = capacity;
    return G_OK;
}
static inline void *g_vec_at(g_vec *v, usize index) {
    return v && index < v->len ? (u8 *)v->data + index * v->element_size : NULL;
}
static inline const void *g_vec_at_const(const g_vec *v, usize index) {
    return v && index < v->len ? (const u8 *)v->data + index * v->element_size : NULL;
}
static inline g_result g_vec_push(g_vec *v, const void *value) {
    if (!v || !v->element_size || !value) return G_INVALID;
    if (v->len == v->capacity) {
        usize capacity;
        void *data;
        g_result r;
        if (v->len == (usize)SIZE_MAX) return G_OVERFLOW;
        r = g_ds_capacity(v->capacity, v->len + 1, v->element_size, &capacity);
        if (r != G_OK) return r;
        data = v->allocator.allocate(v->allocator.context, capacity * v->element_size);
        if (!data) return G_OUT_OF_MEMORY;
        if (v->len) memcpy(data, v->data, v->len * v->element_size);
        /* Copy before freeing old storage, so pushing an existing element works. */
        memcpy((u8 *)data + v->len * v->element_size, value, v->element_size);
        g_ds_free(&v->allocator, v->data);
        v->data = data;
        v->capacity = capacity;
    } else {
        memmove((u8 *)v->data + v->len * v->element_size, value, v->element_size);
    }
    ++v->len;
    return G_OK;
}
/* out may be null to discard; when non-null it must be external storage. */
static inline g_result g_vec_pop(g_vec *v, void *out) {
    if (!v || !v->element_size) return G_INVALID;
    if (!v->len) return G_NOT_FOUND;
    if (out) memcpy(out, (u8 *)v->data + (v->len - 1) * v->element_size, v->element_size);
    --v->len;
    return G_OK;
}
static inline void g_vec_clear(g_vec *v) { if (v) v->len = 0; }

typedef struct g_ring {
    void *data;
    usize len;
    usize capacity;
    usize head;
    usize element_size;
    u8 is_dynamic;
    g_allocator allocator;
} g_ring;

/* Caller supplies storage aligned for T, with capacity * sizeof(T) bytes. */
static inline g_result g_ring_init_fixed(g_ring *r, void *data, usize capacity,
                                         usize size) {
    g_ring next = {0};
    usize bytes;
    if (!r || !size || (capacity && !data)) return G_INVALID;
    if (!g_ds_bytes(capacity, size, &bytes)) return G_OVERFLOW;
    next.data = data;
    next.capacity = capacity;
    next.element_size = size;
    *r = next;
    return G_OK;
}
static inline g_result g_ring_init_dynamic(g_ring *r, usize size,
                                           const g_allocator *a) {
    g_ring next = {0};
    g_result result;
    if (!r || !size) return G_INVALID;
    result = g_ds_allocator(&next.allocator, a);
    if (result != G_OK) return result;
    next.element_size = size;
    next.is_dynamic = 1;
    *r = next;
    return G_OK;
}
static inline void g_ring_destroy(g_ring *r) {
    if (!r) return;
    if (r->is_dynamic) g_ds_free(&r->allocator, r->data);
    memset(r, 0, sizeof(*r));
}
/* Offset is less than capacity; subtraction avoids head + offset overflow. */
static inline usize g_ds_ring_index(const g_ring *r, usize offset) {
    usize tail = r->capacity - r->head;
    return offset >= tail ? offset - tail : r->head + offset;
}
static inline void *g_ring_at(g_ring *r, usize index) {
    return r && index < r->len
        ? (u8 *)r->data + g_ds_ring_index(r, index) * r->element_size : NULL;
}
static inline const void *g_ring_at_const(const g_ring *r, usize index) {
    return r && index < r->len
        ? (const u8 *)r->data + g_ds_ring_index(r, index) * r->element_size : NULL;
}
static inline g_result g_ring_push_back(g_ring *r, const void *value) {
    if (!r || !r->element_size || !value) return G_INVALID;
    if (r->len == r->capacity) {
        usize capacity, i;
        void *data;
        g_result result;
        if (!r->is_dynamic) return G_FULL;
        if (r->len == (usize)SIZE_MAX) return G_OVERFLOW;
        result = g_ds_capacity(r->capacity, r->len + 1, r->element_size, &capacity);
        if (result != G_OK) return result;
        data = r->allocator.allocate(r->allocator.context, capacity * r->element_size);
        if (!data) return G_OUT_OF_MEMORY;
        for (i = 0; i < r->len; ++i)
            memcpy((u8 *)data + i * r->element_size, g_ring_at(r, i), r->element_size);
        memcpy((u8 *)data + r->len * r->element_size, value, r->element_size);
        g_ds_free(&r->allocator, r->data);
        r->data = data;
        r->capacity = capacity;
        r->head = 0;
    } else {
        memmove((u8 *)r->data + g_ds_ring_index(r, r->len) * r->element_size,
                value, r->element_size);
    }
    ++r->len;
    return G_OK;
}
/* Pop outputs, when supplied, must be external storage. */
static inline g_result g_ring_pop_front(g_ring *r, void *out) {
    if (!r || !r->element_size) return G_INVALID;
    if (!r->len) return G_NOT_FOUND;
    if (out) memcpy(out, g_ring_at(r, 0), r->element_size);
    --r->len;
    r->head = r->head == r->capacity - 1 ? 0 : r->head + 1;
    if (!r->len) r->head = 0;
    return G_OK;
}
static inline g_result g_ring_pop_back(g_ring *r, void *out) {
    if (!r || !r->element_size) return G_INVALID;
    if (!r->len) return G_NOT_FOUND;
    if (out) memcpy(out, g_ring_at(r, r->len - 1), r->element_size);
    --r->len;
    if (!r->len) r->head = 0;
    return G_OK;
}
static inline void g_ring_clear(g_ring *r) {
    if (r) { r->len = 0; r->head = 0; }
}

typedef u64 (*g_hash_fn)(const void *key, usize size, void *context);
typedef u8 (*g_equal_fn)(const void *a, const void *b, usize size, void *context);

typedef struct g_dict_entry {
    struct g_dict_entry *next;
    u64 hash;
    void *key;
    void *value;
} g_dict_entry;

typedef struct g_dict {
    g_dict_entry **buckets;
    usize capacity;
    usize len;
    usize key_size;
    usize value_size; /* Zero is used for sets. */
    g_allocator allocator;
    g_hash_fn hash;
    g_equal_fn equal;
    void *key_context;
} g_dict;

static inline u64 g_ds_hash(const void *key, usize size, void *context) {
    const u8 *bytes = (const u8 *)key;
    u64 hash = UINT64_C(14695981039346656037);
    usize i;
    (void)context;
    for (i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}
static inline u8 g_ds_equal(const void *a, const void *b, usize size, void *context) {
    (void)context;
    return (u8)(memcmp(a, b, (size_t)size) == 0);
}
/* Equal keys must hash identically; provide both callbacks or neither. */
static inline g_result g_dict_init(g_dict *d, usize key_size, usize value_size,
                                   const g_allocator *a, g_hash_fn hash,
                                   g_equal_fn equal, void *context) {
    g_dict next = {0};
    g_result r;
    if (!d || !key_size || (!!hash != !!equal)) return G_INVALID;
    r = g_ds_allocator(&next.allocator, a);
    if (r != G_OK) return r;
    next.key_size = key_size;
    next.value_size = value_size;
    next.hash = hash ? hash : g_ds_hash;
    next.equal = equal ? equal : g_ds_equal;
    next.key_context = context;
    *d = next;
    return G_OK;
}
static inline const g_dict_entry *g_ds_find(const g_dict *d, const void *key, u64 hash) {
    const g_dict_entry *entry;
    if (!d->capacity) return NULL;
    entry = d->buckets[hash % d->capacity];
    for (; entry; entry = entry->next)
        if (entry->hash == hash && d->equal(entry->key, key, d->key_size, d->key_context))
            return entry;
    return NULL;
}
static inline u8 g_dict_contains(const g_dict *d, const void *key) {
    if (!d || !d->key_size || !key) return 0;
    return (u8)(g_ds_find(d, key, d->hash(key, d->key_size, d->key_context)) != NULL);
}
static inline const void *g_dict_get_const(const g_dict *d, const void *key) {
    const g_dict_entry *entry;
    if (!d || !d->key_size || !key) return NULL;
    entry = g_ds_find(d, key, d->hash(key, d->key_size, d->key_context));
    return entry ? entry->value : NULL;
}
static inline void *g_dict_get(g_dict *d, const void *key) {
    return (void *)g_dict_get_const(d, key);
}
static inline void g_ds_entry_free(g_dict *d, g_dict_entry *entry) {
    g_ds_free(&d->allocator, entry->key);
    g_ds_free(&d->allocator, entry->value);
    g_ds_free(&d->allocator, entry);
}
/* Internal: new capacity is nonzero. Allocation failure leaves d unchanged. */
static inline g_result g_ds_rehash(g_dict *d, usize capacity) {
    usize bytes, i;
    g_dict_entry **buckets;
    if (!g_ds_bytes(capacity, sizeof(*buckets), &bytes)) return G_OVERFLOW;
    buckets = (g_dict_entry **)d->allocator.allocate(d->allocator.context, bytes);
    if (!buckets) return G_OUT_OF_MEMORY;
    for (i = 0; i < capacity; ++i) buckets[i] = NULL;
    for (i = 0; i < d->capacity; ++i) {
        g_dict_entry *entry = d->buckets[i];
        while (entry) {
            g_dict_entry *next = entry->next;
            usize index = (usize)(entry->hash % capacity);
            entry->next = buckets[index];
            buckets[index] = entry;
            entry = next;
        }
    }
    g_ds_free(&d->allocator, d->buckets);
    d->buckets = buckets;
    d->capacity = capacity;
    return G_OK;
}
/* Insert or replace. Replacement preserves the stored key and dictionary len. */
static inline g_result g_dict_put(g_dict *d, const void *key, const void *value) {
    u64 hash;
    const g_dict_entry *found;
    g_dict_entry *entry;
    usize index;
    if (!d || !d->key_size || !key || (d->value_size && !value)) return G_INVALID;
    hash = d->hash(key, d->key_size, d->key_context);
    found = g_ds_find(d, key, hash);
    if (found) {
        if (d->value_size) memmove(found->value, value, d->value_size);
        return G_OK;
    }
    if (d->len == (usize)SIZE_MAX) return G_OVERFLOW;
    entry = (g_dict_entry *)d->allocator.allocate(d->allocator.context, sizeof(*entry));
    if (!entry) return G_OUT_OF_MEMORY;
    entry->key = NULL;
    entry->value = NULL;
    entry->key = d->allocator.allocate(d->allocator.context, d->key_size);
    if (!entry->key) { g_ds_entry_free(d, entry); return G_OUT_OF_MEMORY; }
    memcpy(entry->key, key, d->key_size);
    if (d->value_size) {
        entry->value = d->allocator.allocate(d->allocator.context, d->value_size);
        if (!entry->value) { g_ds_entry_free(d, entry); return G_OUT_OF_MEMORY; }
        memcpy(entry->value, value, d->value_size);
    }
    if (!d->capacity || d->len >= d->capacity - d->capacity / 4) {
        usize capacity = d->capacity ? d->capacity : 8;
        g_result result;
        if (d->capacity) {
            if (capacity > (usize)SIZE_MAX / 2) {
                g_ds_entry_free(d, entry);
                return G_OVERFLOW;
            }
            capacity *= 2;
        }
        result = g_ds_rehash(d, capacity);
        if (result != G_OK) { g_ds_entry_free(d, entry); return result; }
    }
    index = (usize)(hash % d->capacity);
    entry->hash = hash;
    entry->next = d->buckets[index];
    d->buckets[index] = entry;
    ++d->len;
    return G_OK;
}
static inline g_result g_dict_remove(g_dict *d, const void *key) {
    u64 hash;
    g_dict_entry **link;
    if (!d || !d->key_size || !key) return G_INVALID;
    if (!d->capacity) return G_NOT_FOUND;
    hash = d->hash(key, d->key_size, d->key_context);
    link = &d->buckets[hash % d->capacity];
    while (*link) {
        g_dict_entry *entry = *link;
        if (entry->hash == hash && d->equal(entry->key, key, d->key_size, d->key_context)) {
            *link = entry->next;
            g_ds_entry_free(d, entry);
            --d->len;
            return G_OK;
        }
        link = &entry->next;
    }
    return G_NOT_FOUND;
}
static inline void g_dict_clear(g_dict *d) {
    usize i;
    if (!d) return;
    for (i = 0; i < d->capacity; ++i) {
        g_dict_entry *entry = d->buckets[i];
        while (entry) {
            g_dict_entry *next = entry->next;
            g_ds_entry_free(d, entry);
            entry = next;
        }
        d->buckets[i] = NULL;
    }
    d->len = 0;
}
static inline void g_dict_destroy(g_dict *d) {
    if (!d) return;
    g_dict_clear(d);
    g_ds_free(&d->allocator, d->buckets);
    memset(d, 0, sizeof(*d));
}

typedef struct g_set { g_dict dict; } g_set;
static inline g_result g_set_init(g_set *s, usize size, const g_allocator *a,
                                  g_hash_fn hash, g_equal_fn equal, void *context) {
    return s ? g_dict_init(&s->dict, size, 0, a, hash, equal, context) : G_INVALID;
}
static inline usize g_set_len(const g_set *s) { return s ? s->dict.len : 0; }
static inline g_result g_set_add(g_set *s, const void *key) {
    return s ? g_dict_put(&s->dict, key, NULL) : G_INVALID;
}
static inline u8 g_set_contains(const g_set *s, const void *key) {
    return s ? g_dict_contains(&s->dict, key) : 0;
}
static inline g_result g_set_remove(g_set *s, const void *key) {
    return s ? g_dict_remove(&s->dict, key) : G_INVALID;
}
static inline void g_set_clear(g_set *s) { if (s) g_dict_clear(&s->dict); }
static inline void g_set_destroy(g_set *s) { if (s) g_dict_destroy(&s->dict); }
