#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif
#define _CRT_SECURE_NO_WARNINGS
#include "buildbe.h"
#include <stdio.h>
#include <errno.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#endif

static buildbe_result result(buildbe_status status) {
    buildbe_result r = {status, 0}; return r;
}
#ifdef _WIN32
static u8 wide(const char *text, wchar_t *out) {
    return (u8)(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1,
                                   out, BUILDBE_PATH_MAX) != 0);
}
static u8 utf8(const wchar_t *text, char *out) {
    return (u8)(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1,
                                   out, BUILDBE_PATH_MAX, NULL, NULL) != 0);
}
#endif

buildbe_arch buildbe_host_arch(void) {
#if defined(_M_ARM64) || defined(__aarch64__)
    return BUILDBE_ARCH_ARM64;
#elif defined(_M_X64) || defined(__x86_64__)
    return BUILDBE_ARCH_AMD64;
#else
    return BUILDBE_ARCH_INVALID;
#endif
}
buildbe_result buildbe_host_absolute(const char *path, char *out, usize capacity) {
    char buffer[BUILDBE_PATH_MAX];
    if (!path || !out || !capacity) return result(BUILDBE_INVALID);
#ifdef _WIN32
    wchar_t input[BUILDBE_PATH_MAX], full[BUILDBE_PATH_MAX];
    if (!wide(path, input)) return result(BUILDBE_INVALID);
    DWORD n = GetFullPathNameW(input, BUILDBE_PATH_MAX, full, NULL);
    if (!n || n >= BUILDBE_PATH_MAX || !utf8(full, buffer)) return result(BUILDBE_IO_ERROR);
#else
    /* realpath requires the input to exist; callers canonicalize the project root. */
    if (!realpath(path, buffer)) return result(BUILDBE_IO_ERROR);
#endif
    usize len = strlen(buffer);
    if (len >= capacity) return result(BUILDBE_INVALID);
    for (usize i = 0; i < len; ++i) if (buffer[i] == '\\') buffer[i] = '/';
    while (len > 1 && buffer[len - 1] == '/') buffer[--len] = 0;
    memcpy(out, buffer, len + 1);
    return result(BUILDBE_OK);
}
buildbe_result buildbe_host_info(const char *path, buildbe_file_info *out) {
    if (!path || !out) return result(BUILDBE_INVALID);
    memset(out, 0, sizeof(*out));
#ifdef _WIN32
    wchar_t name[BUILDBE_PATH_MAX];
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!wide(path, name)) return result(BUILDBE_INVALID);
    if (!GetFileAttributesExW(name, GetFileExInfoStandard, &info)) {
        DWORD error = GetLastError();
        return result(error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND
                      ? BUILDBE_OK : BUILDBE_IO_ERROR);
    }
    if (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) return result(BUILDBE_UNSUPPORTED);
    out->exists = 1;
    out->is_regular = (u8)!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
    out->size = ((u64)info.nFileSizeHigh << 32) | info.nFileSizeLow;
    u64 ticks = ((u64)info.ftLastWriteTime.dwHighDateTime << 32) | info.ftLastWriteTime.dwLowDateTime;
    out->modified_seconds = (i64)(ticks / 10000000) - INT64_C(11644473600);
    out->modified_nanoseconds = (u32)((ticks % 10000000) * 100);
    out->resolution_ns = 100;
#else
    struct stat info;
    if (lstat(path, &info) != 0)
        return result(errno == ENOENT || errno == ENOTDIR ? BUILDBE_OK : BUILDBE_IO_ERROR);
    if (S_ISLNK(info.st_mode)) return result(BUILDBE_UNSUPPORTED);
    out->exists = 1;
    out->is_regular = (u8)S_ISREG(info.st_mode);
    if (!out->is_regular && !S_ISDIR(info.st_mode)) return result(BUILDBE_UNSUPPORTED);
    out->size = (u64)info.st_size;
#ifdef __APPLE__
    out->modified_seconds = info.st_mtimespec.tv_sec;
    out->modified_nanoseconds = (u32)info.st_mtimespec.tv_nsec;
#else
    out->modified_seconds = info.st_mtim.tv_sec;
    out->modified_nanoseconds = (u32)info.st_mtim.tv_nsec;
#endif
    out->resolution_ns = 1;
#endif
    out->has_size = out->is_regular;
    out->has_modified = out->reliable_modified = 1;
    return result(BUILDBE_OK);
}
buildbe_result buildbe_host_list(const char *path, buildbe_visit visit, void *context) {
    if (!path || !visit) return result(BUILDBE_INVALID);
#ifdef _WIN32
    char pattern[BUILDBE_PATH_MAX];
    wchar_t wpattern[BUILDBE_PATH_MAX];
    WIN32_FIND_DATAW entry;
    if (snprintf(pattern, sizeof(pattern), "%s/*", path) >= (int)sizeof(pattern) ||
        !wide(pattern, wpattern)) return result(BUILDBE_INVALID);
    HANDLE find = FindFirstFileW(wpattern, &entry);
    if (find == INVALID_HANDLE_VALUE)
        return result(GetLastError() == ERROR_FILE_NOT_FOUND ? BUILDBE_OK : BUILDBE_IO_ERROR);
    buildbe_result r = result(BUILDBE_OK);
    do {
        if (!wcscmp(entry.cFileName, L".") || !wcscmp(entry.cFileName, L"..")) continue;
        if (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) { r = result(BUILDBE_UNSUPPORTED); break; }
        char name[BUILDBE_PATH_MAX];
        if (!utf8(entry.cFileName, name)) { r = result(BUILDBE_IO_ERROR); break; }
        r = visit(context, name, (u8)!!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY));
        if (r.status != BUILDBE_OK) break;
    } while (FindNextFileW(find, &entry));
    if (r.status == BUILDBE_OK && GetLastError() != ERROR_NO_MORE_FILES) r = result(BUILDBE_IO_ERROR);
    FindClose(find);
    return r;
#else
    DIR *dir = opendir(path);
    if (!dir) return result(BUILDBE_IO_ERROR);
    buildbe_result r = result(BUILDBE_OK);
    struct dirent *entry;
    errno = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char full[BUILDBE_PATH_MAX];
        if (snprintf(full, sizeof(full), "%s/%s", path, entry->d_name) >= (int)sizeof(full)) {
            r = result(BUILDBE_INVALID); break;
        }
        buildbe_file_info info;
        r = buildbe_host_info(full, &info);
        if (r.status != BUILDBE_OK) break;
        r = visit(context, entry->d_name, (u8)!info.is_regular);
        if (r.status != BUILDBE_OK) break;
        errno = 0;
    }
    if (r.status == BUILDBE_OK && errno) r = result(BUILDBE_IO_ERROR);
    closedir(dir);
    return r;
#endif
}
buildbe_result buildbe_host_mkdirs(const char *path) {
    char copy[BUILDBE_PATH_MAX];
    if (!path || strlen(path) >= sizeof(copy)) return result(BUILDBE_INVALID);
    strcpy(copy, path);
    usize len = strlen(copy);
    for (usize i = 1; i <= len; ++i) {
        if (copy[i] && copy[i] != '/' && copy[i] != '\\') continue;
        if (i == 2 && copy[1] == ':') continue;
        char saved = copy[i]; copy[i] = 0;
        buildbe_file_info info;
        buildbe_result r = buildbe_host_info(copy, &info);
        if (r.status != BUILDBE_OK || (info.exists && info.is_regular)) return result(BUILDBE_IO_ERROR);
        if (!info.exists) {
#ifdef _WIN32
            wchar_t wpath[BUILDBE_PATH_MAX];
            if (!wide(copy, wpath) || !CreateDirectoryW(wpath, NULL)) return result(BUILDBE_IO_ERROR);
#else
            if (mkdir(copy, 0777) != 0) return result(BUILDBE_IO_ERROR);
#endif
        }
        copy[i] = saved;
    }
    return result(BUILDBE_OK);
}
static u8 same(const buildbe_file_info *a, const buildbe_file_info *b) {
    return (u8)(a->exists && b->exists && a->is_regular && b->is_regular &&
        a->has_size && b->has_size && a->size == b->size &&
        a->has_modified && b->has_modified && a->reliable_modified && b->reliable_modified &&
        a->resolution_ns && a->resolution_ns == b->resolution_ns &&
        a->modified_seconds == b->modified_seconds &&
        a->modified_nanoseconds == b->modified_nanoseconds);
}
buildbe_copy_result buildbe_host_copy(const buildbe_copy *request) {
    buildbe_copy_result out = {{BUILDBE_INVALID, 0}, 0, {0}};
    buildbe_file_info source, dest, after;
    char parent[BUILDBE_PATH_MAX], temporary[BUILDBE_PATH_MAX];
    if (!request || !request->source || !request->destination ||
        strlen(request->destination) >= sizeof(parent)) return out;
    if (!strcmp(request->source, request->destination)) return out;
    out.result = buildbe_host_info(request->source, &source);
    if (out.result.status != BUILDBE_OK) return out;
    if (!source.exists || !source.is_regular) { out.result = result(BUILDBE_IO_ERROR); return out; }
    out.result = buildbe_host_info(request->destination, &dest);
    if (out.result.status != BUILDBE_OK) return out;
    if (request->mode == BUILDBE_COPY_IF_CHANGED && same(&source, &dest)) {
        out.destination = dest; return out;
    }
    strcpy(parent, request->destination);
    char *slash = strrchr(parent, '/');
    if (!slash) slash = strrchr(parent, '\\');
    if (slash) {
        *slash = 0;
        out.result = buildbe_host_mkdirs(parent);
        if (out.result.status != BUILDBE_OK) return out;
    }
#ifdef _WIN32
    static unsigned long sequence;
    wchar_t from[BUILDBE_PATH_MAX], to[BUILDBE_PATH_MAX], temp[BUILDBE_PATH_MAX];
    if (snprintf(temporary, sizeof(temporary), "%s.tmp-%lu-%lu", request->destination,
                 (unsigned long)GetCurrentProcessId(), ++sequence) >= (int)sizeof(temporary) ||
        !wide(request->source, from) || !wide(request->destination, to) || !wide(temporary, temp)) {
        out.result = result(BUILDBE_INVALID); return out;
    }
    if (!CopyFileW(from, temp, TRUE)) { out.result = result(BUILDBE_IO_ERROR); return out; }
    out.result = buildbe_host_info(request->source, &after);
    if (out.result.status == BUILDBE_OK && !same(&source, &after)) out.result = result(BUILDBE_IO_ERROR);
    if (out.result.status == BUILDBE_OK && !MoveFileExW(temp, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        out.result = result(BUILDBE_IO_ERROR);
    if (out.result.status != BUILDBE_OK) { DeleteFileW(temp); return out; }
#else
    if (snprintf(temporary, sizeof(temporary), "%s.tmp-XXXXXX", request->destination) >= (int)sizeof(temporary)) {
        out.result = result(BUILDBE_INVALID); return out;
    }
    int output = mkstemp(temporary), input = -1;
    if (output < 0) { out.result = result(BUILDBE_IO_ERROR); return out; }
    input = open(request->source, O_RDONLY);
    if (input < 0) out.result = result(BUILDBE_IO_ERROR);
    char buffer[65536];
    ssize_t n;
    while (out.result.status == BUILDBE_OK && (n = read(input, buffer, sizeof(buffer))) != 0) {
        if (n < 0) { if (errno == EINTR) continue; out.result = result(BUILDBE_IO_ERROR); break; }
        ssize_t offset = 0;
        while (offset < n) {
            ssize_t wrote = write(output, buffer + offset, (size_t)(n - offset));
            if (wrote < 0 && errno == EINTR) continue;
            if (wrote <= 0) { out.result = result(BUILDBE_IO_ERROR); break; }
            offset += wrote;
        }
    }
    if (input >= 0) close(input);
    if (out.result.status == BUILDBE_OK) {
        out.result = buildbe_host_info(request->source, &after);
        if (out.result.status == BUILDBE_OK && !same(&source, &after)) out.result = result(BUILDBE_IO_ERROR);
    }
    if (request->preserve_modified_time && out.result.status == BUILDBE_OK) {
        struct timespec times[2] = {{0, UTIME_OMIT}, {source.modified_seconds, source.modified_nanoseconds}};
        /* Failure to preserve time is allowed; actual output metadata is returned. */
        (void)futimens(output, times);
    }
    if (close(output) != 0) out.result = result(BUILDBE_IO_ERROR);
    if (out.result.status == BUILDBE_OK && rename(temporary, request->destination) != 0)
        out.result = result(BUILDBE_IO_ERROR);
    if (out.result.status != BUILDBE_OK) { unlink(temporary); return out; }
#endif
    out.copied = 1;
    out.result = buildbe_host_info(request->destination, &out.destination);
    return out;
}
