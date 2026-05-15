/*
 * signame.c — print the human-readable name of a signal number.
 *
 * Real-world consumer of libc strsignal().  Companion to
 * FEEDBACK_SIG_TAB.md Issue S.
 *
 * Usage:
 *   signame              List every supported signal (1..SIGSYS) with name.
 *   signame N            Print strsignal(N).
 *   signame N1 N2 ...    Print each in order.
 *
 * Exit status:
 *   0   At least one argument was a valid signal number, or no args (list).
 *   1   All numeric args were unknown signals (parser still printed
 *       "Unknown signal N" via strsignal).
 *   2   Bad usage (non-numeric argument).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

static int parse_int(const char *s, int *out) {
    if (!s || !*s) return -1;
    const char *p = s;
    int sign = 1;
    if (*p == '-') { sign = -1; p++; }
    if (!*p) return -1;
    int v = 0;
    for (; *p; p++) {
        if (*p < '0' || *p > '9') return -1;
        v = v * 10 + (*p - '0');
    }
    *out = sign * v;
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 1) {
        /* No args: list every signal 1..SIGSYS with its description. */
        for (int sig = 1; sig <= SIGSYS; sig++) {
            printf("%2d) %s\n", sig, strsignal(sig));
        }
        return 0;
    }

    int any_known = 0;
    for (int i = 1; i < argc; i++) {
        int n;
        if (parse_int(argv[i], &n) < 0) {
            fprintf(stderr, "signame: '%s' is not a signal number\n", argv[i]);
            return 2;
        }
        char *desc = strsignal(n);
        printf("%d) %s\n", n, desc);
        /* "Unknown signal" prefix is libc's fallback shape; treat any other
           text as a real known signal for exit-status purposes. */
        if (desc && strncmp(desc, "Unknown signal", 14) != 0) any_known = 1;
    }
    return any_known ? 0 : 1;
}
