/*
 * test_cc_stress.c
 *
 * Single program that interleaves the three phases a real compiler goes through,
 * with progress prints before every step so a kernel stall is pinpointed:
 *
 *   Phase 1 — open+read a few dozen headers in a row (tens to hundreds of KB)
 *   Phase 2 — interleaved with growing the heap past several MB via mmap,
 *              touching every page to force physical allocation
 *   Phase 3 — create a fresh file on the writable mount (/mnt), write it out
 *              in many small chunks, then close it
 *
 * The interleaving matters: after every few headers, we mmap another MB and
 * touch it, so the open/read path and the page-fault path stress each other
 * simultaneously — exactly what happens when a compiler builds symbol tables
 * while scanning headers.
 *
 * Compile:
 *   cc -nostdlib -nostdinc -I/include -I/lib/tcc/include \
 *      /test/test_cc_stress.c /lib/crt.o /lib/libc.a \
 *      /lib/tcc/libtcc1.a -o /tmp/test_cc_stress
 */

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

/* ── tunables ─────────────────────────────────────────────────────────── */

#define PAGE             4096UL
#define HEAP_STEP        (1UL * 1024UL * 1024UL)   /* 1 MB per mmap round   */
#define TOTAL_HEAP_MB    8                           /* 8 MB total heap growth */
#define WRITE_CHUNK      512
#define WRITE_TOTAL      (256 * 1024)               /* 256 KB output file    */
#define OUT_PATH         "/mnt/test_cc_stress.bin"

/* Headers from both include trees — a real compiler opens all of these */
static const char *HEADERS[] = {
    /* libc headers */
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
    /* tcc built-in headers */
    "/lib/tcc/include/float.h",
    "/lib/tcc/include/stdalign.h",
    "/lib/tcc/include/stdarg.h",
    "/lib/tcc/include/stdatomic.h",
    "/lib/tcc/include/stdbool.h",
    "/lib/tcc/include/stddef.h",
    "/lib/tcc/include/stdnoreturn.h",
    "/lib/tcc/include/tcclib.h",
    "/lib/tcc/include/tgmath.h",
};
#define NHEADERS ((int)(sizeof(HEADERS) / sizeof(HEADERS[0])))

/* ── grow heap by one step, touch every page ────────────────────────────── */

static int grow_heap_step(int round) {
    printf("  [heap] round %d: mmap %lu MB ... ", round, HEAP_STEP / (1024*1024));
    void *p = mmap(NULL, HEAP_STEP,
                   PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS,
                   -1, 0);
    if (p == (void *)-1) {
        printf("FAIL (mmap returned MAP_FAILED)\n");
        return 0;
    }
    volatile char *cp = (volatile char *)p;
    for (unsigned long off = 0; off < HEAP_STEP; off += PAGE)
        cp[off] = (char)(off & 0xFF);
    munmap(p, HEAP_STEP);
    printf("OK\n");
    return 1;
}

int main(void) {
    char buf[4096];
    long total_bytes = 0;
    int opened = 0;
    int heap_round = 0;
    int heap_ok = 1;

    printf("cc_stress: start — %d headers, %d MB heap, %d KB output\n",
           NHEADERS, TOTAL_HEAP_MB, WRITE_TOTAL / 1024);

    /* ── Phase 1+2 interleaved: open/read headers, grow heap every N files ── */
    printf("\n--- phase 1+2: interleaved header reads + heap growth ---\n");

    for (int i = 0; i < NHEADERS; i++) {
        /* grow heap every 5 headers */
        if (i % 5 == 0 && heap_round < TOTAL_HEAP_MB) {
            if (!grow_heap_step(++heap_round))
                heap_ok = 0;
        }

        printf("  [file %02d/%02d] open %s ... ", i + 1, NHEADERS, HEADERS[i]);
        int fd = open(HEADERS[i], O_RDONLY);
        if (fd < 0) {
            printf("SKIP (not found)\n");
            continue;
        }
        opened++;

        long file_bytes = 0;
        int n;
        while ((n = (int)read(fd, buf, sizeof(buf))) > 0)
            file_bytes += n;

        close(fd);
        total_bytes += file_bytes;
        printf("OK (%ld B)\n", file_bytes);
    }

    /* finish remaining heap rounds after headers are done */
    while (heap_round < TOTAL_HEAP_MB) {
        if (!grow_heap_step(++heap_round))
            heap_ok = 0;
    }

    printf("\nphase 1+2 done: opened=%d total_read=%ld B heap_ok=%d\n",
           opened, total_bytes, heap_ok);

    if (opened < 10) {
        printf("FAIL: too few headers opened (%d)\n", opened);
        return 1;
    }
    if (total_bytes < 10 * 1024) {
        printf("FAIL: too few bytes read (%ld)\n", total_bytes);
        return 1;
    }
    if (!heap_ok) {
        printf("FAIL: heap growth failed\n");
        return 1;
    }

    /* ── Phase 3: create fresh file on writable mount, write in small chunks ── */
    printf("\n--- phase 3: write %d KB to %s in %d-byte chunks ---\n",
           WRITE_TOTAL / 1024, OUT_PATH, WRITE_CHUNK);

    int fd = open(OUT_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        printf("FAIL: could not create %s\n", OUT_PATH);
        return 1;
    }
    printf("  [write] file created\n");

    char wbuf[WRITE_CHUNK];
    int written = 0;
    int nchunks = WRITE_TOTAL / WRITE_CHUNK;
    for (int i = 0; i < nchunks; i++) {
        memset(wbuf, (char)(i & 0xFF), WRITE_CHUNK);
        int w = (int)write(fd, wbuf, WRITE_CHUNK);
        if (w != WRITE_CHUNK) {
            printf("FAIL: write chunk %d returned %d\n", i, w);
            close(fd);
            return 1;
        }
        written += w;
        if ((i + 1) % 100 == 0)
            printf("  [write] %d KB written\n", written / 1024);
    }

    printf("  [write] closing (%d bytes written) ...\n", written);
    close(fd);
    printf("  [write] close returned\n");

    /* stat check */
    struct stat st;
    if (stat(OUT_PATH, &st) != 0 || st.st_size != WRITE_TOTAL) {
        printf("FAIL: stat size=%lld expected=%d\n",
               (long long)st.st_size, WRITE_TOTAL);
        return 1;
    }
    printf("  [write] stat OK (size=%lld)\n", (long long)st.st_size);

    /* read back first chunk to verify writeback */
    fd = open(OUT_PATH, O_RDONLY);
    if (fd < 0) {
        printf("FAIL: reopen for read\n");
        return 1;
    }
    char rbuf[WRITE_CHUNK];
    int n = 0;
    while (n < WRITE_CHUNK) {
        int r = (int)read(fd, rbuf + n, (size_t)(WRITE_CHUNK - n));
        if (r <= 0) break;
        n += r;
    }
    close(fd);

    int ok = (n == WRITE_CHUNK);
    for (int i = 0; i < n && ok; i++)
        if ((unsigned char)rbuf[i] != 0x00) { ok = 0; break; }
    if (!ok) {
        printf("FAIL: first chunk readback wrong\n");
        return 1;
    }
    printf("  [write] readback OK\n");

    unlink(OUT_PATH);

    printf("\ncc_stress: all phases complete\n");
    printf("PASS\n");
    return 0;
}
