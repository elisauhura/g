# Command-line library

[cmdline.h](../../include/cmdline.h) declares an allocation-free argument iterator.
Link [cmdline.c](cmdline.c) with the caller. It reads the already-tokenized
`argv`; shell quoting and escaping are not parsed again.

| Input | Tokens |
| --- | --- |
| `-abc -d` | Short flags `a`, `b`, `c`, `d` |
| `--param=value` | Parameter named `param` with value `value` |
| `--param=a=b` | Parameter value `a=b` |
| `--param=` | Parameter with an explicitly empty value |
| `--flag` | Long flag named `flag` |
| `file.txt -` | Two positional arguments |
| `--` | Separator; stop outer parsing and expose the remaining arguments |

Options may follow positionals. `--name value` is a flag plus a positional;
there is no option schema or automatic next-argument consumption. Short flags
are individual bytes, so `-42` means flags `4` and `2`. Use the separator when
such text should be treated as positional data. `--=value` is rejected because
its parameter name is empty.

## Usage

```c
cmdline parser;
cmdline_token token;
if (cmdline_init(&parser, argc, argv) != G_OK) return 2;
for (;;) {
    if (cmdline_next(&parser, &token) != G_OK) return 2;
    if (token.kind == CMDLINE_END) break;
    if (token.kind == CMDLINE_SEPARATOR) {
        cmdline_args tail = cmdline_remaining(&parser);
        /* Forward tail, treat it as positional text, or parse it independently: */
        cmdline inner;
        if (cmdline_init_args(&inner, tail) != G_OK) return 2;
        /* Consume inner with cmdline_next as needed. */
        break;
    }
    /* Handle token.kind, token.name, and token.value. */
}
```

`cmdline_init` skips the executable name. `cmdline_init_args` consumes a raw
slice, including its first argument. A separator is emitted once; later calls
on the outer parser return `CMDLINE_END` without consuming the tail. An inner
parser may encounter another separator and expose another tail.

Token names and values are borrowed `str` views with byte `len`. They are not
necessarily null-terminated. No arguments are reordered or modified. Keep the
input strings alive and unchanged. `cmdline_remaining` returns an argument
`count`; it is empty before a separator.

Malformed parameters yield `G_INVALID` and `CMDLINE_ERROR`, with the original
argument and its index retained. That argument is consumed so callers may report
the error and continue. Initialization failure clears the parser.

## Tests

From the repository root:

```sh
clang -std=c11 -Wall -Wextra -Werror -pedantic -I include lib/cmdline/cmdline.c lib/test/test.c tests/cmdline/test.c -o cmdline-test
./cmdline-test
```

On Windows, use `-o cmdline-test.exe` and run `.\cmdline-test.exe`.
The tests use the test library and stay active with `NDEBUG`.
