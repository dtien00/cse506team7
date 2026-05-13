// cc -nostdlib -I/include -I/lib/tcc/include /lib/crt.o /test/test_fileio.c -o /tmp/t -lc

#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "us_test_common.h"

static void run_cc_test(us_test_group_t *group,
                        const char *label,
                        const char *src,
                        const char *out) {
    /* compile */
    pid_t pid = fork();
    TEST(group, label, pid >= 0);
    if (pid == 0) {
        char *argv[] = { "cc", "-nostdlib", "-I/include", "-I/lib/tcc/include",
                         "/lib/crt.o", "-lc",
                         (char *)src, "-o", (char *)out, NULL };
        execve("/bin/cc", argv, NULL);
        _exit(127);
    }
    if (pid < 0)
        return;

    int status = 0;
    TEST(group, "cc child reaped", waitpid(pid, &status, 0) == pid);

    char compile_label[128];
    snprintf(compile_label, sizeof(compile_label), "%s: cc exits 0", label);
    TEST(group, compile_label, WIFEXITED(status) && WEXITSTATUS(status) == 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return;

    /* run compiled binary */
    pid = fork();
    char run_label[128];
    snprintf(run_label, sizeof(run_label), "%s: binary fork", label);
    TEST(group, run_label, pid >= 0);
    if (pid == 0) {
        char *argv[] = { (char *)out, NULL };
        execve(out, argv, NULL);
        _exit(127);
    }
    if (pid < 0)
        return;

    status = 0;
    char reap_label[128];
    snprintf(reap_label, sizeof(reap_label), "%s: binary reaped", label);
    TEST(group, reap_label, waitpid(pid, &status, 0) == pid);

    char exit_label[128];
    snprintf(exit_label, sizeof(exit_label), "%s: binary exits 0", label);
    TEST(group, exit_label, WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

void run_test_cc_compile(us_test_group_t *group) {
    ut_group_begin(group, "cc_compile");

    run_cc_test(group, "test_ctype",   "/test/test_ctype.c",   "/tmp/cc_test_ctype");
    run_cc_test(group, "test_fileio",  "/test/test_fileio.c",  "/tmp/cc_test_fileio");
    run_cc_test(group, "test_malloc",  "/test/test_malloc.c",  "/tmp/cc_test_malloc");
    run_cc_test(group, "test_misc",    "/test/test_misc.c",    "/tmp/cc_test_misc");
    run_cc_test(group, "test_signal",  "/test/test_signal.c",  "/tmp/cc_test_signal");
    run_cc_test(group, "test_stdio",   "/test/test_stdio.c",   "/tmp/cc_test_stdio");
    run_cc_test(group, "test_unistd",  "/test/test_unistd.c",  "/tmp/cc_test_unistd");

    ut_group_end(group);
}
