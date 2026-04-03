# Userspace Test Suite

`/bin/us_tests` is a single userspace binary that runs grouped tests and prints:

- per-test PASS/FAIL/SKIP lines
- per-group totals
- one final aggregate summary

## Layout

- `libc_test.c`: suite entrypoint
- `us_test_common.h/.c`: shared test harness
- `test_*.c`: group tests split by libc header area

## Execution flow

`init` runs `/bin/us_tests` once at boot, then starts the shell loop.

## Build

The project `Makefile` builds all `bin/us_tests/*.c` into `build/rootfs/bin/us_tests`.

