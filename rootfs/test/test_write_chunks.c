/*
 * test_write_chunks.c
 *
 * Phase 3 stress: create a fresh file on the writable mount (/mnt),
 * write it in many small chunks (512 B each), then read it back
 * and verify the pattern.
 * Mimics a compiler emitting an ELF image to the output file.
 *
 * Compile (on the OS):
 *   cc -nostdlib -nostdinc -I/include -I/lib/tcc/include \
 *      /test/test_write_chunks.c /lib/crt.o /lib/libc.a \
 *      /lib/tcc/libtcc1.a -o /tmp/test_write_chunks
 */

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>

#define CHUNK       512
#define TOTAL       (256 * 1024)    /* 256 KB */
#define NCHUNKS     (TOTAL / CHUNK)
#define OUT_PATH    "/mnt/test_write_chunks.bin"

int main(void) {
    printf("write_chunks: writing %d KB to %s in %d-byte chunks\n",
           TOTAL / 1024, OUT_PATH, CHUNK);

    /* ── write phase ── */
    int fd = open(OUT_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        printf("FAIL: open for write failed\n");
        return 1;
    }

    char wbuf[CHUNK];
    int written = 0;
    for (int i = 0; i < NCHUNKS; i++) {
        /* each chunk is filled with its index byte so we can verify later */
        memset(wbuf, (char)(i & 0xFF), CHUNK);
        int w = (int)write(fd, wbuf, CHUNK);
        if (w != CHUNK) {
            printf("FAIL: write chunk %d returned %d\n", i, w);
            close(fd);
            return 1;
        }
        written += w;
        if ((i + 1) % 100 == 0)
            printf("  %d KB written\n", written / 1024);
    }
    close(fd);
    printf("write_chunks: write phase done (%d bytes)\n", written);

    /* ── stat check ── */
    struct stat st;
    if (stat(OUT_PATH, &st) != 0) {
        printf("FAIL: stat after write failed\n");
        return 1;
    }
    if (st.st_size != TOTAL) {
        printf("FAIL: size=%lld expected=%d\n", (long long)st.st_size, TOTAL);
        return 1;
    }
    printf("write_chunks: stat OK (size=%lld)\n", (long long)st.st_size);

    /* ── read-back and verify ── */
    fd = open(OUT_PATH, O_RDONLY);
    if (fd < 0) {
        printf("FAIL: reopen for read failed\n");
        return 1;
    }

    char rbuf[CHUNK];
    int ok = 1;
    for (int i = 0; i < NCHUNKS && ok; i++) {
        int total_read = 0;
        while (total_read < CHUNK) {
            int r = (int)read(fd, rbuf + total_read, (size_t)(CHUNK - total_read));
            if (r <= 0) { ok = 0; break; }
            total_read += r;
        }
        if (!ok) {
            printf("FAIL: short read at chunk %d\n", i);
            break;
        }
        unsigned char expected = (unsigned char)(i & 0xFF);
        for (int j = 0; j < CHUNK; j++) {
            if ((unsigned char)rbuf[j] != expected) {
                printf("FAIL: chunk %d byte %d: got 0x%02x expected 0x%02x\n",
                       i, j, (unsigned char)rbuf[j], expected);
                ok = 0;
                break;
            }
        }
    }
    close(fd);

    /* clean up */
    unlink(OUT_PATH);

    if (!ok) return 1;
    printf("write_chunks: all %d chunks verified\n", NCHUNKS);
    printf("PASS\n");
    return 0;
}
