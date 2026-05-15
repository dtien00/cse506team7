/*
 * test_compile_pipeline.c
 *
 * Mirrors what a real compile-and-link pass does from user space:
 *
 *   Phase 1 — open + read all headers (libc + tcc) in succession
 *   Phase 2 — open + read archive files (.a) sequentially
 *   Phase 3 — build large in-memory buffers simulating parser state,
 *              symbol table, code buffer, and data buffer
 *   Phase 4 — write a new ELF-shaped binary to /mnt, chmod it 0755,
 *              stat it, read it back, then unlink
 *
 * Every phase prints progress so a stall is pinpointed immediately.
 *
 * Compile (from inside the OS):
 *   cc -nostdlib -nostdinc -I/include -I/lib/tcc/include \
 *      /test/c_sample/test_compile_pipeline.c /lib/crt.o /lib/libc.a \
 *      /lib/tcc/libtcc1.a -o /mnt/test_compile_pipeline
 */

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── tunables ─────────────────────────────────────────────────────────────── */

#define PAGE            4096UL
#define SYM_TABLE_SLOTS 4096          /* simulated symbol table entries       */
#define CODE_BUF_MB     4             /* simulated code buffer size           */
#define DATA_BUF_MB     2             /* simulated data buffer size           */
#define ELF_SIZE        (64 * 1024)   /* 64 KB fake ELF                       */
#define OUT_PATH        "/mnt/test_compile_pipeline.elf"

/* ELF magic + minimal header bytes to make the output look real */
static const unsigned char ELF_MAGIC[16] = {
    0x7f, 'E', 'L', 'F',  /* magic                          */
    2,                     /* EI_CLASS  = ELFCLASS64         */
    1,                     /* EI_DATA   = ELFDATA2LSB        */
    1,                     /* EI_VERSION                     */
    0,                     /* EI_OSABI  = ELFOSABI_NONE      */
    0, 0, 0, 0, 0, 0, 0, 0
};

/* ── headers a compiler opens ─────────────────────────────────────────────── */
static const char *HEADERS[] = {
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

/* ── archives a linker opens ──────────────────────────────────────────────── */
static const char *ARCHIVES[] = {
    "/lib/libc.a",
    "/lib/tcc/libtcc1.a",
};
#define NARCHIVES ((int)(sizeof(ARCHIVES) / sizeof(ARCHIVES[0])))

/* ── simulated symbol-table entry ─────────────────────────────────────────── */
typedef struct {
    char  name[48];
    unsigned long value;
    unsigned int  size;
    unsigned char type;
    unsigned char binding;
} sym_entry_t;

/* ── helpers ──────────────────────────────────────────────────────────────── */

static long drain_fd(int fd, char *buf, int bufsz) {
    long total = 0;
    int n;
    while ((n = (int)read(fd, buf, (size_t)bufsz)) > 0)
        total += n;
    return total;
}

int main(void) {
    char buf[8192];
    int  fail = 0;

    printf("compile_pipeline: start\n");
    printf("  headers=%d  archives=%d  sym_slots=%d  "
           "code=%dMB  data=%dMB  elf=%dKB\n\n",
           NHEADERS, NARCHIVES, SYM_TABLE_SLOTS,
           CODE_BUF_MB, DATA_BUF_MB, ELF_SIZE / 1024);

    /* ── Phase 1: open + read every header ──────────────────────────────── */
    printf("--- phase 1: read %d headers ---\n", NHEADERS);
    int  hdr_opened = 0;
    long hdr_bytes  = 0;

    for (int i = 0; i < NHEADERS; i++) {
        printf("  [hdr %02d/%02d] %s ... ", i + 1, NHEADERS, HEADERS[i]);
        int fd = open(HEADERS[i], O_RDONLY);
        if (fd < 0) {
            printf("SKIP\n");
            continue;
        }
        long n = drain_fd(fd, buf, (int)sizeof(buf));
        close(fd);
        hdr_bytes += n;
        hdr_opened++;
        printf("OK (%ld B)\n", n);
    }

    printf("phase 1 done: opened=%d bytes=%ld\n\n", hdr_opened, hdr_bytes);
    if (hdr_opened < 10) {
        printf("FAIL: too few headers (%d)\n", hdr_opened);
        fail = 1;
    }

    /* ── Phase 2: open + read every archive ─────────────────────────────── */
    printf("--- phase 2: read %d archives ---\n", NARCHIVES);
    int  arc_opened = 0;
    long arc_bytes  = 0;

    for (int i = 0; i < NARCHIVES; i++) {
        printf("  [arc %d/%d] %s ... ", i + 1, NARCHIVES, ARCHIVES[i]);
        int fd = open(ARCHIVES[i], O_RDONLY);
        if (fd < 0) {
            printf("SKIP\n");
            continue;
        }
        long n = drain_fd(fd, buf, (int)sizeof(buf));
        close(fd);
        arc_bytes += n;
        arc_opened++;
        printf("OK (%ld B)\n", n);
    }

    printf("phase 2 done: opened=%d bytes=%ld\n\n", arc_opened, arc_bytes);

    /* ── Phase 3: build in-memory structures via mmap ────────────────────── */
    printf("--- phase 3: allocate + touch in-memory buffers ---\n");

    /* symbol table */
    printf("  [sym] allocating %d-entry symbol table (%lu KB) ... ",
           SYM_TABLE_SLOTS,
           (unsigned long)(SYM_TABLE_SLOTS * sizeof(sym_entry_t)) / 1024);
    size_t sym_sz = (size_t)SYM_TABLE_SLOTS * sizeof(sym_entry_t);
    sym_entry_t *symtab = (sym_entry_t *)mmap(NULL, sym_sz,
                              PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (symtab == (sym_entry_t *)-1) {
        printf("FAIL\n");
        fail = 1;
        symtab = NULL;
    } else {
        for (int i = 0; i < SYM_TABLE_SLOTS; i++) {
            symtab[i].value   = (unsigned long)i * 8;
            symtab[i].size    = 8;
            symtab[i].type    = 1;
            symtab[i].binding = 0;
        }
        printf("OK\n");
    }

    /* code buffer */
    size_t code_sz = (size_t)CODE_BUF_MB * 1024 * 1024;
    printf("  [code] allocating %dMB code buffer ... ", CODE_BUF_MB);
    volatile char *code_buf = (volatile char *)mmap(NULL, code_sz,
                                  PROT_READ | PROT_WRITE | PROT_EXEC,
                                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (code_buf == (volatile char *)-1) {
        printf("FAIL\n");
        fail = 1;
        code_buf = NULL;
    } else {
        for (size_t off = 0; off < code_sz; off += PAGE)
            code_buf[off] = (char)(off & 0xFF);
        printf("OK\n");
    }

    /* data buffer */
    size_t data_sz = (size_t)DATA_BUF_MB * 1024 * 1024;
    printf("  [data] allocating %dMB data buffer ... ", DATA_BUF_MB);
    volatile char *data_buf = (volatile char *)mmap(NULL, data_sz,
                                  PROT_READ | PROT_WRITE,
                                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (data_buf == (volatile char *)-1) {
        printf("FAIL\n");
        fail = 1;
        data_buf = NULL;
    } else {
        for (size_t off = 0; off < data_sz; off += PAGE)
            data_buf[off] = (char)((off >> 12) & 0xFF);
        printf("OK\n");
    }

    printf("phase 3 done\n\n");

    /* ── Phase 4: write ELF output, chmod, stat, readback, unlink ────────── */
    printf("--- phase 4: write %dKB ELF to %s ---\n",
           ELF_SIZE / 1024, OUT_PATH);

    int ofd = open(OUT_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (ofd < 0) {
        printf("FAIL: open %s for write\n", OUT_PATH);
        fail = 1;
        goto cleanup;
    }
    printf("  [elf] file created\n");

    /* write ELF magic at offset 0 */
    if ((int)write(ofd, ELF_MAGIC, sizeof(ELF_MAGIC)) != (int)sizeof(ELF_MAGIC)) {
        printf("FAIL: write ELF magic\n");
        close(ofd);
        fail = 1;
        goto cleanup;
    }

    /* pad out to ELF_SIZE with pattern data */
    {
        char pbuf[512];
        int written = (int)sizeof(ELF_MAGIC);
        int chunk   = (int)sizeof(pbuf);
        int idx     = 0;
        while (written < ELF_SIZE) {
            int rem = ELF_SIZE - written;
            if (chunk > rem) chunk = rem;
            memset(pbuf, idx & 0xFF, (size_t)chunk);
            int w = (int)write(ofd, pbuf, (size_t)chunk);
            if (w != chunk) {
                printf("FAIL: write chunk %d (wrote %d)\n", idx, w);
                close(ofd);
                fail = 1;
                goto cleanup;
            }
            written += w;
            idx++;
            if (idx % 32 == 0)
                printf("  [elf] %d KB written\n", written / 1024);
        }
        printf("  [elf] %d KB total written\n", written / 1024);
    }

    printf("  [elf] closing ...\n");
    close(ofd);
    printf("  [elf] closed\n");

    /* chmod 0755 */
    printf("  [elf] chmod 0755 ... ");
    if (chmod(OUT_PATH, 0755) != 0) {
        printf("FAIL\n");
        fail = 1;
    } else {
        printf("OK\n");
    }

    /* stat */
    {
        struct stat st;
        printf("  [elf] stat ... ");
        if (stat(OUT_PATH, &st) != 0 || st.st_size != ELF_SIZE) {
            printf("FAIL (size=%lld expected=%d)\n",
                   (long long)st.st_size, ELF_SIZE);
            fail = 1;
        } else {
            printf("OK (size=%lld mode=%o)\n",
                   (long long)st.st_size, (unsigned)st.st_mode & 07777);
        }
    }

    /* readback — verify ELF magic */
    {
        int rfd = open(OUT_PATH, O_RDONLY);
        printf("  [elf] readback magic ... ");
        if (rfd < 0) {
            printf("FAIL: open for read\n");
            fail = 1;
        } else {
            unsigned char magic[16];
            int n = 0;
            while (n < 16) {
                int r = (int)read(rfd, magic + n, (size_t)(16 - n));
                if (r <= 0) break;
                n += r;
            }
            close(rfd);
            if (n == 16 && memcmp(magic, ELF_MAGIC, 16) == 0)
                printf("OK\n");
            else {
                printf("FAIL (got %d bytes, magic mismatch)\n", n);
                fail = 1;
            }
        }
    }

    /* unlink */
    printf("  [elf] unlink ... ");
    if (unlink(OUT_PATH) != 0) {
        printf("FAIL\n");
        fail = 1;
    } else {
        printf("OK\n");
    }

cleanup:
    if (symtab)   munmap(symtab,   sym_sz);
    if (code_buf) munmap((void *)code_buf, code_sz);
    if (data_buf) munmap((void *)data_buf, data_sz);

    printf("\ncompile_pipeline: %s\n", fail ? "FAIL" : "PASS");
    return fail ? 1 : 0;
}
