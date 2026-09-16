# G language specification

Status: initial draft. This document records the current language design; it is
not yet a complete grammar or a statement of implemented functionality.

## 1. Overview

G is designed to transpile to C. It introduces features supported by a custom C
library named `g`. The language and library are tightly coupled: the library
provides a common foundation for language features across multiple platforms.

## 2. Source files and exports

G source files do not require separately maintained header files. Instead, the
transpiler generates a single header per library. The `export` keyword identifies
symbols exposed through that generated header, keeping the public interface and
implementation in the same source files.

## 3. Language directives and lightweight namespaces

G uses a lightweight namespace based on library names. Directives beginning with
`#` are part of the language and handled by the toolchain.

### 3.1. C header imports

Both `#include <header>` and `#include "header"` are supported as synthetic C
includes. An include produces a parsed header representation, which is processed
to generate a single preprocessed C header.

Header search paths and the handling of C macros remain to be specified.

### 3.2. G library imports

`#use <name>` imports a G library, where `name` is the library name. Import
resolution and the syntax for referencing imported symbols remain to be specified.

### 3.3. Library names and exported symbols

`#lib <name>` declares the library name. Exported symbols are prefixed in the
generated C with `<lib name>_<name>`, where the second name is the symbol's name.
For example, a symbol named `open` exported by the library `files` becomes
`files_open`.

An exported symbol annotated with `@c` keeps its name without the library prefix.
For example, `@c export main` retains the C symbol name `main`. The equivalent
parenthesized annotation is `@(c)`.

Rules for library-name scope and symbol collisions remain to be specified.

### 3.4. Build filters

The `#build(...)` directive sets target filters for a G source file. When present,
it must be the first line of the file, before any blank lines, comments, other
directives, or declarations.

Filters are separated by commas. All filters must match the build target for the
file to be included in the build (logical AND). A filter prefixed with `!` negates
that condition. A file whose filters do not match is excluded from that build.

| Directive | File is included when |
| --- | --- |
| `#build(windows)` | The target operating system is Windows. |
| `#build(windows,arm64)` | The target is Windows on ARM64. |
| `#build(!darwin,amd64)` | The target is not Darwin/macOS and uses x64/AMD64. |

Filters apply to the build target, rather than the machine running the build.
Here, `darwin` denotes Darwin/macOS and `amd64` denotes the x64/AMD64 architecture.
The complete set of filter names and any accepted aliases remain to be specified.

## 4. Declarations and statements

### 4.1. Declaration style

Declarations place names before types, following a style similar to Go. Function
declarations use `\(` to introduce parameters, with the return type following
the parameter list.

For comparison, a C entry point might be written as:

```c
int main(int argc, char **argv) {
    int a = 1;
    printf("%d\n", a);
    return 0;
}
```

A G declaration has this form:

```g
@c export main \(count int, argv [`count]cstr) int {
    a int := 0;
    printf("%d\n", a);
    return 0;
}
```

The `@c` annotation preserves the C entry-point name `main`.

Here, `count int` declares a parameter named `count` of type `int`, and
`a int := 0` declares and initializes a local variable. The array size hint on
`argv` refers to `count`. The example uses `cstr` for null-terminated C strings;
further interoperability rules remain to be specified.

### 4.2. Optional semicolons

Semicolons are optional. Automatic semicolon insertion allows the G example to
be written as:

```g
@c export main \(count int, argv [`count]cstr) int {
    a int := 0
    printf("%d\n", a)
    return 0
}
```

The exact insertion rules, including multiline expressions, remain to be
specified.

## 5. Types and pointers

### 5.1. Type declarations

A type is defined using `symbol type <actual type>`. In this notation, `symbol`
is the name being defined, `type` is a keyword, and `<actual type>` is the type
expression. The angle brackets are placeholders, not literal syntax.

For example:

```g
Byte type u8
```

Here, `Byte` is the new type name, `type` is the declaration keyword, and `u8`
is the actual type. Whether such a declaration creates an alias or a distinct
type remains to be specified.

### 5.2. Primitive types

G favors Rust-style primitive type names:

| Types | Purpose |
| --- | --- |
| `u8`, `u16`, `u32`, `u64` | Unsigned integers of the indicated width |
| `i8`, `i16`, `i32`, `i64` | Signed integers of the indicated width |
| `usize`, `isize` | Unsigned and signed size types |
| `ptr` | Pointer type; its precise relationship to typed pointers is pending |
| `f16`, `f32`, `f64` | Floating-point types of the indicated width |

The declaration example also uses `int`; its width and relationship to the
fixed-width types remain to be specified.

### 5.3. Typed pointers

Typed pointers use the prefix `^`. For example, the G pointer spelling
corresponding to C's `char *` is `^u8`.

### 5.4. String literals

String literals written as `@"..."` are not null-terminated. No trailing null
terminator is appended to the literal contents.

For example:

```g
@"hello"
```

The C runtime defines `cstr` as a null-terminated C string pointer and `str`
as `struct { u8 *data; usize len; }`. A `str` carries its byte length and does
not require a null terminator. The [terminology specification](terminology.md)
distinguishes byte length (`len`) from element count (`count`).

A transpiler can emit a non-null-terminated literal using an explicit byte
initializer rather than a C string literal:

```c
char no_null[] = {'H', 'e', 'l', 'l', 'o'};
str greeting = {(u8 *)no_null, sizeof(no_null)};
```

Here, `greeting.len` is 5 and no sixth terminator byte is emitted. Storage
lifetime, encoding, and escape rules remain to be specified.

## 6. Iota

G uses Go-style `iota` in place of C-style enum declarations. The declaration
syntax, increment and reset rules, and resulting constant types remain to be
specified.

## 7. Attributes and annotations

Symbols can be prefixed with annotations using `@(...)`. A single annotation
can use the shorthand `@name`; for example, `@ref` and `@(ref)` are equivalent
spellings. The grammar for multiple annotations and annotation arguments remains
to be specified.

## 8. Pointer ownership and lifetimes

Pointers carry semantics that the transpiler uses to validate code logic.
Passing a pointer transfers ownership by default: it implies a move.

Temporary pointer leases are expressed with `@ref` or `@(ref)`. Function calls
are considered new inner scopes for these lifetime rules.

| Annotation | Meaning |
| --- | --- |
| `move` | The pointer is now owned by the new location. |
| `ref` | The pointer is temporarily leased; ownership returns when the scope returns or ends. |
| `rc` | The pointer is reference counted using the built-in `rc` library. |
| `pool` | The pointer belongs to a pool and must not be freed directly. |

The `weak` and `strong` annotations apply to struct members and participate in
automatic reference counting. Their precise retention and lifetime behavior
remains to be specified.

### 8.1. Runtime reference counting

The C runtime's `rc` box contains a payload pointer and a signed `isize count`.
The initial boxing chooses the mode:

| Mode | Initial count | Retain | Release |
| --- | --- | --- | --- |
| Plain data | +1 | Increment away from zero | Decrement toward zero |
| Object | -1 | Decrement away from zero | Increment toward zero |

The absolute count is the number of strong references. Reaching zero invokes
the cleanup handler passed to `rc_release`. The handler is supplied at release
time, not stored in the box, and must match the payload's cleanup requirements.
No destructor selector is implicitly looked up.

The static inline functions `rc_box`, `rc_retain`, and `rc_release` implement
these operations. `rc_count` returns the unsigned reference count and
`rc_is_object` identifies object mode while the box is live. All owners share
one box; copying an `rc` value does not share its counter.

An empty box must be zero-initialized before boxing. Null payloads, live-box
replacement, invalid retain/release, and missing release handlers are rejected.
Retain detects signed counter overflow. Final release clears the box before
calling the handler, preventing a reentrant release from freeing it twice.
The initial implementation is non-atomic; weak-reference semantics remain open.

### 8.2. Objects and classes

`id` is the basic object header and contains a pointer to its `class`.
Concrete object structs place this header first. A class contains a lookup
function that maps a G `str` name to a function pointer, returning null when
no method is found. Lookup must use the string's byte length, not assume a null
terminator. `id_lookup` provides this operation for the C runtime.

Classes follow a mulle-objc-style convention in which a method receives the
object pointer first and writes its return value through the last argument.
For example, an integer-returning `domath(int a, int b)` has this C shape:

```c
void domath(ptr self, int a, int b, int *ret);
typedef void (*domath_method)(ptr self, int a, int b, int *ret);
```

The runtime's generic `func` is an erased function pointer used for lookup.
It must be converted back to the exact method signature before calling.
It is not a universal variadic calling convention.

Class descriptors are normally supplied by libraries through external pointers,
for example `extern const class *math_class;`, and linked into object programs.
The C runtime declares the object header as `struct id { const class *cls; }`
and the class as `struct class { func (*lookup)(str name); }`.
Method-name encoding, overload resolution, and inheritance remain to be specified.

## 9. Data structures

G supports C-style arrays as well as collection types backed by the `g` library.
In the notation below, `T` is an element or value type and `K` is a key type.
The words `symbol` and `const` are placeholders.

| Syntax | Structure |
| --- | --- |
| `[]T` | C-style array |
| ``[`symbol]T`` | C-style array with a size hint referring to `symbol` |
| `[#]T` | G-style array that includes its element count |
| `[#K]T` | Dictionary with keys of type `K` and values of type `T` |
| `[#K]()` | Set of values of type `K` |
| `[#?]T` | Vector (growable array) |
| `[<const>]T` or `[<symbol>]T` | Fixed-size FIFO/FILO structure |
| `[<?>]T` | Dynamically sized FIFO/FILO structure |

The shorthand `[<>]T` denotes the fixed-size FIFO/FILO family. FIFO means
first in, first out; FILO means first in, last out. How operations select these
behaviors remains to be specified.

The `#` in `[#]T` indicates that an element count is included with the array. The backtick
in ``[`symbol]T`` introduces an array size hint to the compiler. These collection
markers are part of type syntax, not preprocessor directives.

## 10. Assembly

Assembly can be embedded using `%{ ... }` markers. The block begins with `%{`
and ends with `}`. Assembly syntax is similar to NASM-style assembly.

For example, an x86 assembly block can be written as:

```g
%{
    mov eax, 1
}
```

The exact supported syntax, target architectures, interaction with G symbols,
and translation to the C toolchain remain to be specified.

## 11. Details to specify

Future revisions need to define:

- Library boundaries, library-name scope, symbol collisions, generated header naming, and C interoperability.
- The full declaration grammar, initialization rules, and semicolon insertion rules.
- Header search paths, C macro handling, G library resolution, imported-symbol references, and directive evaluation rules.
- Primitive type mappings, `int`, `ptr`, and platform support for floating-point types.
- `iota` declaration syntax and constant evaluation.
- Annotation placement, combinations, and arguments.
- Ownership validation, use after move, lease restrictions, cleanup, and escaping pointers.
- Reference counting, weak and strong members, pool lifetimes, and annotation interactions.
- Collection layout, initialization, operations, bounds behavior, growth, and element ownership.
- The distinction between array size hints and enforced bounds, and FIFO/FILO capacity rules.
