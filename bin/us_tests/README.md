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

---

## Signal helpers (FEEDBACK_SIG_TAB.md Issue S)

The libc previously declared `char *strsignal(int signum)` in
`<string.h>:70` with **no implementation** — any user program that
referenced the symbol failed to link, was never installed, and its
sub-tools never reached the rootfs.  Background and patch design live in
`FEEDBACK_SIG_TAB.md` (audit) and `MEMORY.md` (project history).

### Library

- `libc/strsignal.c` — table-driven `strsignal()` covering
  `SIGHUP`..`SIGSYS` (1..31) with conventional glibc/musl wording
  (`"Hangup"`, `"Interrupt"`, `"Killed"`, `"Segmentation fault"`,
  `"Terminated"`, `"Broken pipe"`, ...).  Unknown signals fall back to
  `"Unknown signal N"` via a single static buffer (POSIX-permitted;
  SBUnix has no threads).

  Picked up automatically by the `LIBC_OBJ` wildcard at
  `Makefile:18` — no Makefile edit required.

### Regression test

- `bin/us_tests/test_strsignal.c` — wired into
  `bin/us_tests/us_test.c` `RUN_ALL_TESTS` immediately after
  `run_test_chdir_relative`.  Covers:
  - known signals return non-NULL strings containing the conventional
    wording (`"Segmentation"`, `"Terminated"`, `"Broken pipe"`, ...)
  - unknown / out-of-range signal numbers (`0`, `-1`, `99`, `123456`)
    fall back to `"Unknown signal N"` instead of returning NULL or
    crashing
  - canonical use case: a forked child calls `raise(SIGTERM)`; the
    parent's `WIFSIGNALED(status)` + `strsignal(WTERMSIG(status))` chain
    produces `"Terminated"`

### Standalone user programs

- `bin/probe_strsignal/probe_strsignal.c` — walks every signal in
  `[1..SIGSYS]` plus three edge cases (`0`, `-1`, `99`), printing each
  `strsignal()` result.  Then forks a child that raises `SIGTERM` and
  reports `strsignal(WTERMSIG(status))` from the parent — the exact
  "report on a child's exit status" use case named by the evaluator.

  Run it from the shell:

  ```
  $ /bin/probe_strsignal
  strsignal(   1) = Hangup
  strsignal(   2) = Interrupt
  ...
  strsignal(  31) = Bad system call
  strsignal(   0) = Unknown signal 0
  strsignal(  -1) = Unknown signal -1
  strsignal(  99) = Unknown signal 99
  child[pid=N] terminated by signal 15 (Terminated)
  ```

- `bin/signame/signame.c` — small `kill -l`-style CLI:
  - `signame`        — list every signal 1..SIGSYS with its description
  - `signame N`      — print `strsignal(N)` for one numeric arg
  - `signame N1 N2…` — same for multiple numbers in order

  Exit status: `0` if at least one numeric arg resolved to a known
  signal (or no args); `1` if every numeric arg was unknown; `2` if any
  argument was not numeric.

  ```
  $ /bin/signame 9
  9) Killed
  $ /bin/signame 11 15 99
  11) Segmentation fault
  15) Terminated
  99) Unknown signal 99
  $ /bin/signame
   1) Hangup
   2) Interrupt
   3) Quit
   ...
  31) Bad system call
  ```

### Verifying end-to-end

```
make qemu
# at shell prompt:
$ /bin/probe_strsignal
$ /bin/signame
$ /bin/us_tests                 # the strsignal group runs as part of RUN_ALL_TESTS
```

After patches land, `nm build/libc.a | grep ' T strsignal'` shows a
defined symbol; before the patch the same `nm` showed nothing and any
caller of `strsignal()` linked against `libc.a` failed with
`undefined reference to 'strsignal'`.

(The `uname()` companion gap — Issue T in the audit — is deferred to a
later pass.)

