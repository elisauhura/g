#include "test.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    usize id;
} ArtifactData;

static ArtifactData alpha = { .id = 1 };
static ArtifactData zeta = { .id = 2 };
static usize artifact_events[10];
static usize artifact_event_count;

static void ArtifactBeforeAll(ptr data) {
    ArtifactData *artifact = data;
    artifact_events[artifact_event_count++] = 10 + artifact->id;
}

static void ArtifactBeforeEach(ptr data) {
    ArtifactData *artifact = data;
    artifact_events[artifact_event_count++] = 20 + artifact->id;
}

static void ArtifactAfterEach(ptr data) {
    ArtifactData *artifact = data;
    artifact_events[artifact_event_count++] = 30 + artifact->id;
}

static void ArtifactAfterAll(ptr data) {
    ArtifactData *artifact = data;
    artifact_events[artifact_event_count++] = 40 + artifact->id;
}

void TestArtifact_Zeta(C *c) {
    artifact_events[artifact_event_count++] = zeta.id;
    test_add_artifact(c, test_s("zeta"), &zeta,
        ArtifactBeforeAll, ArtifactBeforeEach, ArtifactAfterEach, ArtifactAfterAll);
}

void TestArtifact_Alpha(C *c) {
    artifact_events[artifact_event_count++] = alpha.id;
    test_add_artifact(c, test_s("alpha"), &alpha,
        ArtifactBeforeAll, ArtifactBeforeEach, ArtifactAfterEach, ArtifactAfterAll);
}

static void Nested(T *t) {
    if (test_getcache(t) == NULL) {
        test_fail(t);
    }
}

void Test_DoNothing(T *t) {
    const usize expected[] = { 1, 2, 11, 12, 21, 22 };
    test_func nested = Nested;

    test_l(t, test_s("This test does nothing"));
    if (artifact_event_count != sizeof(expected) / sizeof(expected[0])) {
        test_fail(t);
    } else {
        for (usize index = 0; index < artifact_event_count; index++) {
            if (artifact_events[index] != expected[index]) {
                test_fail(t);
            }
        }
    }
    test_run(t, &nested, test_s("nested"));
}

static void Bench_DoNothing(ptr data) {
    usize *calls = data;
    (*calls)++;
}

void TestBench_DoNothing(B *b) {
    usize calls = 0;

    bench_runs(b, 10);
    bench_warm_up(b, 2);
    bench_run(b, Bench_DoNothing, &calls);
    if (calls != 12) abort();
}
static void FailureChild(T *t) {
    test_l(t, test_s("line one\n\"quoted\"\\path"));
    test_check(t, 0);
}
static void Test_Failure(T *t) {
    test_func child = FailureChild;
    test_run(t, &child, test_s("failure child"));
}
static void MustNotRegister(C *c) {
    (void)c;
    abort(); /* Metadata-only mode must never execute registration callbacks. */
}
static void MustNotRun(T *t) { (void)t; abort(); }
static void MustNotBenchmark(B *b) { (void)b; abort(); }

int main(int argc, char **argv) {
    test_case tests[] = {{test_s("Test_DoNothing"), Test_DoNothing}};
    test_artifact_case artifacts[] = {
        {test_s("TestArtifact_Zeta"), TestArtifact_Zeta},
        {test_s("TestArtifact_Alpha"), TestArtifact_Alpha}
    };
    test_benchmark_case benchmarks[] = {{test_s("TestBench_DoNothing"), TestBench_DoNothing}};
    test_suite suite = {tests, 1, artifacts, 2, benchmarks, 1};
    const char *mode = NULL;
    if (argc > 1 && strncmp(argv[1], "--fixture=", 10) == 0) {
        mode = argv[1] + 10;
        ++argv;
        --argc;
    }
    if (mode && strcmp(mode, "failure") == 0) {
        tests[0].func = Test_Failure;
        suite.artifact_count = 0;
        suite.benchmark_count = 0;
    } else if (mode && strcmp(mode, "metadata") == 0) {
        tests[0].func = MustNotRun;
        artifacts[0].func = artifacts[1].func = MustNotRegister;
        benchmarks[0].func = MustNotBenchmark;
    } else if (mode && strcmp(mode, "empty") == 0) {
        memset(&suite, 0, sizeof(suite));
    }
    int result = test_main(argc, argv, &suite);
    if (!mode && result == 0) {
        const usize expected[] = {1, 2, 11, 12, 21, 22, 31, 32, 41, 42};
        if (artifact_event_count != sizeof(expected) / sizeof(expected[0])) return 1;
        for (usize i = 0; i < artifact_event_count; ++i)
            if (artifact_events[i] != expected[i]) return 1;
    }
    return result;
}
