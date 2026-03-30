# Libc Testing Suite - Quick Guide

## What This Is

A complete test suite to verify your libc implementation and ensure all binaries are packaged into your kernel image, as required by the assignment.

## Quick Start

```bash
./test_libc.sh
```

This will test your libc on the remote server and report any missing symbols.

## What Was Created

All test files are in `dump/shell/`:

### Core Files
- **`libc_test_minimal.c`** - Tests getpid, write, fork (as mentioned in assignment)
- **`libc_test.c`** - Comprehensive POSIX function test
- **`build_libc_test.sh`** - Main test script (compiles and links tests)

### Verification Scripts
- **`verify_tarfs.sh`** - Checks what's packaged in kernel image
- **`check_binaries.sh`** - Quick binary status check
- **`list_libc_symbols.sh`** - Lists all libc symbols

### Documentation
- **`INDEX_LIBC_TESTS.md`** - File navigation index
- **`QUICK_START_LIBC_TEST.md`** - Quick start guide
- **`README_LIBC_COMPLETE.md`** - Complete documentation
- **`LIBC_TESTING_GUIDE.md`** - Detailed guide with troubleshooting
- **`TESTING_SUMMARY.md`** - Overview and summary
- **`INTEGRATION.md`** - Workflow integration guide
- **`MOVED_FILES_SUMMARY.md`** - List of moved files

### Root Scripts (Convenience)
- **`test_libc.sh`** - Quick libc test (pushes to remote and runs test)
- **`verify_build.sh`** - Full verification pipeline

## Usage

### Test Libc for Missing Symbols

**From your Windows machine:**
```bash
./test_libc.sh
```

**On remote server:**
```bash
cd ~/jchlee/dump/shell
./build_libc_test.sh
```

### Verify Binaries Are Packaged

**After building kernel:**
```bash
ssh 506 "cd ~/jchlee/dump/shell && ./verify_tarfs.sh"
```

### Full Verification Pipeline

**Tests libc, builds kernel, verifies packaging:**
```bash
./verify_build.sh
```

## What to Expect

### Success Output
```
✓ Minimal test linked successfully!
✓ Comprehensive test linked successfully!
No missing symbols in minimal test!
No missing symbols in comprehensive test!
```

### If Missing Symbols
```
Missing symbols in minimal test:
  - some_function
  - another_function
```

→ Implement these functions in your libc

## File Locations

```
sbunix-skeleton/
├── test_libc.sh              ← Run this for quick test
├── verify_build.sh           ← Run this for full verification
├── compile.sh                ← Your existing boot script
└── dump/
    └── shell/                ← All test files here
        ├── libc_test_*.c     ← Test programs
        ├── build_libc_test.* ← Build scripts
        ├── verify_*.sh       ← Verification scripts
        ├── check_*.sh        ← Status scripts
        └── *.md              ← Documentation
```

## Path Updates

All scripts have been updated to work from `dump/shell/`:
- ✓ Libc path: `../../libc` (was `../libc`)
- ✓ Build path: `../../build` (was `../build`)
- ✓ Root scripts: `cd ~/jchlee/dump/shell` (was `cd ~/jchlee/dump`)

## Next Steps

1. **Run the test:**
   ```bash
   ./test_libc.sh
   ```

2. **Fix any missing symbols** (if reported)

3. **Verify packaging:**
   ```bash
   ./verify_build.sh
   ```

4. **Boot and test:**
   ```bash
   ./compile.sh
   ```

## Documentation

For more details, see:
- `dump/shell/INDEX_LIBC_TESTS.md` - Navigation index
- `dump/shell/QUICK_START_LIBC_TEST.md` - Quick start
- `dump/shell/README_LIBC_COMPLETE.md` - Complete guide

## Why This Matters

The assignment states:
> "Without a shell binary and the standard utilities, the OS cannot start an interactive session after boot."

This test suite ensures:
1. Your libc has all necessary functions
2. Your binaries compile successfully
3. Everything is packaged into the kernel
4. Your OS can start an interactive shell

Run `./test_libc.sh` to get started!
