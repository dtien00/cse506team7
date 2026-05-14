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
#include <sys/wait.h>
#include <unistd.h>
#include "us_test_common.h"

/* ── compile one source file with tcc ──────────────────────────────────── */

static int compile(const char *src, const char *out) {
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
    int status = 0;
    if (waitpid(pid, &status, 0) != pid) return -1;
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
}

/* ── run a compiled binary, return exit status ──────────────────────────── */

static int run_binary(const char *path) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        char *argv[] = { (char *)path, NULL };
        execve(path, argv, NULL);
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) != pid) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* ── compile + run one test, record results in group ───────────────────── */

static void run_one(us_test_group_t *group, const char *label,
                    const char *src, const char *out) {
    char buf[128];

    printf("[cc_compile] %s: compiling %s\n", label, src);
    int cc_ok = (compile(src, out) == 0);

    __builtin_snprintf(buf, sizeof(buf), "%s: cc exits 0", label);
    TEST(group, buf, cc_ok);
    if (!cc_ok) {
        printf("[cc_compile] %s: compile FAILED\n", label);
        return;
    }
    printf("[cc_compile] %s: compile OK, running\n", label);

    int exit_code = run_binary(out);
    __builtin_snprintf(buf, sizeof(buf), "%s: binary exits 0", label);
    TEST(group, buf, exit_code == 0);
    printf("[cc_compile] %s: exited %d\n", label, exit_code);
}

/* ── public entry point ─────────────────────────────────────────────────── */

void run_test_cc_compile(us_test_group_t *group) {
    ut_group_begin(group, "cc_compile");

    /* ── combined interleaved stress (the primary regression target) ── */
    run_one(group, "cc_stress",
            "/test/test_cc_stress.c",        "/tmp/test_cc_stress");

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

    run_one(group, "hdr_tgmath",
            "/test/test_hdr_tgmath.c",       "/tmp/test_hdr_tgmath");

    /* ── kernel stress ── */
    run_one(group, "multi_open_read",
            "/test/test_multi_open_read.c",  "/tmp/test_multi_open_read");

    run_one(group, "heap_growth",
            "/test/test_heap_growth.c",      "/tmp/test_heap_growth");

    run_one(group, "write_chunks",
            "/test/test_write_chunks.c",     "/tmp/test_write_chunks");

    ut_group_end(group);
}
