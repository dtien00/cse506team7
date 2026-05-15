/*
 * test_strsignal.c — Regression test for libc strsignal().
 *
 * Companion to FEEDBACK_SIG_TAB.md Issue S.  The header
 * <string.h>:70 declares `char *strsignal(int signum);` and historically
 * no implementation existed in libc.a — any caller would fail to link.
 *
 * This test exercises:
 *   - known signal numbers (SIGHUP, SIGINT, SIGKILL, SIGSEGV, SIGTERM, ...)
 *     return non-NULL strings containing the conventional wording.
 *   - unknown / out-of-range signal numbers (0, negative, > SIGSYS) fall
 *     back to "Unknown signal N" rather than crashing or returning NULL.
 *   - the canonical use case: WIFSIGNALED(status) → strsignal(WTERMSIG)
 *     produces a readable string for a child killed by SIGTERM.
 */

#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include "us_test_common.h"

static int has_substr(const char *hay, const char *needle) {
    return hay && needle && strstr(hay, needle) != NULL;
}

void run_test_strsignal(us_test_group_t *group) {
    ut_group_begin(group, "strsignal");

    /* ── Known signals: non-NULL and contain the conventional wording ──── */
    char *s_hup  = strsignal(SIGHUP);
    char *s_int  = strsignal(SIGINT);
    char *s_kill = strsignal(SIGKILL);
    char *s_segv = strsignal(SIGSEGV);
    char *s_term = strsignal(SIGTERM);
    char *s_pipe = strsignal(SIGPIPE);
    char *s_chld = strsignal(SIGCHLD);

    TEST(group, "strsignal(SIGHUP)  non-NULL", s_hup  != NULL);
    TEST(group, "strsignal(SIGINT)  non-NULL", s_int  != NULL);
    TEST(group, "strsignal(SIGKILL) non-NULL", s_kill != NULL);
    TEST(group, "strsignal(SIGSEGV) non-NULL", s_segv != NULL);
    TEST(group, "strsignal(SIGTERM) non-NULL", s_term != NULL);
    TEST(group, "strsignal(SIGPIPE) non-NULL", s_pipe != NULL);
    TEST(group, "strsignal(SIGCHLD) non-NULL", s_chld != NULL);

    TEST(group, "strsignal(SIGHUP)  contains 'Hangup'",       has_substr(s_hup,  "Hangup"));
    TEST(group, "strsignal(SIGINT)  contains 'Interrupt'",    has_substr(s_int,  "Interrupt"));
    TEST(group, "strsignal(SIGKILL) contains 'Killed'",       has_substr(s_kill, "Killed"));
    TEST(group, "strsignal(SIGSEGV) contains 'Segmentation'", has_substr(s_segv, "Segmentation"));
    TEST(group, "strsignal(SIGTERM) contains 'Terminated'",   has_substr(s_term, "Terminated"));
    TEST(group, "strsignal(SIGPIPE) contains 'Broken pipe'",  has_substr(s_pipe, "Broken pipe"));

    /* ── Edge cases: 0, negative, > SIGSYS all fall back to "Unknown signal N" */
    char *s_zero = strsignal(0);
    char *s_neg  = strsignal(-1);
    char *s_high = strsignal(99);
    char *s_huge = strsignal(123456);

    TEST(group, "strsignal(0)      non-NULL",                    s_zero != NULL);
    TEST(group, "strsignal(-1)     non-NULL",                    s_neg  != NULL);
    TEST(group, "strsignal(99)     non-NULL",                    s_high != NULL);
    TEST(group, "strsignal(123456) non-NULL",                    s_huge != NULL);

    TEST(group, "strsignal(0)      starts 'Unknown signal'",     s_zero != NULL && strncmp(s_zero, "Unknown signal", 14) == 0);
    TEST(group, "strsignal(-1)     starts 'Unknown signal'",     s_neg  != NULL && strncmp(s_neg,  "Unknown signal", 14) == 0);
    TEST(group, "strsignal(99)     starts 'Unknown signal'",     s_high != NULL && strncmp(s_high, "Unknown signal", 14) == 0);
    TEST(group, "strsignal(123456) starts 'Unknown signal'",     s_huge != NULL && strncmp(s_huge, "Unknown signal", 14) == 0);

    /* ── Canonical use case: report on a child killed by a signal ───────── */
    pid_t pid = fork();
    if (pid == 0) {
        /* Child: terminate ourselves with SIGTERM via raise() so the parent's
           waitpid sees WIFSIGNALED with WTERMSIG == SIGTERM. */
        raise(SIGTERM);
        _exit(99); /* unreachable if SIGTERM is the default-fatal disposition */
    }
    TEST(group, "fork() succeeds", pid > 0);
    if (pid > 0) {
        int wst = 0;
        waitpid(pid, &wst, 0);
        TEST(group, "child terminated by signal", WIFSIGNALED(wst));
        if (WIFSIGNALED(wst)) {
            char *msg = strsignal(WTERMSIG(wst));
            TEST(group, "strsignal(WTERMSIG(status)) non-NULL", msg != NULL);
            TEST(group, "WTERMSIG(status) == SIGTERM",          WTERMSIG(wst) == SIGTERM);
            TEST(group, "strsignal(WTERMSIG) contains 'Terminated'",
                 has_substr(msg, "Terminated"));
        }
    }

    ut_group_end(group);
}
