# Test Programs for libc Verification

This directory contains test programs to verify the libc implementation for SBUnix.

## Test Programs

### test_basic.c
Tests fundamental POSIX functions:
- `getpid()` - Get process ID
- `getppid()` - Get parent process ID
- `write()` - Direct system write
- `fork()` - Create child process
- `waitpid()` - Wait for child process

### test_string.c
Tests string manipulation functions:
- `strlen()`, `strcmp()`, `strncmp()`
- `strcpy()`, `strcat()`, `strncat()`
- `strchr()`, `strrchr()`, `strstr()`
- `memcpy()`, `memset()`, `memmove()`, `memcmp()`
- `strdup()`

### test_malloc.c
Tests memory allocation functions:
- `malloc()` - Allocate memory
- `calloc()` - Allocate and zero memory
- `realloc()` - Resize allocation
- `free()` - Free memory
- Large allocations (mmap threshold)

### test_file.c
Tests file I/O operations:
- `open()` - Open files with various flags
- `read()` - Read from files
- `write()` - Write to files
- `close()` - Close file descriptors
- `lseek()` - Seek within files
- `unlink()` - Delete files

### test_exec.c
Tests process execution:
- `fork()` - Create processes
- `execve()` - Execute programs
- `waitpid()` - Wait for children
- `pipe()` - Create pipes for IPC

### test_stdio.c
Tests standard I/O and conversion:
- `printf()` - Formatted output
- `atoi()` - String to integer
- `strtol()` - String to long with base

### test_comprehensive.c
A comprehensive test that exercises all major libc components in one program.

## Building and Running

These test programs are designed to be compiled against the SBUnix libc:

```bash
# Compile a test program
riscv64-unknown-elf-gcc -gdwarf-4 -Wall -Werror -Os -ffreestanding -fno-builtin \
  -nostdlib -nostdinc -isystem $(riscv64-unknown-elf-gcc -print-file-name=include) \
  -mcmodel=medany -march=rv64imac_zicsr_zifencei -mabi=lp64 \
  -Ilibc/include build/libc/crt.S.o dump/test_basic.c build/libc.a -o test_basic

# Or use the Makefile pattern (if integrated)
make build/libc.a
riscv64-unknown-elf-gcc $(CFLAGS) -Ilibc/include build/libc/crt.S.o \
  dump/test_basic.c build/libc.a -o test_basic
```

## Checking for Missing Symbols

To identify missing libc symbols, try compiling a test program and check for linker errors:

```bash
# Build libc
make build/libc.a

# Try linking a test
riscv64-unknown-elf-gcc $(CFLAGS) -Ilibc/include build/libc/crt.S.o \
  dump/test_comprehensive.c build/libc.a -o test_comprehensive 2>&1 | grep "undefined reference"
```

Any "undefined reference" errors indicate missing implementations that need to be added to libc.

## Expected Output

Each test program prints its progress and results. Successful tests end with:
```
=== All [test name] tests passed! ===
```

Failed tests will print error messages and return non-zero exit codes.
