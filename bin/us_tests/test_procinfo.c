#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/procinfo.h>
#include "us_test_common.h"

// Declared in libc/syscall.S -> kernel SYS_procinfo (26)
int procinfo(struct proc_info *infos, int max_count);

static int find_pid(const struct proc_info *infos, int n, int pid) {
    for (int i = 0; i < n; i++)
        if (infos[i].pid == pid) return i;
    return -1;
}

void run_test_procinfo(us_test_group_t *group) {
    ut_group_begin(group, "procinfo");

    struct proc_info infos[64];

    /* ── basic return value ──────────────────────────────────────────── */
    int n = procinfo(infos, 64);
    TEST(group, "procinfo: returns > 0", n > 0);

    if (n <= 0) {
        SKIP(group, "procinfo: self PID found in table",        "procinfo returned 0");
        SKIP(group, "procinfo: all PIDs positive",             "procinfo returned 0");
        SKIP(group, "procinfo: all states in valid range",     "procinfo returned 0");
        SKIP(group, "procinfo: all names non-empty",           "procinfo returned 0");
        SKIP(group, "procinfo: max_count=0 returns 0",         "procinfo returned 0");
        SKIP(group, "procinfo: max_count=1 returns exactly 1", "procinfo returned 0");
        SKIP(group, "procinfo: child visible after fork",      "procinfo returned 0");
        SKIP(group, "procinfo: reaped child not in table",     "procinfo returned 0");
        ut_group_end(group);
        return;
    }

    /* ── self PID visible ────────────────────────────────────────────── */
    pid_t me = getpid();
    TEST(group, "procinfo: self PID found in table", find_pid(infos, n, me) >= 0);

    /* ── all returned PIDs must be positive ─────────────────────────── */
    int all_pids_ok = 1;
    for (int i = 0; i < n; i++)
        if (infos[i].pid <= 0) { all_pids_ok = 0; break; }
    TEST(group, "procinfo: all PIDs positive", all_pids_ok);

    /* ── all states in valid range (1=created … 6=stopped) ──────────── */
    int all_states_ok = 1;
    for (int i = 0; i < n; i++)
        if (infos[i].state < 1 || infos[i].state > 6) { all_states_ok = 0; break; }
    TEST(group, "procinfo: all states in valid range (1-6)", all_states_ok);

    /* ── all names non-empty ─────────────────────────────────────────── */
    int all_names_ok = 1;
    for (int i = 0; i < n; i++)
        if (infos[i].name[0] == '\0') { all_names_ok = 0; break; }
    TEST(group, "procinfo: all names non-empty", all_names_ok);

    /* ── max_count=0: returns 0 without touching the buffer ─────────── */
    int r0 = procinfo(infos, 0);
    TEST(group, "procinfo: max_count=0 returns 0", r0 == 0);

    /* ── max_count=1: returns exactly 1 (at least init exists) ──────── */
    struct proc_info one;
    int r1 = procinfo(&one, 1);
    TEST(group, "procinfo: max_count=1 returns exactly 1", r1 == 1);

    /* ── child visible while alive, gone after reaping ───────────────── */
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        SKIP(group, "procinfo: child visible after fork",  "pipe failed");
        SKIP(group, "procinfo: reaped child not in table", "pipe failed");
        ut_group_end(group);
        return;
    }

    pid_t child = fork();
    if (child < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        SKIP(group, "procinfo: child visible after fork",  "fork failed");
        SKIP(group, "procinfo: reaped child not in table", "fork failed");
        ut_group_end(group);
        return;
    }

    if (child == 0) {
        /* child: block on pipe read so the parent can observe us alive */
        close(pipefd[1]);
        char c;
        read(pipefd[0], &c, 1);
        close(pipefd[0]);
        _exit(0);
    }

    /* parent path */
    close(pipefd[0]);

    struct proc_info snap[64];
    int snap_n = procinfo(snap, 64);
    TEST(group, "procinfo: child visible after fork",
         find_pid(snap, snap_n, child) >= 0);

    /* release child, then reap it */
    write(pipefd[1], "x", 1);
    close(pipefd[1]);
    waitpid(child, NULL, 0);

    /* after reaping, the entry should be gone (state UNUSED, skipped) */
    struct proc_info snap2[64];
    int snap2_n = procinfo(snap2, 64);
    TEST(group, "procinfo: reaped child not in table",
         find_pid(snap2, snap2_n, child) < 0);

    ut_group_end(group);
}
