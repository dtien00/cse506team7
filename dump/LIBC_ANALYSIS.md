# libc Analysis: Correctness & Kernel Compatibility

Generated against kernel state on branch `jchlee-procSchedTEST`.

---

## Quick Summary

| Category | Status |
|---|---|
| printf / stdout | **WORKS** — direct ecall to `sys_write(fd=1)` → `printk` |
| malloc / free | **WORKS** — mmap-backed arena allocator |
| string / ctype / stdlib | **WORKS** — pure-C, no kernel dependency |
| fork / exec / wait / exit | **WORKS** — kernel handlers implemented |
| open / read (tarfs files) | **WORKS** — VFS + tarfs read path correct |
| write to files (non-stdout) | **BROKEN** — `sys_write` returns -1 for fd≥3 |
| lseek | **BROKEN** — syscall number collision (uses a7=8 = SYS_exec) |
| getdents64 / readdir | **BROKEN** — no kernel handler (a7=61 unhandled) |
| stat / fstat | **BROKEN** — no kernel handler (a7=79,80 unhandled) |
| mkdir | **BROKEN** — no kernel handler (a7=83 unhandled) |
| pipe / dup / dup2 | **BROKEN** — no kernel handler |
| signal / sigaction | **STUBS** — returns -1/-SIG_DFL, no kernel support |
| stdin / read from terminal | **BROKEN** — no terminal driver, vfs_read returns -1 |
| time / gettimeofday | **STUBS** — returns epoch/0, no kernel time syscalls |
| errno propagation | **INCOMPLETE** — global defined, never set |

---

## 1. stdout Write Path — CORRECT

`printf` → `do_write()` (printf.c:7–13) → `ecall a7=4` → `sys_write(fd=1)` → `printk` loop.

`sys_write` (syscall.c:178–195) special-cases fd==1 and fd==2 to use `printk("%c", ...)`.
This is the correct approach given no persistent write support yet.

**printf is fully usable**: `%s`, `%d`, `%u`, `%x`, `%p`, `%c`, `%%`, `%ld`/`%lu`/`%lx` all work.
Missing: `%f`, `%e`, `%g`, width/padding specifiers (`%-10s`, `%05d`).

---

## 2. Syscall Number Map — CRITICAL MISALIGNMENT

The kernel defines syscalls 1–16 in `kernel/include/trap/syscall.h`.
`libc/syscall.S` defines 30 syscalls, many using numbers the kernel does not handle.

### Working (kernel handler exists)

| Function | a7 | Kernel SYS_* |
|---|---|---|
| exit | 1 | SYS_exit |
| fork | 2 | SYS_fork |
| read | 3 | SYS_read |
| write | 4 | SYS_write |
| open | 5 | SYS_open |
| close | 6 | SYS_close |
| wait | 7 | SYS_wait |
| execve | 8 | SYS_exec |
| getpid | 9 | SYS_getpid |
| sbrk | 10 | SYS_sbrk |
| kill | 13 | SYS_kill (stub, returns -1) |
| mmap | 14 | SYS_mmap |
| munmap | 15 | SYS_munmap |
| brk | 16 | SYS_brk |

### Broken — Number Collision

| Function | a7 in syscall.S | Kernel interprets as |
|---|---|---|
| **lseek** | **8** | **SYS_exec — DANGEROUS** |

Calling `lseek()` from userspace will invoke `exec()` with garbage arguments.
Any code path that calls `lseek` (including `rewinddir`) risks corrupting the process.

### Broken — Unhandled (kernel returns -1 via default case)

| Function | a7 in syscall.S | Status |
|---|---|---|
| getuid | 24 | unhandled |
| getgid | 47 | unhandled |
| chdir | 49 | unhandled |
| getcwd | 17 | unhandled |
| getdents64 | 61 | unhandled — breaks `readdir` |
| stat | 79 | unhandled |
| fstat | 80 | unhandled |
| mkdir | 83 | unhandled |
| chmod | 90 | unhandled |
| fchmod | 91 | unhandled |
| unlink | 87 | unhandled |
| link | 86 | unhandled |
| symlink | 88 | unhandled |
| readlink | 89 | unhandled |
| pipe | 22 | unhandled |
| dup | 23 | unhandled |
| dup2 | 33 | unhandled |

---

## 3. File-by-File Assessment

### syscall.S — 95% correct, 1 critical bug
- All 14 working syscalls are correct.
- **Bug**: `lseek` uses `a7=8` which collides with `SYS_exec`. Must be renumbered or the kernel must add a real lseek handler at a new number.

### crt.S — 100%
`_start` calls `main(argc, argv)` then `exit()`. Correct for RISC-V ABI with args in a0/a1.

### printf.c — 95%
- Works: `%s`, `%d`, `%u`, `%x`, `%p`, `%c`, `%%`, `%l` modifier.
- Missing: `%f/%e/%g` (no floating point), width/precision/padding specifiers.
- `snprintf` correctly NUL-terminates and counts even when truncated.
- `sprintf` uses `INT_MAX` capacity — safe only because caller must ensure buffer is large enough (standard C behavior).

### string.c — 100%
All standard functions implemented correctly. No syscall dependencies.

### stdlib.c — 100%
`atoi/atol/strtol/strtoul` correct. `qsort` (insertion-sort variant) correct for small arrays.
`getenv/setenv` stubs returning NULL/-1 are appropriate.
`atexit` stores handlers but they are never called because `exit()` in exit.c goes directly to ecall. Harmless.

### malloc.c — 100%
Two-tier allocator: arena blocks via `mmap` for small allocs, direct `mmap` for large (≥4KB).
Correct block coalescing, realloc, and bounds checking.
Depends on `mmap`/`munmap` — both kernel-implemented and working.

### ctype.c — 100%
Pure lookup/arithmetic, no dependencies.

### exit.c — 100%
`exit(status)` → inline `ecall a7=1`. Correct.

### unistd.c — partial
- `_exit`: correct (inline ecall).
- `sleep`: calls `SYS_sleep=11`, kernel stub returns -1. Sleep does nothing.
- `usleep`: no-op stub, appropriate.
- `isatty`: returns 1 for fd 0/1/2, appropriate stub.
- `lstat`, `access`, `getppid`, `getpg*`: stubs returning -1, appropriate.

### wait.c — 80%
`wait()` works (SYS_wait=7). `waitpid` ignores pid/options and calls `wait()` — correct given kernel only supports wait-any-child.

### signal.c — 20% (stubs only)
`raise(sig)` calls `kill(getpid(), sig)` — correct structure, but `sys_kill` is a stub returning -1.
`signal/sigaction/sigprocmask`: stubs, appropriate since there is no signal delivery infrastructure.
`sigset_t` operations (sigemptyset etc.) work correctly as bitmask operations.

### fcntl.c — 60%
`open()` via syscall works. `creat()` wraps open correctly. `fcntl()` stub returning -1 is appropriate.

### dirent.c — 40% (blocked on kernel)
`opendir/readdir/closedir` logic is correct — but:
1. `getdents64` (a7=61) has no kernel handler → always returns -1.
2. `rewinddir` calls `lseek` which collides with `SYS_exec` — **dangerous**.
Directory listing is completely non-functional until kernel adds `getdents64`.

### mman.c — 50%
`mmap/munmap` in syscall.S work. `mprotect/madvise` stubs return -1/0. Appropriate.
Note: `sys_mmap` only supports `MAP_ANONYMOUS`. File-backed mappings fail.

### time.c / sys_time.c — 5% (stubs)
All return 0/epoch. No kernel time infrastructure exists. Stubs are correct behavior.

### ioctl.c / select.c — 0% (stubs)
No kernel support. Returning -1 is correct.

### errno.c — 50%
`errno` global is defined. But neither the kernel nor any libc wrapper ever sets it.
Callers checking `errno` after a failed syscall will always see 0.

### assert.c — 100%
Calls `printf` then `exit(1)`. Works correctly.

---

## 4. What Must Be Added to the Kernel

### MUST FIX (breaks existing code)

**1. lseek collision** — `libc/syscall.S` uses a7=8 for lseek, same as SYS_exec.
   - Option A: Add `SYS_lseek = 18` to kernel and update syscall.S.
   - Option B: Stub it at a new number that returns -1 gracefully.
   - Until fixed, any call to `lseek()` or `rewinddir()` will corrupt the process.

### NEEDED FOR FULL SHELL FUNCTIONALITY

**2. `SYS_getcwd` (a7=17)** — kernel defines the number but has no switch case.
   - Shell/pwd need this.

**3. `SYS_getdents64` (a7=61)** — needed for `readdir` / directory listing (`ls`).
   - tarfs has no directory iteration; tmpfs would also need it.

**4. `SYS_write` for fd≥3** — currently returns -1 for any non-stdout fd.
   - Writing to files (tmpfs) requires this path through `vfs_write`.

**5. `SYS_stat`/`SYS_fstat` (a7=79,80)** — needed for `ls -l`, file size queries.

**6. `SYS_chdir` (a7=49)** — needed for `cd` in the shell.

**7. `SYS_mkdir` (a7=83)** — needed for `mkdir` userspace program.

**8. `SYS_unlink` (a7=87)** — needed for `rm`.

### NICE TO HAVE

**9. `SYS_pipe` (a7=22)** — needed for shell pipes (`ls | grep foo`).

**10. `SYS_dup`/`SYS_dup2` (a7=23,33)** — needed for I/O redirection in shell.

**11. `SYS_kill` (a7=13)** — currently a stub; needs real signal delivery.

**12. errno propagation** — kernel should write errno into a per-process location; libc wrappers should read it.

---

## 5. What Works Right Now

Programs in `bin/` that should work end-to-end:

| Program | Works? | Notes |
|---|---|---|
| echo | Yes | printf to stdout only |
| true / false | Yes | exit(0)/exit(1) only |
| sleep | Partial | returns immediately (sys_sleep stub) |
| cat | Yes (read from tarfs) | reads tarfs files, writes to stdout |
| head / tail | Yes (from tarfs) | same as cat |
| wc | Yes | reads tarfs, counts, printf |
| grep | Yes | reads tarfs, printf matches |
| ls | No | needs getdents64 |
| mkdir | No | needs SYS_mkdir |
| rm | No | needs SYS_unlink |
| ln | No | needs SYS_link/SYS_symlink |
| pwd | No | needs SYS_getcwd |
| ps | Partial | getpid works; process list lookup TBD |
| sh | Partial | can run programs; pipes/redirect/cd broken |
