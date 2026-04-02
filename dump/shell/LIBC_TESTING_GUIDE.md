# Libc Testing and Verification Guide

## Problem Statement

From the assignment:
> "Finally, the root filesystem that gets packaged into the kernel image contains only three files. Without a shell binary and the standard utilities, the OS cannot start an interactive session after boot. Make sure your build process is packaging all the necessary user-space binaries into the filesystem image. A good exercise is to try compiling a trivial program (one that just calls a few basic POSIX functions like getpid, write, and fork) against your libc and see which symbols are reported as missing at link time. Each missing symbol needs a corresponding implementation in your library source."

## Solution Overview

This guide provides tools to:
1. Test your libc implementation for missing symbols
2. Verify that binaries are being packaged into the kernel image
3. Ensure your shell can start after boot

## Files Created

### Test Programs

1. **`libc_test_minimal.c`** - Minimal test (getpid, write, fork)
   - Tests only the three functions mentioned in the assignment
   - Good for quick verification

2. **`libc_test.c`** - Comprehensive test
   - Tests all major POSIX functions your shell needs
   - Includes: file I/O, process management, pipes, directories, memory allocation

### Build Scripts

3. **`build_libc_test.sh`** - Main build script (for remote server)
   - Compiles libc sources into object files
   - Creates `libc.a` archive
   - Links test programs
   - Reports missing symbols

4. **`build_libc_test.ps1`** - PowerShell version (for local testing on Windows)
   - Same functionality as bash version

5. **`compile_libc_test.sh`** - Deployment script
   - Runs `flat_kernel.sh` to prepare code
   - Pushes to remote server via rsync
   - Executes libc test build remotely

### Verification Scripts

6. **`verify_tarfs.sh`** - Tarfs verification script
   - Lists contents of `build/rootfs.tar`
   - Checks for shell binary (`bin/sh`)
   - Checks for essential utilities
   - Verifies init script (`etc/rc`)

## Usage Instructions

### Step 1: Test your libc for missing symbols

On your local machine:
```bash
cd dump/shell
./compile_libc_test.sh
```

Or directly on the remote server:
```bash
ssh 506
cd ~/jchlee/dump/shell
chmod +x build_libc_test.sh
./build_libc_test.sh
```

**Expected output:**
- List of compiled libc sources
- Link status for both test programs
- **Any missing symbols will be reported**

### Step 2: Fix missing symbols

If you see errors like:
```
undefined reference to `symbol_name'
```

You need to implement that function. Common locations:

| Function Type | File to edit |
|--------------|--------------|
| System calls (open, read, write, fork, etc.) | `libc/syscall.S` |
| String functions (strlen, strcpy, etc.) | `libc/string.c` or `libc/include/string.h` |
| Memory functions (malloc, free) | `libc/malloc.c` |
| I/O functions (printf, etc.) | `libc/printf.c` |
| Process functions (exit, wait, etc.) | `libc/exit.c`, `libc/wait.c` |

### Step 3: Verify binaries are packaged

After your libc is complete, build the full kernel and check the tarfs:

```bash
cd ~/jchlee
make clean && make

cd dump/shell
chmod +x verify_tarfs.sh
./verify_tarfs.sh
```

This will show you:
- Complete list of files in the tarfs
- Whether `/bin/sh` exists
- Whether essential utilities exist
- Whether `/etc/rc` exists

### Step 4: Test in QEMU

Once everything is packaged correctly:

```bash
cd ~/jchlee
make qemu
```

You should see:
1. Kernel boots
2. Init process starts
3. `/etc/rc` executes
4. Shell starts and you get a prompt

## Current Libc Status

Based on your code, you have:

### ✓ Implemented Syscalls (in `libc/syscall.S`)
- mmap, munmap, brk, sbrk
- open, read, write, close
- fork, execve, getpid, getppid, getuid, getgid
- wait4, lseek, pipe, dup, dup2
- chdir, getcwd, isatty
- stat, fstat, mkdir, chmod, fchmod
- getdents64, unlink, kill
- link, symlink, readlink

### ✓ Implemented Library Functions
- String functions: strlen, strcpy, strcmp, strchr, strstr, etc. (in `libc/string.c`)
- Memory functions: malloc, free (in `libc/malloc.c`)
- I/O functions: printf (in `libc/printf.c`)
- Process functions: exit, wait, waitpid (in `libc/exit.c`, `libc/wait.c`)
- Utility functions: atoi, rand, abs, etc. (in `libc/stdlib.c`)

### ⚠ Potential Missing Functions

You may need to check if these are implemented:
- `readdir` - Check `libc/dirent.c`
- `opendir` - Check `libc/dirent.c`
- `closedir` - Check `libc/dirent.c`

## Troubleshooting

### Problem: "undefined reference to X"

**Solution:** Implement function X in the appropriate libc file.

### Problem: Shell doesn't start after boot

**Possible causes:**
1. Shell binary not in tarfs - run `verify_tarfs.sh`
2. Shell binary not executable - check permissions in Makefile
3. `/etc/rc` not found - verify it's in `rootfs/etc/rc`
4. Missing libc symbols in shell - run libc test

### Problem: Test program compiles but crashes at runtime

**Possible causes:**
1. Syscall numbers in `libc/syscall.S` don't match kernel
2. Syscall implementation in kernel is buggy
3. Memory corruption in malloc/free

## Build Process Flow

Understanding how binaries get into your kernel:

```
1. Source files (bin/*/\*.c) 
   ↓
2. Compiled with libc (Makefile: $(C_BIN) target)
   ↓
3. Placed in build/rootfs/bin/
   ↓
4. Packaged into build/rootfs.tar (Makefile: build/tarfs.o target)
   ↓
5. Converted to ELF object (objcopy)
   ↓
6. Linked into kernel.elf (Makefile: build/kernel.elf target)
   ↓
7. Kernel extracts tarfs at boot
   ↓
8. Init process runs /etc/rc
   ↓
9. /etc/rc execs /bin/sh
```

## Quick Reference Commands

```bash
# Test libc linkage
cd ~/jchlee/dump/shell && ./build_libc_test.sh

# Verify tarfs contents
cd ~/jchlee/dump/shell && ./verify_tarfs.sh

# Full build and test
cd ~/jchlee && make clean && make && make qemu

# Check what binaries Makefile will build
cd ~/jchlee && make -n | grep "build/rootfs/bin/"

# List current binaries in build
ls -lh build/rootfs/bin/
```

## What the Assignment is Testing

The assignment wants you to ensure:

1. **Libc is complete** - All symbols your programs need are implemented
2. **Binaries are built** - Shell and utilities compile successfully
3. **Binaries are packaged** - They're included in the tarfs
4. **Boot process works** - Kernel extracts tarfs and starts shell

The test programs help with #1, and the verification script helps with #2-3.

## Expected Results

After running `build_libc_test.sh`, you should see:
```
✓ Minimal test linked successfully!
✓ Comprehensive test linked successfully!
No missing symbols in minimal test!
No missing symbols in comprehensive test!
```

After running `verify_tarfs.sh`, you should see:
```
✓ Shell binary found: bin/sh
✓ Utility found: bin/ls
✓ Utility found: bin/cat
... (etc)
✓ Init script found: etc/rc
```

If you see any ✗ marks, those need to be fixed before your OS can boot properly.
