# G

G is an extension of C designed to improve language safety and support code generation, reflection, templating, and domain-specific languages (DSLs). Its tooling brings building, language services, and documentation generation together under a single command: `g`.

G transpiles to C and is tightly coupled to a custom C library named `g`, which provides a common foundation for language features across platforms. The draft [G language specification](spec/G.md) describes the language's syntax and semantics as they develop.

The draft [build system specification](spec/build.md) describes project organization, G and C integration, and the build flow.

## Goals

- **Build system:** Manage G and C files across platforms with a custom build system.
- **Language server:** Understand and reason about projects that contain both C and G code.
- **Documentation generation:** Generate project documentation as part of the toolchain.
- **Safety annotations:** Express additional information to improve language safety.
- **Metaprogramming:** Provide better support for code generation, reflection, templating, and DSLs.

## Tooling and platforms

All commands are provided through the `g` command-line tool.

G aims to support Windows, macOS, Linux, Xk, and bare-metal targets on AMD64 and ARM64, with GCC, Clang, and MSVC backends. Target combinations are described in the [build system specification](spec/build.md).

## Repository layout

```text
include/                             Shared headers
cmd/<cmd name>/                       Command implementations
lib/<lib name>/                       Library implementations
tests/<test name>/                    Test implementations
spec/                                Language and build specifications
    G.md                             Draft G language specification
    build.md                         Draft build system specification
docs/
    md/                              Generated Markdown documentation
    html/                            Generated HTML documentation
vendor/<vendored dep>/<version>/      Vendored dependency files
scripts/                             Helper scripts
out/<target>/                        Generated build outputs per target
README.md                            Project overview
Tasks.md                             Task tracking
Changelog                            Change history
project.g                            Root project configuration and dependencies
```
