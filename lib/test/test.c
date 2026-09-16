#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "test.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef _WIN32
static int fopen_s(FILE **file, const char *path, const char *mode) {
    *file = fopen(path, mode);
    return *file == NULL;
}

static int strcpy_s(char *destination, usize size, const char *source) {
    usize length = strlen(source);
    if (length >= size) return 1;
    memcpy(destination, source, length + 1);
    return 0;
}

static int localtime_s(struct tm *result, const time_t *value) {
    return localtime_r(value, result) == NULL;
}
#endif

/* Registration is explicit until the G build tool can generate these tables. */
static test_case *test_functions;
static usize test_functions_count;
static test_artifact_case *testartifact_functions;
static usize testartifact_functions_count;
static test_benchmark_case *testbench_functions;
static usize testbench_functions_count;
struct B {
    test_text name;
    usize warm_up_runs;
    usize runs;
    usize index;
    f64 elapsed_seconds;
    f64 nanoseconds_per_run;
    u8 measured;
};

typedef struct {
    test_text name;
    ptr data;
    testartifact_handler before_all;
    testartifact_handler before_each;
    testartifact_handler after_each;
    testartifact_handler after_all;
} TestArtifact;

struct C {
    g_vec artifacts;
    u8 failed;
};

typedef struct {
    const char * file;
    int line;
    test_text message;
    time_t at;
} TestLog;

typedef struct TestRun TestRun;

struct TestRun {
    test_text name;
    g_vec logs;
    g_vec inner_runs;
    usize index;
    usize parent_index;
    u8 has_parent;
    time_t started_at;
    time_t stopped_at;
    u8 passed;
};

struct T {
    C *cache;
    TestRun *run;
};

static FILE *monitor_file;
static usize next_test_index;

static time_t now(void) {
    return time(NULL);
}

static void json_string_bytes(FILE *file, const u8 *data, usize length) {
    fputc('"', file);
    for (usize index = 0; index < length; index++) {
        u8 value = data[index];
        switch (value) {
        case '"': fputs("\\\"", file); break;
        case '\\': fputs("\\\\", file); break;
        case '\b': fputs("\\b", file); break;
        case '\f': fputs("\\f", file); break;
        case '\n': fputs("\\n", file); break;
        case '\r': fputs("\\r", file); break;
        case '\t': fputs("\\t", file); break;
        default:
            if (value < 0x20) {
                fprintf(file, "\\u%04x", (unsigned int)value);
            } else {
                fputc(value, file);
            }
        }
    }
    fputc('"', file);
}

static void json_string(FILE *file, test_text value) {
    json_string_bytes(file, value.data, value.len);
}

static void json_cstr(FILE *file, const char * value) {
    json_string_bytes(file, (const u8 *)value, strlen(value));
}

static void monitor_begin(const char * event) {
    if (monitor_file == NULL) {
        return;
    }
    fputs("{\"event\":", monitor_file);
    json_cstr(monitor_file, event);
    fputs(",\"details\":{", monitor_file);
}

static void monitor_end(void) {
    if (monitor_file == NULL) {
        return;
    }
    fputs("}}\n", monitor_file);
    fflush(monitor_file);
}

static void monitor_named_event(const char * event, test_text name) {
    if (monitor_file == NULL) {
        return;
    }
    monitor_begin(event);
    fputs("\"name\":", monitor_file);
    json_string(monitor_file, name);
    monitor_end();
}

static void monitor_suite_event(const char * event, time_t at) {
    if (monitor_file == NULL) {
        return;
    }
    monitor_begin(event);
    fprintf(monitor_file, "\"at\":%lld", (long long)at);
    monitor_end();
}

static void monitor_test_event(const char * event, const TestRun *run) {
    if (monitor_file == NULL) {
        return;
    }
    monitor_begin(event);
    fprintf(monitor_file, "\"testIndex\":%zu,\"parent\":", run->index);
    if (run->has_parent) {
        fprintf(monitor_file, "%zu", run->parent_index);
    } else {
        fputs("null", monitor_file);
    }
    fputs(",\"name\":", monitor_file);
    json_string(monitor_file, run->name);
    fprintf(monitor_file, ",\"timestamp\":%lld", (long long)now());
    if (strcmp(event, "stopTest") == 0) {
        fprintf(monitor_file, ",\"passed\":%s", run->passed ? "true" : "false");
    }
    monitor_end();
}

static void *runner_allocate(void *context, usize bytes) {
    (void)context;
    return malloc((size_t)bytes);
}
static void runner_deallocate(void *context, void *data) {
    (void)context;
    free(data);
}
static g_vec new_vec(usize size, usize capacity) {
    g_vec result = {0};
    g_allocator allocator = {NULL, runner_allocate, runner_deallocate};
    (void)capacity; /* Lazy allocation; each push reports allocation failure. */
    (void)g_vec_init(&result, size, &allocator);
    return result;
}
static u8 vec_append(g_vec *items, const void *item) {
    return (u8)(g_vec_push(items, item) == G_OK);
}
static void shuffle(void *items, usize count, usize size) {
    u8 *bytes = items;
    for (usize i = count; i > 1; --i) {
        usize other = (usize)rand() % i;
        for (usize j = 0; j < size; ++j) {
            u8 value = bytes[(i - 1) * size + j];
            bytes[(i - 1) * size + j] = bytes[other * size + j];
            bytes[other * size + j] = value;
        }
    }
}
static int compare_named_functions(const void *left_entry, const void *right_entry) {
    const test_text *left = left_entry;
    const test_text *right = right_entry;
    usize common_length = left->len < right->len ? left->len : right->len;
    int compared = memcmp(left->data, right->data, common_length);

    if (compared != 0) {
        return compared;
    }
    return left->len < right->len ? -1 : left->len > right->len;
}

static void test_run_delete(TestRun *run) {
    if (run == NULL) {
        return;
    }

    for (usize index = 0; index < run->inner_runs.count; index++) {
        test_run_delete(g_vec_at(&run->inner_runs, index));
    }
    g_vec_destroy(&run->inner_runs);
    g_vec_destroy(&run->logs);
}

static void print_test_run(const TestRun *run, usize depth) {
    for (usize index = 0; index < depth; index++) {
        fputs("  ", stdout);
    }
    printf("[%s] %.*s\n", run->passed ? "PASS" : "FAIL",
        (int)run->name.len, (const char *)run->name.data);

    for (usize index = 0; index < run->logs.count; index++) {
        const TestLog *log = g_vec_at((g_vec *)&run->logs, index);
        for (usize indent = 0; indent <= depth; indent++) {
            fputs("  ", stdout);
        }
        printf("%s:%d: %.*s\n", log->file, log->line,
            (int)log->message.len, (const char *)log->message.data);
    }

    for (usize index = 0; index < run->inner_runs.count; index++) {
        print_test_run(g_vec_at((g_vec *)&run->inner_runs, index), depth + 1);
    }
}

void test_log(T *t, const char * file, int line, test_text message) {
    TestLog log = {
        .file = file,
        .line = line,
        .message = message,
        .at = now()
    };

    if (t == NULL || t->run == NULL || !vec_append(&t->run->logs, &log)) {
        if (t != NULL && t->run != NULL) {
            t->run->passed = 0;
        }
        return;
    }

    if (monitor_file != NULL) {
        monitor_begin("testLog");
        fprintf(monitor_file, "\"testIndex\":%zu,\"file\":", t->run->index);
        json_cstr(monitor_file, file);
        fprintf(monitor_file, ",\"line\":%d,\"message\":", line);
        json_string(monitor_file, message);
        fprintf(monitor_file, ",\"timestamp\":%lld", (long long)log.at);
        monitor_end();
    }
}

void test_fail(T *t) {
    if (t != NULL && t->run != NULL) {
        u8 was_passed = t->run->passed;
        t->run->passed = 0;
        if (was_passed && monitor_file != NULL) {
            monitor_begin("testFailed");
            fprintf(monitor_file, "\"testIndex\":%zu,\"timestamp\":%lld",
                t->run->index, (long long)now());
            monitor_end();
        }
    }
}

static void execute_test(T *context, test_func func) {
    context->run->started_at = now();
    monitor_test_event("startTest", context->run);
    func(context);
    context->run->stopped_at = now();
    monitor_test_event("stopTest", context->run);
}

void test_run(T *t, test_func *func, test_text name) {
    TestRun child = {
        .name = name,
        .logs = new_vec(sizeof(TestLog), 0),
        .inner_runs = new_vec(sizeof(TestRun), 0),
        .index = next_test_index++,
        .parent_index = t == NULL || t->run == NULL ? 0 : t->run->index,
        .has_parent = 1,
        .passed = 1
    };
    T child_context;

    if (t == NULL || t->run == NULL || func == NULL || *func == NULL) {
        test_fail(t);
        return;
    }

    child_context.cache = t->cache;
    child_context.run = &child;
    execute_test(&child_context, *func);
    if (!child.passed) {
        test_fail(t);
    }
    if (!vec_append(&t->run->inner_runs, &child)) {
        test_run_delete(&child);
        t->run->passed = 0;
    }
}

void test_add_artifact(C *c, test_text name, ptr data,
    testartifact_handler before_all,
    testartifact_handler before_each,
    testartifact_handler after_each,
    testartifact_handler after_all) {
    TestArtifact artifact = {
        .name = name,
        .data = data,
        .before_all = before_all,
        .before_each = before_each,
        .after_each = after_each,
        .after_all = after_all
    };

    if (c == NULL || !vec_append(&c->artifacts, &artifact)) {
        if (c != NULL) {
            c->failed = 1;
        }
        return;
    }
    monitor_named_event("registeredArtifact", name);
}

C *test_getcache(T *t) {
    return t == NULL ? NULL : t->cache;
}

static void run_artifact_handlers(C *cache, usize handler_offset) {
    for (usize index = 0; index < cache->artifacts.count; index++) {
        TestArtifact *artifact = g_vec_at(&cache->artifacts, index);
        testartifact_handler *handler = (testartifact_handler *)
            ((u8 *)artifact + handler_offset);
        if (*handler != NULL) {
            (*handler)(artifact->data);
        }
    }
}

void bench_warm_up(B *b, usize runs) {
    if (b != NULL) {
        b->warm_up_runs = runs;
    }
}

void bench_runs(B *b, usize runs) {
    if (b != NULL) {
        b->runs = runs;
    }
}

void bench_run(B *b, bench_func func, ptr data) {
    struct timespec start;
    struct timespec end;
    f64 elapsed;
    f64 nanoseconds_per_run;

    if (b == NULL || func == NULL) {
        return;
    }

    for (usize index = 0; index < b->warm_up_runs; index++) {
        if (monitor_file != NULL) {
            monitor_begin("startBenchWarmUp");
            fprintf(monitor_file, "\"benchIndex\":%zu,\"ith\":%zu,\"timestamp\":%lld",
                b->index, index, (long long)now());
            monitor_end();
        }
        func(data);
        if (monitor_file != NULL) {
            monitor_begin("stopBenchWarmUp");
            fprintf(monitor_file, "\"benchIndex\":%zu,\"ith\":%zu,\"timestamp\":%lld",
                b->index, index, (long long)now());
            monitor_end();
        }
    }

    if (timespec_get(&start, TIME_UTC) != TIME_UTC) return;
    for (usize index = 0; index < b->runs; index++) {
        if (monitor_file != NULL) {
            monitor_begin("startBenchRun");
            fprintf(monitor_file, "\"benchIndex\":%zu,\"ith\":%zu,\"timestamp\":%lld",
                b->index, index, (long long)now());
            monitor_end();
        }
        func(data);
        if (monitor_file != NULL) {
            monitor_begin("stopBenchRun");
            fprintf(monitor_file, "\"benchIndex\":%zu,\"ith\":%zu,\"timestamp\":%lld",
                b->index, index, (long long)now());
            monitor_end();
        }
    }
    if (timespec_get(&end, TIME_UTC) != TIME_UTC) return;

    elapsed = (f64)(end.tv_sec - start.tv_sec) +
        (f64)(end.tv_nsec - start.tv_nsec) / 1000000000.0;
    if (elapsed < 0) return;
    nanoseconds_per_run = b->runs == 0 ? 0.0 : elapsed * 1000000000.0 / (f64)b->runs;
    b->elapsed_seconds = elapsed;
    b->nanoseconds_per_run = nanoseconds_per_run;
    b->measured = 1;
    printf("%.*s: %zu runs in %.9f s (%.3f ns/run)\n",
        (int)b->name.len, (const char *)b->name.data, b->runs, elapsed, nanoseconds_per_run);
}

typedef struct {
    const char * output_path;
    const char * monitor_path;
    const char * metadata_path;
} RunnerOptions;

static u8 parse_options(int argc, char **argv, RunnerOptions *options) {
    for (int index = 1; index < argc; ++index) {
        const char **slot = NULL;
        const char *value = NULL;
        const char *arg = argv[index];
        if (strcmp(arg, "--output") == 0 || strncmp(arg, "--output=", 9) == 0) {
            slot = &options->output_path;
            if (arg[8] == '=') value = arg + 9;
        } else if (strcmp(arg, "--monitor") == 0 || strncmp(arg, "--monitor=", 10) == 0) {
            slot = &options->monitor_path;
            if (arg[9] == '=') value = arg + 10;
        } else if (strcmp(arg, "--emit-metadata") == 0 || strncmp(arg, "--emit-metadata=", 16) == 0) {
            slot = &options->metadata_path;
            if (arg[15] == '=') value = arg + 16;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", arg);
            return 0;
        }
        if (*slot) { fputs("Repeated output option.\n", stderr); return 0; }
        if (!value && index + 1 < argc) value = argv[++index];
        if (!value || !*value || *value == '-') {
            fputs("Output options require a nonempty path.\n", stderr);
            return 0;
        }
        *slot = value;
    }
    if (options->metadata_path && (options->output_path || options->monitor_path)) {
        fputs("--emit-metadata cannot be combined with output or monitor.\n", stderr);
        return 0;
    }
    if (options->output_path && options->monitor_path &&
        strcmp(options->output_path, options->monitor_path) == 0) {
        fputs("Report and monitor require different paths.\n", stderr);
        return 0;
    }
    return 1;
}

static u8 write_metadata(const char * path) {
    FILE *file = NULL;
    u8 failed;
    if (fopen_s(&file, path, "wb") != 0) {
        fprintf(stderr, "Could not open metadata file: %s\n", path);
        return 0;
    }
    if (test_functions_count > 1) qsort(test_functions, test_functions_count, sizeof(test_functions[0]), compare_named_functions);
    if (testartifact_functions_count > 1) qsort(testartifact_functions, testartifact_functions_count,
        sizeof(testartifact_functions[0]), compare_named_functions);
    if (testbench_functions_count > 1) qsort(testbench_functions, testbench_functions_count,
        sizeof(testbench_functions[0]), compare_named_functions);
    fprintf(file, "{\"schema\":1,\"testCount\":%zu,\"artifactCount\":%zu,"
        "\"benchmarkCount\":%zu,\"functionCount\":%zu,\"tests\":[",
        test_functions_count, testartifact_functions_count, testbench_functions_count,
        test_functions_count + testartifact_functions_count + testbench_functions_count);
    for (usize index = 0; index < test_functions_count; index++) {
        if (index != 0) fputc(',', file);
        json_string(file, test_functions[index].name);
    }
    fputs("],\"artifacts\":[", file);
    for (usize index = 0; index < testartifact_functions_count; index++) {
        if (index != 0) fputc(',', file);
        json_string(file, testartifact_functions[index].name);
    }
    fputs("],\"benchmarks\":[", file);
    for (usize index = 0; index < testbench_functions_count; index++) {
        if (index != 0) fputc(',', file);
        json_string(file, testbench_functions[index].name);
    }
    fputs("]}\n", file);
    failed = ferror(file) != 0;
    if (fclose(file) != 0) failed = 1;
    if (failed) fprintf(stderr, "Could not finish metadata file: %s\n", path);
    return !failed;
}

static void write_test_json(FILE *file, const TestRun *run) {
    fputs("{\"testIndex\":", file);
    fprintf(file, "%zu,\"parent\":", run->index);
    if (run->has_parent) {
        fprintf(file, "%zu", run->parent_index);
    } else {
        fputs("null", file);
    }
    fputs(",\"name\":", file);
    json_string(file, run->name);
    fprintf(file, ",\"startedAt\":%lld,\"stoppedAt\":%lld,\"passed\":%s,\"logs\":[",
        (long long)run->started_at, (long long)run->stopped_at,
        run->passed ? "true" : "false");
    for (usize index = 0; index < run->logs.count; index++) {
        const TestLog *log = g_vec_at((g_vec *)&run->logs, index);
        if (index > 0) {
            fputc(',', file);
        }
        fputs("{\"file\":", file);
        json_cstr(file, log->file);
        fprintf(file, ",\"line\":%d,\"message\":", log->line);
        json_string(file, log->message);
        fprintf(file, ",\"timestamp\":%lld}", (long long)log->at);
    }
    fputs("],\"tests\":[", file);
    for (usize index = 0; index < run->inner_runs.count; index++) {
        if (index > 0) {
            fputc(',', file);
        }
        write_test_json(file, g_vec_at((g_vec *)&run->inner_runs, index));
    }
    fputs("]}", file);
}

static u8 write_output(const char * path, time_t run_at, const C *cache,
    const g_vec *runs, const g_vec *benchmarks) {
    FILE *file;
    struct tm local_time;
    char local_text[64];

    if (path == NULL) {
        return 1;
    }
    if (fopen_s(&file, path, "wb") != 0) {
        fprintf(stderr, "Could not open output file: %s\n", path);
        return 0;
    }

    if (localtime_s(&local_time, &run_at) != 0 ||
        strftime(local_text, sizeof(local_text), "%Y-%m-%d %H:%M:%S %z", &local_time) == 0) {
        strcpy_s(local_text, sizeof(local_text), "unknown");
    }

    fprintf(file, "{\"runAt\":%lld,\"runAtLocal\":", (long long)run_at);
    json_cstr(file, local_text);
    fputs(",\"artifacts\":[", file);
    for (usize index = 0; index < cache->artifacts.count; index++) {
        const TestArtifact *artifact = g_vec_at((g_vec *)&cache->artifacts, index);
        if (index > 0) {
            fputc(',', file);
        }
        json_string(file, artifact->name);
    }
    fputs("],\"tests\":[", file);
    for (usize index = 0; index < runs->count; index++) {
        if (index > 0) {
            fputc(',', file);
        }
        write_test_json(file, g_vec_at((g_vec *)runs, index));
    }
    fputs("],\"benchmarks\":[", file);
    for (usize index = 0; index < benchmarks->count; index++) {
        const B *benchmark = g_vec_at((g_vec *)benchmarks, index);
        if (index > 0) {
            fputc(',', file);
        }
        fputs("{\"benchIndex\":", file);
        fprintf(file, "%zu,\"name\":", benchmark->index);
        json_string(file, benchmark->name);
        fprintf(file,
            ",\"warmUpRuns\":%zu,\"runs\":%zu,\"elapsedSeconds\":%.9f,\"nanosecondsPerRun\":%.3f}",
            benchmark->warm_up_runs, benchmark->runs, benchmark->elapsed_seconds,
            benchmark->nanoseconds_per_run);
    }
    fputs("]}\n", file);
    u8 write_failed = ferror(file) != 0;
    if (fclose(file) != 0) write_failed = 1;
    if (write_failed) {
        fprintf(stderr, "Could not finish writing output file: %s\n", path);
        return 0;
    }
    return 1;
}

int test_main(int argc, char **argv, test_suite *suite) {
    if (!suite || (suite->test_count && !suite->tests) ||
        (suite->artifact_count && !suite->artifacts) ||
        (suite->benchmark_count && !suite->benchmarks)) return 2;
    test_functions = suite->tests;
    test_functions_count = suite->test_count;
    testartifact_functions = suite->artifacts;
    testartifact_functions_count = suite->artifact_count;
    testbench_functions = suite->benchmarks;
    testbench_functions_count = suite->benchmark_count;
    monitor_file = NULL;
    next_test_index = 0;
    for (usize i = 0; i < test_functions_count; ++i)
        if (!test_functions[i].func || !test_functions[i].name.data) return 2;
    for (usize i = 0; i < testartifact_functions_count; ++i)
        if (!testartifact_functions[i].func || !testartifact_functions[i].name.data) return 2;
    for (usize i = 0; i < testbench_functions_count; ++i)
        if (!testbench_functions[i].func || !testbench_functions[i].name.data) return 2;
    RunnerOptions options = { 0 };
    if (!parse_options(argc, argv, &options)) return 2;
    if (options.metadata_path != NULL) return write_metadata(options.metadata_path) ? 0 : 2;
    C cache = {
        .artifacts = new_vec(sizeof(TestArtifact), testartifact_functions_count)
    };
    g_vec runs = new_vec(sizeof(TestRun), test_functions_count);
    g_vec benchmarks = new_vec(sizeof(B), testbench_functions_count);
    time_t run_at = now();
    u8 failed = 0;

    if (options.monitor_path != NULL) {
        if (fopen_s(&monitor_file, options.monitor_path, "wb") != 0) {
            fprintf(stderr, "Could not open monitor file: %s\n", options.monitor_path);
            return 2;
        }
    }

    srand((unsigned int)time(NULL));
    monitor_suite_event("startSuite", run_at);
    for (usize index = 0; index < testartifact_functions_count; index++) {
        monitor_named_event("loadedArtifact", testartifact_functions[index].name);
    }
    for (usize index = 0; index < test_functions_count; index++) {
        monitor_named_event("loadedTest", test_functions[index].name);
    }
    for (usize index = 0; index < testbench_functions_count; index++) {
        monitor_named_event("loadedBench", testbench_functions[index].name);
    }

    if (testartifact_functions_count > 1) qsort(testartifact_functions, testartifact_functions_count,
        sizeof(testartifact_functions[0]), compare_named_functions);
    for (usize index = 0; index < testartifact_functions_count; index++) {
        testartifact_functions[index].func(&cache);
    }

    if (cache.failed) {
        fputs("Failed to register test artifacts.\n", stderr);
        g_vec_destroy(&cache.artifacts);
        g_vec_destroy(&runs);
        g_vec_destroy(&benchmarks);
        monitor_suite_event("stopSuite", now());
        if (monitor_file != NULL) {
            fclose(monitor_file);
        }
        return 1;
    }

    run_artifact_handlers(&cache, offsetof(TestArtifact, before_all));
    shuffle(test_functions, test_functions_count, sizeof(test_functions[0]));
    for (usize index = 0; index < test_functions_count; index++) {
        TestRun run = {
            .name = test_functions[index].name,
            .logs = new_vec(sizeof(TestLog), 0),
            .inner_runs = new_vec(sizeof(TestRun), 0),
            .index = next_test_index++,
            .passed = 1
        };
        T context = {
            .cache = &cache,
            .run = &run
        };

        run_artifact_handlers(&cache, offsetof(TestArtifact, before_each));
        execute_test(&context, test_functions[index].func);
        run_artifact_handlers(&cache, offsetof(TestArtifact, after_each));
        print_test_run(&run, 0);
        if (!run.passed) {
            failed = 1;
        }
        if (!vec_append(&runs, &run)) {
            test_run_delete(&run);
            failed = 1;
        }
    }
    run_artifact_handlers(&cache, offsetof(TestArtifact, after_all));

    shuffle(testbench_functions, testbench_functions_count, sizeof(testbench_functions[0]));
    for (usize index = 0; index < testbench_functions_count; index++) {
        B benchmark = {
            .name = testbench_functions[index].name,
            .warm_up_runs = 3,
            .runs = 1,
            .index = index
        };
        monitor_begin("startBench");
        if (monitor_file != NULL) {
            fprintf(monitor_file, "\"benchIndex\":%zu,\"name\":", benchmark.index);
            json_string(monitor_file, benchmark.name);
            fprintf(monitor_file, ",\"timestamp\":%lld", (long long)now());
            monitor_end();
        }
        testbench_functions[index].func(&benchmark);
        if (!benchmark.measured) failed = 1;
        if (monitor_file != NULL) {
            monitor_begin("stopBench");
            fprintf(monitor_file,
                "\"benchIndex\":%zu,\"timestamp\":%lld,\"runs\":%zu,\"elapsedSeconds\":%.9f",
                benchmark.index, (long long)now(), benchmark.runs, benchmark.elapsed_seconds);
            monitor_end();
        }
        if (!vec_append(&benchmarks, &benchmark)) {
            failed = 1;
        }
    }

    monitor_suite_event("stopSuite", now());
    if (!write_output(options.output_path, run_at, &cache, &runs, &benchmarks)) {
        failed = 1;
    }
    for (usize index = 0; index < runs.count; index++) {
        test_run_delete(g_vec_at(&runs, index));
    }
    g_vec_destroy(&runs);
    g_vec_destroy(&benchmarks);
    g_vec_destroy(&cache.artifacts);
    if (monitor_file != NULL) {
        if (ferror(monitor_file)) failed = 1;
        if (fclose(monitor_file) != 0) failed = 1;
        monitor_file = NULL;
    }
    return failed ? 1 : 0;
}
