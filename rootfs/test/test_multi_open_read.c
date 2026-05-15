/*
 * test_multi_open_read.c
 *
 * Phase 1 stress: open and read through many files in sequence.
 * Mimics what a compiler does opening header files — dozens of opens,
 * each file read to completion, then closed before the next.
 *
 * Compile (on the OS):
 *   cc -nostdlib -nostdinc -I/include -I/lib/tcc/include \
 *      /test/test_multi_open_read.c /lib/crt.o /lib/libc.a \
 *      /lib/tcc/libtcc1.a -o /tmp/test_multi_open_read
 */

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

static const char *FILES[] = {
    "/include/stdio.h",
    "/include/stdlib.h",
    "/include/string.h",
    "/include/unistd.h",
    "/include/fcntl.h",
    "/include/errno.h",
    "/include/assert.h",
    "/include/ctype.h",
    "/include/math.h",
    "/include/stddef.h",
    "/include/stdint.h",
    "/include/limits.h",
    "/include/signal.h",
    "/include/setjmp.h",
    "/include/time.h",
    "/include/stdarg.h",
    "/include/stdbool.h",
    "/include/inttypes.h",
    "/include/types.h",
    "/include/sys/stat.h",
    "/include/sys/types.h",
    "/include/sys/wait.h",
    "/include/sys/mman.h",
    "/include/sys/ioctl.h",
    "/include/sys/time.h",
    "/include/sys/select.h",
    "/include/sys/resource.h",
    "/include/dirent.h",
    "/include/termios.h",
    "/include/pwd.h",
    "/include/grp.h",
    "/include/sched.h",
    "/include/getopt.h",
    "/include/locale.h",
    /* tcc's own built-in headers (/lib/tcc/include) */
    "/lib/tcc/include/float.h",
    "/lib/tcc/include/stdalign.h",
    "/lib/tcc/include/stdarg.h",
    "/lib/tcc/include/stdatomic.h",
    "/lib/tcc/include/stdbool.h",
    "/lib/tcc/include/stddef.h",
    "/lib/tcc/include/stdnoreturn.h",
    "/lib/tcc/include/tccdefs.h",
    "/lib/tcc/include/tcclib.h",
    "/lib/tcc/include/tgmath.h",
    "/lib/tcc/include/varargs.h",
};

#define NFILES ((int)(sizeof(FILES) / sizeof(FILES[0])))

int main(void) {
    char buf[4096];
    long total_bytes = 0;
    int opened = 0;
    int failed = 0;

    printf("multi_open_read: opening %d files\n", NFILES);

    for (int i = 0; i < NFILES; i++) {
        printf("  [%02d/%02d] open %s\n", i + 1, NFILES, FILES[i]);

        int fd = open(FILES[i], O_RDONLY);
        if (fd < 0) {
            printf("         SKIP (not found)\n");
            failed++;
            continue;
        }
        opened++;

        long file_bytes = 0;
        int n;
        while ((n = (int)read(fd, buf, sizeof(buf))) > 0)
            file_bytes += n;

        close(fd);
        total_bytes += file_bytes;
        printf("         OK (%ld bytes)\n", file_bytes);
    }

    printf("\nmulti_open_read: opened=%d skipped=%d total_bytes=%ld\n",
           opened, failed, total_bytes);

    if (opened < 10) {
        printf("FAIL: too few files opened (%d < 10)\n", opened);
        return 1;
    }
    if (total_bytes < 10 * 1024) {
        printf("FAIL: too few bytes read (%ld < 10240)\n", total_bytes);
        return 1;
    }

    printf("PASS\n");
    return 0;
}
