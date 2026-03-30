# Libc Test Suite

This directory contains test programs to verify your libc implementation completeness.

## Purpose

The assignment states:
> "A good exercise is to try compiling a trivial program (one that just calls a few basic POSIX functions like getpid, write, and fork) against your libc and see which symbols are reported as missing at link time. Each missing symbol needs a corresponding implementation in your library source."

These test programs help you identify missing symbols in your libc implementation.

## Files

- `libc_test_minimal.c` - Minimal test with just getpid, write, fork (as mentioned in assignment)
- `libc_test.c` - Comprehensive test covering all major POSIX functions
- `build_libc_test.sh` - Build script that compiles tests and reports missing symbols

## Usage

### On your remote server:

```bash
cd ~/jchlee/dump/shell
chmod +x build_libc_test.sh
./build_libc_test.sh
```

The script will:
1. Compile all libc sources into object files
2. Create a `libc.a` archive
3. Attempt to link both test programs
4. Report any missing symbols (undefined references)

### Understanding the output

If you see errors like:
```
undefined reference to `symbol_name'
```

This means you need to implement `symbol_name` in your libc.

### Common missing symbols and where to implement them:

| Symbol | File to implement in | Notes |
|--------|---------------------|-------|
| `open` | `libc/syscall.S` | Already implemented |
| `read` | `libc/syscall.S` | Already implemented |
| `write` | `libc/syscall.S` | Already implemented |
| `close` | `libc/syscall.S` | Already implemented |
| `fork` | `libc/syscall.S` | Already implemented |
| `execve` | `libc/syscall.S` | Already implemented |
| `getpid` | `libc/syscall.S` | Already implemented |
| `strlen` | `libc/string.c` | Should implement |
| `strcpy` | `libc/string.c` | Already implemented |
| `strcmp` | `libc/string.c` | Already implemented |
| `memset` | `libc/string.c` | Should implement |
| `memcpy` | `libc/string.c` | Should implement |
| `malloc` | `libc/malloc.c` | Already implemented |
| `free` | `libc/malloc.c` | Already implemented |
| `printf` | `libc/printf.c` | Already implemented |

## Verifying your filesystem packaging

After fixing any missing symbols, you should:

1. Build your kernel with the updated libc: `make clean && make`
2. Ensure your shell binary (`bin/sh/sh.c`) is being packaged
3. Verify the tarfs contains all binaries by checking the Makefile's `USER_BIN` target

The Makefile shows that binaries are packaged via:
```makefile
$(C_BIN): build/rootfs/bin/%: $$(wildcard bin/%/*.c) build/libc/crt.S.o build/libc.a
	$(CC) $(CFLAGS) -Ilibc/include build/libc/crt.S.o bin/$*/*.c build/libc.a -o $@
```

And then packaged into the kernel via:
```makefile
build/tarfs.o: $(USER_BIN)
	cp -a rootfs/. build/rootfs/
	tar cf build/rootfs.tar -C build/rootfs .
	cd build && $(OBJCOPY) ... rootfs.tar tarfs.o
```

## Next Steps

1. Run the build script to identify missing symbols
2. Implement any missing functions in the appropriate libc source files
3. Rebuild and retest until all symbols are resolved
4. Verify your shell and utilities are being packaged into the kernel image
