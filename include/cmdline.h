#pragma once

#include "g.h"

/*
 * Allocation-free argv iterator. Input strings are borrowed and never modified.
 * Names/values are str slices and need not be null-terminated. Keep argv and
 * its strings alive and unchanged while reading tokens or the separator tail.
 */
typedef enum cmdline_kind {
    CMDLINE_END,
    CMDLINE_SHORT_FLAG, /* -abc produces a, b, c as separate tokens. */
    CMDLINE_LONG_FLAG,  /* --flag */
    CMDLINE_PARAMETER,  /* --name=value; split at the first '=' only. */
    CMDLINE_ARGUMENT,   /* Positional argument, including "" and a lone "-". */
    CMDLINE_SEPARATOR,  /* --; the outer parser stops and exposes untouched tail. */
    CMDLINE_ERROR
} cmdline_kind;

typedef struct cmdline_args {
    cstr const *items;
    usize count;
} cmdline_args;

typedef struct cmdline_token {
    cmdline_kind kind;
    usize argument_index; /* Index within the argument slice, excluding argv[0]. */
    str raw;              /* Complete original argument, even for clustered flags. */
    str name;             /* Flag/parameter name without dashes. */
    str value;            /* Parameter value or positional argument. */
} cmdline_token;

typedef struct cmdline {
    cmdline_args arguments;
    usize index;
    usize short_offset;
    u8 has_separator;
    u8 initialized;
} cmdline;

/* Validates argc/argv, then skips the program name. argc=0 may use null argv. */
g_result cmdline_init(cmdline *parser, int argc, cstr const *argv);
/* Parse a slice without skipping its first element; also used for inner parsing. */
g_result cmdline_init_args(cmdline *parser, cmdline_args arguments);

/*
 * Returns G_OK for tokens and END. Repeated calls after END remain END.
 * A malformed --=value returns G_INVALID with CMDLINE_ERROR and consumes that
 * argument. Invalid API inputs also return G_INVALID. No option schema is
 * imposed; --name value is a long flag followed by a positional, not a parameter.
 * Flags are single bytes. Options can appear after positional arguments.
 */
g_result cmdline_next(cmdline *parser, cmdline_token *token);

/*
 * Empty before a separator. After it, returns all arguments following the first
 * --, unchanged and without the separator. Use as positional data, forward to
 * another program, or pass to cmdline_init_args for an independent inner parser.
 * cmdline_next never consumes this tail.
 */
cmdline_args cmdline_remaining(const cmdline *parser);
