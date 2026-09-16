# G build system specification

Status: initial draft. This document describes the established build-system
goals and their relationship to the language. It does not yet define a complete
build configuration format or command interface, or claim implemented behavior.

## 1. Purpose

G provides a custom build system for projects containing both G and C source
files. Build tooling is accessed through the `g` command-line tool, alongside
language services and documentation generation.

The intended targets cover Windows, macOS, Linux, Xk, and bare metal, as detailed
in section 5. Platform-specific toolchain selection and cross-compilation rules
remain to be specified.

## 2. Project definition and layout

`project.g` is a special file at the project root that specifies project
configuration and how dependencies work. Its syntax, evaluation model, and
configuration fields remain to be specified.

Most G projects follow the `include/`, `cmd/`, `lib/`, `tests/`, `vendor/`, and
`docs/` folder structure. These are conventions rather than a requirement that
every project contain every directory.

The project layout identifies these locations:

| Location | Purpose |
| --- | --- |
| `include/` | Shared headers |
| `cmd/<cmd name>/` | Command implementations |
| `lib/<lib name>/` | Library implementations |
| `tests/<test name>/` | Test implementations |
| `vendor/<vendored dep>/<version>/` | Vendored dependency files |
| `scripts/` | Bootstrap and other helper scripts |
| `docs/md/` | Generated Markdown documentation |
| `docs/html/` | Generated HTML documentation |

Whether these directories imply automatic source discovery or require explicit
configuration in `project.g` remains to be specified.

## 3. G and C integration

G transpiles to C and is tightly coupled to the custom C library `g`, which
provides a common foundation for language features across platforms. The build
system must account for generated C, project C sources, and the supporting
libraries needed by the generated code.

The [G language specification](G.md) defines the source-level inputs to this
process:

- `#lib <name>` declares a library name.
- `#use <name>` imports a G library.
- `#include <header>` and `#include "header"` produce parsed header
  representations that are processed to generate a single preprocessed C header.
- `export` identifies symbols exposed through the single generated header per
  library. Exported C names use the library prefix unless annotated with `@c`.

These rules describe language behavior. Dependency lookup and build ordering
still need build-system definitions. Default artifact locations are described
in section 5.

## 4. Build flow

At a conceptual level, building a G project involves the following work:

1. Read the root `project.g`, select the build target, and determine the sources
   and dependencies.
2. Process G library imports and C header imports.
3. Validate G source and generate C source and library headers.
4. Compile the generated C together with the project's C sources.
5. Link the required libraries and produce the target's build outputs under
   `out/<target>/` by default.

This is an outline of responsibilities, not a fixed execution order or a command
specification. The task graph, compiler invocation, link behavior, and output
formats remain to be specified.

## 5. Targets and output layout

### 5.1. Target dimensions

A project is built against a target with an architecture, operating system, and
compiler backend. The expected supported values are:

| Dimension | Values |
| --- | --- |
| Architecture (`arch`) | `amd64`, `arm64` |
| Operating system (`os`) | `windows`, `darwin`, `linux`, `xk`, `baremetal` |
| Compiler backend (`backend`) | `gcc`, `clang`, `msvc` |

`amd64` denotes x64/AMD64, and `darwin` denotes Darwin/macOS. `baremetal` denotes
a target without a host operating system.

An OS and a specific backend are combined with `/`: `linux/gcc` means Linux
using the GCC backend. This notation does not select an architecture. The syntax
for combining architecture with OS and backend, target defaults, and the valid
combinations of these dimensions remain to be specified.

These values describe expected support, not currently implemented availability
or a guarantee that every architecture, OS, and backend combination is valid.

### 5.2. Output layout

By default, generated build outputs are placed under `out/<target>/` at the
project root, where `<target>` is the target name. The mapping from target
notation to the output directory name remains to be specified.

Each target has the following output layout:

```text
out/<target>/
    include/                         Generated C headers
    lib/                             Generated or copied C files for libraries
    tests/                           Generated or copied C files for tests
    cmd/                             Generated or copied C files for commands
    obj/
        <libname>.lib                Library build artifact
        <libname>/                  Object files for the library
        cmd/
            <cmdname>.lib           Command build artifact
            <cmdname>/              Object files for the command
    exe/                             Compiled executables, including test executables
```

For a library named `libname`, its `.lib` artifact is written to
`obj/libname.lib`, and its individual object files are placed in `obj/libname/`.
For a command named `cmdname`, the corresponding paths are
`obj/cmd/cmdname.lib` and `obj/cmd/cmdname/`. All of these paths are relative to
`out/<target>/`.

The `.lib` paths above record the build layout; their binary format and
platform-specific handling remain to be specified. Test object-file placement
and individual object and executable filenames also remain to be specified.

## 6. Bootstrapping the G toolchain

Most of the G language implementation must be written in G. Some features depend
on code generation that itself depends on G syntax, so building the complete
`g` command requires an initial tool capable of processing that syntax.

Special scripts in `scripts/` provide this bootstrap path. They build a subset
of the project's files to produce `bootstrap-g`, which can then build the whole
project and produce the complete `g` command.

### 6.1. Build the bootstrap command

The bootstrap scripts compile the subset needed to create `bootstrap-g` without
requiring the complete `g` command first. This subset must provide enough G
processing and build functionality to perform the next stage, including the
code-generation work required by the full project.

The subset must avoid depending on outputs that can only be generated by the
complete `g` command; otherwise the initial build would have a circular
dependency. The exact source subset and how its initial sources are supplied
remain to be specified.

### 6.2. Build the complete command

Once `bootstrap-g` is available, it compiles the whole project. This includes
processing the G implementation sources and performing the required code
generation, then compiling and linking the resulting code to produce the
complete `g` command.

The resulting `g` command provides the full toolchain for subsequent project
builds. `bootstrap-g` serves as the initial tool needed to reach that stage.

| Stage | Input | Tool | Result |
| --- | --- | --- | --- |
| Bootstrap | Required subset of project files | Scripts in `scripts/` and their compiler toolchain | `bootstrap-g` command |
| Complete build | Whole project, including its G implementation sources | `bootstrap-g` | Complete `g` command |

Script filenames, invocation syntax, prerequisites, the bootstrap language
subset, and bootstrap artifact locations remain to be specified.

## 7. Details to specify

Future revisions need to define:

- `project.g` syntax, evaluation, and how build targets are declared.
- Build commands, arguments, defaults, and configuration selection.
- Source discovery, library resolution, dependency versions, and vendoring.
- Compiler and linker selection, flags, and platform configuration.
- Output-directory overrides, individual artifact names, test object-file placement,
  and platform-specific artifact formats.
- How the supporting `g` C library is located, built, and linked.
- Incremental builds, dependency tracking, caching, and parallel execution.
- Test execution and documentation-generation integration.
- Diagnostics, failure handling, and cleaning build outputs.
- Bootstrap scripts, prerequisites, source subset, supported G features, and artifact locations.
