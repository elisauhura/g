# G

G is an extension of C designed to improve language safety and support code generation, reflection, templating, and domain-specific languages (DSLs). Its tooling brings building, language services, and documentation generation together under a single command: `g`.

## Goals

- **Build system:** Manage G and C files across platforms with a custom build system.
- **Language server:** Understand and reason about projects that contain both C and G code.
- **Documentation generation:** Generate project documentation as part of the toolchain.
- **Safety annotations:** Express additional information to improve language safety.
- **Metaprogramming:** Provide better support for code generation, reflection, templating, and DSLs.

## Tooling and platforms

All commands are provided through the `g` command-line tool.

G aims to support Windows, Linux, macOS, BSD, and Xk.

## Repository layout

```text
include/                             Shared headers
cmd/<cmd name>/                       Command implementations
lib/<lib name>/                       Library implementations
tests/<test name>/                    Test implementations
docs/
    md/                              Generated Markdown documentation
    html/                            Generated HTML documentation
vendor/<vendored dep>/<version>/      Vendored dependency files
scripts/                             Helper scripts
README.md                            Project overview
Tasks.md                             Task tracking
Changelog                            Change history
project.g                            Project definition
```
