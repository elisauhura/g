#include "cmdline.h"

#include <string.h>

static str cmdline_slice(cstr data, usize len) {
    str result = {(u8 *)data, len};
    return result;
}

g_result cmdline_init_args(cmdline *parser, cmdline_args arguments) {
    if (!parser) return G_INVALID;
    *parser = (cmdline){0};
    if (arguments.count && !arguments.items) return G_INVALID;
    for (usize i = 0; i < arguments.count; ++i)
        if (!arguments.items[i]) return G_INVALID;
    parser->arguments = arguments;
    parser->initialized = 1;
    return G_OK;
}

g_result cmdline_init(cmdline *parser, int argc, cstr const *argv) {
    if (!parser) return G_INVALID;
    *parser = (cmdline){0};
    if (argc < 0 || (argc && (!argv || !argv[0]))) return G_INVALID;
    cmdline_args arguments = {argc ? argv + 1 : NULL, argc ? (usize)argc - 1 : 0};
    return cmdline_init_args(parser, arguments);
}

g_result cmdline_next(cmdline *parser, cmdline_token *token) {
    if (!token) return G_INVALID;
    *token = (cmdline_token){0};
    if (!parser || !parser->initialized) {
        token->kind = CMDLINE_ERROR;
        return G_INVALID;
    }
    if (parser->has_separator || parser->index == parser->arguments.count)
        return G_OK;

    cstr argument = parser->arguments.items[parser->index];
    usize len = (usize)strlen(argument);
    token->argument_index = parser->index;
    token->raw = cmdline_slice(argument, len);

    if (parser->short_offset) {
        token->kind = CMDLINE_SHORT_FLAG;
        token->name = cmdline_slice(argument + parser->short_offset, 1);
        if (++parser->short_offset == len) {
            parser->short_offset = 0;
            ++parser->index;
        }
        return G_OK;
    }

    if (len > 1 && argument[0] == '-') {
        if (argument[1] != '-') {
            token->kind = CMDLINE_SHORT_FLAG;
            token->name = cmdline_slice(argument + 1, 1);
            if (len > 2) parser->short_offset = 2;
            else ++parser->index;
            return G_OK;
        }
        ++parser->index;
        if (len == 2) {
            token->kind = CMDLINE_SEPARATOR;
            parser->has_separator = 1;
            return G_OK;
        }
        cstr equals = strchr(argument + 2, '=');
        usize name_len = equals ? (usize)(equals - argument - 2) : len - 2;
        if (!name_len) {
            token->kind = CMDLINE_ERROR;
            return G_INVALID;
        }
        token->name = cmdline_slice(argument + 2, name_len);
        if (equals) {
            token->kind = CMDLINE_PARAMETER;
            token->value = cmdline_slice(equals + 1, len - (usize)(equals + 1 - argument));
        } else {
            token->kind = CMDLINE_LONG_FLAG;
        }
        return G_OK;
    }

    token->kind = CMDLINE_ARGUMENT;
    token->value = token->raw;
    ++parser->index;
    return G_OK;
}

cmdline_args cmdline_remaining(const cmdline *parser) {
    cmdline_args result = {0};
    if (parser && parser->initialized && parser->has_separator) {
        result.items = parser->arguments.items + parser->index;
        result.count = parser->arguments.count - parser->index;
    }
    return result;
}
