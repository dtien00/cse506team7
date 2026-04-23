/*
 * test_forkforkfork.c — Deep recursive fork tree / init reaping (xv6 forkforkfork).
 *
 * A process forks a child which forks a grandchild which forks a
 * great-grandchild, etc.  Each ancestor exits immediately, orphaning the
 * deeper levels.  Init (pid 1) must adopt and reap every orphan.
 *
 * We verify:
 *  1. Deep 5-level fork chain: all orphans eventually reaped by init
 *     (no zombie leak — verified by checking procinfo after enough time).
 *  2. Wide fan-out: each of N children immediately forks M grandchildren
 *     then exits — all grandchildren become orphans and init reaps them.
 *  3. Orphan reparenting happens before init observes the orphan: grandchild
 *     calls getppid() after sleeping past parent exit → must be 1.
 *  4. Pipe-based synchronization: deepest descendant writes through
 *     inherited pipe; root reads to confirm the full chain executed.
 */

#include <sys/procinfo.h>
#include <sys/wait.h>
#include <unistd.h>
#include "us_test_common.h"

int procinfo(struct proc_info *infos, int max_count);

/* ── helper: count processes with given pid in procinfo ─────────────────── */
static int pid_in_table(pid_t pid) {
    struct proc_info infos[128];
    int n = procinfo(infos, 128);
    for (int i = 0; i < n; i++)
        if (infos[i].pid == pid) return 1;
    return 0;
}

/* ── 1. deep 5-level fork chain ─────────────────────────────────────────── */

static void test_deep_chain(us_test_group_t *group) {
    /* Each level forks one child then exits.  The pipe lets the leaf
     * signal completion back to the root. */
    int fds[2];
    int r = pipe(fds);
    TEST(group, "deep-chain: pipe created", r == 0);
    if (r < 0) return;

#define DEPTH 5
    pid_t child = fork();
    if (child == 0) {
        /* depth 1 */
        close(fds[0]);
        pid_t c = fork();
        if (c == 0) {
            /* depth 2 */
            pid_t c2 = fork();
            if (c2 == 0) {
                /* depth 3 */
                pid_t c3 = fork();
                if (c3 == 0) {
                    /* depth 4 */
                    pid_t c4 = fork();
                    if (c4 == 0) {
                        /* depth 5 — leaf */
                        char sig = 0x5A;
                        write(fds[1], &sig, 1);
                        close(fds[1]);
                        _exit(0);
                    }
                    close(fds[1]);
                    _exit(0); /* orphan c4 */
                }
                close(fds[1]);
                _exit(0); /* orphan c3 */
            }
            close(fds[1]);
            _exit(0); /* orphan c2 */
        }
        close(fds[1]);
        _exit(0); /* orphan c */
    }
    close(fds[1]);

    /* reap direct child */
    waitpid(child, NULL, 0);

    /* read sentinel from leaf (may take a moment — leaf is orphaned) */
    char sig = 0;
    int n = (int)read(fds[0], &sig, 1);
    close(fds[0]);
    TEST(group, "deep-chain: leaf descendant executed and wrote sentinel",
         n == 1 && sig == 0x5A);
#undef DEPTH
}

/* ── 2. wide fan-out orphan flood ───────────────────────────────────────── */

static void test_wide_orphan(us_test_group_t *group) {
#define N_CHILDREN  5
#define N_GRAND     4
    /*
     * Fork N_CHILDREN children; each forks N_GRAND grandchildren then exits.
     * Grandchildren sleep briefly then exit.  We wait for all direct children
     * and then give init time to reap grandchildren.
     */
    int fds[2];
    pipe(fds);

    pid_t children[N_CHILDREN];
    for (int i = 0; i < N_CHILDREN; i++) {
        children[i] = fork();
        if (children[i] == 0) {
            close(fds[0]);
            for (int j = 0; j < N_GRAND; j++) {
                pid_t gc = fork();
                if (gc == 0) {
                    sleep(1); /* stay alive briefly */
                    char b = 1;
                    write(fds[1], &b, 1);
                    close(fds[1]);
                    _exit(0);
                }
            }
            close(fds[1]);
            _exit(0); /* orphan all grandchildren */
        }
    }
    close(fds[1]);

    /* reap all direct children */
    for (int i = 0; i < N_CHILDREN; i++)
        waitpid(children[i], NULL, 0);

    /* collect N_CHILDREN*N_GRAND signals from grandchildren */
    int total = N_CHILDREN * N_GRAND;
    int received = 0;
    for (int i = 0; i < total; i++) {
        char b = 0;
        if (read(fds[0], &b, 1) == 1) received++;
    }
    close(fds[0]);

    TEST(group, "wide-orphan: all grandchildren ran and wrote sentinel",
         received == total);
#undef N_CHILDREN
#undef N_GRAND
}

/* ── 3. orphan reparented before getppid ─────────────────────────────────── */

static void test_orphan_ppid(us_test_group_t *group) {
    int fds[2];
    pipe(fds);
    pid_t child = fork();
    if (child == 0) {
        close(fds[0]);
        pid_t gc = fork();
        if (gc == 0) {
            /* grandchild: wait for parent (child) to exit */
            sleep(1);
            pid_t pp = getppid();
            write(fds[1], &pp, sizeof(pp));
            close(fds[1]);
            _exit(0);
        }
        close(fds[1]);
        _exit(0); /* orphan gc immediately */
    }
    close(fds[1]);
    waitpid(child, NULL, 0); /* reap child; gc is now orphan */

    pid_t gc_ppid = 0;
    read(fds[0], &gc_ppid, sizeof(gc_ppid));
    close(fds[0]);
    TEST(group, "orphan-ppid: orphaned grandchild ppid == 1 (init)",
         gc_ppid == 1);
}

/* ── 4. no zombie leak: orphaned chain leaves no zombies ─────────────────── */

static void test_no_zombie_leak(us_test_group_t *group) {
    /* Fork a child that immediately forks 8 grandchildren and exits.
     * After 3 seconds, verify none of those pids are still in the proc table
     * (init should have reaped them all). */
    pid_t tracked[8];
    int fds[2];
    pipe(fds);

    pid_t child = fork();
    if (child == 0) {
        close(fds[0]);
        for (int i = 0; i < 8; i++) {
            pid_t gc = fork();
            if (gc == 0) {
                sleep(1);
                _exit(0);
            }
            write(fds[1], &gc, sizeof(gc));
        }
        close(fds[1]);
        _exit(0);
    }
    close(fds[1]);

    for (int i = 0; i < 8; i++)
        read(fds[0], &tracked[i], sizeof(tracked[i]));
    close(fds[0]);
    waitpid(child, NULL, 0);

    /* wait for grandchildren to finish + init to reap */
    sleep(3);

    int leaked = 0;
    for (int i = 0; i < 8; i++)
        if (pid_in_table(tracked[i])) leaked++;

    TEST(group, "no-zombie-leak: all 8 orphaned grandchildren reaped by init",
         leaked == 0);
}

/* ── public entry point ─────────────────────────────────────────────────── */

void run_test_forkforkfork(us_test_group_t *group) {
    ut_group_begin(group, "forkforkfork");

    test_deep_chain(group);
    test_wide_orphan(group);
    test_orphan_ppid(group);
    test_no_zombie_leak(group);

    ut_group_end(group);
}
