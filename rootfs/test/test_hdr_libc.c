/*
 * test_hdr_libc.c — include every libc header and exercise at least one
 * declaration from each so the compiler actually parses the full file.
 *
 * Compile:
 *   cc -nostdlib -nostdinc -I/include -I/lib/tcc/include \
 *      /test/test_hdr_libc.c /lib/crt.o /lib/libc.a \
 *      /lib/tcc/libtcc1.a -o /tmp/test_hdr_libc
 */

/* ── libc/include/ ── */
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <grp.h>
#include <inttypes.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <pwd.h>
#include <sched.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <types.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>

int main(void) {
    /* assert.h */
    assert(1);

    /* ctype.h */
    int c = isalpha('a');
    (void)c;

    /* errno.h */
    errno = 0;

    /* limits.h */
    int lim = INT_MAX;
    (void)lim;

    /* math.h */
    double d = 0.0;
    (void)d;

    /* stdarg.h — use va_list directly */
    va_list ap;
    (void)ap;

    /* stdbool.h */
    bool b = true;
    (void)b;

    /* stddef.h */
    size_t sz = sizeof(int);
    (void)sz;

    /* stdint.h */
    uint32_t u = 0;
    (void)u;

    /* stdio.h */
    printf("test_hdr_libc: headers OK\n");

    /* stdlib.h */
    void *p = malloc(16);
    free(p);

    /* string.h */
    char buf[8];
    memset(buf, 0, sizeof(buf));

    /* inttypes.h */
    uint64_t v = 0;
    (void)v;

    /* setjmp.h */
    jmp_buf jb;
    (void)jb;

    /* signal.h */
    (void)SIGTERM;

    /* time.h */
    time_t t = 0;
    (void)t;

    /* fcntl.h */
    (void)O_RDONLY;

    /* sys/stat.h */
    (void)S_IRUSR;

    /* sys/wait.h */
    (void)WNOHANG;

    /* sys/mman.h */
    (void)PROT_READ;

    /* sys/select.h */
    fd_set fds;
    FD_ZERO(&fds);

    printf("PASS\n");
    return 0;
}
