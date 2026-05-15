/*
 * test_cc_stress_sustained.c
 *
 * Sustained-pressure complement to test_cc_stress.c. Targets the gaps the
 * stall-investigation feedback called out specifically:
 *
 *   A. Sustained working set: cumulative mmap (no munmap until end) so the
 *      kernel actually has to keep ~12 MB of pages mapped + faulted in at
 *      once — mimics tcc's parser/symbol-table holding state across the
 *      whole compile rather than the churn pattern of test_cc_stress.
 *
 *   B. Simultaneous-fd pressure: 16 header fds held open at once, round-
 *      robin-read 4 KB from each until all EOF. A compiler holds many
 *      nested-include fds live; test_multi_open_read only opens-then-closes
 *      serially and never stresses that path.
 *
 *   C. Write-then-exec: copy /bin/true onto the writable mount in many
 *      small chunks, chmod 0755, fork+execve. This is the exact prod
 *      scenario for tcc (emit ELF to /tmp, then run it) and is not
 *      covered by test_write_chunks (writes then unlinks, never execs)
 *      or test_cc_stress (writes to /mnt, never execs).
 *
 * Progress prints precede every syscall-cluster so a stall pinpoints the
 * offending subsystem (allocator/page-fault vs file-read vs writeback/exec).
 *
 * Compile:
 *   cc -nostdlib -nostdinc -I/include -I/lib/tcc/include \
 *      /test/test_cc_stress_sustained.c /lib/crt.o /lib/libc.a \
 *      /lib/tcc/libtcc1.a -o /tmp/test_cc_stress_sustained
 */

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* ── Phase A tunables ──────────────────────────────────────────────────── */

#define PAGE              4096UL
#define STEP_BYTES        (1UL * 1024UL * 1024UL)   /* 1 MB per round       */
#define HEAP_ROUNDS       12                         /* 12 MB cumulative     */

/* ── Phase B tunables ──────────────────────────────────────────────────── */

#define FD_PRESSURE_N     16
#define FD_READ_CHUNK     4096
#define FD_TOTAL_CAP      (256 * 1024)               /* stop after 256 KB    */

/* ── Phase C ───────────────────────────────────────────────────────────── */

#define COPY_SRC          "/bin/true"
#define COPY_DST          "/tmp/test_cc_stress_sustained_copy"
#define COPY_CHUNK        512

/* 16 header paths kept open simultaneously */
static const char *FD_FILES[FD_PRESSURE_N] = {
    "/include/stdio.h",   "/include/stdlib.h",  "/include/string.h",
    "/include/unistd.h",  "/include/fcntl.h",   "/include/errno.h",
    "/include/ctype.h",   "/include/stddef.h",  "/include/stdint.h",
    "/include/limits.h",  "/include/signal.h",  "/include/time.h",
    "/include/sys/stat.h","/include/sys/wait.h","/include/sys/mman.h",
    "/include/dirent.h",
};

/* ── Phase A: cumulative mmap, no munmap until cleanup ─────────────────── */

static int phase_a_sustained_heap(void *holds[HEAP_ROUNDS]) {
    printf("\n--- phase A: cumulative mmap, hold %d MB live ---\n", HEAP_ROUNDS);

    for (int r = 0; r < HEAP_ROUNDS; r++) {
        printf("  [A] round %2d/%d: mmap 1 MB ... ", r + 1, HEAP_ROUNDS);
        void *p = mmap(NULL, STEP_BYTES,
                       PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == (void *)-1) {
            printf("FAIL (mmap returned MAP_FAILED at round %d)\n", r + 1);
            return 0;
        }
        holds[r] = p;

        /* touch every page in THIS round */
        volatile char *cp = (volatile char *)p;
        for (unsigned long off = 0; off < STEP_BYTES; off += PAGE)
            cp[off] = (char)((r + 1) ^ (off & 0xFF));

        /* re-touch every previously-held region to make sure earlier
         * pages didn't quietly evaporate under pressure */
        for (int k = 0; k < r; k++) {
            volatile char *prev = (volatile char *)holds[k];
            unsigned char want = (unsigned char)((k + 1) ^ 0);   /* off=0 */
            if ((unsigned char)prev[0] != want) {
                printf("FAIL (round %d region clobbered at off=0)\n", k + 1);
                return 0;
            }
        }
        printf("OK (live=%d MB)\n", r + 1);
    }
    return 1;
}

static void phase_a_cleanup(void *holds[HEAP_ROUNDS]) {
    for (int r = 0; r < HEAP_ROUNDS; r++)
        if (holds[r]) munmap(holds[r], STEP_BYTES);
}

/* ── Phase B: 16 fds open at once, round-robin read ────────────────────── */

static int phase_b_fd_pressure(void) {
    printf("\n--- phase B: %d fds held open simultaneously ---\n", FD_PRESSURE_N);

    int fds[FD_PRESSURE_N];
    int eof[FD_PRESSURE_N] = {0};
    long bytes_per_fd[FD_PRESSURE_N] = {0};
    int live = 0;

    for (int i = 0; i < FD_PRESSURE_N; i++) {
        printf("  [B] open %2d/%d %s ... ", i + 1, FD_PRESSURE_N, FD_FILES[i]);
        fds[i] = open(FD_FILES[i], O_RDONLY);
        if (fds[i] < 0) {
            printf("SKIP\n");
            eof[i] = 1;
        } else {
            printf("fd=%d\n", fds[i]);
            live++;
        }
    }
    if (live < FD_PRESSURE_N / 2) {
        printf("FAIL: only %d/%d fds opened\n", live, FD_PRESSURE_N);
        for (int i = 0; i < FD_PRESSURE_N; i++) if (fds[i] >= 0) close(fds[i]);
        return 0;
    }
    printf("  [B] %d fds live, round-robin reading 4 KB each\n", live);

    char buf[FD_READ_CHUNK];
    long total = 0;
    int pass = 0;
    while (total < FD_TOTAL_CAP) {
        int active = 0;
        for (int i = 0; i < FD_PRESSURE_N; i++) {
            if (eof[i]) continue;
            int n = (int)read(fds[i], buf, sizeof(buf));
            if (n <= 0) {
                eof[i] = 1;
                close(fds[i]);
                fds[i] = -1;
            } else {
                bytes_per_fd[i] += n;
                total += n;
                active++;
            }
        }
        pass++;
        if (active == 0) break;
        if (pass % 4 == 0)
            printf("  [B] pass %d: %d still active, total=%ld B\n",
                   pass, active, total);
    }

    /* close any leftover */
    for (int i = 0; i < FD_PRESSURE_N; i++)
        if (fds[i] >= 0) close(fds[i]);

    printf("  [B] done: %d passes, total=%ld B\n", pass, total);
    if (total < 16 * 1024) {
        printf("FAIL: too few bytes via fd pressure (%ld)\n", total);
        return 0;
    }
    return 1;
}

/* ── Phase C: write then exec a fresh binary on the writable mount ─────── */

static int phase_c_write_then_exec(void) {
    printf("\n--- phase C: copy %s -> %s, chmod, exec ---\n", COPY_SRC, COPY_DST);

    int src = open(COPY_SRC, O_RDONLY);
    if (src < 0) {
        printf("FAIL: open %s\n", COPY_SRC);
        return 0;
    }
    int dst = open(COPY_DST, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst < 0) {
        printf("FAIL: create %s\n", COPY_DST);
        close(src);
        return 0;
    }

    char buf[COPY_CHUNK];
    long total = 0;
    int n;
    int chunks = 0;
    while ((n = (int)read(src, buf, sizeof(buf))) > 0) {
        int off = 0;
        while (off < n) {
            int w = (int)write(dst, buf + off, (size_t)(n - off));
            if (w <= 0) {
                printf("FAIL: write returned %d at total=%ld\n", w, total);
                close(src); close(dst);
                return 0;
            }
            off += w;
        }
        total += n;
        chunks++;
        if (chunks % 32 == 0)
            printf("  [C] copied %ld B in %d chunks\n", total, chunks);
    }
    close(src);
    close(dst);
    printf("  [C] copy done: %ld B in %d chunks\n", total, chunks);

    if (chmod(COPY_DST, 0755) != 0) {
        printf("FAIL: chmod 0755 %s\n", COPY_DST);
        unlink(COPY_DST);
        return 0;
    }

    /* stat sanity */
    struct stat st;
    if (stat(COPY_DST, &st) != 0 || st.st_size != total) {
        printf("FAIL: stat after copy size=%lld expected=%ld\n",
               (long long)st.st_size, total);
        unlink(COPY_DST);
        return 0;
    }
    printf("  [C] stat OK (size=%lld), about to fork+exec\n",
           (long long)st.st_size);

    pid_t pid = fork();
    if (pid < 0) {
        printf("FAIL: fork\n");
        unlink(COPY_DST);
        return 0;
    }
    if (pid == 0) {
        char *argv[] = { (char *)COPY_DST, NULL };
        execve(COPY_DST, argv, NULL);
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) != pid) {
        printf("FAIL: waitpid\n");
        unlink(COPY_DST);
        return 0;
    }
    unlink(COPY_DST);

    if (!WIFEXITED(status)) {
        printf("FAIL: child died via signal %d\n", WTERMSIG(status));
        return 0;
    }
    int code = WEXITSTATUS(status);
    /* /bin/true exits 0 — that's the whole point of this case */
    if (code != 0) {
        printf("FAIL: exec'd copy exited %d (expected 0)\n", code);
        return 0;
    }
    printf("  [C] exec'd copy exited 0\n");
    return 1;
}

/* ── main ──────────────────────────────────────────────────────────────── */

int main(void) {
    void *holds[HEAP_ROUNDS] = {0};

    printf("cc_stress_sustained: start (sustained heap, fd pressure, write+exec)\n");

    int a_ok = phase_a_sustained_heap(holds);
    if (!a_ok) { phase_a_cleanup(holds); return 1; }

    /* keep all 12 MB live while phase B runs — the whole point */
    int b_ok = phase_b_fd_pressure();
    if (!b_ok) { phase_a_cleanup(holds); return 1; }

    /* and still live while phase C runs */
    int c_ok = phase_c_write_then_exec();

    phase_a_cleanup(holds);
    if (!c_ok) return 1;

    printf("\ncc_stress_sustained: all phases complete\nPASS\n");
    return 0;
}
