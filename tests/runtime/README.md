# Runtime type tests

These tests cover `cstr`, `str`, class lookup and return-by-pointer calls,
positive and negative reference-counting modes, overflow, invalid operations,
reentrant cleanup, and the container `count`/`len` convention.

From the repository root:

```sh
clang -std=c11 -Wall -Wextra -Werror -pedantic -I include tests/runtime/test.c -o runtime-test
./runtime-test
```

On Windows use `-o runtime-test.exe` and run `.\runtime-test.exe`.
Compile without `NDEBUG` so the assertions run.

See [terminology](../../spec/terminology.md) and the
[G specification](../../spec/G.md) for the runtime contracts.
