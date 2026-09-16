#pragma once

#include "g.h"

/*
 * Initial backend API; declarations only. Platform implementations will live
 * under lib/buildbe/. No compiler discovery or process execution is implemented
 * by this header. Backend names describe target OS/compiler combinations, not
 * the host OS; check() must validate host tools and target compatibility.
 */
#define BUILDBE_ABI 1u

typedef enum buildbe_arch {
    BUILDBE_ARCH_INVALID,
    BUILDBE_ARCH_AMD64,
    BUILDBE_ARCH_ARM64
} buildbe_arch;

typedef enum buildbe_os {
    BUILDBE_OS_INVALID,
    BUILDBE_OS_WINDOWS,
    BUILDBE_OS_DARWIN,
    BUILDBE_OS_LINUX,
    BUILDBE_OS_XK,
    BUILDBE_OS_BAREMETAL
} buildbe_os;

typedef enum buildbe_compiler {
    BUILDBE_COMPILER_INVALID,
    BUILDBE_COMPILER_GCC,
    BUILDBE_COMPILER_CLANG,
    BUILDBE_COMPILER_MSVC
} buildbe_compiler;

typedef struct buildbe_target {
    buildbe_arch arch;
    buildbe_os os;
    buildbe_compiler compiler;
} buildbe_target;

/* Borrowed, null-terminated UTF-8 strings. Null items is valid for count zero. */
typedef struct buildbe_strings {
    const char *const *items;
    usize count;
} buildbe_strings;

typedef enum buildbe_status {
    BUILDBE_OK,
    BUILDBE_INVALID,
    BUILDBE_UNSUPPORTED,
    BUILDBE_TOOL_NOT_FOUND,
    BUILDBE_IO_ERROR,
    BUILDBE_PROCESS_FAILED,
    BUILDBE_OUT_OF_MEMORY
} buildbe_status;

typedef struct buildbe_result {
    buildbe_status status;
    int exit_code; /* Meaningful for PROCESS_FAILED; zero for success. */
} buildbe_result;

typedef struct buildbe_toolchain {
    buildbe_target target;
    const char *compiler;
    const char *assembler; /* Optional until an assembly operation needs it. */
    const char *archiver;
    const char *linker;
    const char *symbols;   /* Optional symbol inspection tool. */
    const char *sysroot;   /* Optional SDK/sysroot, including a Darwin SDK. */
    const char *target_triple; /* Optional cross-compilation triple. */
    buildbe_strings compile_flags;
    buildbe_strings archive_flags;
    buildbe_strings link_flags;
} buildbe_toolchain;

typedef struct buildbe_compile {
    const char *source; /* Staged C file in out/<target>/{lib,cmd,tests}/. */
    const char *object;
    const char *dependency_file; /* Optional; unsupported requests must be reported. */
    buildbe_strings include_paths;
    buildbe_strings defines; /* Individual NAME or NAME=value strings. */
    buildbe_strings flags;   /* Per-file flags, following toolchain flags. */
} buildbe_compile;

typedef struct buildbe_archive {
    const char *output; /* Frontend chooses the .lib layout, even for ar backends. */
    buildbe_strings objects;
} buildbe_archive;

typedef struct buildbe_link {
    const char *output;
    /* Ordered object/archive paths; preserve order for Unix static-library resolution. */
    buildbe_strings inputs;
    buildbe_strings library_paths;
    buildbe_strings system_libraries;
    buildbe_strings flags;
} buildbe_link;

typedef struct buildbe_run {
    const char *executable;
    buildbe_strings arguments; /* Excludes argv[0]; individual arguments, not shell text. */
    const char *working_directory;
    const char *stdout_path; /* Null inherits stdout. */
    const char *stderr_path; /* Null inherits stderr. */
} buildbe_run;

/*
 * Host filesystem metadata, independent of the target OS. Timestamp fields
 * describe UTC modification time; resolution_ns is the filesystem granularity.
 * A timestamp is usable only when has_modified and reliable_modified are set
 * and resolution_ns is nonzero. Missing metadata is not a missing file.
 */
typedef struct buildbe_file_info {
    u8 exists;
    u8 is_regular;
    u8 has_size;
    u64 size;
    u8 has_modified;
    u8 reliable_modified;
    i64 modified_seconds;
    u32 modified_nanoseconds; /* 0..999999999 */
    u64 resolution_ns;
} buildbe_file_info;

typedef enum buildbe_copy_mode {
    BUILDBE_COPY_IF_CHANGED, /* Default: skip only with usable matching metadata. */
    BUILDBE_COPY_ALWAYS
} buildbe_copy_mode;

typedef struct buildbe_copy {
    const char *source;
    const char *destination;
    buildbe_copy_mode mode;
    u8 preserve_modified_time; /* Best effort; report inability via resulting metadata. */
} buildbe_copy;

typedef struct buildbe_copy_result {
    buildbe_result result;
    u8 copied; /* Meaningful only on success; zero means destination was reused. */
    buildbe_file_info destination;
} buildbe_copy_result;

/*
 * Explicit owned files only: objects, archives, executables, generated/staged
 * C/headers, dependency files, compiler sidecars, or incremental-state records.
 * No arbitrary directory recursion. Missing files are successful no-ops.
 * Resolve paths and reject escapes through .., symlinks, or Windows junctions.
 * Never remove output_root itself, project inputs, or another target's outputs.
 */
typedef struct buildbe_clean {
    const char *output_root;
    buildbe_strings files;
    u8 remove_empty_directories; /* Only ancestors of removed files, below root. */
} buildbe_clean;

typedef struct buildbe_backend {
    u32 abi;
    const char *name;
    buildbe_os os;
    buildbe_compiler compiler;
    buildbe_result (*check)(const buildbe_toolchain *toolchain);
    buildbe_result (*compile)(const buildbe_toolchain *toolchain,
                             const buildbe_compile *request);
    buildbe_result (*archive)(const buildbe_toolchain *toolchain,
                             const buildbe_archive *request);
    buildbe_result (*link)(const buildbe_toolchain *toolchain,
                          const buildbe_link *request);
    /* Optional; null means unavailable. */
    buildbe_result (*symbols)(const buildbe_toolchain *toolchain,
                             const char *object, const char *output);
    /* Must reject executables that cannot run on the current host. */
    buildbe_result (*run)(const buildbe_run *request);
    /*
     * Optional host filesystem services. file_info reports a missing file as
     * OK with exists=0; inability to read it is IO_ERROR, not a reason to skip.
     * A null file_info callback disables metadata-based skipping.
     */
    buildbe_result (*file_info)(const char *path, buildbe_file_info *out);
    buildbe_copy_result (*copy)(const buildbe_copy *request);
    buildbe_result (*clean)(const buildbe_clean *request);
} buildbe_backend;

/*
 * Borrowed inputs remain valid for the duration of each synchronous call.
 * A backend must pass separate process arguments with platform-appropriate
 * escaping, report nonzero tool exits, and never silently change the target.
 * Object/archive contents follow the selected toolchain, not filename suffixes.
 */
extern const buildbe_backend buildbe_windows;     /* windows/msvc: cl, lib, link */
extern const buildbe_backend buildbe_linux_gcc;   /* linux/gcc: gcc, ar */
extern const buildbe_backend buildbe_linux_clang; /* linux/clang: clang, ar/llvm-ar */
extern const buildbe_backend buildbe_darwin;      /* darwin/clang: SDK-aware Clang */

/* Unsupported pairs (including unimplemented XK/bare-metal backends) return null. */
const buildbe_backend *buildbe_select(buildbe_os os, buildbe_compiler compiler);
u8 buildbe_valid(const buildbe_backend *backend);
