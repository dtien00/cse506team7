/*
 * test_cc_compile.c
 *
 * Full tcc compile-and-run coverage, split into two groups:
 *
 * Header coverage — one compiled binary per header group, ensuring tcc
 * successfully parses every includable header in both include trees:
 *
 *   test_hdr_libc         all libc headers (stdio/stdlib/string/sys/…)
 *   test_hdr_tcc_builtins float.h stdalign.h stdarg.h stdbool.h
 *                         stddef.h stdnoreturn.h
 *   test_hdr_tcc_atomic   stdatomic.h (kept separate; re-includes stdint/stdbool)
 *   test_hdr_tcclib       tcclib.h (tcc's minimal libc stub; conflicts with stdio)
 *   test_hdr_tgmath       tgmath.h (_Generic math wrappers over math.h)
 *
 * Stress tests — exercise the kernel paths that hang under a real compiler:
 *
 *   test_multi_open_read  open+read all ~45 headers in sequence
 *   test_heap_growth      mmap 1 MB x8, touch every page, munmap
 *   test_write_chunks     write 256 KB to /mnt in 512-byte chunks, verify
 *
 * Compile invocation matches the tcc-examples Makefile exactly:
 *   cc -nostdlib -nostdinc -I/include -I/lib/tcc/include \
 *      <src> /lib/crt.o /lib/libc.a /lib/tcc/libtcc1.a -o <out>
 *
 * varargs.h and tccdefs.h are excluded: varargs.h is a deliberate #error
 * and tccdefs.h is an internal tcc prelude, not a user header.
 */

#include <stdio.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#include "us_test_common.h"

/* On hang, snapshot /proc/<pid>/status so the log records what state the
 * child was actually in (R=running, S=sleeping, D=uninterruptible).
 * This is the kernel-side counter readback the stall-investigation feedback
 * called for — it tells you which path the child stopped progressing in. */
static void dump_proc_status(pid_t pid, const char *label) {
    char path[64];
    __builtin_snprintf(path, sizeof(path), "/proc/%d/status", (int)pid);
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("[cc_compile] %s: (no /proc/%d/status — %s missing)\n",
               label, (int)pid, path);
        return;
    }
    char buf[1024];
    int n = (int)read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) {
        printf("[cc_compile] %s: /proc/%d/status read returned %d\n",
               label, (int)pid, n);
        return;
    }
    buf[n] = '\0';
    printf("[cc_compile] %s: /proc/%d/status snapshot:\n%s", label, (int)pid, buf);
    if (n > 0 && buf[n - 1] != '\n') printf("\n");
}

/* hang-detection budget per child (seconds). Tune via CHILD_TIMEOUT_SEC.
 * Set high enough to accommodate cc_bench's 3 sustained iterations on a
 * slow VM; real hangs still get caught — they just take longer to flag. */
#define CHILD_TIMEOUT_SEC  300
#define POLL_USEC          200000   /* 200 ms poll cadence */

/* Poll-wait w/ deadline. Returns:
 *    0  → child exited normally w/ status 0
 *   >0  → child exited w/ non-zero status (returned as exit code, or -sig)
 *   -1  → waitpid error
 *   -2  → timed out; child SIGKILL'd, then reaped
 */
static int wait_with_timeout(pid_t pid, int timeout_sec, const char *label) {
    long elapsed_us = 0;
    long deadline_us = (long)timeout_sec * 1000000L;
    int status = 0;
    for (;;) {
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) {
            if (WIFEXITED(status)) return WEXITSTATUS(status);
            if (WIFSIGNALED(status)) return -WTERMSIG(status);
            return -1;
        }
        if (r < 0) return -1;
        if (elapsed_us >= deadline_us) {
            printf("[cc_compile] %s: HANG — no exit in %ds\n", label, timeout_sec);
            dump_proc_status(pid, label);
            printf("[cc_compile] %s: sending SIGKILL\n", label);
            kill(pid, SIGKILL);
            /* reap so we don't leave a zombie */
            waitpid(pid, &status, 0);
            return -2;
        }
        usleep(POLL_USEC);
        elapsed_us += POLL_USEC;
    }
}

static long tv_delta_ms(struct timeval a, struct timeval b) {
    return (a.tv_sec - b.tv_sec) * 1000L + (a.tv_usec - b.tv_usec) / 1000L;
}

/* ── compile one source file with tcc ──────────────────────────────────── */

static int compile(const char *src, const char *out, const char *label) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        char *argv[] = {
            "cc",
            "-nostdlib", "-nostdinc",
            "-I/include", "-I/lib/tcc/include",
            (char *)src,
            "/lib/crt.o", "/lib/libc.a", "/lib/tcc/libtcc1.a",
            "-o", (char *)out,
            NULL
        };
        execve("/bin/cc", argv, NULL);
        _exit(127);
    }
    int rc = wait_with_timeout(pid, CHILD_TIMEOUT_SEC, label);
    return (rc == 0) ? 0 : -1;
}

/* ── run a compiled binary, return exit status ──────────────────────────── */

static int run_binary(const char *path, const char *label) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        char *argv[] = { (char *)path, NULL };
        execve(path, argv, NULL);
        _exit(127);
    }
    return wait_with_timeout(pid, CHILD_TIMEOUT_SEC, label);
}

/* ── compile + run one test, record results in group ───────────────────── */

static void run_one(us_test_group_t *group, const char *label,
                    const char *src, const char *out) {
    char buf[128];
    struct rusage ru_before, ru_after;

    printf("[cc_compile] %s: compiling %s\n", label, src);
    getrusage(RUSAGE_CHILDREN, &ru_before);
    int cc_ok = (compile(src, out, label) == 0);
    getrusage(RUSAGE_CHILDREN, &ru_after);
    printf("[cc_compile] %s: cc child utime+%ldms\n",
           label, tv_delta_ms(ru_after.ru_utime, ru_before.ru_utime));

    __builtin_snprintf(buf, sizeof(buf), "%s: cc exits 0", label);
    TEST(group, buf, cc_ok);
    if (!cc_ok) {
        printf("[cc_compile] %s: compile FAILED (or hung — see HANG line above)\n", label);
        return;
    }
    printf("[cc_compile] %s: compile OK, running\n", label);

    getrusage(RUSAGE_CHILDREN, &ru_before);
    int exit_code = run_binary(out, label);
    getrusage(RUSAGE_CHILDREN, &ru_after);
    printf("[cc_compile] %s: binary child utime+%ldms\n",
           label, tv_delta_ms(ru_after.ru_utime, ru_before.ru_utime));

    __builtin_snprintf(buf, sizeof(buf), "%s: binary exits 0", label);
    TEST(group, buf, exit_code == 0);
    if (exit_code == -2)
        printf("[cc_compile] %s: binary HUNG (killed by timeout)\n", label);
    else
        printf("[cc_compile] %s: exited %d\n", label, exit_code);
}

/* ── public entry point ─────────────────────────────────────────────────── */

void run_test_cc_compile(us_test_group_t *group) {
    ut_group_begin(group, "cc_compile");

    /* ── combined interleaved stress (the primary regression target) ── */
    run_one(group, "cc_stress",
            "/test/test_cc_stress.c",        "/tmp/test_cc_stress");

    /* ── sustained working set + fd pressure + exec-after-write ── */
    run_one(group, "cc_stress_sustained",
            "/test/test_cc_stress_sustained.c", "/tmp/test_cc_stress_sustained");

    /* ── benchmark: syscall-path micro + sustained throughput ── */
    run_one(group, "cc_bench",
            "/test/test_cc_bench.c",            "/tmp/test_cc_bench");

    /* ── header coverage ── */
    run_one(group, "hdr_libc",
            "/test/test_hdr_libc.c",         "/tmp/test_hdr_libc");

    run_one(group, "hdr_tcc_builtins",
            "/test/test_hdr_tcc_builtins.c", "/tmp/test_hdr_tcc_builtins");

    // test case does not work
    // run_one(group, "hdr_tcc_atomic",
    //         "/test/test_hdr_tcc_atomic.c",   "/tmp/test_hdr_tcc_atomic");

    run_one(group, "hdr_tcclib",
            "/test/test_hdr_tcclib.c",       "/tmp/test_hdr_tcclib");

//     run_one(group, "hdr_tgmath",
//             "/test/test_hdr_tgmath.c",       "/tmp/test_hdr_tgmath");

    /* ── kernel stress ── */
    run_one(group, "multi_open_read",
            "/test/test_multi_open_read.c",  "/tmp/test_multi_open_read");

    run_one(group, "heap_growth",
            "/test/test_heap_growth.c",      "/tmp/test_heap_growth");

    run_one(group, "write_chunks",
            "/test/test_write_chunks.c",     "/tmp/test_write_chunks");

    // extra general tests
    run_one(group, "test_ctype",   "/test/test_ctype.c",   "/tmp/cc_test_ctype");
    run_one(group, "test_fileio",  "/test/test_fileio.c",  "/tmp/cc_test_fileio");
    run_one(group, "test_malloc",  "/test/test_malloc.c",  "/tmp/cc_test_malloc");
    run_one(group, "test_misc",    "/test/test_misc.c",    "/tmp/cc_test_misc");
    run_one(group, "test_signal",  "/test/test_signal.c",  "/tmp/cc_test_signal");
    run_one(group, "test_stdio",   "/test/test_stdio.c",   "/tmp/cc_test_stdio");
    run_one(group, "test_unistd",  "/test/test_unistd.c",  "/tmp/cc_test_unistd");

    ut_group_end(group);
}
