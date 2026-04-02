# Quick Start: Testing Your Libc

## The Problem

Your assignment says:
> "Without a shell binary and the standard utilities, the OS cannot start an interactive session after boot."

You need to verify:
1. Your libc has all necessary functions implemented
2. Your binaries (shell + utilities) link successfully
3. Everything is packaged into the kernel image

## The Solution (3 Steps)

### Step 1: Test Libc Linkage

Run on your remote server:
```bash
ssh 506
cd ~/jchlee/dump/shell
chmod +x build_libc_test.sh
./build_libc_test.sh
```

**What this does:**
- Compiles your libc into `libc.a`
- Tries to link two test programs against it
- Reports any missing symbols (undefined references)

**What to look for:**
- ✓ Green checkmarks = success
- ✗ Red X marks = missing symbols that need implementation

### Step 2: Fix Missing Symbols

If you see `undefined reference to 'function_name'`, implement it:

**Common missing functions:**

| If missing | Where to add it |
|------------|-----------------|
| `strlen`, `memcpy`, `memset` | Already in `libc/include/string.h` as inline |
| `strcpy`, `strcmp`, `strcat` | Already in `libc/string.c` |
| `open`, `read`, `write`, `close` | Already in `libc/syscall.S` |
| `fork`, `execve`, `getpid` | Already in `libc/syscall.S` |
| `malloc`, `free` | Already in `libc/malloc.c` |
| `printf` | Already in `libc/printf.c` |

Your libc looks pretty complete! The test will tell you if anything is missing.

### Step 3: Verify Packaging

After libc tests pass, verify binaries are in the kernel:

```bash
cd ~/jchlee
make clean && make

cd dump/shell
chmod +x verify_tarfs.sh
./verify_tarfs.sh
```

**What to look for:**
- ✓ Shell binary found: `bin/sh`
- ✓ Utilities found: `bin/ls`, `bin/cat`, etc.
- ✓ Init script found: `etc/rc`

## Test Programs Explained

### Minimal Test (`libc_test_minimal.c`)

Tests exactly what the assignment mentions:
```c
pid_t pid = getpid();     // Get process ID
write(STDOUT_FILENO, ...); // Write to stdout
pid_t child = fork();      // Fork a child process
```

If this links successfully, your basic libc works.

### Comprehensive Test (`libc_test.c`)

Tests everything your shell needs:
- File I/O (open, read, write, close)
- Process management (fork, wait, execve)
- Pipes (pipe, dup, dup2)
- Directories (opendir, readdir, closedir)
- Memory (malloc, free)
- String operations (strlen, strcpy, strcmp)

If this links successfully, your full libc is ready.

## Quick Commands

```bash
# From your Windows machine - test libc on remote
./compile_libc_test.sh

# On remote server - verify what's in kernel image
ssh 506 "cd ~/jchlee/dump/shell && ./verify_tarfs.sh"

# Full build and boot test
./compile.sh
```

## What Success Looks Like

**After `build_libc_test.sh`:**
```
✓ Minimal test linked successfully!
✓ Comprehensive test linked successfully!
No missing symbols in minimal test!
No missing symbols in comprehensive test!
```

**After `verify_tarfs.sh`:**
```
✓ Shell binary found: bin/sh
✓ Utility found: bin/ls
✓ Utility found: bin/cat
✓ Init script found: etc/rc
```

**After `make qemu`:**
```
[kernel boots]
Running /etc/rc
$ [you get a shell prompt]
```

## Troubleshooting

### "undefined reference to X"
→ Implement function X in your libc

### "Shell binary MISSING: bin/sh"
→ Check that `bin/sh/sh.c` exists and Makefile is building it

### Shell binary exists but no prompt after boot
→ Check kernel's init process and tarfs extraction code

### Binaries exist but crash immediately
→ Syscall numbers in `libc/syscall.S` might not match kernel implementation

## Architecture Overview

```
Your Libc Structure:
├── libc/syscall.S       ← System call wrappers (ecall instructions)
├── libc/string.c        ← String manipulation functions
├── libc/malloc.c        ← Memory allocation
├── libc/printf.c        ← Formatted output
├── libc/exit.c          ← Process termination
├── libc/wait.c          ← Process waiting
├── libc/stdlib.c        ← Standard library utilities
├── libc/dirent.c        ← Directory operations
├── libc/crt.S           ← C runtime startup (_start)
└── libc/include/*.h     ← Headers (some have inline implementations)

User Programs:
├── bin/sh/sh.c          ← Your shell (CRITICAL for interactive session)
├── bin/ls/ls.c          ← List directory
├── bin/cat/cat.c        ← Concatenate files
├── bin/echo/echo.c      ← Print arguments
└── ... (other utilities)

Build Process:
1. Compile libc → build/libc.a
2. Compile each bin/*/*.c with libc → build/rootfs/bin/*
3. Package all binaries → build/rootfs.tar
4. Convert tar to ELF object → build/tarfs.o
5. Link kernel + tarfs.o → build/kernel.elf
```

## Key Makefile Targets

```makefile
# Build libc archive
build/libc.a: $(LIBC_OBJ)
	$(AR) rcs $@ $^

# Build user binaries (links with libc)
$(C_BIN): build/rootfs/bin/%: $$(wildcard bin/%/*.c) build/libc/crt.S.o build/libc.a
	$(CC) $(CFLAGS) -Ilibc/include build/libc/crt.S.o bin/$*/*.c build/libc.a -o $@

# Package into tarfs
build/tarfs.o: $(USER_BIN)
	cp -a rootfs/. build/rootfs/
	tar cf build/rootfs.tar -C build/rootfs .
	$(OBJCOPY) ... rootfs.tar tarfs.o

# Link kernel with tarfs
build/kernel.elf: $(KERN_ALL) build/tarfs.o
	$(LD) -T kernel/kernel.ld $(KERN_ALL) build/tarfs.o -o $@
```

## Boot Sequence

```
1. QEMU loads kernel.elf
2. Kernel initializes (memory, processes, VFS)
3. Kernel extracts tarfs to root filesystem
4. Kernel creates init process
5. Init process executes /etc/rc
6. /etc/rc executes: exec /bin/sh
7. Shell starts and displays prompt
8. User can now interact with OS
```

If any step fails, you won't get a shell prompt.

## Testing Workflow

```
┌─────────────────────────┐
│ 1. Run libc test build  │
│    ./build_libc_test.sh │
└───────────┬─────────────┘
            │
            ├─ Missing symbols? ──→ Implement in libc ──┐
            │                                            │
            └─ All symbols OK ──→ Continue              │
                     │                                   │
                     ↓                                   │
         ┌───────────────────────┐                      │
         │ 2. Build full kernel  │                      │
         │    make clean && make │                      │
         └───────────┬───────────┘                      │
                     │                                   │
                     ↓                                   │
         ┌───────────────────────┐                      │
         │ 3. Verify tarfs       │                      │
         │    ./verify_tarfs.sh  │                      │
         └───────────┬───────────┘                      │
                     │                                   │
                     ├─ Binaries missing? ──→ Fix Makefile ┘
                     │
                     └─ All present ──→ Continue
                              │
                              ↓
                  ┌───────────────────┐
                  │ 4. Test in QEMU   │
                  │    make qemu      │
                  └───────────────────┘
```

## Summary

You now have:
1. **Test programs** to verify libc completeness
2. **Build scripts** to identify missing symbols
3. **Verification scripts** to check packaging
4. **Documentation** explaining the entire process

Run `./compile_libc_test.sh` to get started!
