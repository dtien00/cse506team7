/*
 * probe_strsignal.c — exercise libc strsignal() across the full signal
 * range plus edge cases.  Companion to FEEDBACK_SIG_TAB.md §9.
 *
 * Usage:
 *   probe_strsignal
 *   probe_strsignal child   (internal — used by the fork+exec branch)
 *
 * Prints one line per probed signal number.  Also forks a child that
 * exits via raise(SIGTERM) and reports strsignal(WTERMSIG(status)) — the
 * canonical use case the evaluator named: print "Terminated" /
 * "Segmentation fault" when reporting on a child's exit status.
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

static void probe_one(int sig) {
    char *s = strsignal(sig);
    printf("strsignal(%4d) = %s\n", sig, s ? s : "<NULL>");
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    /* ── Walk every signal number in [1..SIGSYS] plus three edge cases. ── */
    for (int sig = 1; sig <= SIGSYS; sig++) probe_one(sig);
    probe_one(0);
    probe_one(-1);
    probe_one(99);

    /* ── Canonical use case: report a child's exit status by signal name. */
    pid_t pid = fork();
    if (pid == 0) {
        /* Child kills itself with SIGTERM so parent sees WIFSIGNALED. */
        raise(SIGTERM);
        _exit(99);
    }
    if (pid < 0) {
        printf("fork failed\n");
        return 1;
    }
    int st = 0;
    waitpid(pid, &st, 0);
    if (WIFSIGNALED(st)) {
        int s = WTERMSIG(st);
        printf("child[pid=%d] terminated by signal %d (%s)\n",
               pid, s, strsignal(s));
    } else if (WIFEXITED(st)) {
        printf("child[pid=%d] exited %d\n", pid, WEXITSTATUS(st));
    } else {
        printf("child[pid=%d] unknown wait status 0x%x\n", pid, st);
    }
    return 0;
}
