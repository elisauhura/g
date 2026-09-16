#pragma once

#include "g.h"

/* Hosted test runner, modeled on C:/src/c's test library. */
typedef struct T T;
typedef struct C C;
typedef struct B B;
typedef struct test_text {
    const u8 *data;
    usize len;
} test_text;

/* String literals only. Other byte slices can be constructed explicitly. */
#define test_s(literal) ((test_text){(const u8 *)(literal), sizeof(literal) - 1})
#define test_l(t, message) test_log((t), __FILE__, __LINE__, (message))
#define test_check(t, condition) do { \
    if (!(condition)) { test_l((t), test_s(#condition)); test_fail((t)); } \
} while (0)

typedef void (*test_func)(T *t);
typedef void (*testartifact_func)(C *c);
typedef void (*testartifact_handler)(ptr data);
typedef void (*bench_func)(ptr data);
typedef void (*testbench_func)(B *b);

typedef struct test_case { test_text name; test_func func; } test_case;
typedef struct test_artifact_case { test_text name; testartifact_func func; } test_artifact_case;
typedef struct test_benchmark_case { test_text name; testbench_func func; } test_benchmark_case;

typedef struct test_suite {
    test_case *tests;
    usize test_count;
    test_artifact_case *artifacts;
    usize artifact_count;
    test_benchmark_case *benchmarks;
    usize benchmark_count;
} test_suite;

/*
 * Tables are mutable: artifacts sort by name, tests/benchmarks are shuffled.
 * Names, log text, and fixture data are borrowed until test_main returns.
 * Null tables are valid only for zero counts. No parallel/reentrant execution.
 * Returns 0 on success, 1 on suite/report failure, 2 on argument/setup error.
 * No main is supplied; call this from a small explicit or generated entry point.
 */
int test_main(int argc, char **argv, test_suite *suite);
void test_log(T *t, const char *file, int line, test_text message);
void test_fail(T *t);
void test_run(T *t, test_func *func, test_text name);
C *test_getcache(T *t);

/* Hooks surround top-level tests only; nested tests share the parent's cache. */
void test_add_artifact(C *c, test_text name, ptr data,
    testartifact_handler before_all, testartifact_handler before_each,
    testartifact_handler after_each, testartifact_handler after_all);
void bench_warm_up(B *b, usize runs);
void bench_runs(B *b, usize runs);
void bench_run(B *b, bench_func func, ptr data);
