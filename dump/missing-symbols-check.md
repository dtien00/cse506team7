# Missing Symbols Check

## How to Test for Missing Symbols

When you build on the server, the linker will report any missing symbols. Here's what to look for:

### Common Missing Symbols and Their Locations

If you see linker errors like `undefined reference to 'symbol_name'`, here's where to implement them:

#### Already Implemented in libc:
- `fork`, `execve`, `getpid`, `wait`, `waitpid`, `_exit` → `libc/process.c`
- `read`, `write`, `open`, `close`, `creat` → `libc/fileio.c`
- `strlen`, `strcpy`, `strcmp`, `memcpy`, `memset` → `libc/string.c` (inline) and `libc/include/string.h`
- `malloc`, `calloc`, `realloc`, `free` → `libc/malloc.c`
- `printf`, `fprintf`, `sprintf`, `snprintf`, `puts`, `putchar`, `getchar` → `libc/printf.c`
- `atoi`, `strtol`, `exit`, `abort`, `rand` → `libc/stdlib.c`
- `sleep`, `usleep` → `libc/sleep.c`
- `opendir`, `readdir`, `closedir`, `getcwd` → `libc/dirent.c`
- `stat`, `fstat`, `mkdir`, `chmod` → `libc/stat.c`
- `sigaction`, `kill`, `raise`, `sigprocmask` → `libc/signal.c`
- `time`, `clock_gettime`, `mktime`, `strftime` → `libc/time.c`
- `mmap`, `munmap`, `brk`, `sbrk` → `libc/syscall.S`

#### Stub Implementations (may need full implementation):
- `lseek`, `pipe`, `dup`, `dup2`, `fcntl`, `unlink` → `libc/fileio.c` (currently return -1)
- `chdir` → `libc/dirent.c` (currently returns -1)
- `getppid`, `getuid`, `getgid` → `libc/process.c` (currently return 0)

## Test Program

The `bin/test/test.c` program exercises:
- Process functions: `getpid()`, `fork()`, `wait()`
- I/O functions: `write()`, `open()`, `read()`, `close()`
- String functions: `strcpy()`, `strcat()`, `strlen()`
- Printf family: `printf()`

## What Gets Packaged

The build process packages:
1. All binaries from `bin/*/` → `/bin/` in the filesystem
2. Static files from `rootfs/` → `/` in the filesystem
3. Everything is tar'd and embedded into the kernel as `.tarfs` section

## Verifying the Build

After building, you should have:
- `build/rootfs/bin/sh` - The shell (most critical!)
- `build/rootfs/bin/ls` - Directory listing
- `build/rootfs/bin/cat` - File display
- `build/rootfs/bin/echo` - Echo command
- Plus all other utilities (pwd, cp, rm, mkdir, ps, kill, sleep, true, false, uname, test)

## Common Issues

### Missing `unlink` symbol
- Added to `libc/fileio.c` (stub implementation)
- Declared in `libc/include/unistd.h`

### Missing `getchar` symbol
- Added to `libc/printf.c`
- Declared in `libc/include/stdio.h`

### Shell not starting
- Verify `/bin/sh` exists in the filesystem
- Check that `/etc/rc` has correct shebang and exec line
- Ensure shell binary is executable

### Programs can't find libraries
- All programs are statically linked with `build/libc.a`
- No dynamic linking is used
- `crt.S` provides the `_start` entry point that calls `main()`

## Next Steps

1. Build the kernel on the server: `make`
2. Check for any linker errors about undefined references
3. Implement any missing functions in the appropriate libc source file
4. Add function declarations to the corresponding header file
5. Rebuild and test
