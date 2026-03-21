# SBUnix Grading Server Fixes

## Problems Identified

### Problem 2: Missing POSIX Headers
The libc only had 3 headers (stdio.h, stdlib.h, types.h). User programs need standard POSIX headers to compile.

---

## Fixes Applied

### Fix 2: Added Missing POSIX Headers

Created the following headers in `libc/include/`:

1. **string.h** - String manipulation functions (memcpy, memset, strlen, strcmp, etc.)
2. **unistd.h** - POSIX OS API (read, write, fork, exec, getpid, etc.)
3. **fcntl.h** - File control (open, creat, O_RDONLY, O_WRONLY, etc.)
4. **errno.h** - Error numbers (ENOENT, EINVAL, ENOMEM, etc.)
5. **signal.h** - Signal handling (SIGINT, SIGTERM, signal(), kill(), etc.)
6. **time.h** - Time structures and functions (time_t, struct tm, time(), etc.)
7. **dirent.h** - Directory operations (DIR, opendir, readdir, closedir)
8. **stddef.h** - Standard definitions (NULL, offsetof, ptrdiff_t)
9. **stdint.h** - Integer types with size guarantees (INT8_MAX, UINT64_MAX, etc.)
10. **limits.h** - Implementation limits (INT_MAX, PATH_MAX, etc.)
11. **stdbool.h** - Boolean type (bool, true, false)
12. **assert.h** - Assertions (assert macro)
13. **ctype.h** - Character classification (isdigit, isalpha, tolower, etc.)

Created the following headers in `libc/include/sys/`:

14. **sys/types.h** - System types (pid_t, uid_t, mode_t, etc.)
15. **sys/stat.h** - File status (struct stat, S_ISREG, mkdir, chmod, etc.)
16. **sys/wait.h** - Process wait (wait, waitpid, WEXITSTATUS, etc.)
17. **sys/mman.h** - Memory management (mmap, munmap, PROT_READ, MAP_PRIVATE, etc.)
18. **sys/ioctl.h** - I/O control (ioctl, TIOCGWINSZ, etc.)

---

## Verification Commands (Run on Remote)

### Step 1: Verify the Makefile changes
```bash
cd /path/to/sbunix-skeleton
grep "KERN_ASM :=" Makefile
grep "KERN_C   :=" Makefile
```

Expected output should show `wildcard` instead of `find`.

### Step 2: Clean and rebuild
```bash
make clean
make build/kernel.elf
```

### Step 3: Verify kernel object files were compiled
```bash
ls -lh build/kernel/arch/riscv/*.o
ls -lh build/kernel/core/*.o
ls -lh build/kernel/mm/*.o
ls -lh build/kernel/trap/*.o
```

You should see `.o` files for all kernel source files.

### Step 4: Check kernel.elf has an entry point
```bash
riscv64-unknown-elf-readelf -h build/kernel.elf | grep "Entry point"
```

Expected output: `Entry point address: 0x80200000`

### Step 5: Verify kernel.elf has .text section with code
```bash
riscv64-unknown-elf-readelf -S build/kernel.elf | grep -A1 "\.text"
riscv64-unknown-elf-size build/kernel.elf
```

The .text section should have a non-zero size (should be several KB).

### Step 6: Verify all POSIX headers exist
```bash
ls -1 libc/include/*.h
ls -1 libc/include/sys/*.h
```

You should see at least:
- libc/include/string.h
- libc/include/unistd.h
- libc/include/fcntl.h
- libc/include/errno.h
- libc/include/signal.h
- libc/include/time.h
- libc/include/dirent.h
- libc/include/sys/types.h
- libc/include/sys/stat.h
- libc/include/sys/wait.h
- libc/include/sys/mman.h
- libc/include/sys/ioctl.h
- And others...

### Step 7: Test a simple user program compiles
```bash
# Create a test program that uses standard headers
cat > /tmp/test_headers.c << 'EOF'
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <signal.h>

int main() {
    printf("Headers work!\n");
    return 0;
}
EOF

# Try to compile it (will fail at link time since we don't have implementations, but should pass header checks)
riscv64-unknown-elf-gcc -ffreestanding -nostdlib -Ilibc/include -c /tmp/test_headers.c -o /tmp/test_headers.o
echo "If this compiled without errors, headers are working!"
```

### Step 8: Submit and check grading
```bash
make submit
```

Wait for grading results. The kernel should now:
1. Have an entry point at 0x80200000
2. Contain actual executable code in .text section
3. Include all compiled kernel object files

The libc should now:
1. Provide standard POSIX headers
2. Allow user programs to compile against standard interfaces

---

## What Was Wrong

**Kernel Build Issue:**
- The Makefile used `find` command in shell expansion
- On Windows/PowerShell, `find` doesn't work the same way as Unix
- This caused `KERN_C` and `KERN_ASM` to be empty lists
- The linker received no object files, only tarfs.o
- Result: kernel.elf had no .text section, no entry point

**Libc Headers Issue:**
- Only 3 headers provided: stdio.h, stdlib.h, types.h
- Missing all standard POSIX headers
- User programs couldn't compile because they include standard headers like string.h, unistd.h, etc.

**Solution:**
- Replaced `find` with explicit `wildcard` patterns that work cross-platform
- Added 18 standard POSIX headers with proper type definitions, constants, and function prototypes
