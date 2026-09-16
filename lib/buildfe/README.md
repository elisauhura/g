# Build frontend

[buildfe.h](../../include/buildfe.h) exposes project loading, build planning,
read-only plan views, and execution. `buildfe.c` discovers units, parses
`dep.g`, detects missing dependencies/cycles, and stages sources before invoking
the backend. `cli.c` implements the `g build` frontend using `cmdline`.

The current `project.g` is a root marker containing only comments/whitespace.
Configuration expressions, G translation, cleanup execution, and compile caching
are not implemented. G inputs fail explicitly before any output is staged.
C copies use timestamp/size checks; objects and linked products are rebuilt.

See the [build specification](../../spec/build.md) for manifests, commands,
output paths, and bootstrap behavior. Run `scripts/Test-Build.ps1` on Windows
after setup for isolated end-to-end dependency, staging, and failure tests.
