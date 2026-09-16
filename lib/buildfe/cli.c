#define _CRT_SECURE_NO_WARNINGS
#include "buildfe.h"
#include "cmdline.h"
#include <stdio.h>

static int usage(int code) {
    FILE *out = code ? stderr : stdout;
    fputs("Usage: g build [all|lib/name|cmd/name|tests/name ...]\n"
          "       g lib list [--project=PATH]\n"
          "       g cmd list [--project=PATH]\n"
          "       g tests list [--project=PATH]\n"
          "  --project=PATH       Project root (default: current directory)\n"
          "  --target=OS/BACKEND  windows/msvc, linux/gcc, linux/clang, darwin/clang\n"
          "  --arch=ARCH          amd64 or arm64 (default: native build architecture)\n"
          "  --force              Copy sources even when timestamps match\n"
          "  --help, -h           Show this help\n", out);
    return code;
}
static u8 equal(str value, const char *literal) {
    usize n = strlen(literal);
    return (u8)(value.len == n && !memcmp(value.data, literal, n));
}
static const char *value(cmdline *p, const cmdline_token *option) {
    if (option->kind == CMDLINE_PARAMETER)
        return option->value.len ? (const char *)option->value.data : NULL;
    cmdline_token next;
    if (cmdline_next(p, &next) != G_OK || next.kind != CMDLINE_ARGUMENT || !next.value.len)
        return NULL;
    return (const char *)next.value.data;
}
static int compare_names(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}
static int list_units(cmdline *parser, buildfe_unit_kind kind) {
    cmdline_token token;
    const char *root = ".";
    if (cmdline_next(parser, &token) != G_OK || token.kind != CMDLINE_ARGUMENT ||
        !equal(token.value, "list")) return usage(2);
    while (cmdline_next(parser, &token) == G_OK && token.kind != CMDLINE_END) {
        if ((token.kind == CMDLINE_LONG_FLAG && equal(token.name, "help")) ||
            (token.kind == CMDLINE_SHORT_FLAG && equal(token.name, "h"))) return usage(0);
        if ((token.kind != CMDLINE_PARAMETER && token.kind != CMDLINE_LONG_FLAG) ||
            !equal(token.name, "project")) return usage(2);
        root = value(parser, &token);
        if (!root) return usage(2);
    }
    if (token.kind != CMDLINE_END) return usage(2);
    buildfe_project *project = NULL;
    buildfe_result result = buildfe_project_load(root, NULL, NULL, &project);
    if (result.status != BUILDFE_OK) {
        fprintf(stderr, "g list failed (frontend status %d).\n", result.status);
        return 1;
    }
    g_vec names;
    (void)g_vec_init(&names, sizeof(const char *), NULL);
    int code = 0;
    for (usize i = 0; i < buildfe_project_unit_count(project); ++i) {
        buildfe_unit_kind unit_kind;
        const char *name = buildfe_project_unit_name(project, i, &unit_kind);
        if (unit_kind == kind && g_vec_push(&names, &name) != G_OK) {
            fputs("Cannot allocate project listing.\n", stderr); code = 1; goto done;
        }
    }
    if (names.count > 1) qsort(names.data, names.count, sizeof(const char *), compare_names);
    for (usize i = 0; i < names.count; ++i)
        if (puts(*(const char *const *)g_vec_at_const(&names, i)) == EOF) { code = 1; break; }
done:
    g_vec_destroy(&names); buildfe_project_destroy(project);
    return code;
}
int buildfe_execute(int argc, char **argv) {
    cmdline parser;
    cmdline_token token;
    if (cmdline_init(&parser, argc, argv) != G_OK ||
        cmdline_next(&parser, &token) != G_OK) return usage(2);
    if ((token.kind == CMDLINE_LONG_FLAG && equal(token.name, "help")) ||
        (token.kind == CMDLINE_SHORT_FLAG && equal(token.name, "h"))) return usage(0);
    if (token.kind != CMDLINE_ARGUMENT) return usage(2);
    if (equal(token.value, "lib")) return list_units(&parser, BUILDFE_UNIT_LIBRARY);
    if (equal(token.value, "cmd")) return list_units(&parser, BUILDFE_UNIT_COMMAND);
    if (equal(token.value, "tests")) return list_units(&parser, BUILDFE_UNIT_TEST);
    if (!equal(token.value, "build")) return usage(2);
    buildfe_options options = {0};
    options.project_root = ".";
    options.toolchain.target.arch = buildbe_host_arch();
#ifdef _WIN32
    const char *target = "windows/msvc";
#elif defined(__APPLE__)
    const char *target = "darwin/clang";
#else
    const char *target = "linux/gcc";
#endif
    g_vec requested;
    (void)g_vec_init(&requested, sizeof(const char *), NULL);
    int exit_code = 2;
    while (cmdline_next(&parser, &token) == G_OK && token.kind != CMDLINE_END) {
        if (token.kind == CMDLINE_ARGUMENT) {
            const char *unit = (const char *)token.value.data;
            if (g_vec_push(&requested, &unit) != G_OK) goto done;
        } else if ((token.kind == CMDLINE_LONG_FLAG && equal(token.name, "help")) ||
                   (token.kind == CMDLINE_SHORT_FLAG && equal(token.name, "h"))) {
            exit_code = usage(0); goto done;
        } else if (token.kind == CMDLINE_LONG_FLAG && equal(token.name, "force")) {
            options.copy_mode = BUILDBE_COPY_ALWAYS;
        } else if (token.kind == CMDLINE_PARAMETER || token.kind == CMDLINE_LONG_FLAG) {
            if (equal(token.name, "project")) {
                options.project_root = value(&parser, &token);
                if (!options.project_root) goto bad;
            } else if (equal(token.name, "target")) {
                target = value(&parser, &token); if (!target) goto bad;
            } else if (equal(token.name, "arch")) {
                const char *arch = value(&parser, &token);
                if (!arch) goto bad;
                options.toolchain.target.arch = !strcmp(arch, "amd64") ? BUILDBE_ARCH_AMD64 :
                    !strcmp(arch, "arm64") ? BUILDBE_ARCH_ARM64 : BUILDBE_ARCH_INVALID;
                if (!options.toolchain.target.arch) goto bad;
            } else goto bad;
        } else goto bad;
    }
    if (token.kind != CMDLINE_END) goto bad;
    if (!strcmp(target, "windows/msvc")) {
        options.toolchain.target.os = BUILDBE_OS_WINDOWS;
        options.toolchain.target.compiler = BUILDBE_COMPILER_MSVC;
        options.toolchain.compiler = "cl.exe";
        options.toolchain.archiver = "lib.exe";
        options.toolchain.linker = "link.exe";
    } else if (!strcmp(target, "linux/gcc") || !strcmp(target, "linux/clang") || !strcmp(target, "darwin/clang")) {
        options.toolchain.target.os = !strncmp(target, "darwin/", 7) ? BUILDBE_OS_DARWIN : BUILDBE_OS_LINUX;
        options.toolchain.target.compiler = !strcmp(target, "linux/gcc") ? BUILDBE_COMPILER_GCC : BUILDBE_COMPILER_CLANG;
        options.toolchain.compiler = options.toolchain.target.compiler == BUILDBE_COMPILER_GCC ? "gcc" : "clang";
        options.toolchain.archiver = "ar";
        options.toolchain.linker = options.toolchain.compiler;
    } else goto bad;
    char target_name[128];
    snprintf(target_name, sizeof(target_name), "%s-%s-%s",
        options.toolchain.target.os == BUILDBE_OS_WINDOWS ? "windows" :
        options.toolchain.target.os == BUILDBE_OS_DARWIN ? "darwin" : "linux",
        options.toolchain.target.arch == BUILDBE_ARCH_ARM64 ? "arm64" : "amd64",
        options.toolchain.target.compiler == BUILDBE_COMPILER_MSVC ? "msvc" :
        options.toolchain.target.compiler == BUILDBE_COMPILER_GCC ? "gcc" : "clang");
    options.target_name = target_name;
    options.requested_units.items = requested.data;
    options.requested_units.count = requested.count;
    buildfe_project *project = NULL;
    buildfe_plan *plan = NULL;
    buildfe_result result = buildfe_project_load(options.project_root, NULL, NULL, &project);
    if (result.status == BUILDFE_OK) result = buildfe_plan_create(project, &options, NULL, &plan);
    if (result.status == BUILDFE_OK)
        result = buildfe_plan_execute(plan, buildbe_select(options.toolchain.target.os, options.toolchain.target.compiler), NULL);
    buildfe_plan_destroy(plan); buildfe_project_destroy(project);
    if (result.status != BUILDFE_OK)
        fprintf(stderr, "g build failed (frontend status %d, backend status %d, tool exit %d).\n",
                result.status, result.backend.status, result.backend.exit_code);
    exit_code = result.status == BUILDFE_OK ? 0 : 1;
    goto done;
bad:
    usage(2);
done:
    g_vec_destroy(&requested); return exit_code;
}
