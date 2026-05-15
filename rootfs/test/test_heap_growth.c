/*
 * test_heap_growth.c
 *
 * Phase 2 stress: grow the heap past several megabytes via mmap,
 * touching every page to force physical allocation.
 * Mimics a compiler's symbol table and code buffer growing large.
 *
 * Compile (on the OS):
 *   cc -nostdlib -nostdinc -I/include -I/lib/tcc/include \
 *      /test/test_heap_growth.c /lib/crt.o /lib/libc.a \
 *      /lib/tcc/libtcc1.a -o /tmp/test_heap_growth
 */

#include <sys/mman.h>
#include <stdio.h>

#define MB           (1UL * 1024UL * 1024UL)
#define PAGE         4096UL
#define STEP_MB      1UL
#define STEP_BYTES   (STEP_MB * MB)
#define TOTAL_ROUNDS 8          /* 8 MB total */

int main(void) {
    printf("heap_growth: %d rounds of %lu MB mmap+touch+munmap\n",
           TOTAL_ROUNDS, STEP_MB);

    for (int r = 0; r < TOTAL_ROUNDS; r++) {
        printf("  round %d/%d: mmap %lu MB ...", r + 1, TOTAL_ROUNDS, STEP_MB);

        void *p = mmap(NULL, STEP_BYTES,
                       PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS,
                       -1, 0);
        if (p == (void *)-1) {
            printf(" FAIL (mmap returned MAP_FAILED)\n");
            return 1;
        }

        /* touch every page — forces physical allocation */
        volatile char *cp = (volatile char *)p;
        for (unsigned long off = 0; off < STEP_BYTES; off += PAGE)
            cp[off] = (char)(off & 0xFF);

        /* verify a sample of written pages */
        int ok = 1;
        for (unsigned long off = 0; off < STEP_BYTES; off += PAGE * 64) {
            if ((unsigned char)cp[off] != (unsigned char)(off & 0xFF)) {
                ok = 0;
                break;
            }
        }

        munmap(p, STEP_BYTES);

        if (!ok) {
            printf(" FAIL (page content wrong)\n");
            return 1;
        }
        printf(" OK\n");
    }

    printf("heap_growth: all %d rounds OK (%d MB touched)\n",
           TOTAL_ROUNDS, TOTAL_ROUNDS);
    printf("PASS\n");
    return 0;
}
