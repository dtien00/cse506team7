# Quick Reference - SBUnix User-Space Implementation

## What Was Done

### libc Functions Added

**5 new source files:**
- `libc/string.c` - 11 string functions
- `libc/unistd.c` - sleep/usleep
- `libc/wait.c` - wait/waitpid
- `libc/dirent.c` - directory operations
- `libc/ctype.c` - character classification

**22 syscalls added to `libc/syscall.S`:**
- fork, execve, getpid, getppid, getuid, getgid
- wait4, lseek, pipe, dup, dup2
- chdir, getcwd, isatty
- stat, fstat, mkdir, chmod, fchmod
- getdents64, unlink, kill, link, symlink

### /bin Utilities (19 total)

**Existing:** cat, echo

**New (17):** ls, pwd, mkdir, rm, cp, grep, wc, sleep, kill, ps, sh, head, tail, touch, true, false, ln

### Test Programs (8 in dump/)

- test_minimal.c - Simplest test (getpid, write, fork)
- test_basic.c - Basic POSIX
- test_string.c - String functions
- test_malloc.c - Memory allocation
- test_file.c - File I/O
- test_exec.c - Exec and pipes
- test_stdio.c - Printf and conversions
- test_comprehensive.c - All-in-one

## Build and Test

```bash
# Build everything
make clean && make

# Run in QEMU
make qemu

# Test libc compilation (check for missing symbols)
cd dump && ./build_tests.sh
```

## Key Files to Review

1. `libc/syscall.S` - All syscall wrappers
2. `libc/string.c` - String manipulation
3. `bin/sh/sh.c` - Interactive shell
4. `dump/test_comprehensive.c` - Complete test
5. `dump/TESTING_GUIDE.md` - Detailed testing instructions

## Expected Behavior

After `make qemu`:
1. Kernel boots
2. `/etc/rc` runs
3. Shell prompt appears: `$`
4. Can run any `/bin` utility
5. Interactive Unix-like environment

## Verifying Missing Symbols

The "good exercise" from requirements:

```bash
make build/libc.a

riscv64-unknown-elf-gcc -gdwarf-4 -Wall -Werror -Os -ffreestanding \
  -fno-builtin -nostdlib -nostdinc \
  -isystem $(riscv64-unknown-elf-gcc -print-file-name=include) \
  -mcmodel=medany -march=rv64imac_zicsr_zifencei -mabi=lp64 \
  -Ilibc/include build/libc/crt.S.o dump/test_minimal.c build/libc.a \
  -Wl,--gc-sections -o dump/test_minimal 2>&1 | grep "undefined"
```

No output = all symbols resolved!

## Implementation Complete

All requirements satisfied:
- Root filesystem has more than 3 files
- Shell binary and utilities packaged
- Build process packages binaries into image
- Test programs verify POSIX functions work
- Each missing symbol has implementation
