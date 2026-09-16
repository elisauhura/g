# Build backend

[buildbe.h](../../include/buildbe.h) defines a compiler-independent function
table. `buildbe.c` implements Windows/MSVC, Linux/GCC, Linux/Clang, and
Darwin/Clang command construction and native process execution. It passes
arguments directly to processes rather than evaluating shell command strings.

`host.c` provides host filesystem enumeration, absolute paths, directory
creation, file metadata, and timestamp-aware atomic source copying. These host
services do not depend on the compilation target. Reparse points/symlinks in
discovered trees and output paths are rejected.

The initial backends use tools from the current environment. Windows requires
MSVC setup; POSIX targets require their compiler and `ar` on PATH. Only native
POSIX architectures are accepted. Darwin uses the active compiler/SDK environment.
Explicit cleanup and symbol extraction are not implemented and their optional
callbacks are null. Run redirection/working-directory overrides return
`BUILDBE_UNSUPPORTED`.

The Windows ARM64 bootstrap and full project build are verified locally.
Linux and Darwin code paths require validation on their respective hosts.
