# Runtime collection tests

The initial runtime containers are header-only in [g.h](../../include/g.h).

| G type | C runtime representation |
| --- | --- |
| `[]T`, ``[`symbol]T`` | Native C arrays/pointers; hints belong to the transpiler |
| `[#]T` | `g_array`: borrowed storage, element count, and element byte length |
| `[#?]T` | `g_vec`: owned, growable contiguous storage |
| `[#K]V` | `g_dict`: hash table with copied keys and values |
| `[#K]()` | `g_set`: dictionary storage without values |
| `[<capacity>]T` | `g_ring_init_fixed`: caller-provided storage |
| `[<?>]T` | `g_ring_init_dynamic`: growable ring storage |

Use `g_ring_push_back` with `g_ring_pop_front` for FIFO or
`g_ring_pop_back` for FILO. Fixed rings return `G_FULL` without overwriting data.
Out-of-range access returns null; empty pop and missing-key removal return
`G_NOT_FOUND`.

Containers use byte copies. `count` is the number of elements; `len` is the
element byte length. Dictionaries use `key_len` and `value_len`, and sets expose
`g_set_count`. This replaces the original count-as-`len` API. Initialize with
`sizeof(T)`; the generated G bindings will provide type checking. They do not
yet implement G ownership, element destructors, or automatic reference counting.
A dictionary key must remain stable after insertion; keys are never exposed as
mutable values. Custom equality requires a matching hash callback.

Pass a null allocator to use malloc/free, or provide `g_allocator` callbacks.
Define `G_NO_STDLIB_ALLOCATOR` to require explicit allocators and avoid the
default malloc/free implementation. A freestanding port still needs the
memory-copy functions from `<string.h>`.

Run from the repository root with a C11 or newer compiler:

```sh
clang -std=c11 -Wall -Wextra -Werror -pedantic -I include tests/ds/test.c -o ds-test
./ds-test
```

On Windows, use `-o ds-test.exe` and run `.\ds-test.exe`. Repeat with
`-DG_NO_STDLIB_ALLOCATOR` to check explicit-allocator mode.

The tests exercise bounds, aliasing during growth, FIFO/FILO order, ring
wraparound, hash collisions, resizing, replacement/removal, set deduplication,
and allocation failures with live-allocation accounting. They also include both
AST headers to check compatibility.
