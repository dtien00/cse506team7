# Libc Test Suite - File Index

All libc testing files are now in `dump/shell/`.

## 🚀 Quick Start

**From your Windows machine:**
```bash
./test_libc.sh
```

**On remote server:**
```bash
cd ~/jchlee/dump/shell
./build_libc_test.sh
```

## 📁 File Organization

### Test Programs
- `libc_test_minimal.c` - Minimal test (getpid, write, fork)
- `libc_test.c` - Comprehensive POSIX test

### Build Scripts
- `build_libc_test.sh` - Main test script (bash)
- `build_libc_test.ps1` - PowerShell version
- `compile_libc_test.sh` - Deploy and test from local machine

### Verification Scripts
- `verify_tarfs.sh` - Check tarfs contents
- `check_binaries.sh` - Quick binary status
- `list_libc_symbols.sh` - List libc symbols

### Shell Test Scripts (Original)
- `build_tests.sh` - Original shell test suite
- `build_tests.ps1` - PowerShell version
- `test_*.c` - Various shell test programs

### Documentation
- `INDEX_LIBC_TESTS.md` - This file (navigation index)
- `QUICK_START_LIBC_TEST.md` - Quick start guide
- `README_LIBC_COMPLETE.md` - Complete documentation
- `LIBC_TESTING_GUIDE.md` - Detailed testing guide
- `TESTING_SUMMARY.md` - Summary and overview
- `INTEGRATION.md` - Integration with workflow
- `README_libc_test.md` - Basic libc test readme
- `README.md` - Original shell test readme
- `OVERVIEW.md` - Shell test overview
- `IMPLEMENTATION_*.md` - Implementation documentation

## 📖 Where to Start

### New to this test suite?
→ Read `QUICK_START_LIBC_TEST.md`

### Want complete documentation?
→ Read `README_LIBC_COMPLETE.md`

### Need troubleshooting help?
→ Read `LIBC_TESTING_GUIDE.md`

### Want to integrate with your workflow?
→ Read `INTEGRATION.md`

### Just want to run the test?
→ Run `./build_libc_test.sh`

## 🎯 Common Tasks

### Test if libc is complete
```bash
cd ~/jchlee/dump/shell
./build_libc_test.sh
```

### Check what's in kernel image
```bash
cd ~/jchlee/dump/shell
./verify_tarfs.sh
```

### List all libc symbols
```bash
cd ~/jchlee/dump/shell
./list_libc_symbols.sh
```

### Quick status check
```bash
cd ~/jchlee/dump/shell
./check_binaries.sh
```

## 🔄 Workflow Integration

### From root directory:
```bash
# Quick libc test
./test_libc.sh

# Full verification
./verify_build.sh

# Normal build and boot
./compile.sh
```

### From dump/shell directory:
```bash
# Test libc
./build_libc_test.sh

# Verify packaging
./verify_tarfs.sh

# Check status
./check_binaries.sh
```

## 📊 Output Locations

After running tests:
- `build_test/` - Libc test build artifacts
  - `libc.a` - Compiled libc archive
  - `crt.o` - C runtime startup
  - `libc_test_minimal` - Minimal test binary
  - `libc_test` - Comprehensive test binary
  - `minimal_link.log` - Link output for minimal test
  - `full_link.log` - Link output for comprehensive test

## 🔍 What Gets Tested

### Minimal Test
- getpid() - Get process ID
- write() - Write to stdout
- fork() - Create child process
- _exit() - Exit process

### Comprehensive Test
- File I/O: open, read, write, close, lseek, unlink
- Process: fork, execve, wait, waitpid, getpid, getppid
- Pipes: pipe, dup, dup2
- Directories: opendir, readdir, closedir, mkdir, chdir, getcwd
- Memory: malloc, free, memset, memcpy
- Strings: strlen, strcpy, strcmp, strchr, strstr, strcat

## ✅ Success Criteria

**Libc test passes when:**
- ✓ No undefined references in minimal test
- ✓ No undefined references in comprehensive test
- ✓ Both test binaries link successfully

**Packaging verified when:**
- ✓ Shell binary exists in tarfs
- ✓ Utilities exist in tarfs
- ✓ Init script exists in tarfs

**System ready when:**
- ✓ Kernel boots
- ✓ Shell prompt appears
- ✓ Commands work (ls, cat, echo, etc.)

## 🆘 Getting Help

If you see errors:
1. Check the `.log` files in `build_test/`
2. Read the troubleshooting section in `LIBC_TESTING_GUIDE.md`
3. Check the FAQ in `README_LIBC_COMPLETE.md`

## 📝 Notes

- All scripts work with your remote server setup (506)
- Scripts use same compiler flags as your Makefile
- Test output is separate from main build (`build_test/` vs `build/`)
- Scripts are safe to run repeatedly
- No changes to your existing code or Makefile needed
