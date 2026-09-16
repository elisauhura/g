#define _CRT_SECURE_NO_WARNINGS
#include "buildfe.h"
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

typedef struct memory {
    g_allocator allocator;
    g_vec blocks;
    u8 failed;
} memory;
static u8 memory_init(memory *m, const g_allocator *a) {
    memset(m, 0, sizeof(*m));
    if (g_ds_allocator(&m->allocator, a) != G_OK) return 0;
    return (u8)(g_vec_init(&m->blocks, sizeof(void *), &m->allocator) == G_OK);
}
static void *allocate(memory *m, usize size) {
    void *p = m->allocator.allocate(m->allocator.context, size ? size : 1);
    if (!p) { m->failed = 1; return NULL; }
    memset(p, 0, size);
    if (g_vec_push(&m->blocks, &p) != G_OK) {
        m->allocator.deallocate(m->allocator.context, p); m->failed = 1; return NULL;
    }
    return p;
}
static char *text(memory *m, const char *s, usize len) {
    char *p = allocate(m, len + 1);
    if (p) memcpy(p, s, len);
    return p;
}
static char *format(memory *m, const char *fmt, ...) {
    va_list args, copy;
    va_start(args, fmt); va_copy(copy, args);
    int n = vsnprintf(NULL, 0, fmt, copy); va_end(copy);
    char *p = n >= 0 ? allocate(m, (usize)n + 1) : NULL;
    if (p) vsnprintf(p, (usize)n + 1, fmt, args);
    else m->failed = 1;
    va_end(args); return p;
}
static void memory_destroy(memory *m) {
    for (usize i = 0; i < m->blocks.count; ++i)
        m->allocator.deallocate(m->allocator.context, *(void **)g_vec_at(&m->blocks, i));
    g_vec_destroy(&m->blocks);
}
static buildfe_result answer(buildfe_status status) {
    buildfe_result r = {status, {BUILDBE_OK, 0}}; return r;
}
static buildfe_result backend_error(buildbe_result error) {
    buildfe_result r = {BUILDFE_BACKEND_FAILED, error}; return r;
}
static buildbe_result visited(u8 ok) {
    buildbe_result r = {ok ? BUILDBE_OK : BUILDBE_IO_ERROR, 0}; return r;
}
static u8 append(g_vec *v, const void *item) { return (u8)(g_vec_push(v, item) == G_OK); }
static u8 valid_name(const char *s) {
    if (!s || (!isalpha((unsigned char)*s) && *s != '_')) return 0;
    for (++s; *s; ++s)
        if (!isalnum((unsigned char)*s) && *s != '_' && *s != '-') return 0;
    return 1;
}
static const char *category(buildfe_unit_kind kind) {
    return kind == BUILDFE_UNIT_LIBRARY ? "lib" : kind == BUILDFE_UNIT_COMMAND ? "cmd" : "tests";
}
typedef struct source_file { const char *path, *relative; buildfe_source_kind kind; } source_file;
typedef struct unit {
    const char *name, *directory;
    buildfe_unit_kind kind;
    g_vec deps, sources;
} unit;
struct buildfe_project {
    memory mem;
    const char *root;
    g_vec units, headers;
    buildfe_status error;
};
struct buildfe_plan {
    memory mem;
    const buildfe_project *project;
    buildfe_options options;
    const char *output, *include;
    buildfe_unit *units;
    usize unit_count;
    g_vec stages, order;
    u8 *marks;
    buildfe_status error;
};

static FILE *open_read(const char *path) {
#ifdef _WIN32
    wchar_t wide[BUILDBE_PATH_MAX];
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, BUILDBE_PATH_MAX)) return NULL;
    return _wfopen(wide, L"rb");
#else
    return fopen(path, "rb");
#endif
}
static char *read_file(memory *m, const char *path) {
    FILE *file = open_read(path);
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END) || ftell(file) < 0) { fclose(file); return NULL; }
    long size = ftell(file);
    if (fseek(file, 0, SEEK_SET)) { fclose(file); return NULL; }
    char *buffer = allocate(m, (usize)size + 1);
    if (!buffer) { fclose(file); return NULL; }
    usize n = fread(buffer, 1, (usize)size, file);
    u8 ok = (u8)(n == (usize)size && !ferror(file) && !memchr(buffer, 0, n));
    fclose(file);
    return ok ? buffer : NULL;
}
typedef struct parser { const char *p; memory *mem; u8 bad; } parser;
static void spaces(parser *p) {
    for (;;) {
        while (isspace((unsigned char)*p->p)) ++p->p;
        if (p->p[0] == '/' && p->p[1] == '/') {
            while (*p->p && *p->p != '\n') ++p->p;
        } else if (p->p[0] == '/' && p->p[1] == '*') {
            const char *end = strstr(p->p + 2, "*/");
            if (!end) { p->bad = 1; return; }
            p->p = end + 2;
        } else break;
    }
}
static u8 take(parser *p, const char *token) {
    spaces(p);
    usize n = strlen(token);
    if (p->bad || strncmp(p->p, token, n)) return 0;
    p->p += n; return 1;
}
static char *name(parser *p) {
    spaces(p);
    const char *start = p->p;
    if (!isalpha((unsigned char)*start) && *start != '_') return NULL;
    while (isalnum((unsigned char)*p->p) || *p->p == '_' || *p->p == '-') ++p->p;
    return text(p->mem, start, (usize)(p->p - start));
}
static u8 add_dep(unit *u, const char *dependency) {
    for (usize i = 0; i < u->deps.count; ++i)
        if (!strcmp(*(const char **)g_vec_at(&u->deps, i), dependency)) return 1;
    return append(&u->deps, &dependency);
}
static u8 manifest(buildfe_project *project, unit *u) {
    const char *path = format(&project->mem, "%s/dep.g", u->directory);
    char *contents = path ? read_file(&project->mem, path) : NULL;
    if (!contents) { fprintf(stderr, "Cannot read %s.\n", path ? path : "dep.g"); return 0; }
    parser p = {contents, &project->mem, 0};
    char *symbol = name(&p);
    u8 ok = (u8)(symbol && !strcmp(symbol, u->name) && take(&p, ":="));
    char *kind = ok ? name(&p) : NULL;
    const char *expected = u->kind == BUILDFE_UNIT_TEST ? "test" : category(u->kind);
    ok = (u8)(kind && !strcmp(kind, expected) && take(&p, "{"));
    if (ok && !take(&p, "}")) {
        ok = (u8)(take(&p, ".dep") && take(&p, ":") && take(&p, "{"));
        if (ok && !take(&p, "}")) {
            do {
                if (!take(&p, "\"")) { ok = 0; break; }
                const char *start = p.p;
                while (*p.p && *p.p != '"' && *p.p != '\\' && *p.p != '\n') ++p.p;
                char *dependency = text(&project->mem, start, (usize)(p.p - start));
                if (!dependency || !valid_name(dependency) || *p.p != '"' || !add_dep(u, dependency)) {
                    ok = 0; break;
                }
                ++p.p; /* Closing quote: whitespace inside a string is not accepted. */
                if (take(&p, "}")) break;
                if (!take(&p, ",")) { ok = 0; break; }
                if (take(&p, "}")) break;
            } while (ok);
        }
        (void)take(&p, ",");
        ok = (u8)(ok && take(&p, "}"));
    }
    (void)take(&p, ";"); spaces(&p);
    if (!ok || p.bad || *p.p || project->mem.failed) {
        fprintf(stderr, "Invalid dependency manifest: %s (expected %s := %s { .dep: {\"library\"} }).\n",
                path, u->name, expected);
        return 0;
    }
    if (u->kind == BUILDFE_UNIT_TEST && !add_dep(u, "test")) return 0;
    return 1;
}
typedef struct scan {
    buildfe_project *project;
    g_vec *files;
    const char *base, *relative;
    u8 headers_only;
} scan;
static buildbe_result scan_file(void *context, const char *name_value, u8 directory) {
    scan *s = context;
    if (!strcmp(name_value, "dep.g")) return visited(1);
    const char *relative = format(&s->project->mem, "%s%s%s", s->relative,
                                  *s->relative ? "/" : "", name_value);
    const char *path = relative ? format(&s->project->mem, "%s/%s", s->base, relative) : NULL;
    if (!path) return visited(0);
    if (directory) {
        scan child = *s; child.relative = relative;
        return buildbe_host_list(path, scan_file, &child);
    }
    const char *ext = strrchr(name_value, '.');
    if (!ext) return visited(1);
    buildfe_source_kind kind = !strcmp(ext, ".h") ? BUILDFE_SOURCE_C_HEADER :
        !strcmp(ext, ".c") ? BUILDFE_SOURCE_C : !strcmp(ext, ".g") ? BUILDFE_SOURCE_G : BUILDFE_SOURCE_INVALID;
    if (kind == BUILDFE_SOURCE_INVALID || (s->headers_only && kind != BUILDFE_SOURCE_C_HEADER)) return visited(1);
    source_file file = {path, relative, kind};
    return visited(append(s->files, &file));
}
typedef struct discover { buildfe_project *project; buildfe_unit_kind kind; } discover;
static buildbe_result discover_unit(void *context, const char *name_value, u8 directory) {
    discover *d = context;
    if (!directory) return visited(1);
    if (!valid_name(name_value)) { fprintf(stderr, "Invalid unit directory: %s\n", name_value); return visited(0); }
    unit u = {0};
    u.name = text(&d->project->mem, name_value, strlen(name_value));
    u.kind = d->kind;
    u.directory = format(&d->project->mem, "%s/%s/%s", d->project->root, category(u.kind), name_value);
    (void)g_vec_init(&u.deps, sizeof(const char *), &d->project->mem.allocator);
    (void)g_vec_init(&u.sources, sizeof(source_file), &d->project->mem.allocator);
    u8 ok = (u8)(u.name && u.directory && manifest(d->project, &u));
    if (ok) {
        scan s = {d->project, &u.sources, u.directory, "", 0};
        ok = (u8)(buildbe_host_list(u.directory, scan_file, &s).status == BUILDBE_OK);
    }
    if (ok) ok = append(&d->project->units, &u);
    if (!ok) { g_vec_destroy(&u.deps); g_vec_destroy(&u.sources); }
    return visited(ok);
}
static isize library_index(const buildfe_project *p, const char *name_value) {
    for (usize i = 0; i < p->units.count; ++i) {
        const unit *u = g_vec_at_const(&p->units, i);
        if (u->kind == BUILDFE_UNIT_LIBRARY && !strcmp(u->name, name_value)) return (isize)i;
    }
    return -1;
}
buildfe_result buildfe_project_load(const char *root, const char *project_file,
                                   const g_allocator *allocator, buildfe_project **out) {
    if (!out || !root) return answer(BUILDFE_INVALID);
    *out = NULL;
    memory m;
    if (!memory_init(&m, allocator)) return answer(BUILDFE_INVALID);
    buildfe_project *p = m.allocator.allocate(m.allocator.context, sizeof(*p));
    if (!p) { memory_destroy(&m); return answer(BUILDFE_OUT_OF_MEMORY); }
    memset(p, 0, sizeof(*p)); p->mem = m;
    (void)g_vec_init(&p->units, sizeof(unit), &p->mem.allocator);
    (void)g_vec_init(&p->headers, sizeof(source_file), &p->mem.allocator);
    char absolute[BUILDBE_PATH_MAX];
    if (buildbe_host_absolute(root, absolute, sizeof(absolute)).status != BUILDBE_OK) goto invalid;
    p->root = text(&p->mem, absolute, strlen(absolute));
    if (!p->root) goto invalid;
    const char *config = project_file ? project_file : format(&p->mem, "%s/project.g", p->root);
    char *contents = config ? read_file(&p->mem, config) : NULL;
    if (!contents) { fprintf(stderr, "Cannot read project definition: %s\n", config ? config : root); goto invalid; }
    parser definition = {contents, &p->mem, 0};
    spaces(&definition);
    if (definition.bad || *definition.p) {
        fprintf(stderr, "Initial bootstrap accepts only comments/whitespace in project.g; configure dependencies in dep.g.\n");
        goto invalid;
    }
    for (buildfe_unit_kind kind = BUILDFE_UNIT_LIBRARY; kind <= BUILDFE_UNIT_TEST; ++kind) {
        const char *path = format(&p->mem, "%s/%s", p->root, category(kind));
        buildbe_file_info info;
        if (!path || buildbe_host_info(path, &info).status != BUILDBE_OK) goto invalid;
        if (!info.exists) continue;
        discover d = {p, kind};
        if (info.is_regular || buildbe_host_list(path, discover_unit, &d).status != BUILDBE_OK) goto invalid;
    }
    for (usize i = 0; i < p->units.count; ++i) {
        unit *u = g_vec_at(&p->units, i);
        for (usize j = 0; j < u->deps.count; ++j) {
            const char *dep = *(const char **)g_vec_at(&u->deps, j);
            if (library_index(p, dep) < 0) {
                fprintf(stderr, "%s/%s: missing library dependency '%s'.\n", category(u->kind), u->name, dep);
                p->error = BUILDFE_DEPENDENCY_ERROR; goto invalid;
            }
        }
    }
    const char *include = format(&p->mem, "%s/include", p->root);
    buildbe_file_info info;
    if (!include || buildbe_host_info(include, &info).status != BUILDBE_OK) goto invalid;
    if (info.exists) {
        scan s = {p, &p->headers, include, "", 1};
        if (info.is_regular || buildbe_host_list(include, scan_file, &s).status != BUILDBE_OK) goto invalid;
    }
    *out = p; return answer(BUILDFE_OK);
invalid:
    {
        buildfe_status code = p->mem.failed ? BUILDFE_OUT_OF_MEMORY :
                              p->error ? p->error : BUILDFE_PROJECT_ERROR;
        buildfe_project_destroy(p); return answer(code);
    }
}
void buildfe_project_destroy(buildfe_project *p) {
    if (!p) return;
    for (usize i = 0; i < p->units.count; ++i) {
        unit *u = g_vec_at(&p->units, i);
        g_vec_destroy(&u->deps); g_vec_destroy(&u->sources);
    }
    g_vec_destroy(&p->units); g_vec_destroy(&p->headers);
    g_allocator a = p->mem.allocator;
    memory_destroy(&p->mem); a.deallocate(a.context, p);
}

usize buildfe_project_unit_count(const buildfe_project *p) {
    return p ? p->units.count : 0;
}
const char *buildfe_project_unit_name(const buildfe_project *p, usize index,
                                    buildfe_unit_kind *kind) {
    if (!p || index >= p->units.count) return NULL;
    const unit *u = g_vec_at_const(&p->units, index);
    if (kind) *kind = u->kind;
    return u->name;
}

static u8 visit_unit(buildfe_plan *p, usize index) {
    if (p->marks[index] == 2) return 1;
    if (p->marks[index] == 1) {
        fprintf(stderr, "Dependency cycle at %s/%s.\n", category(p->units[index].kind), p->units[index].name);
        p->error = BUILDFE_DEPENDENCY_ERROR; return 0;
    }
    p->marks[index] = 1;
    const unit *u = g_vec_at_const(&p->project->units, index);
    for (usize i = 0; i < u->deps.count; ++i) {
        isize dependency = library_index(p->project, *(const char *const *)g_vec_at_const(&u->deps, i));
        if (dependency < 0 || !visit_unit(p, (usize)dependency)) return 0;
    }
    p->marks[index] = 2;
    return append(&p->order, &index);
}
static buildbe_strings strings(memory *m, const char *const *items, usize count) {
    buildbe_strings out = {0};
    if (!count) return out;
    const char **copy = allocate(m, count * sizeof(*copy));
    if (!copy) return out;
    memcpy(copy, items, count * sizeof(*copy));
    out.items = copy; out.count = count; return out;
}
static u8 stage(buildfe_plan *p, buildfe_stage_kind kind, usize index,
                buildbe_strings inputs, const char *output) {
    buildfe_stage s = {0};
    s.kind = kind; s.unit_index = index; s.inputs = inputs;
    s.copy_mode = p->options.copy_mode;
    s.outputs = strings(&p->mem, &output, 1);
    if (!output || p->mem.failed) return 0;
    for (usize i = 0; i < p->stages.count; ++i) {
        const buildfe_stage *old = g_vec_at_const(&p->stages, i);
#ifdef _WIN32
        if (!_stricmp(old->outputs.items[0], output))
#else
        if (!strcmp(old->outputs.items[0], output))
#endif
        {
            fprintf(stderr, "Conflicting output: %s\n", output);
            p->error = BUILDFE_PROJECT_ERROR; return 0;
        }
    }
    if (p->stages.count) {
        usize *dependency = allocate(&p->mem, sizeof(*dependency));
        if (!dependency) return 0;
        *dependency = p->stages.count - 1;
        s.dependencies = dependency; s.dependency_count = 1;
    }
    return append(&p->stages, &s);
}
static void dependency_set(const buildfe_plan *p, usize index, u8 *marks) {
    if (marks[index]) return;
    marks[index] = 1;
    const unit *u = g_vec_at_const(&p->project->units, index);
    for (usize i = 0; i < u->deps.count; ++i)
        dependency_set(p, (usize)library_index(p->project, *(const char *const *)g_vec_at_const(&u->deps, i)), marks);
}
buildfe_result buildfe_plan_create(const buildfe_project *project,
                                  const buildfe_options *options,
                                  const g_allocator *allocator, buildfe_plan **out) {
    if (!out) return answer(BUILDFE_INVALID);
    *out = NULL;
    if (!project || !options || !valid_name(options->target_name) ||
        (options->requested_units.count && !options->requested_units.items) ||
        (options->copy_mode != BUILDBE_COPY_IF_CHANGED && options->copy_mode != BUILDBE_COPY_ALWAYS))
        return answer(BUILDFE_INVALID);
    memory m;
    if (!memory_init(&m, allocator)) return answer(BUILDFE_INVALID);
    buildfe_plan *p = m.allocator.allocate(m.allocator.context, sizeof(*p));
    if (!p) { memory_destroy(&m); return answer(BUILDFE_OUT_OF_MEMORY); }
    memset(p, 0, sizeof(*p)); p->mem = m; p->project = project; p->options = *options;
    (void)g_vec_init(&p->order, sizeof(usize), &p->mem.allocator);
    (void)g_vec_init(&p->stages, sizeof(buildfe_stage), &p->mem.allocator);
    p->unit_count = project->units.count;
    p->units = allocate(&p->mem, p->unit_count * sizeof(*p->units));
    p->marks = allocate(&p->mem, p->unit_count);
    p->output = format(&p->mem, "%s/out/%s", project->root, options->target_name);
    /* Initial implementation keeps outputs in the target's standard directory. */
    if (options->output_root && (!p->output || strcmp(options->output_root, p->output))) {
        fprintf(stderr, "Custom output roots are not supported by the bootstrap frontend.\n");
        p->error = BUILDFE_INVALID; goto failed;
    }
    p->include = p->output ? format(&p->mem, "%s/include", p->output) : NULL;
    if (p->mem.failed) goto failed;
    for (usize i = 0; i < p->unit_count; ++i) {
        const unit *input = g_vec_at_const(&project->units, i);
        buildfe_unit *u = &p->units[i];
        u->name = input->name; u->kind = input->kind;
        u->dependencies = strings(&p->mem, input->deps.data, input->deps.count);
    }
    if (!options->requested_units.count) {
        for (usize i = 0; i < p->unit_count; ++i) if (!visit_unit(p, i)) goto failed;
    } else {
        for (usize n = 0; n < options->requested_units.count; ++n) {
            const char *request = options->requested_units.items[n];
            u8 found = 0;
            for (usize i = 0; i < p->unit_count; ++i) {
                const char *label = format(&p->mem, "%s/%s", category(p->units[i].kind), p->units[i].name);
                if (!label) goto failed;
                if (!strcmp(request, "all") || !strcmp(request, label)) {
                    found = 1; if (!visit_unit(p, i)) goto failed;
                }
            }
            if (!found) {
                fprintf(stderr, "Unknown build unit: %s\n", request);
                p->error = BUILDFE_PROJECT_ERROR; goto failed;
            }
        }
    }
    for (usize i = 0; i < project->headers.count; ++i) {
        const source_file *h = g_vec_at_const(&project->headers, i);
        const char *destination = format(&p->mem, "%s/%s", p->include, h->relative);
        if (!stage(p, BUILDFE_STAGE_COPY_HEADER, (usize)-1, strings(&p->mem, &h->path, 1), destination)) goto failed;
    }
    for (usize order = 0; order < p->order.count; ++order) {
        usize index = *(const usize *)g_vec_at_const(&p->order, order);
        const unit *input = g_vec_at_const(&project->units, index);
        buildfe_unit *u = &p->units[index];
        const char *base = format(&p->mem, "%s/%s/%s", p->output, category(u->kind), u->name);
        const char *object_base = u->kind == BUILDFE_UNIT_LIBRARY ?
            format(&p->mem, "%s/obj/%s", p->output, u->name) :
            format(&p->mem, "%s/obj/%s/%s", p->output, category(u->kind), u->name);
        const char *includes[] = {p->include, base};
        u->include_paths = strings(&p->mem, includes, 2);
        buildfe_source *sources = allocate(&p->mem, input->sources.count * sizeof(*sources));
        u->sources = sources; u->source_count = input->sources.count;
        const char **objects = allocate(&p->mem, input->sources.count * sizeof(*objects));
        usize object_count = 0;
        u8 has_g = 0;
        if (p->mem.failed) goto failed;
        for (usize j = 0; j < input->sources.count; ++j) {
            const source_file *file = g_vec_at_const(&input->sources, j);
            buildfe_source *s = &sources[j];
            s->kind = file->kind; s->original_path = file->path; s->relative_path = file->relative;
            char *staged = format(&p->mem, "%s/%s", base, file->relative);
            if (!staged) goto failed;
            if (s->kind == BUILDFE_SOURCE_G) { staged[strlen(staged) - 1] = 'c'; has_g = 1; }
            s->staged_path = staged;
            buildfe_stage_kind action = s->kind == BUILDFE_SOURCE_C_HEADER ? BUILDFE_STAGE_COPY_HEADER :
                s->kind == BUILDFE_SOURCE_G ? BUILDFE_STAGE_TRANSPILE_G : BUILDFE_STAGE_COPY_C;
            if (!stage(p, action, index, strings(&p->mem, &s->original_path, 1), staged)) goto failed;
            if (s->kind != BUILDFE_SOURCE_C_HEADER) {
                s->object_path = format(&p->mem, "%s/%s.obj", object_base, file->relative);
                if (!s->object_path) goto failed;
                objects[object_count++] = s->object_path;
            }
        }
        if (has_g && u->kind == BUILDFE_UNIT_LIBRARY) {
            u->generated_header = format(&p->mem, "%s/%s.h", p->include, u->name);
            if (!stage(p, BUILDFE_STAGE_GENERATE_LIBRARY_HEADER, index, (buildbe_strings){0}, u->generated_header)) goto failed;
        }
        for (usize j = 0; j < u->source_count; ++j) {
            const buildfe_source *s = &u->sources[j];
            if (s->object_path && !stage(p, BUILDFE_STAGE_COMPILE_C, index,
                strings(&p->mem, &s->staged_path, 1), s->object_path)) goto failed;
        }
        if (object_count) {
            u->archive_path = format(&p->mem, "%s.lib", object_base);
            if (!stage(p, BUILDFE_STAGE_ARCHIVE, index, strings(&p->mem, objects, object_count), u->archive_path)) goto failed;
        }
        if (u->kind != BUILDFE_UNIT_LIBRARY) {
            const char *suffix = options->toolchain.target.os == BUILDBE_OS_WINDOWS ? ".exe" : "";
            u->executable_path = format(&p->mem, "%s/exe/%s%s%s", p->output,
                u->kind == BUILDFE_UNIT_TEST ? "test-" : "", u->name, suffix);
            u8 *closure = allocate(&p->mem, p->unit_count);
            if (p->mem.failed) goto failed;
            dependency_set(p, index, closure);
            usize count = 0;
            /* Link own objects directly so archive extraction cannot discard main. */
            const char **links = allocate(&p->mem, (object_count + p->unit_count) * sizeof(*links));
            if (!links) goto failed;
            for (usize k = 0; k < object_count; ++k) links[count++] = objects[k];
            for (usize k = p->order.count; k > 0; --k) {
                usize dep = *(const usize *)g_vec_at_const(&p->order, k - 1);
                if (dep != index && closure[dep] && p->units[dep].archive_path)
                    links[count++] = p->units[dep].archive_path;
            }
            if (!stage(p, BUILDFE_STAGE_LINK, index, strings(&p->mem, links, count), u->executable_path)) goto failed;
        }
    }
    *out = p; return answer(BUILDFE_OK);
failed:
    {
        buildfe_status code = p->mem.failed ? BUILDFE_OUT_OF_MEMORY : p->error ? p->error : BUILDFE_OUT_OF_MEMORY;
        buildfe_plan_destroy(p); return answer(code);
    }
}
void buildfe_plan_destroy(buildfe_plan *p) {
    if (!p) return;
    g_vec_destroy(&p->stages); g_vec_destroy(&p->order);
    g_allocator a = p->mem.allocator;
    memory_destroy(&p->mem); a.deallocate(a.context, p);
}
const buildfe_unit *buildfe_plan_units(const buildfe_plan *p, usize *count) {
    if (count) *count = p ? p->unit_count : 0;
    return p ? p->units : NULL;
}
const buildfe_stage *buildfe_plan_stages(const buildfe_plan *p, usize *count) {
    if (count) *count = p ? p->stages.count : 0;
    return p ? p->stages.data : NULL;
}
static buildbe_result make_parent(const char *path) {
    char parent[BUILDBE_PATH_MAX];
    if (!path || strlen(path) >= sizeof(parent)) return visited(0);
    strcpy(parent, path);
    char *slash = strrchr(parent, '/');
    if (!slash) return visited(0);
    *slash = 0; return buildbe_host_mkdirs(parent);
}
buildfe_result buildfe_plan_execute(buildfe_plan *p, const buildbe_backend *backend,
                                   const buildfe_translator *translator) {
    if (!p || !buildbe_valid(backend) || backend->os != p->options.toolchain.target.os ||
        backend->compiler != p->options.toolchain.target.compiler) return answer(BUILDFE_INVALID);
    for (usize i = 0; i < p->stages.count; ++i) {
        const buildfe_stage *s = g_vec_at_const(&p->stages, i);
        if ((s->kind == BUILDFE_STAGE_TRANSPILE_G && (!translator || !translator->transpile)) ||
            (s->kind == BUILDFE_STAGE_GENERATE_LIBRARY_HEADER && (!translator || !translator->library_header))) {
            fprintf(stderr, "G source requires a translator: %s\n", s->outputs.items[0]);
            return answer(BUILDFE_TRANSLATOR_REQUIRED);
        }
    }
    buildbe_result r = backend->check(&p->options.toolchain);
    if (r.status != BUILDBE_OK) return backend_error(r);
    usize copied = 0, reused = 0;
    for (usize i = 0; i < p->stages.count; ++i) {
        const buildfe_stage *s = g_vec_at_const(&p->stages, i);
        const char *output = s->outputs.items[0];
        r = make_parent(output);
        if (r.status != BUILDBE_OK) return backend_error(r);
        buildbe_file_info existing_output;
        r = buildbe_host_info(output, &existing_output);
        if (r.status != BUILDBE_OK) return backend_error(r);
        if (existing_output.exists && !existing_output.is_regular) return answer(BUILDFE_IO_ERROR);
        const buildfe_unit *u = s->unit_index < p->unit_count ? &p->units[s->unit_index] : NULL;
        switch (s->kind) {
        case BUILDFE_STAGE_COPY_C:
        case BUILDFE_STAGE_COPY_HEADER: {
            buildbe_copy request = {s->inputs.items[0], output, s->copy_mode, 1};
            buildbe_copy_result copy = backend->copy ? backend->copy(&request) : buildbe_host_copy(&request);
            r = copy.result; if (copy.copied) ++copied; else ++reused;
            break;
        }
        case BUILDFE_STAGE_COMPILE_C: {
            if (!u) return answer(BUILDFE_INVALID);
            printf("compile %s\n", s->inputs.items[0]);
            buildbe_compile request = {0};
            request.source = s->inputs.items[0]; request.object = output;
            request.include_paths = u->include_paths;
            r = backend->compile(&p->options.toolchain, &request); break;
        }
        case BUILDFE_STAGE_ARCHIVE: {
            printf("archive %s\n", output);
            buildbe_archive request = {output, s->inputs};
            r = backend->archive(&p->options.toolchain, &request); break;
        }
        case BUILDFE_STAGE_LINK: {
            printf("link %s\n", output);
            buildbe_link request = {0}; request.output = output; request.inputs = s->inputs;
            r = backend->link(&p->options.toolchain, &request); break;
        }
        case BUILDFE_STAGE_TRANSPILE_G: {
            if (!u) return answer(BUILDFE_INVALID);
            const buildfe_source *source = NULL;
            for (usize j = 0; j < u->source_count; ++j)
                if (!strcmp(u->sources[j].staged_path, output)) source = &u->sources[j];
            buildfe_transpile_request request = {source, u, &p->options.toolchain.target, p->include};
            buildfe_result translated = translator->transpile(translator->context, &request);
            if (translated.status != BUILDFE_OK) return translated;
            r = visited(1); break;
        }
        case BUILDFE_STAGE_GENERATE_LIBRARY_HEADER: {
            if (!u) return answer(BUILDFE_INVALID);
            buildfe_header_request request = {u, &p->options.toolchain.target, output};
            buildfe_result translated = translator->library_header(translator->context, &request);
            if (translated.status != BUILDFE_OK) return translated;
            r = visited(1); break;
        }
        default: return answer(BUILDFE_INVALID);
        }
        if (r.status != BUILDBE_OK) {
            fprintf(stderr, "Build stage failed for %s (backend status %d, tool exit %d).\n",
                    output, r.status, r.exit_code);
            return backend_error(r);
        }
    }
    printf("Build complete: %zu files copied, %zu unchanged.\n", copied, reused);
    return answer(BUILDFE_OK);
}
