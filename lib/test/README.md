# Test library

The hosted C test library is declared in [test.h](../../include/test.h) and
implemented in [test.c](test.c). It follows the test library in `C:\src\c`:
nested tests, source-location logs, artifact lifecycle hooks, shuffled tests,
benchmarks, JSON reports, streamed events, and metadata-only output.

G's containers remain in `g.h`. The runner uses `g_vec` internally and an
explicit malloc/free allocator. It requires a hosted C11 runtime.

## Registration and execution

There is no automatic discovery yet. Define mutable registration tables and
pass them to `test_main`; a future build generator can emit the same tables.
The library supplies no `main` and does not depend on unresolved registry globals.

```c
#include "test.h"

static void Test_Add(T *t) {
    test_check(t, 2 + 2 == 4);
    test_l(t, test_s("addition checked"));
}

int main(int argc, char **argv) {
    test_case cases[] = {{test_s("Test_Add"), Test_Add}};
    test_suite suite = {0};
    suite.tests = cases;
    suite.test_count = 1;
    return test_main(argc, argv, &suite);
}
```

`test_check` logs and marks failure without returning from the test. Return
explicitly when a failed prerequisite makes it unsafe to continue. Checks remain
active with `NDEBUG`. `test_fail` marks failure without aborting the process.

Run a child test with a `test_func` variable and
`test_run(t, &child, test_s("child name"))`. A child failure also fails its parent.
Nested tests share `test_getcache(t)` with the parent.

Names and log messages are borrowed byte slices. Keep their backing storage
alive until `test_main` returns. Use `test_s` only for string literals. The runner
is synchronous and is not thread-safe or reentrant; test crashes are not isolated.

## Artifacts and benchmarks

Register `TestArtifact_*` functions in `suite.artifacts`. They run in name order
and call `test_add_artifact` to register caller-owned data and optional
`before_all`, `before_each`, `after_each`, and `after_all` callbacks. Hooks run
in registration order. Per-test hooks surround top-level tests only.

Tests and benchmarks are shuffled independently. Benchmark callbacks receive
`B *`; use `bench_warm_up`, `bench_runs`, and `bench_run`. Defaults are three
warm-up iterations and one measured iteration. Fixtures finish before benchmarks,
as in the reference library. Measurements use C11 wall-clock time and include
monitoring overhead when enabled; they are not a monotonic microbenchmark clock.
A benchmark that produces no valid measurement fails the suite.

## Output

- `--output <path>` writes a JSON report with artifacts, nested results, logs,
  timestamps, and benchmark measurements.
- `--monitor <path>` streams newline-delimited JSON lifecycle events, flushing
  each event. Events include `startSuite`, `loadedArtifact`, `loadedTest`,
  `loadedBench`, `registeredArtifact`, `startTest`, `testLog`, `testFailed`,
  `stopTest`, benchmark events, and `stopSuite`. The registration event corrects
  the reference implementation's `registredArtifact` spelling.
- `--emit-metadata <path>` writes sorted registration names and counts with
  schema version 1, without running artifact registration, hooks, tests, or
  benchmarks. It cannot be combined with report or monitor options.

All options also accept `--option=path`. Unknown options, missing/empty paths,
and duplicate options are rejected. Use distinct report and monitor files.
Parent directories must already exist. Files are overwritten.

Exit codes: 0 for success, 1 for a failed suite or report write, and 2 for invalid
arguments/registration or failure to open the monitor or write metadata.

## Build and validate

From the repository root:

```sh
clang -std=c11 -Wall -Wextra -Werror -pedantic -I include lib/test/test.c tests/test/test.c -o test-runner
./test-runner --output report.json --monitor monitor.jsonl
```

On Windows, use `-o test-runner.exe`, then run:

```powershell
.\tests\test\validate.ps1 -Runner .\test-runner.exe
```

The integration validator checks artifact order, nesting, benchmark counts, JSON
escaping, metadata-only mode, empty suites, expected failures, argument
validation, and output I/O failures. The sample's `--fixture=...` switches are
self-test controls, not runner API options.
