/*
 * test_cc_bench.c
 *
 * Benchmark companion to test_cc_stress / test_cc_stress_sustained.
 * Produces machine-readable numbers ("[BENCH] key=value unit") so output
 * can be diffed across runs to spot perf regressions in the kernel paths
 * the compile-and-link workload actually hits.
 *
 * Two parts:
 *
 *   Part A — syscall-path micro: tight loops over the three primitives
 *            that dominate a compile run, each measured in isolation:
 *                fork + execve(/bin/true) + wait      (proc lifecycle)
 *                open + read-to-EOF + close header    (FS read path)
 *                mmap(1 MB anon) + touch + munmap     (vma + page-fault)
 *            Reports ns/op and ops/sec for each.
 *
 *   Part B — sustained workload throughput: runs the 3-phase pattern
 *            (12 MB cumulative mmap held live, 16 fds round-robin read,
 *            write+exec /bin/true copy) 3× back-to-back. Per-iteration
 *            wall time + total. This is the regression gate for the
 *            actual hang scenario — if any iteration takes >2× the
 *            median, something has regressed under sustained pressure.
 *
 * Compile:
 *   cc -nostdlib -nostdinc -I/include -I/lib/tcc/include \
 *      /test/test_cc_bench.c /lib/crt.o /lib/libc.a \
 *      /lib/tcc/libtcc1.a -o /tmp/test_cc_bench
 */

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* ── tunables ──────────────────────────────────────────────────────────── */

#define MICRO_FORK_N      50
#define MICRO_OPEN_N      100
#define MICRO_MMAP_N      50
#define MICRO_MMAP_BYTES  (1UL * 1024 * 1024)
#define PAGE              4096UL

#define SUSTAINED_ITERS   3
#define HEAP_ROUNDS       12
#define HEAP_STEP         (1UL * 1024 * 1024)
#define FD_PRESSURE_N     16
#define FD_READ_CHUNK     4096
#define FD_TOTAL_CAP      (128 * 1024)
#define COPY_DST          "/tmp/test_cc_bench_copy"
#define COPY_CHUNK        512

static const char *MICRO_HEADER = "/include/stdio.h";

static const char *FD_FILES[FD_PRESSURE_N] = {
    "/include/stdio.h",   "/include/stdlib.h",  "/include/string.h",
    "/include/unistd.h",  "/include/fcntl.h",   "/include/errno.h",
    "/include/ctype.h",   "/include/stddef.h",  "/include/stdint.h",
    "/include/limits.h",  "/include/signal.h",  "/include/time.h",
    "/include/sys/stat.h","/include/sys/wait.h","/include/sys/mman.h",
    "/include/dirent.h",
};

/* ── timing helpers ────────────────────────────────────────────────────── */

static long now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (long)ts.tv_sec * 1000000000L + (long)ts.tv_nsec;
}

static void report(const char *key, long ns, long iters) {
    long ns_per = iters > 0 ? ns / iters : 0;
    long ops    = ns_per > 0 ? 1000000000L / ns_per : 0;
    printf("[BENCH] %s total_ns=%ld iters=%ld ns_per_op=%ld ops_per_sec=%ld\n",
           key, ns, iters, ns_per, ops);
}

/* ── micro A.1: fork + exec(/bin/true) + wait ──────────────────────────── */

static int bench_fork_exec(void) {
    printf("--- micro: fork+exec(/bin/true)+wait × %d ---\n", MICRO_FORK_N);
    long t0 = now_ns();
    for (int i = 0; i < MICRO_FORK_N; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            printf("FAIL: fork at iter %d\n", i);
            return 0;
        }
        if (pid == 0) {
            char *argv[] = { "/bin/true", NULL };
            execve("/bin/true", argv, NULL);
            _exit(127);
        }
        int st = 0;
        if (waitpid(pid, &st, 0) != pid || !WIFEXITED(st) || WEXITSTATUS(st) != 0) {
            printf("FAIL: child %d did not exit cleanly\n", i);
            return 0;
        }
    }
    long t1 = now_ns();
    report("fork_exec_true", t1 - t0, MICRO_FORK_N);
    return 1;
}

/* ── micro A.2: open + read-to-EOF + close ─────────────────────────────── */

static int bench_open_read(void) {
    printf("--- micro: open+read-EOF+close %s × %d ---\n",
           MICRO_HEADER, MICRO_OPEN_N);
    char buf[4096];
    long total_bytes = 0;
    long t0 = now_ns();
    for (int i = 0; i < MICRO_OPEN_N; i++) {
        int fd = open(MICRO_HEADER, O_RDONLY);
        if (fd < 0) {
            printf("FAIL: open at iter %d\n", i);
            return 0;
        }
        int n;
        while ((n = (int)read(fd, buf, sizeof(buf))) > 0)
            total_bytes += n;
        close(fd);
    }
    long t1 = now_ns();
    report("open_read_close", t1 - t0, MICRO_OPEN_N);
    printf("[BENCH] open_read_close bytes_per_open=%ld\n",
           total_bytes / MICRO_OPEN_N);
    return 1;
}

/* ── micro A.3: mmap(1 MB) + touch every page + munmap ─────────────────── */

static int bench_mmap_touch(void) {
    printf("--- micro: mmap(%lu KB)+touch+munmap × %d ---\n",
           MICRO_MMAP_BYTES / 1024, MICRO_MMAP_N);
    long t0 = now_ns();
    for (int i = 0; i < MICRO_MMAP_N; i++) {
        void *p = mmap(NULL, MICRO_MMAP_BYTES,
                       PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == (void *)-1) {
            printf("FAIL: mmap at iter %d\n", i);
            return 0;
        }
        volatile char *cp = (volatile char *)p;
        for (unsigned long off = 0; off < MICRO_MMAP_BYTES; off += PAGE)
            cp[off] = (char)(off & 0xFF);
        munmap(p, MICRO_MMAP_BYTES);
    }
    long t1 = now_ns();
    report("mmap_touch_munmap_1mb", t1 - t0, MICRO_MMAP_N);
    long ns_per = (t1 - t0) / MICRO_MMAP_N;
    long pages = MICRO_MMAP_BYTES / PAGE;
    printf("[BENCH] mmap_touch_munmap_1mb ns_per_page=%ld\n",
           pages > 0 ? ns_per / pages : 0);
    return 1;
}

/* ── sustained iteration: 3 phases held live, time-stamped ─────────────── */

static long sustained_iter(int iter_idx, int *out_ok) {
    void *holds[HEAP_ROUNDS] = {0};
    *out_ok = 0;
    long t0 = now_ns();

    /* phase 1: cumulative mmap, hold live */
    for (int r = 0; r < HEAP_ROUNDS; r++) {
        void *p = mmap(NULL, HEAP_STEP,
                       PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == (void *)-1) {
            printf("FAIL iter %d: mmap round %d\n", iter_idx, r);
            for (int k = 0; k < r; k++) munmap(holds[k], HEAP_STEP);
            return -1;
        }
        holds[r] = p;
        volatile char *cp = (volatile char *)p;
        for (unsigned long off = 0; off < HEAP_STEP; off += PAGE)
            cp[off] = (char)((r + 1) ^ (off & 0xFF));
    }

    /* phase 2: 16-fd round-robin reads (heap still live) */
    int fds[FD_PRESSURE_N];
    int eof[FD_PRESSURE_N] = {0};
    int live = 0;
    for (int i = 0; i < FD_PRESSURE_N; i++) {
        fds[i] = open(FD_FILES[i], O_RDONLY);
        if (fds[i] < 0) eof[i] = 1; else live++;
    }
    char buf[FD_READ_CHUNK];
    long total = 0;
    while (total < FD_TOTAL_CAP) {
        int active = 0;
        for (int i = 0; i < FD_PRESSURE_N; i++) {
            if (eof[i]) continue;
            int n = (int)read(fds[i], buf, sizeof(buf));
            if (n <= 0) { eof[i] = 1; close(fds[i]); fds[i] = -1; }
            else        { total += n; active++; }
        }
        if (!active) break;
    }
    for (int i = 0; i < FD_PRESSURE_N; i++)
        if (fds[i] >= 0) close(fds[i]);

    /* phase 3: copy /bin/true → /tmp, chmod, fork+exec (heap still live) */
    int src = open("/bin/true", O_RDONLY);
    if (src < 0) {
        printf("FAIL iter %d: open /bin/true\n", iter_idx);
        for (int r = 0; r < HEAP_ROUNDS; r++) munmap(holds[r], HEAP_STEP);
        return -1;
    }
    int dst = open(COPY_DST, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst < 0) {
        close(src);
        printf("FAIL iter %d: create %s\n", iter_idx, COPY_DST);
        for (int r = 0; r < HEAP_ROUNDS; r++) munmap(holds[r], HEAP_STEP);
        return -1;
    }
    char cbuf[COPY_CHUNK];
    int n;
    while ((n = (int)read(src, cbuf, sizeof(cbuf))) > 0) {
        int off = 0;
        while (off < n) {
            int w = (int)write(dst, cbuf + off, (size_t)(n - off));
            if (w <= 0) {
                close(src); close(dst);
                printf("FAIL iter %d: write short\n", iter_idx);
                for (int r = 0; r < HEAP_ROUNDS; r++) munmap(holds[r], HEAP_STEP);
                return -1;
            }
            off += w;
        }
    }
    close(src);
    close(dst);
    chmod(COPY_DST, 0755);

    pid_t pid = fork();
    if (pid == 0) {
        char *argv[] = { (char *)COPY_DST, NULL };
        execve(COPY_DST, argv, NULL);
        _exit(127);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    unlink(COPY_DST);

    /* cleanup */
    for (int r = 0; r < HEAP_ROUNDS; r++) munmap(holds[r], HEAP_STEP);

    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
        printf("FAIL iter %d: exec'd copy exited %d\n",
               iter_idx, WIFEXITED(st) ? WEXITSTATUS(st) : -1);
        return -1;
    }

    long t1 = now_ns();
    *out_ok = 1;
    printf("[BENCH] sustained_iter idx=%d wall_ns=%ld fd_bytes=%ld live_fds=%d\n",
           iter_idx, t1 - t0, total, live);
    return t1 - t0;
}

/* ── sustained driver: run N iterations, print stats ───────────────────── */

static int bench_sustained(void) {
    printf("--- sustained: 3-phase workload × %d back-to-back ---\n",
           SUSTAINED_ITERS);
    long times[SUSTAINED_ITERS] = {0};
    long total = 0, min = 0, max = 0;
    for (int i = 0; i < SUSTAINED_ITERS; i++) {
        int ok = 0;
        long ns = sustained_iter(i, &ok);
        if (!ok) return 0;
        times[i] = ns;
        total += ns;
        if (i == 0 || ns < min) min = ns;
        if (ns > max) max = ns;
    }
    long mean = total / SUSTAINED_ITERS;
    printf("[BENCH] sustained_total ns=%ld iters=%d mean_ns=%ld min_ns=%ld max_ns=%ld\n",
           total, SUSTAINED_ITERS, mean, min, max);
    /* regression flag: max > 2× min suggests something accumulates badly
     * across iterations (e.g. fd leak, slab fragmentation, dirty-page
     * writeback never catching up) */
    if (min > 0 && max > 2 * min) {
        printf("[BENCH] sustained_warn ratio=%ld/%ld — max > 2× min, possible regression\n",
               max, min);
    }
    return 1;
}

/* ── main ──────────────────────────────────────────────────────────────── */

int main(void) {
    printf("cc_bench: start\n");

    long t_all = now_ns();

    if (!bench_fork_exec()) return 1;
    if (!bench_open_read())  return 1;
    if (!bench_mmap_touch()) return 1;
    if (!bench_sustained())  return 1;

    long t_end = now_ns();
    printf("[BENCH] total_wall_ns=%ld\n", t_end - t_all);
    printf("cc_bench: all phases complete\nPASS\n");
    return 0;
}
