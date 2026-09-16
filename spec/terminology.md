# G terminology

Status: initial draft. These terms apply to the language specs and C runtime.

## Count and len

- `count` is the number of entities or elements.
- `len` is a byte length. For a type or a container element, it means
  `sizeof(type)`, not the number of elements.
- A string uses `len` for its byte length. It is not a Unicode character count
  and does not include an implied null terminator.
- `capacity` is the number of elements that allocated storage can hold.

For an array of ten `f64` elements, `count = 10` and
`len = sizeof(f64)` (8 bytes on the intended targets). Its occupied byte size
is `count * len`; capacity is also measured in elements.

The runtime's `g_array`, `g_vec`, and `g_ring` follow this convention.
Dictionaries and sets use `count` for entries; a dictionary records `key_len`
and `value_len` separately. `g_set_count` returns the number of set members.

## Strings

`cstr` is a null-terminated C string (`char *` in the C runtime).
`str` is a non-null-terminated byte string:

```text
struct { data ^u8, len usize }
```

Its data is described by the pointer and explicit byte length. Neither type
implies ownership. A trailing null is required by `cstr`, but is not required,
included, or automatically appended by `str`.

See the [G specification](G.md) for string literals, reference counting,
object/class layout, and method calling conventions.
