#include "cmdline.h"
#include "test.h"
#include <string.h>

static void text_is(T *t, str actual, const char *expected) {
    usize len = (usize)strlen(expected);
    test_check(t, actual.len == len);
    if (actual.len == len && len) {
        test_check(t, actual.data != NULL);
        if (actual.data) test_check(t, memcmp(actual.data, expected, len) == 0);
    }
}
static cmdline_token next(T *t, cmdline *parser, cmdline_kind kind) {
    cmdline_token token = {0};
    test_check(t, cmdline_next(parser, &token) == G_OK);
    test_check(t, token.kind == kind);
    return token;
}
static void Test_Forms(T *t) {
    cstr argv[] = {"tool", "-abc", "-d", "--param=value=tail", "--flag", "--empty=", "file", "-", ""};
    cmdline parser;
    test_check(t, cmdline_init(&parser, 9, argv) == G_OK);
    test_check(t, cmdline_remaining(&parser).count == 0);
    for (usize i = 0; i < 3; ++i) {
        cmdline_token token = next(t, &parser, CMDLINE_SHORT_FLAG);
        test_check(t, token.name.len == 1 && token.name.data[0] == (u8)('a' + i));
        test_check(t, token.argument_index == 0);
        text_is(t, token.raw, "-abc");
    }
    text_is(t, next(t, &parser, CMDLINE_SHORT_FLAG).name, "d");
    cmdline_token token = next(t, &parser, CMDLINE_PARAMETER);
    text_is(t, token.name, "param");
    text_is(t, token.value, "value=tail");
    test_check(t, token.argument_index == 2);
    text_is(t, next(t, &parser, CMDLINE_LONG_FLAG).name, "flag");
    token = next(t, &parser, CMDLINE_PARAMETER);
    text_is(t, token.name, "empty");
    text_is(t, token.value, "");
    text_is(t, next(t, &parser, CMDLINE_ARGUMENT).value, "file");
    text_is(t, next(t, &parser, CMDLINE_ARGUMENT).value, "-");
    text_is(t, next(t, &parser, CMDLINE_ARGUMENT).value, "");
    (void)next(t, &parser, CMDLINE_END);
    (void)next(t, &parser, CMDLINE_END);
    test_check(t, strcmp(argv[1], "-abc") == 0);
    test_check(t, strcmp(argv[3], "--param=value=tail") == 0);
}
static void Test_Separator(T *t) {
    cstr argv[] = {"tool", "outer", "--", "-xy", "--inner=yes", "--", "--literal"};
    cmdline outer, inner, deepest;
    test_check(t, cmdline_init(&outer, 7, argv) == G_OK);
    text_is(t, next(t, &outer, CMDLINE_ARGUMENT).value, "outer");
    (void)next(t, &outer, CMDLINE_SEPARATOR);
    cmdline_args tail = cmdline_remaining(&outer);
    test_check(t, tail.count == 4 && tail.items == argv + 3);
    (void)next(t, &outer, CMDLINE_END);
    test_check(t, cmdline_remaining(&outer).count == 4);
    test_check(t, cmdline_init_args(&inner, tail) == G_OK);
    text_is(t, next(t, &inner, CMDLINE_SHORT_FLAG).name, "x");
    text_is(t, next(t, &inner, CMDLINE_SHORT_FLAG).name, "y");
    text_is(t, next(t, &inner, CMDLINE_PARAMETER).value, "yes");
    (void)next(t, &inner, CMDLINE_SEPARATOR);
    tail = cmdline_remaining(&inner);
    test_check(t, tail.count == 1 && tail.items == argv + 6);
    test_check(t, cmdline_init_args(&deepest, tail) == G_OK);
    text_is(t, next(t, &deepest, CMDLINE_LONG_FLAG).name, "literal");
    (void)next(t, &deepest, CMDLINE_END);
}
static void Test_Positionals(T *t) {
    cstr args[] = {"file", "--flag", "other", "--name", "value with spaces", "--"};
    cmdline parser;
    test_check(t, cmdline_init_args(&parser, (cmdline_args){args, 6}) == G_OK);
    text_is(t, next(t, &parser, CMDLINE_ARGUMENT).value, "file");
    (void)next(t, &parser, CMDLINE_LONG_FLAG);
    text_is(t, next(t, &parser, CMDLINE_ARGUMENT).value, "other");
    text_is(t, next(t, &parser, CMDLINE_LONG_FLAG).name, "name");
    text_is(t, next(t, &parser, CMDLINE_ARGUMENT).value, "value with spaces");
    (void)next(t, &parser, CMDLINE_SEPARATOR);
    test_check(t, cmdline_remaining(&parser).count == 0);
    (void)next(t, &parser, CMDLINE_END);
}
static void Test_InvalidAndEmpty(T *t) {
    cmdline parser = {0};
    cmdline_token token;
    cstr missing[] = {"tool", NULL};
    cstr malformed[] = {"--=value", "--", "--=untouched"};
    test_check(t, cmdline_next(&parser, &token) == G_INVALID && token.kind == CMDLINE_ERROR);
    test_check(t, cmdline_next(NULL, &token) == G_INVALID);
    test_check(t, cmdline_next(&parser, NULL) == G_INVALID);
    test_check(t, cmdline_init(NULL, 0, NULL) == G_INVALID);
    test_check(t, cmdline_init(&parser, -1, NULL) == G_INVALID);
    test_check(t, cmdline_init(&parser, 1, NULL) == G_INVALID);
    test_check(t, cmdline_init(&parser, 2, missing) == G_INVALID);
    test_check(t, cmdline_init_args(&parser, (cmdline_args){NULL, 1}) == G_INVALID);
    test_check(t, cmdline_init(&parser, 0, NULL) == G_OK);
    (void)next(t, &parser, CMDLINE_END);
    test_check(t, cmdline_remaining(NULL).count == 0);
    test_check(t, cmdline_init_args(&parser, (cmdline_args){malformed, 3}) == G_OK);
    test_check(t, cmdline_next(&parser, &token) == G_INVALID);
    test_check(t, token.kind == CMDLINE_ERROR && token.argument_index == 0);
    text_is(t, token.raw, "--=value");
    (void)next(t, &parser, CMDLINE_SEPARATOR);
    test_check(t, cmdline_remaining(&parser).count == 1);
    (void)next(t, &parser, CMDLINE_END);
}
int main(int argc, char **argv) {
    test_case cases[] = {
        {test_s("Test_Forms"), Test_Forms},
        {test_s("Test_Separator"), Test_Separator},
        {test_s("Test_Positionals"), Test_Positionals},
        {test_s("Test_InvalidAndEmpty"), Test_InvalidAndEmpty}
    };
    test_suite suite = {0};
    suite.tests = cases;
    suite.test_count = sizeof(cases) / sizeof(cases[0]);
    return test_main(argc, argv, &suite);
}
