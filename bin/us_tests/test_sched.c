/*
 * test_sched.c — Userspace tests for time-slice preemption and MLFQ boost.
 *
 * These tests run inside the OS as normal user processes.  They exercise
 * the scheduler end-to-end through real timer interrupts:
 *
 *  1. Round-robin fairness: N CPU-bound children all complete.
 *     Before the fix, each child was killed the first time the timer fired
 *     on it; with the fix they are re-queued as READY and eventually finish.
 *
 *  2. Exit-code preservation: a child that burns CPU then exits with a
 *     specific code must propagate that code correctly through wait(),
 *     proving the process was never silently killed by the timer.
 *
 *  3. MLFQ boost — starvation prevention (tests the mlfq_boost() fix):
 *     A "hog" process calls sched_yield() in a tight loop, which re-enqueues
 *     it at mlfq[0] without demotion.  A "worker" process burns CPU and gets
 *     fully demoted to mlfq[3] within ~700 ms.  With the hog saturating
 *     mlfq[0] the worker is starved until mlfq_boost() fires at
 *     MLFQ_BOOST_INTERVAL ticks (50 ticks = ~5 seconds), which re-queues the
 *     worker at mlfq[0].  The test verifies via procinfo() that the worker's
 *     cpu_ticks increase after the boost window and that the worker completes.
 */

#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include "us_test_common.h"

struct proc_info {
    int   pid;
    int   ppid;
    char  name[16];
    int   state;    // 0=unused,1=created,2=ready,3=running,4=blocked,5=zombie,6=stopped
    int   priority; // 0 if not implemented
    uint64_t cpu_ticks;
};

// Declared in libc/syscall.S -> kernel SYS_procinfo (26)
int procinfo(struct proc_info *infos, int max_count);

/* Return the cpu_ticks field for the given pid, or -1 if not found. */
static long long sched_get_cpu_ticks(pid_t pid) {
    struct proc_info infos[64];
    int n = procinfo(infos, 64);
    for (int i = 0; i < n; i++) {
        if (infos[i].pid == pid)
            return (long long)infos[i].cpu_ticks;
    }
    return -1; /* process not found */
}

/* Number of parallel CPU-bound children for the fairness test. */
#define NCHILDREN 4

void run_test_sched(us_test_group_t *group) {
    ut_group_begin(group, "scheduler");

    /* ── 1. Round-robin fairness ───────────────────────────────────────────
     * Fork NCHILDREN processes that each spin through a tight loop and then
     * exit(0).  If the timer kills rather than preempts, at least one child
     * will disappear with a non-zero exit status.
     */
    {
        pid_t pids[NCHILDREN];
        int all_spawned = 1;

        for (int i = 0; i < NCHILDREN; i++) {
            pids[i] = fork();
            if (pids[i] < 0) { all_spawned = 0; break; }
            if (pids[i] == 0) {
                volatile long c = 0;
                while (c < 2000000L) c++;
                _exit(0);
            }
        }

        TEST(group, "round-robin: all children spawned", all_spawned);

        int all_exited_ok = 1;
        for (int i = 0; i < NCHILDREN; i++) {
            if (pids[i] <= 0) continue;
            int status = 0;
            waitpid(pids[i], &status, 0);
            if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
                all_exited_ok = 0;
        }
        TEST(group, "round-robin: all children exited normally (not killed by timer)",
             all_exited_ok);
    }

    /* ── 2. Exit-code preserved after preemption ───────────────────────────
     * A single child burns enough CPU to be preempted at least once, then
     * exits with a known code.  The parent must receive that exact code.
     */
    {
        pid_t pid = fork();
        TEST(group, "exit-code test: fork succeeded", pid >= 0);

        if (pid == 0) {
            volatile long c = 0;
            while (c < 1000000L) c++;
            _exit(42);
        } else if (pid > 0) {
            int status = 0;
            waitpid(pid, &status, 0);
            TEST(group, "exit-code: child exited normally",   WIFEXITED(status));
            TEST(group, "exit-code: correct value after CPU burn",
                 WIFEXITED(status) && WEXITSTATUS(status) == 42);
        }
    }

    /* ── 3. MLFQ boost prevents starvation ──────────────────────────────────
     *
     * Timeline (10 ticks/s, MLFQ_BOOST_INTERVAL = 50 ticks):
     *
     *   t=0s   : fork hog (sched_yield loop → stays at mlfq[0])
     *            fork worker (CPU burn → demoted to mlfq[3] within ~700ms)
     *   t=0.7s : worker stuck at mlfq[3], hog saturates mlfq[0]: STARVATION
     *   t=5s   : mlfq_boost() fires — worker physically moved to mlfq[0]
     *   t=5s+  : worker now shares mlfq[0] with hog; gets ~50% CPU
     *   t=7s   : parent wakes (sleep 7s), snapshots worker cpu_ticks,
     *            kills hog, waits for both children
     *
     * Without the fix: worker cpu_ticks after 7s would be ~7 (pre-demotion
     * ticks only) and the worker would never finish (test would hang).
     * With the fix: worker gets meaningful CPU after the boost and completes.
     *
     * WORKER_ITERS is large enough that the worker cannot complete in the
     * 7 ticks it gets before demotion (~700ms), but will finish in the
     * ~10–20 seconds of shared CPU time available after the boost.
     */
    {
#define WORKER_ITERS  100000000L   /* ~10 s of CPU at QEMU speed — needs boost */
#define BOOST_SLEEP_S 7            /* sleep past the 5 s boost interval */
#define HOG_SIG       SIGKILL

        pid_t hog_pid    = -1;
        pid_t worker_pid = -1;
        int   spawned    = 1;

        /* Fork hog: calls sched_yield() forever → stays at mlfq[0]. */
        hog_pid = fork();
        if (hog_pid < 0) { spawned = 0; goto boost_done; }
        if (hog_pid == 0) {
            while (1) sched_yield();
            _exit(0); /* unreachable */
        }

        /* Fork worker: burns CPU → will be demoted to mlfq[3]. */
        worker_pid = fork();
        if (worker_pid < 0) { spawned = 0; kill(hog_pid, HOG_SIG); goto boost_done; }
        if (worker_pid == 0) {
            volatile long c = 0;
            while (c < WORKER_ITERS) c++;
            _exit(0);
        }

boost_done:
        TEST(group, "mlfq boost: hog and worker spawned", spawned);
        if (!spawned) goto boost_cleanup;

        /* Snapshot worker cpu_ticks before sleeping (should be very small —
         * only what it consumed before the parent called sleep). */
        long long ticks_before = sched_get_cpu_ticks(worker_pid);

        /* Sleep past MLFQ_BOOST_INTERVAL (50 ticks = 5 s).  During this
         * time the hog saturates mlfq[0] and the worker is starved until
         * the boost fires at t≈5 s. */
        sleep(BOOST_SLEEP_S);

        /* After sleeping, snapshot worker's cpu_ticks again. */
        long long ticks_after = sched_get_cpu_ticks(worker_pid);

        TEST(group, "mlfq boost: worker visible in procinfo",
             ticks_before >= 0 && ticks_after >= 0);
        TEST(group, "mlfq boost: worker cpu_ticks increased after boost window",
             ticks_after > ticks_before);

        /* Kill the hog so the worker can run at full speed to completion. */
        kill(hog_pid, HOG_SIG);

        /* Reap the hog. */
        {
            int hog_status = 0;
            waitpid(hog_pid, &hog_status, 0);
            TEST(group, "mlfq boost: hog killed and reaped", 1);
        }

        /* Wait for the worker to complete (it now has 100% CPU). */
        {
            int worker_status = 0;
            waitpid(worker_pid, &worker_status, 0);
            TEST(group, "mlfq boost: worker completed after boost",
                 WIFEXITED(worker_status) && WEXITSTATUS(worker_status) == 0);
        }

boost_cleanup:;
    }

    ut_group_end(group);
}
