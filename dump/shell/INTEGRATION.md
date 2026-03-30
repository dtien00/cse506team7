# Integration with Your Build System

## How to Use These Tests with Your Existing Workflow

### Your Current Setup

From `compile.sh`, you currently:
```bash
./flat_kernel.sh                    # Prepare code
rsync -avz --delete ./ 506:~/jchlee/  # Push to server
exec ssh -t 506 "cd ~/jchlee/flat_kernel/dump/shell && ./build_tests.sh"
```

### New Testing Options

#### Option A: Quick Libc Test (Recommended First)

Before building the full kernel, test if your libc is complete:

```bash
./test_libc.sh
```

This will:
- Push your code to the server
- Run `build_libc_test.sh` in the `dump/` directory
- Report any missing symbols

**Use this when:** You've made changes to libc and want to verify it's complete.

#### Option B: Full Verification Pipeline

Test everything in sequence:

```bash
./verify_build.sh
```

This will:
1. Test libc for missing symbols
2. Build the full kernel
3. Verify binaries are packaged
4. Report overall status

**Use this when:** You want to ensure everything is ready before booting.

#### Option C: Manual Control

Keep your existing workflow and add verification steps:

```bash
# 1. Test libc
ssh 506 "cd ~/jchlee/dump/shell && ./build_libc_test.sh"

# 2. Build kernel (your existing process)
./compile.sh

# 3. Verify packaging
ssh 506 "cd ~/jchlee/dump/shell && ./verify_tarfs.sh"
```

## Modifying Your compile.sh

### Add Pre-Build Libc Check

You can add a libc check before building:

```bash
#!/bin/bash

./flat_kernel.sh

rsync -avz --delete \
  --exclude='build/kernel' \
  ./ 506:~/jchlee/

# NEW: Quick libc check
echo -e "\n\033[0;33m=== Pre-Build: Checking Libc ===\033[0m\n"
ssh 506 "cd ~/jchlee/dump/shell && chmod +x build_libc_test.sh && ./build_libc_test.sh" || {
    echo -e "\n\033[0;31mLibc check failed! Fix missing symbols.\033[0m\n"
    exit 1
}

echo -e "\n\033[0;32m=== Libc OK - Proceeding to Build ===\033[0m\n"

# Continue with your existing build...
exec ssh -t 506 "cd ~/jchlee/flat_kernel/dump/shell && ./build_tests.sh"
```

### Add Post-Build Verification

Or add verification after building:

```bash
#!/bin/bash

./flat_kernel.sh

rsync -avz --delete \
  --exclude='build/kernel' \
  ./ 506:~/jchlee/

# Your existing build
ssh 506 "cd ~/jchlee && make 2>&1"

# NEW: Verify packaging
echo -e "\n\033[0;33m=== Post-Build: Verifying Packaging ===\033[0m\n"
ssh 506 "cd ~/jchlee/dump/shell && chmod +x check_binaries.sh && ./check_binaries.sh"

# Launch QEMU
echo -e "\n\033[0;32m=== Launching QEMU ===\033[0m\n"
exec ssh -t 506 "cd ~/jchlee && make qemu"
```

## Directory Structure

After running the tests, your directory structure will be:

```
sbunix-skeleton/
├── compile.sh              ← Your existing compile script
├── test_libc.sh            ← NEW: Quick libc test
├── verify_build.sh         ← NEW: Full verification
├── Makefile                ← Your existing Makefile
├── libc/                   ← Your libc implementation
│   ├── syscall.S
│   ├── string.c
│   ├── malloc.c
│   └── ...
├── bin/                    ← Your user programs
│   ├── sh/sh.c            ← CRITICAL: Shell
│   ├── ls/ls.c
│   └── ...
├── rootfs/                 ← Static files to include
│   └── etc/rc             ← Init script
└── dump/
    └── shell/                   ← Test suite (NEW)
        ├── libc_test_minimal.c      ← Minimal test program
        ├── libc_test.c              ← Comprehensive test program
        ├── build_libc_test.sh       ← Main test script
        ├── build_libc_test.ps1      ← PowerShell version
        ├── verify_tarfs.sh          ← Tarfs verification
        ├── check_binaries.sh        ← Quick binary check
        ├── README_libc_test.md      ← Detailed docs
        ├── LIBC_TESTING_GUIDE.md    ← Complete guide
        ├── QUICK_START_LIBC_TEST.md ← Quick reference
        ├── TESTING_SUMMARY.md       ← Overview
        └── INTEGRATION.md           ← This file

On remote server after build:
~/jchlee/
├── build/
│   ├── libc.a                   ← Compiled libc
│   ├── rootfs/
│   │   └── bin/
│   │       ├── sh              ← Shell binary (CRITICAL)
│   │       ├── ls
│   │       └── ...
│   ├── rootfs.tar              ← Packaged filesystem
│   ├── tarfs.o                 ← Tarfs as ELF object
│   └── kernel.elf              ← Final kernel image
└── dump/
    └── shell/
        └── build_test/          ← Libc test artifacts
            ├── libc.a
            ├── libc_test_minimal
            ├── libc_test
            ├── minimal_link.log
            └── full_link.log
```

## Understanding the Tests

### What `build_libc_test.sh` Tests

This script answers: **"Is my libc complete?"**

It compiles test programs that use common POSIX functions:
- `getpid()` - Get process ID
- `write()` - Write to file descriptor
- `fork()` - Create child process
- `open()`, `read()`, `close()` - File I/O
- `malloc()`, `free()` - Memory allocation
- `printf()` - Formatted output
- And many more...

If any function is missing, the linker will report `undefined reference`.

### What `verify_tarfs.sh` Tests

This script answers: **"Are my binaries packaged into the kernel?"**

It checks the `build/rootfs.tar` archive to ensure:
- Shell binary exists at `bin/sh`
- Utilities exist (ls, cat, echo, etc.)
- Init script exists at `etc/rc`

If shell is missing, your OS won't have an interactive prompt.

### What `check_binaries.sh` Tests

This script answers: **"Did the build complete successfully?"**

Quick sanity check:
- Binaries exist in `build/rootfs/bin/`
- Tarfs archive was created
- Kernel image was created

## The Assignment Requirement

From the assignment:
> "A good exercise is to try compiling a trivial program (one that just calls a few basic POSIX functions like getpid, write, and fork) against your libc and see which symbols are reported as missing at link time."

**This is exactly what `build_libc_test.sh` does!**

It compiles `libc_test_minimal.c` which calls:
- `getpid()` ✓
- `write()` ✓
- `fork()` ✓

And reports any missing symbols.

## Common Scenarios

### Scenario 1: Fresh Implementation

You've just implemented your libc and want to test it:

```bash
./test_libc.sh
```

Fix any missing symbols, then:

```bash
./verify_build.sh
```

### Scenario 2: Debugging Boot Issues

Your kernel boots but no shell prompt appears:

```bash
ssh 506 "cd ~/jchlee/dump/shell && ./verify_tarfs.sh"
```

Check if shell binary is in tarfs. If not, check Makefile.

### Scenario 3: Shell Crashes on Start

Shell is packaged but crashes:

```bash
ssh 506 "cd ~/jchlee/dump/shell && ./build_libc_test.sh"
```

Check if libc has all symbols. If yes, debug shell code.

### Scenario 4: Continuous Development

You're actively developing and want to test frequently:

```bash
# Quick test after libc changes
./test_libc.sh

# Full test before submission
./verify_build.sh
./compile.sh  # Boot and manually test
```

## Integration Points

### With Your Makefile

The test scripts use the same compiler flags as your Makefile:
```bash
CFLAGS="-gdwarf-4 -Wall -Werror -Os -ffreestanding -fno-builtin -nostdlib -nostdinc ..."
```

This ensures test results match actual build results.

### With Your Build Process

The scripts respect your build structure:
- Libc sources in `libc/`
- Binaries in `bin/*/`
- Build output in `build/`
- Test output in `dump/build_test/`

### With Your Remote Server

All scripts are designed to run on your remote server (506) where the cross-compiler is available.

## Workflow Recommendation

**For development:**
1. Make changes to libc or binaries
2. Run `./test_libc.sh` to verify libc
3. If OK, run `./compile.sh` to boot test

**Before submission:**
1. Run `./verify_build.sh` to check everything
2. Run `./compile.sh` to boot test
3. Manually test shell commands
4. Submit when all checks pass

## Summary

You now have a complete test suite that:
- ✓ Tests libc completeness (missing symbols)
- ✓ Verifies binary packaging (tarfs contents)
- ✓ Provides quick status checks
- ✓ Integrates with your existing workflow
- ✓ Works with your remote server setup

**Start with:** `./test_libc.sh`

This will tell you immediately if your libc is missing any symbols!
