#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#define _CRT_SECURE_NO_WARNINGS
#include "buildbe.h"
#include <stdio.h>
#include <errno.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

static buildbe_result status(buildbe_status code) {
    buildbe_result r = {code, 0}; return r;
}
typedef struct command { g_vec args; u8 failed; } command;
static void add(command *c, const char *prefix, const char *value) {
    if (c->failed) return;
    if (!value) { c->failed = 1; return; }
    usize a = strlen(prefix), b = strlen(value);
    char *text = malloc(a + b + 1);
    if (!text) { c->failed = 1; return; }
    memcpy(text, prefix, a); memcpy(text + a, value, b + 1);
    if (g_vec_push(&c->args, &text) != G_OK) { free(text); c->failed = 1; }
}
static void list(command *c, const char *prefix, buildbe_strings values) {
    for (usize i = 0; i < values.count; ++i) add(c, prefix, values.items[i]);
}
static command begin(const char *tool) {
    command c = {0};
    (void)g_vec_init(&c.args, sizeof(char *), NULL);
    add(&c, "", tool);
    return c;
}
#ifdef _WIN32
static u8 byte(g_vec *v, char c) { return (u8)(g_vec_push(v, &c) == G_OK); }
static buildbe_result process(char **args) {
    g_vec line;
    (void)g_vec_init(&line, 1, NULL);
    u8 ok = 1;
    for (usize i = 0; args[i] && ok; ++i) {
        if (i) ok = byte(&line, ' ');
        ok = ok && byte(&line, '"');
        usize slashes = 0;
        for (const char *p = args[i]; ok; ++p) {
            if (*p == '\\') { ++slashes; continue; }
            usize n = (*p == '"' || !*p) ? slashes * 2 : slashes;
            for (usize j = 0; j < n && ok; ++j) ok = byte(&line, '\\');
            slashes = 0;
            if (!*p) break;
            if (*p == '"') ok = ok && byte(&line, '\\');
            ok = ok && byte(&line, *p);
        }
        ok = ok && byte(&line, '"');
    }
    ok = ok && byte(&line, 0);
    if (!ok) { g_vec_destroy(&line); return status(BUILDBE_OUT_OF_MEMORY); }
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, line.data, -1, NULL, 0);
    wchar_t *wide = count > 0 ? malloc((usize)count * sizeof(wchar_t)) : NULL;
    if (!wide) { g_vec_destroy(&line); return status(BUILDBE_OUT_OF_MEMORY); }
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, line.data, -1, wide, count);
    STARTUPINFOW startup = {0};
    PROCESS_INFORMATION child = {0};
    startup.cb = sizeof(startup);
    buildbe_result r = status(BUILDBE_OK);
    if (!CreateProcessW(NULL, wide, NULL, NULL, TRUE, 0, NULL, NULL, &startup, &child)) {
        fprintf(stderr, "Could not start %s (Windows error %lu).\n", args[0], (unsigned long)GetLastError());
        r = status(BUILDBE_TOOL_NOT_FOUND);
    } else {
        DWORD code = 1;
        if (WaitForSingleObject(child.hProcess, INFINITE) != WAIT_OBJECT_0 ||
            !GetExitCodeProcess(child.hProcess, &code)) r = status(BUILDBE_IO_ERROR);
        else if (code) { r.status = BUILDBE_PROCESS_FAILED; r.exit_code = (int)code; }
        CloseHandle(child.hThread); CloseHandle(child.hProcess);
    }
    free(wide); g_vec_destroy(&line);
    return r;
}
static u8 tool_exists(const char *name) {
    wchar_t wide[BUILDBE_PATH_MAX], full[BUILDBE_PATH_MAX];
    if (!name || !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, wide, BUILDBE_PATH_MAX)) return 0;
    DWORD n = SearchPathW(NULL, wide, L".exe", BUILDBE_PATH_MAX, full, NULL);
    return (u8)(n && n < BUILDBE_PATH_MAX);
}
#else
static buildbe_result process(char **args) {
    pid_t child = fork();
    if (child < 0) return status(BUILDBE_IO_ERROR);
    if (!child) { execvp(args[0], args); perror(args[0]); _exit(127); }
    int value;
    while (waitpid(child, &value, 0) < 0) if (errno != EINTR) return status(BUILDBE_IO_ERROR);
    buildbe_result r = status(BUILDBE_OK);
    if (!WIFEXITED(value) || WEXITSTATUS(value)) {
        r.status = BUILDBE_PROCESS_FAILED;
        r.exit_code = WIFEXITED(value) ? WEXITSTATUS(value) : 128 + WTERMSIG(value);
    }
    return r;
}
static u8 tool_exists(const char *name) {
    if (!name) return 0;
    if (strchr(name, '/')) return (u8)(access(name, X_OK) == 0);
    const char *path = getenv("PATH");
    if (!path) return 0;
    do {
        const char *end = strchr(path, ':');
        usize len = end ? (usize)(end - path) : strlen(path);
        char candidate[BUILDBE_PATH_MAX];
        if (len + strlen(name) + 2 < sizeof(candidate)) {
            if (len) { memcpy(candidate, path, len); candidate[len] = '/'; strcpy(candidate + len + 1, name); }
            else strcpy(candidate, name);
            if (access(candidate, X_OK) == 0) return 1;
        }
        path = end ? end + 1 : NULL;
    } while (path);
    return 0;
}
#endif
static buildbe_result finish(command *c) {
    char *end = NULL;
    if (g_vec_push(&c->args, &end) != G_OK) c->failed = 1;
    buildbe_result r = c->failed ? status(BUILDBE_OUT_OF_MEMORY) : process(c->args.data);
    for (usize i = 0; i < c->args.count; ++i) free(*(char **)g_vec_at(&c->args, i));
    g_vec_destroy(&c->args);
    return r;
}
static buildbe_result check(const buildbe_toolchain *t) {
    if (!t || !t->compiler || !t->archiver || !t->linker) return status(BUILDBE_INVALID);
#ifdef _WIN32
    if (t->target.os != BUILDBE_OS_WINDOWS || t->target.compiler != BUILDBE_COMPILER_MSVC)
        return status(BUILDBE_UNSUPPORTED);
    const char *arch = getenv("VSCMD_ARG_TGT_ARCH");
    const char *expected = t->target.arch == BUILDBE_ARCH_ARM64 ? "arm64" : "x64";
    if (!arch || strcmp(arch, expected)) {
        fprintf(stderr, "Initialize the MSVC environment for %s using Setup.ps1.\n", expected);
        return status(BUILDBE_UNSUPPORTED);
    }
#else
#ifdef __APPLE__
    if (t->target.os != BUILDBE_OS_DARWIN || t->target.compiler != BUILDBE_COMPILER_CLANG)
        return status(BUILDBE_UNSUPPORTED);
#else
    if (t->target.os != BUILDBE_OS_LINUX || (t->target.compiler != BUILDBE_COMPILER_GCC &&
        t->target.compiler != BUILDBE_COMPILER_CLANG)) return status(BUILDBE_UNSUPPORTED);
#endif
    if (t->target.arch != buildbe_host_arch()) return status(BUILDBE_UNSUPPORTED);
#endif
    if (t->target.arch != BUILDBE_ARCH_AMD64 && t->target.arch != BUILDBE_ARCH_ARM64)
        return status(BUILDBE_UNSUPPORTED);
    if (!tool_exists(t->compiler) || !tool_exists(t->archiver) || !tool_exists(t->linker))
        return status(BUILDBE_TOOL_NOT_FOUND);
    return status(BUILDBE_OK);
}
static void target_flags(command *c, const buildbe_toolchain *t) {
    if (t->target.os == BUILDBE_OS_DARWIN) {
        add(c, "", "-arch");
        add(c, "", t->target.arch == BUILDBE_ARCH_ARM64 ? "arm64" : "x86_64");
    }
    if (t->sysroot) {
        if (t->target.compiler == BUILDBE_COMPILER_MSVC) c->failed = 1;
        else add(c, "--sysroot=", t->sysroot);
    }
    if (t->target_triple) {
        if (t->target.compiler != BUILDBE_COMPILER_CLANG) c->failed = 1;
        else add(c, "--target=", t->target_triple);
    }
}
static buildbe_result compile(const buildbe_toolchain *t, const buildbe_compile *r) {
    if (!t || !r || !r->source || !r->object) return status(BUILDBE_INVALID);
    if (t->target.compiler == BUILDBE_COMPILER_MSVC && r->dependency_file)
        return status(BUILDBE_UNSUPPORTED);
    command c = begin(t->compiler);
    if (t->target.compiler == BUILDBE_COMPILER_MSVC) {
        add(&c, "", "/nologo"); add(&c, "", "/std:c11"); add(&c, "", "/W4");
        add(&c, "", "/O2"); add(&c, "", "/c");
        add(&c, "/Fo", r->object);
        list(&c, "/I", r->include_paths); list(&c, "/D", r->defines);
    } else {
        target_flags(&c, t);
        add(&c, "", "-std=c11"); add(&c, "", "-Wall"); add(&c, "", "-Wextra");
        add(&c, "", "-O2"); add(&c, "", "-c");
        add(&c, "", "-o"); add(&c, "", r->object);
        list(&c, "-I", r->include_paths); list(&c, "-D", r->defines);
        if (r->dependency_file) { add(&c, "", "-MMD"); add(&c, "", "-MF"); add(&c, "", r->dependency_file); }
    }
    list(&c, "", t->compile_flags); list(&c, "", r->flags); add(&c, "", r->source);
    return finish(&c);
}
static buildbe_result archive(const buildbe_toolchain *t, const buildbe_archive *r) {
    if (!t || !r || !r->output || !r->objects.count) return status(BUILDBE_INVALID);
    command c = begin(t->archiver);
    if (t->target.compiler == BUILDBE_COMPILER_MSVC) {
        add(&c, "", "/nologo"); add(&c, "/OUT:", r->output);
    } else {
        /* Fresh archive avoids stale members left after source deletion. */
        if (remove(r->output) != 0 && errno != ENOENT) { c.failed = 1; }
        add(&c, "", "rcs"); add(&c, "", r->output);
    }
    list(&c, "", t->archive_flags); list(&c, "", r->objects);
    return finish(&c);
}
static buildbe_result link_binary(const buildbe_toolchain *t, const buildbe_link *r) {
    if (!t || !r || !r->output) return status(BUILDBE_INVALID);
    command c = begin(t->linker);
    if (t->target.compiler == BUILDBE_COMPILER_MSVC) {
        add(&c, "", "/nologo"); add(&c, "/OUT:", r->output);
        list(&c, "/LIBPATH:", r->library_paths);
    } else {
        target_flags(&c, t); add(&c, "", "-o"); add(&c, "", r->output);
        list(&c, "-L", r->library_paths);
    }
    list(&c, "", r->inputs);
    list(&c, t->target.compiler == BUILDBE_COMPILER_MSVC ? "" : "-l", r->system_libraries);
    list(&c, "", t->link_flags); list(&c, "", r->flags);
    return finish(&c);
}
static buildbe_result run(const buildbe_run *r) {
    if (!r || !r->executable) return status(BUILDBE_INVALID);
    if (r->working_directory || r->stdout_path || r->stderr_path) return status(BUILDBE_UNSUPPORTED);
    command c = begin(r->executable); list(&c, "", r->arguments); return finish(&c);
}

#define BACKEND(label, os_value, compiler_value) \
    {BUILDBE_ABI, label, os_value, compiler_value, check, compile, archive, \
     link_binary, NULL, run, buildbe_host_info, buildbe_host_copy, NULL}
const buildbe_backend buildbe_windows = BACKEND("windows/msvc", BUILDBE_OS_WINDOWS, BUILDBE_COMPILER_MSVC);
const buildbe_backend buildbe_linux_gcc = BACKEND("linux/gcc", BUILDBE_OS_LINUX, BUILDBE_COMPILER_GCC);
const buildbe_backend buildbe_linux_clang = BACKEND("linux/clang", BUILDBE_OS_LINUX, BUILDBE_COMPILER_CLANG);
const buildbe_backend buildbe_darwin = BACKEND("darwin/clang", BUILDBE_OS_DARWIN, BUILDBE_COMPILER_CLANG);

const buildbe_backend *buildbe_select(buildbe_os os, buildbe_compiler compiler) {
    const buildbe_backend *all[] = {&buildbe_windows, &buildbe_linux_gcc, &buildbe_linux_clang, &buildbe_darwin};
    for (usize i = 0; i < sizeof(all) / sizeof(all[0]); ++i)
        if (all[i]->os == os && all[i]->compiler == compiler) return all[i];
    return NULL;
}
u8 buildbe_valid(const buildbe_backend *backend) {
    return (u8)(backend && backend->abi == BUILDBE_ABI && backend->check &&
                backend->compile && backend->archive && backend->link);
}
