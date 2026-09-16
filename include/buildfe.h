#pragma once

#include "buildbe.h"

/*
 * Frontend API implemented under lib/buildfe/. Loading project.g, discovering files,
 * planning dependencies, copying sources, and scheduling tool calls belong here.
 * Compilation/archive/link command syntax belongs to buildbe.h's backends.
 * These interfaces do not settle the still-open project.g configuration grammar.
 */
typedef struct buildfe_project buildfe_project;
typedef struct buildfe_plan buildfe_plan;
struct astg_directive;

typedef enum buildfe_status {
    BUILDFE_OK,
    BUILDFE_INVALID,
    BUILDFE_PROJECT_ERROR,
    BUILDFE_DEPENDENCY_ERROR,
    BUILDFE_IO_ERROR,
    BUILDFE_OUT_OF_MEMORY,
    BUILDFE_TRANSLATOR_REQUIRED,
    BUILDFE_TRANSLATION_FAILED,
    BUILDFE_BACKEND_FAILED
} buildfe_status;

typedef struct buildfe_result {
    buildfe_status status;
    buildbe_result backend; /* Meaningful when status is BACKEND_FAILED. */
} buildfe_result;

typedef enum buildfe_unit_kind {
    BUILDFE_UNIT_INVALID,
    BUILDFE_UNIT_LIBRARY,
    BUILDFE_UNIT_COMMAND,
    BUILDFE_UNIT_TEST
} buildfe_unit_kind;

typedef enum buildfe_source_kind {
    BUILDFE_SOURCE_INVALID,
    BUILDFE_SOURCE_C,
    BUILDFE_SOURCE_G,
    BUILDFE_SOURCE_C_HEADER
} buildfe_source_kind;

typedef struct buildfe_source {
    buildfe_source_kind kind;
    const char *original_path; /* Original absolute path, retained for diagnostics. */
    const char *relative_path; /* Within its source unit; preserves nested directories. */
    const char *staged_path;   /* Copied C/header or generated C output path. */
    const char *object_path;   /* Null for headers. */
    /*
     * Optional parsed #build directive. Evaluate all filters against the target,
     * including negation, and check its first-line placement before staging.
     * Null for a file without #build, including ordinary C source.
     */
    const struct astg_directive *build_filter;
} buildfe_source;

typedef struct buildfe_unit {
    buildfe_unit_kind kind;
    const char *name;
    const buildfe_source *sources;
    usize source_count;
    buildbe_strings dependencies; /* Resolved unit names in the project graph. */
    buildbe_strings include_paths;
    const char *generated_header; /* Library exports under out/<target>/include/. */
    const char *archive_path;     /* obj/<lib>.lib or obj/cmd/<cmd>.lib as applicable. */
    const char *executable_path;  /* Commands/tests under out/<target>/exe/. */
} buildfe_unit;

typedef struct buildfe_options {
    const char *project_root; /* Root containing project.g. */
    const char *project_file; /* Null selects <project_root>/project.g. */
    const char *target_name;  /* Output-directory identity; validate as a safe relative path. */
    const char *output_root;  /* Null selects <project_root>/out/<target_name>/. */
    buildbe_toolchain toolchain;
    buildbe_copy_mode copy_mode; /* Defaults to timestamp-based reuse when possible. */
    buildbe_strings requested_units; /* Empty selects the project's default units. */
} buildfe_options;

typedef enum buildfe_stage_kind {
    BUILDFE_STAGE_INVALID,
    BUILDFE_STAGE_COPY_C,
    BUILDFE_STAGE_COPY_HEADER,
    BUILDFE_STAGE_TRANSPILE_G,
    BUILDFE_STAGE_GENERATE_LIBRARY_HEADER,
    BUILDFE_STAGE_COMPILE_C,
    BUILDFE_STAGE_ARCHIVE,
    BUILDFE_STAGE_LINK
} buildfe_stage_kind;

typedef struct buildfe_stage {
    buildfe_stage_kind kind;
    usize unit_index;
    buildbe_strings inputs;
    buildbe_strings outputs;
    const usize *dependencies; /* Stage indices, not compiler library arguments. */
    usize dependency_count;
    buildbe_copy_mode copy_mode; /* Meaningful for COPY_C and COPY_HEADER only. */
} buildfe_stage;

typedef struct buildfe_transpile_request {
    const buildfe_source *source;
    const buildfe_unit *unit;
    const buildbe_target *target;
    const char *generated_include_directory;
} buildfe_transpile_request;

typedef struct buildfe_header_request {
    const buildfe_unit *unit; /* All included library sources, not one header per file. */
    const buildbe_target *target;
    const char *output;
} buildfe_header_request;

/*
 * Future G-to-C integration. A C-only bootstrap does not need these callbacks.
 * If the plan includes G work and its callback is absent, fail with
 * TRANSLATOR_REQUIRED before executing any stage; never copy G text as C.
 * On success callbacks have written the declared C/header outputs. They can
 * retain parser/AST state in context for the duration of plan execution.
 */
typedef struct buildfe_translator {
    void *context;
    buildfe_result (*transpile)(void *context, const buildfe_transpile_request *request);
    buildfe_result (*library_header)(void *context, const buildfe_header_request *request);
} buildfe_translator;

/*
 * Required staging contract:
 * - Copy C files into out/<target>/{lib,cmd,tests}/<unit>/, preserving their
 *   unit-relative directory paths. Compile those copies, never original files.
 * - Copy required local headers while preserving relative quoted-include
 *   resolution, and stage shared project headers under the target include tree.
 * - Transpile G files into the same staged source tree and emit one exported
 *   header per library. Track the original path for diagnostics/source mapping.
 * - Detect conflicting outputs (including matching C/G basenames), unsafe
 *   relative paths, and dependency cycles before writing files.
 * - Keep outputs inside output_root and never overwrite original project inputs.
 * - Copy/transpile and header stages precede dependent compilation; archives and
 *   links depend on their objects and libraries. Preserve link input order.
 * Staging policy is represented here; no filesystem operation occurs in a header.
 */

/*
 * Incremental copy contract (whole-file selection, not byte-range copying):
 * - Re-stat at execution time. Reuse only an existing regular destination with
 *   matching size and matching reliable modification time at compatible
 *   resolution. Never skip merely because the destination is newer.
 * - Missing/unreliable timestamp or size information falls back to a full copy.
 *   A missing source or actual metadata/read/write error fails the stage.
 * - Copy C and headers byte-for-byte, publish completed output atomically where
 *   available, and preserve source modification time when supported. If the
 *   source changes during copying, retry or fail; do not record it as current.
 * - Keep unchanged output timestamps intact. Record actual post-copy metadata
 *   and output ownership only after success. COPY_ALWAYS bypasses reuse.
 * - Metadata equality is a heuristic: same-size edits with preserved timestamps
 *   need COPY_ALWAYS. It is not a content-hash guarantee.
 * - Copy reuse alone never justifies skipping G translation, compilation, or
 *   linking. Those require dependency/configuration tracking, still to be
 *   implemented, and must rerun conservatively until such tracking exists.
 * Null backend filesystem callbacks require frontend host-service fallbacks,
 * or an explicit error if unavailable; never silently report staging/cleaning
 * success without doing the work.
 */

/* Allocator callbacks are copied; null requests the runtime default allocator. */
buildfe_result buildfe_project_load(const char *root, const char *project_file,
                                   const g_allocator *allocator, buildfe_project **out);
void buildfe_project_destroy(buildfe_project *project);
/* Discovery needs no build plan/compiler. Names are borrowed until project
 * destruction. Invalid indices return NULL. */
usize buildfe_project_unit_count(const buildfe_project *project);
const char *buildfe_project_unit_name(const buildfe_project *project, usize index,
                                    buildfe_unit_kind *kind);

/*
 * Creation performs no build-output writes. On failure *out is null. The project,
 * options' strings, and toolchain input storage must outlive the resulting plan.
 * Destroy the plan before destroying its project.
 */
buildfe_result buildfe_plan_create(const buildfe_project *project,
                                  const buildfe_options *options,
                                  const g_allocator *allocator, buildfe_plan **out);
void buildfe_plan_destroy(buildfe_plan *plan);

/* Read-only plan-owned views remain valid until the plan is destroyed. */
const buildfe_unit *buildfe_plan_units(const buildfe_plan *plan, usize *count);
const buildfe_stage *buildfe_plan_stages(const buildfe_plan *plan, usize *count);

/* Synchronous; stops on failure and does not run dependent stages. */
buildfe_result buildfe_plan_execute(buildfe_plan *plan, const buildbe_backend *backend,
                                   const buildfe_translator *translator);

/* Cleanup planning/execution remains reserved for a future implementation. */
typedef enum buildfe_clean_scope {
    BUILDFE_CLEAN_INVALID,
    BUILDFE_CLEAN_UNITS,
    BUILDFE_CLEAN_TARGET
} buildfe_clean_scope;

typedef enum buildfe_clean_artifacts {
    BUILDFE_CLEAN_STAGED = 1u << 0,       /* Copied/generated sources and headers. */
    BUILDFE_CLEAN_INTERMEDIATES = 1u << 1, /* Objects, dependency files, sidecars. */
    BUILDFE_CLEAN_PRODUCTS = 1u << 2,     /* Archives and executables. */
    BUILDFE_CLEAN_ALL = BUILDFE_CLEAN_STAGED |
                       BUILDFE_CLEAN_INTERMEDIATES | BUILDFE_CLEAN_PRODUCTS
} buildfe_clean_artifacts;

typedef struct buildfe_clean_options {
    buildfe_clean_scope scope;
    buildbe_strings units; /* Required for UNITS; empty for TARGET. */
    u32 artifacts;          /* Nonzero OR of buildfe_clean_artifacts. */
    u8 remove_empty_directories;
    u8 dry_run; /* Produce the validated file list without deleting anything. */
} buildfe_clean_options;

typedef struct buildfe_clean_plan buildfe_clean_plan;

/*
 * Build the removal list from recorded output ownership, including stale
 * outputs of removed sources. Reject a missing/invalid ownership record rather
 * than recursively deleting an unverified directory. For unit cleanup, preserve
 * files shared with unselected units. Invalidate affected incremental records
 * even when only intermediates/products are removed; never clean source trees.
 * Creation performs no deletions; the plan owns its returned list.
 */
buildfe_result buildfe_clean_plan_create(const buildfe_options *options,
                                        const buildfe_clean_options *clean_options,
                                        const g_allocator *allocator,
                                        buildfe_clean_plan **out);
buildbe_strings buildfe_clean_plan_files(const buildfe_clean_plan *plan);
buildfe_result buildfe_clean_plan_execute(buildfe_clean_plan *plan,
                                         const buildbe_backend *backend);
void buildfe_clean_plan_destroy(buildfe_clean_plan *plan);

/* Initial g command frontend: build is the only subcommand. */
int buildfe_execute(int argc, char **argv);
