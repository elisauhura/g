# G

G is an extension of C designed to improve language safety and support code generation, reflection, templating, and domain-specific languages (DSLs). Its tooling brings building, language services, and documentation generation together under a single command: `g`.

G transpiles to C and is tightly coupled to a custom C library named `g`, which provides a common foundation for language features across platforms. The draft [G language specification](spec/G.md) describes the language's syntax and semantics as they develop.

The draft [build system specification](spec/build.md) describes project organization, G and C integration, and the build flow.

The [terminology specification](spec/terminology.md) defines count, byte length, and string terminology.

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
    terminology.md                   Shared language and runtime terminology
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

## Testing

The [test library](lib/test/README.md) provides nested tests, artifact hooks, benchmarks, JSON reports, and streamed test events. Registration is explicit until the G build tool can generate it.

## Command-line arguments

The [cmdline library](lib/cmdline/README.md) handles clustered short flags, long flags and parameters, positional arguments, and `--` tails for nested parsing.

## Bootstrap and build

On Windows with Visual Studio C++ tools installed:

```powershell
.\Setup.ps1
.\g.exe build all
```

Setup initializes MSVC, builds `bootstrap-g`, uses it to build `g`, and copies
`g.exe` to the repository root. An existing root executable skips bootstrapping;
use `.\Setup.ps1 --force` to rebuild it. From a fresh shell, initialize MSVC with
`. .\scripts\MSVCSetup.ps1` before calling `g.exe` directly.

Each library, command, and test unit declares its dependencies in `dep.g`.
Tests implicitly depend on `lib/test`. See the [build specification](spec/build.md)
for syntax and target selection. Run `.\scripts\Test-Build.ps1` for build regressions.

List available project units without building:

```powershell
.\g.exe lib list
.\g.exe cmd list
.\g.exe tests list
```

Lists print names alphabetically, one per line. Use `--project=PATH` to inspect
another project; empty categories produce no output.
