# Testing Guide for SBUnix libc Implementation

## Quick Start

### 1. Build Everything

```bash
make clean
make
```

This will:
- Compile all libc source files into `build/libc.a`
- Build all `/bin` utilities (cat, echo, ls, pwd, mkdir, rm, cp, grep, wc, sleep, kill, ps, sh)
- Package everything into the kernel image

### 2. Run in QEMU

```bash
make qemu
```

The system should boot and start `/bin/sh` automatically via `/etc/rc`.

## Testing Individual libc Functions

### Method 1: Using Test Programs

Build the test programs:

```bash
# On Linux/Mac
cd dump
./build_tests.sh

# On Windows
cd dump
.\build_tests.ps1
```

This will compile each test program and report any missing symbols.

### Method 2: Manual Compilation

Compile a single test to check for missing symbols:

```bash
make build/libc.a

riscv64-unknown-elf-gcc -gdwarf-4 -Wall -Werror -Os -ffreestanding \
  -fno-builtin -nostdlib -nostdinc \
  -isystem $(riscv64-unknown-elf-gcc -print-file-name=include) \
  -mcmodel=medany -march=rv64imac_zicsr_zifencei -mabi=lp64 \
  -Ilibc/include build/libc/crt.S.o dump/test_basic.c build/libc.a \
  -Wl,--gc-sections -o dump/test_basic
```

If you see errors like:
```
undefined reference to `some_function'
```

Then you need to implement `some_function` in libc.

## Test Programs Included

### test_minimal.c
The simplest possible test - uses only `getpid()`, `write()`, and `fork()`.
This is the "trivial program" mentioned in the requirements.

**Functions tested:**
- `getpid()`
- `write()`
- `fork()`
- `waitpid()`

### test_basic.c
Tests fundamental process and I/O functions.

**Functions tested:**
- `getpid()`, `getppid()`
- `write()`
- `fork()`, `waitpid()`
- `printf()`

### test_string.c
Comprehensive string manipulation test.

**Functions tested:**
- `strlen()`, `strcmp()`, `strncmp()`
- `strcpy()`, `strcat()`
- `strchr()`, `strstr()`
- `memcpy()`, `memset()`
- `strdup()`, `malloc()`, `free()`

### test_malloc.c
Memory allocation and management.

**Functions tested:**
- `malloc()`, `calloc()`, `realloc()`, `free()`
- Large allocations (mmap threshold)

### test_file.c
File I/O operations.

**Functions tested:**
- `open()`, `read()`, `write()`, `close()`
- `lseek()`, `unlink()`

### test_exec.c
Process execution and IPC.

**Functions tested:**
- `fork()`, `execve()`, `waitpid()`
- `pipe()`, `read()`, `write()`

### test_stdio.c
Standard I/O and conversions.

**Functions tested:**
- `printf()` with various formats
- `atoi()`, `strtol()`

### test_comprehensive.c
All-in-one test covering all major libc components.

## Interactive Testing in QEMU

Once booted into the shell, try these commands:

```bash
# Basic commands
$ echo Hello World
$ pwd
$ ls /
$ ls /bin

# File operations
$ echo "test content" > /tmp/test.txt
$ cat /tmp/test.txt
$ cp /tmp/test.txt /tmp/test2.txt
$ cat /tmp/test2.txt
$ rm /tmp/test.txt /tmp/test2.txt

# Process operations
$ ps
$ sleep 2

# Text processing
$ echo "line1" > /tmp/file.txt
$ echo "line2" >> /tmp/file.txt
$ cat /tmp/file.txt | wc
$ grep "line1" /tmp/file.txt

# Directory operations
$ mkdir /tmp/testdir
$ ls /tmp
```

## Debugging Missing Symbols

If compilation fails with undefined references:

1. **Identify the missing symbol:**
   ```
   undefined reference to `some_function'
   ```

2. **Check if it's a syscall:**
   - Look in `libc/include/unistd.h`, `fcntl.h`, etc.
   - If it's a syscall, add it to `libc/syscall.S`

3. **Check if it's a libc function:**
   - String functions → add to `libc/string.c`
   - Memory functions → check `libc/malloc.c`
   - I/O functions → add to `libc/unistd.c` or create new file
   - Conversion functions → check `libc/stdlib.c`

4. **Add the implementation:**
   - Create or update the appropriate `.c` file
   - Ensure the function is declared in the header
   - Rebuild: `make clean && make`

## Common Missing Symbols and Solutions

| Symbol | Location | Type |
|--------|----------|------|
| `fork` | `libc/syscall.S` | Syscall wrapper |
| `getpid` | `libc/syscall.S` | Syscall wrapper |
| `write` | `libc/syscall.S` | Syscall wrapper |
| `strlen` | `libc/include/string.h` | Inline function |
| `strcmp` | `libc/string.c` | Library function |
| `malloc` | `libc/malloc.c` | Library function |
| `printf` | `libc/printf.c` | Library function |
| `exit` | `libc/exit.c` | Syscall wrapper |

## Syscall Numbers Reference

The syscall numbers used in `libc/syscall.S`:

| Syscall | Number | Description |
|---------|--------|-------------|
| exit | 1 | Terminate process |
| fork | 2 | Create child process |
| read | 3 | Read from file descriptor |
| write | 4 | Write to file descriptor |
| open | 5 | Open file |
| close | 6 | Close file descriptor |
| lseek | 8 | Seek in file |
| sbrk | 10 | Grow heap |
| execve | 11 | Execute program |
| mmap | 14 | Map memory |
| munmap | 15 | Unmap memory |
| brk | 16 | Set heap break |
| getcwd | 17 | Get current directory |
| getpid | 20 | Get process ID |
| pipe | 22 | Create pipe |
| dup | 23 | Duplicate fd |
| getuid | 24 | Get user ID |
| isatty | 29 | Check if tty |
| dup2 | 33 | Duplicate fd to specific number |
| getppid | 39 | Get parent process ID |
| getgid | 47 | Get group ID |
| chdir | 49 | Change directory |
| wait4 | 61 | Wait for process |
| kill | 62 | Send signal |
| stat | 79 | Get file status |
| fstat | 80 | Get file status by fd |
| mkdir | 83 | Create directory |
| unlink | 87 | Delete file |
| chmod | 90 | Change file mode |
| fchmod | 91 | Change file mode by fd |

## Expected Results

When all tests compile successfully, you should see:
```
✓ test_minimal compiled successfully
✓ test_basic compiled successfully
✓ test_string compiled successfully
✓ test_malloc compiled successfully
✓ test_file compiled successfully
✓ test_exec compiled successfully
✓ test_stdio compiled successfully
✓ test_comprehensive compiled successfully
```

If you see undefined references, those are the symbols you still need to implement.

## Integration with Kernel

The `/etc/rc` script runs at boot and executes `/bin/sh`, which provides an interactive shell. The shell can then run any of the utilities in `/bin/`.

All binaries are packaged into the kernel image via the tarfs mechanism (see Makefile lines 88-96).
