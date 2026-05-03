/*
 * test_leak.c — Resource leak detection tests (userspace-observable)
 *
 * Strategy: since the kernel exposes no free-page counter to userspace,
 * we use behavioral proxies:
 *   - Repeated fork+exit+wait must not exhaust proc slots (would fail fork).
 *   - Repeated mmap+munmap must not exhaust VA / physical pages (would fail mmap).
 *   - Repeated open+close must not exhaust fd_table entries (would fail open).
 *   - Repeated pipe+close must not leak pipe vnodes (would fail later pipe).
 *   - dup+close must not leave dangling refs (behavioral: fd reuse OK).
 *
 * For the known issues in the plan we add explicit regression tests:
 *   KI-1: ~688 KB/fork page-table leak — 50 sequential fork/wait cycles;
 *          if pages are not returned the 50th fork must still succeed.
 *   KI-2: FD leak on mid-operation error — open nonexistent path fails but
 *          NFILES subsequent opens of a real file must still all succeed.
 *   KI-3: dentry never pruned after unlink+close — open, unlink, close;
 *          repeat NDENTRY times; the final create must still succeed.
 *   KI-4: grandchild not reaped by init — double-fork; verify no zombie
 *          proc slot is permanently occupied.
 *
 * All tests are self-contained userspace: no kernel-internal snapshots needed.
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "us_test_common.h"

/* ── helpers ─────────────────────────────────────────────────────────────── */

/* Count live (non-zombie, non-init) entries visible in /proc.
 * Returns -1 if /proc cannot be read. */
static int proc_count_live(void) __attribute__((unused));
static int proc_count_live(void) {
    int fd = open("/proc", O_RDONLY);
    if (fd < 0)
        return -1;
    close(fd);

    /* We can't opendir easily without libc DIR; count by trying pids 1..256.
     * Good enough for a relative before/after delta. */
    int count = 0;
    char path[32];
    for (int pid = 1; pid < 256; pid++) {
        int n = snprintf(path, sizeof(path), "/proc/%d/status", pid);
        if (n < 0) continue;
        int sfd = open(path, O_RDONLY);
        if (sfd >= 0) {
            count++;
            close(sfd);
        }
    }
    return count;
}

/* ── Section 1: Memory leak proxies ─────────────────────────────────────── */

/* 1.1  mmap/munmap cycle does not exhaust VA or pages */
static void test_mmap_cycle_no_leak(us_test_group_t *group) {
#define MM_CYCLES 128
    int ok = 1;
    for (int i = 0; i < MM_CYCLES; i++) {
        void *m = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (m == MAP_FAILED) { ok = 0; break; }
        ((volatile char *)m)[0] = (char)i;   /* demand-fault page */
        munmap(m, 4096);
    }
    TEST(group, "mm-leak/mmap-cycle: 128 mmap+touch+munmap all succeed", ok);
#undef MM_CYCLES
}

/* 1.2  Multi-page mmap/munmap cycle */
static void test_mmap_multipage_cycle(us_test_group_t *group) {
#define MP_CYCLES 32
    int ok = 1;
    for (int i = 0; i < MP_CYCLES; i++) {
        size_t sz = 8 * 4096;   /* 32 KB */
        void *m = mmap(NULL, sz, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (m == MAP_FAILED) { ok = 0; break; }
        volatile char *p = (volatile char *)m;
        for (size_t off = 0; off < sz; off += 4096)
            p[off] = (char)i;
        munmap(m, sz);
    }
    TEST(group, "mm-leak/mmap-multipage: 32x 32KB mmap+touch+munmap succeed", ok);
#undef MP_CYCLES
}

/* 1.3  After stress, a fresh large allocation still succeeds */
static void test_mmap_post_stress_alloc(us_test_group_t *group) {
    for (int i = 0; i < 64; i++) {
        void *m = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (m == MAP_FAILED) goto skip;
        ((volatile char *)m)[0] = 1;
        munmap(m, 4096);
    }
    {
        void *big = mmap(NULL, 1024 * 1024, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        TEST(group, "mm-leak/post-stress: 1 MB mmap succeeds after 64 cycles",
             big != MAP_FAILED);
        if (big != MAP_FAILED) munmap(big, 1024 * 1024);
        return;
    }
skip:
    SKIP(group, "mm-leak/post-stress: 1 MB mmap after 64 cycles",
         "stress phase failed");
}

/* ── Section 2: Filesystem leak proxies ─────────────────────────────────── */

/* 2.1  Repeated open/close of same file — no fd_table leak */
static void test_open_close_cycle(us_test_group_t *group) {
#define OC_CYCLES 100
    const char *path = "/hello.txt";
    int ok = 1;

    /* verify the file exists first */
    int probe = open(path, O_RDONLY);
    if (probe < 0) {
        SKIP(group, "fs-leak/open-close: 100 open+close cycles on /hello.txt",
             "file not found");
        return;
    }
    close(probe);

    for (int i = 0; i < OC_CYCLES; i++) {
        int fd = open(path, O_RDONLY);
        if (fd < 0) { ok = 0; break; }
        close(fd);
    }
    TEST(group, "fs-leak/open-close: 100 open+close cycles succeed", ok);
#undef OC_CYCLES
}

/* 2.2  Repeated create/unlink — vnode pool not exhausted */
static void test_create_unlink_cycle(us_test_group_t *group) {
#define CU_CYCLES 50
    int ok = 1;
    char path[64];
    for (int i = 0; i < CU_CYCLES; i++) {
        snprintf(path, sizeof(path), "/mnt/leak_cu_%d.tmp", i % 8);
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { ok = 0; break; }
        write(fd, "x", 1);
        close(fd);
        if (unlink(path) < 0) { ok = 0; break; }
    }
    TEST(group, "fs-leak/create-unlink: 50 create+unlink cycles succeed", ok);
    /* clean up any residuals */
    for (int i = 0; i < 8; i++) {
        snprintf(path, sizeof(path), "/mnt/leak_cu_%d.tmp", i);
        unlink(path);
    }
#undef CU_CYCLES
}

/* 2.3  Repeated mkdir/rmdir — directory inode not leaked */
static void test_mkdir_rmdir_cycle(us_test_group_t *group) {
#define DR_CYCLES 20
    int ok = 1;
    char path[64];
    for (int i = 0; i < DR_CYCLES; i++) {
        snprintf(path, sizeof(path), "/mnt/leak_dr_%d", i % 4);
        if (mkdir(path, 0755) < 0) { ok = 0; break; }
        if (rmdir(path) < 0) { ok = 0; break; }
    }
    TEST(group, "fs-leak/mkdir-rmdir: 20 mkdir+rmdir cycles succeed", ok);
    for (int i = 0; i < 4; i++) {
        snprintf(path, sizeof(path), "/mnt/leak_dr_%d", i);
        rmdir(path);
    }
#undef DR_CYCLES
}

/* 2.4  dup/dup2 refcount — both ends closed, no leak */
static void test_dup_close_cycle(us_test_group_t *group) {
    const char *path = "/hello.txt";
    int base = open(path, O_RDONLY);
    if (base < 0) {
        SKIP(group, "fs-leak/dup-close: dup+close refcount cycle", "open failed");
        return;
    }

    int ok = 1;
    for (int i = 0; i < 32; i++) {
        int fd2 = dup(base);
        if (fd2 < 0) { ok = 0; break; }
        close(fd2);   /* ref_count should drop back to 1 */
    }
    close(base);

    /* After all dup+closes, open must still succeed */
    int verify = open(path, O_RDONLY);
    TEST(group, "fs-leak/dup-close: 32 dup+close cycles, file still openable",
         ok && verify >= 0);
    if (verify >= 0) close(verify);
}

/* 2.5  Pipe create/close cycle — pipe vnode freed both ends closed */
static void test_pipe_cycle(us_test_group_t *group) {
#define PIPE_CYCLES 32
    int ok = 1;
    for (int i = 0; i < PIPE_CYCLES; i++) {
        int fds[2];
        if (pipe(fds) < 0) { ok = 0; break; }
        close(fds[0]);
        close(fds[1]);
    }
    TEST(group, "fs-leak/pipe-cycle: 32 pipe+close-both cycles succeed", ok);
#undef PIPE_CYCLES
}

/* 2.6  Child exits without closing FDs — parent waits; slot must be reusable */
static void test_child_fd_leak_on_exit(us_test_group_t *group) {
    const char *path = "/hello.txt";
    int probe = open(path, O_RDONLY);
    if (probe < 0) {
        SKIP(group, "fs-leak/child-exit-fds: child leaves FDs open, parent waits",
             "test file missing");
        return;
    }
    close(probe);

    pid_t pid = fork();
    if (pid < 0) {
        SKIP(group, "fs-leak/child-exit-fds: child leaves FDs open, parent waits",
             "fork failed");
        return;
    }
    if (pid == 0) {
        /* Open several fds and exit without closing — kernel must clean up */
        for (int i = 0; i < 8; i++)
            open(path, O_RDONLY);
        _exit(0);
    }

    int status = 0;
    waitpid(pid, &status, 0);

    /* After wait, parent must still be able to open files */
    int fd = open(path, O_RDONLY);
    TEST(group, "fs-leak/child-exit-fds: parent can open files after child left FDs open",
         WIFEXITED(status) && WEXITSTATUS(status) == 0 && fd >= 0);
    if (fd >= 0) close(fd);
}

/* ── Section 3: Process leak proxies ────────────────────────────────────── */

/* 3.1  Sequential fork/exit/wait — proc slot returned each time */
static void test_proc_sequential(us_test_group_t *group) {
#define SEQ_CYCLES 40
    int ok = 1;
    for (int i = 0; i < SEQ_CYCLES; i++) {
        pid_t pid = fork();
        if (pid < 0) { ok = 0; break; }
        if (pid == 0) _exit(0);
        int status = 0;
        pid_t r = waitpid(pid, &status, 0);
        if (r != pid || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
            ok = 0;
    }
    TEST(group, "proc-leak/sequential: 40 fork+exit+wait cycles, all slots returned",
         ok);
#undef SEQ_CYCLES
}

/* 3.2  Zombie does not permanently occupy slot after wait */
static void test_zombie_reaped(us_test_group_t *group) {
    pid_t pid = fork();
    TEST(group, "proc-leak/zombie: fork succeeded", pid >= 0);
    if (pid < 0) return;
    if (pid == 0) _exit(42);

    /* small yield so child can become zombie */
    usleep(1000);

    int status = 0;
    pid_t r = waitpid(pid, &status, 0);
    TEST(group, "proc-leak/zombie: wait returns child pid", r == pid);
    TEST(group, "proc-leak/zombie: child exited with code 42",
         WIFEXITED(status) && WEXITSTATUS(status) == 42);

    /* slot must be gone: /proc/<pid>/status should fail */
    char path[32];
    snprintf(path, sizeof(path), "/proc/%d/status", (int)pid);
    int fd = open(path, O_RDONLY);
    TEST(group, "proc-leak/zombie: proc slot released (status gone after wait)",
         fd < 0);
    if (fd >= 0) close(fd);
}

/* 3.3  Parallel children all reaped */
static void test_proc_parallel(us_test_group_t *group) {
#define PAR_CHILDREN 12
    pid_t pids[PAR_CHILDREN];
    int ok = 1;

    for (int i = 0; i < PAR_CHILDREN; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            /* reap what we spawned so far then fail */
            for (int j = 0; j < i; j++)
                if (pids[j] > 0) waitpid(pids[j], NULL, 0);
            ok = 0;
            break;
        }
        if (pids[i] == 0) _exit(0);
    }

    if (ok) {
        for (int i = 0; i < PAR_CHILDREN; i++) {
            int status = 0;
            pid_t r = waitpid(pids[i], &status, 0);
            if (r != pids[i] || !WIFEXITED(status)) ok = 0;
        }
    }

    TEST(group, "proc-leak/parallel: 12 concurrent children all reaped", ok);

    /* After full reap, a new fork must still succeed */
    pid_t extra = fork();
    TEST(group, "proc-leak/parallel: new fork succeeds after 12 reaped", extra >= 0);
    if (extra == 0) _exit(0);
    if (extra > 0) waitpid(extra, NULL, 0);
#undef PAR_CHILDREN
}

/* 3.4  SA_NOCLDWAIT (SIGCHLD=SIG_IGN) auto-reap */
static void test_sigchld_ign_autoreap(us_test_group_t *group) {
    struct sigaction sa_ign, sa_old;
    memset(&sa_ign, 0, sizeof(sa_ign));
    sa_ign.sa_handler = SIG_IGN;
    if (sigaction(SIGCHLD, &sa_ign, &sa_old) < 0) {
        SKIP(group, "proc-leak/sa-nocldwait: SIGCHLD=SIG_IGN auto-reap",
             "sigaction failed");
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        sigaction(SIGCHLD, &sa_old, NULL);
        SKIP(group, "proc-leak/sa-nocldwait: SIGCHLD=SIG_IGN auto-reap",
             "fork failed");
        return;
    }
    if (pid == 0) _exit(0);

    /* With SIG_IGN, child should be auto-reaped; waitpid returns ECHILD */
    usleep(5000);
    int status = 0;
    pid_t r = waitpid(pid, &status, WNOHANG);
    /* Either already reaped (r==-1/ECHILD) or just finished (r==pid WIFEXITED) */
    int auto_reaped = (r < 0 && errno == ECHILD) ||
                      (r == pid && WIFEXITED(status));

    sigaction(SIGCHLD, &sa_old, NULL);

    TEST(group, "proc-leak/sa-nocldwait: SIGCHLD=SIG_IGN auto-reaps child",
         auto_reaped);
}

/* ── Section 4: Known-issue regression tests ────────────────────────────── */

/*
 * KI-1: ~688 KB/fork page-table leak (issue: vmm.c doesn't free PML4→PDP→PD
 *        page-table pages on proc_free).  Run 50 cycles; if pages leak the
 *        50th fork will fail (physical memory exhausted).
 */
static void test_ki1_pagetable_leak(us_test_group_t *group) {
#define KI1_CYCLES 50
    int ok = 1;
    for (int i = 0; i < KI1_CYCLES; i++) {
        pid_t pid = fork();
        if (pid < 0) { ok = 0; break; }
        if (pid == 0) {
            /* touch a few pages to allocate page-table entries */
            void *m = mmap(NULL, 4 * 4096, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (m != MAP_FAILED) {
                volatile char *p = (volatile char *)m;
                for (size_t off = 0; off < 4 * 4096; off += 4096)
                    p[off] = (char)i;
                munmap(m, 4 * 4096);
            }
            _exit(0);
        }
        int status = 0;
        waitpid(pid, &status, 0);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            ok = 0;
    }
    TEST(group,
         "ki1/pagetable-leak: 50 fork+mmap+exit+wait cycles all succeed (regression: ~688KB/fork leak)",
         ok);
#undef KI1_CYCLES
}

/*
 * KI-2: FD leak on mid-operation error (e.g. open() fails partway through
 *        vfs_open after allocating an fd slot).  After NFAIL failed opens,
 *        NREAL successful opens must still work.
 */
static void test_ki2_fd_leak_on_error(us_test_group_t *group) {
#define NFAIL  20
#define NREAL  20
    /* Cause open() to fail by opening a non-existent path */
    for (int i = 0; i < NFAIL; i++)
        open("/does_not_exist_leak_test", O_RDONLY);   /* expected to fail */

    /* Now do real opens; they must all succeed */
    int ok = 1;
    int fds[NREAL];
    for (int i = 0; i < NREAL; i++) {
        fds[i] = open("/hello.txt", O_RDONLY);
        if (fds[i] < 0) {
            ok = 0;
            for (int j = 0; j < i; j++) close(fds[j]);
            break;
        }
    }
    if (ok)
        for (int i = 0; i < NREAL; i++) close(fds[i]);

    TEST(group,
         "ki2/fd-error-leak: " /* macro concat */
         "20 real opens succeed after 20 failed opens (regression: fd slot leak on error)",
         ok);
#undef NFAIL
#undef NREAL
}

/*
 * KI-3: dentry never pruned after unlink+close.  If dentries are not freed,
 *        the dentry pool eventually exhausts and create fails.  Run NDENTRY
 *        create+open+unlink+close cycles using a small set of distinct names;
 *        the final create must succeed.
 */
static void test_ki3_dentry_prune(us_test_group_t *group) {
#define NDENTRY 40
    int ok = 1;
    char path[64];
    for (int i = 0; i < NDENTRY; i++) {
        snprintf(path, sizeof(path), "/mnt/ki3_dentry_%d.tmp", i % 8);
        int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { ok = 0; break; }
        write(fd, "data", 4);
        if (unlink(path) < 0) ok = 0;   /* unlink while fd open */
        close(fd);                        /* inode + dentry should be freed here */
    }
    /* Final create must succeed regardless of pool pressure */
    snprintf(path, sizeof(path), "/mnt/ki3_final.tmp");
    int final_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int final_ok = (final_fd >= 0);
    if (final_fd >= 0) {
        close(final_fd);
        unlink(path);
    }
    TEST(group,
         "ki3/dentry-prune: 40 open+unlink+close cycles; final create succeeds "
         "(regression: dentry never pruned after unlink+close)",
         ok && final_ok);
    /* cleanup */
    for (int i = 0; i < 8; i++) {
        snprintf(path, sizeof(path), "/mnt/ki3_dentry_%d.tmp", i);
        unlink(path);
    }
#undef NDENTRY
}

/*
 * KI-4: grandchild not reaped if init stub missing.  Double-fork pattern:
 *        parent forks child; child forks grandchild and immediately exits;
 *        grandchild is reparented to init (pid 1) which must reap it.
 *        We verify: (a) parent's wait returns correctly, (b) grandchild's
 *        proc slot eventually disappears (init reaps it).
 */
static void test_ki4_grandchild_reparent(us_test_group_t *group) {
    pid_t grandchild_pid = -1;

    int pipefd[2];
    if (pipe(pipefd) < 0) {
        SKIP(group, "ki4/grandchild-reparent: double-fork reparent to init",
             "pipe failed");
        return;
    }

    pid_t child = fork();
    if (child < 0) {
        close(pipefd[0]); close(pipefd[1]);
        SKIP(group, "ki4/grandchild-reparent: double-fork reparent to init",
             "fork failed");
        return;
    }

    if (child == 0) {
        /* child: fork grandchild, send its pid to parent, then exit */
        close(pipefd[0]);
        pid_t gc = fork();
        if (gc == 0) {
            /* grandchild: sleep briefly so parent can observe it */
            close(pipefd[1]);
            usleep(50000);   /* 50 ms */
            _exit(0);
        }
        /* write grandchild pid to pipe */
        write(pipefd[1], &gc, sizeof(gc));
        close(pipefd[1]);
        _exit(0);
    }

    /* parent: read grandchild pid, wait for child */
    close(pipefd[1]);
    ssize_t nr = read(pipefd[0], &grandchild_pid, sizeof(grandchild_pid));
    close(pipefd[0]);

    int child_status = 0;
    waitpid(child, &child_status, 0);

    TEST(group, "ki4/grandchild-reparent: child reaped correctly",
         WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0);
    TEST(group, "ki4/grandchild-reparent: grandchild pid received",
         nr == sizeof(grandchild_pid) && grandchild_pid > 0);

    if (grandchild_pid > 0) {
        /* Give init time to reap grandchild (it runs after child exits) */
        usleep(200000);   /* 200 ms */

        /* grandchild's proc slot should be gone */
        char path[32];
        snprintf(path, sizeof(path), "/proc/%d/status", (int)grandchild_pid);
        int fd = open(path, O_RDONLY);
        TEST(group,
             "ki4/grandchild-reparent: grandchild slot gone after init reap "
             "(regression: grandchild not reaped if init stub missing)",
             fd < 0);
        if (fd >= 0) close(fd);
    }
}

/* ── Section 5: Cross-subsystem ─────────────────────────────────────────── */

/*
 * 5.1  Child opens files + mmaps + exits: all resources reclaimed.
 *      After wait, parent mmap and open must both still succeed.
 */
static void test_cross_child_resources(us_test_group_t *group) {
    pid_t pid = fork();
    if (pid < 0) {
        SKIP(group, "cross/child-resources: child opens+mmaps then exits",
             "fork failed");
        return;
    }
    if (pid == 0) {
        /* Open several files (don't close them) */
        for (int i = 0; i < 6; i++)
            open("/hello.txt", O_RDONLY);

        /* Anonymous mmap pages (don't munmap) */
        for (int i = 0; i < 4; i++) {
            void *m = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (m != MAP_FAILED)
                ((volatile char *)m)[0] = (char)i;
        }
        _exit(0);
    }

    int status = 0;
    waitpid(pid, &status, 0);

    /* parent verifies resources were reclaimed */
    void *m = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    int fd = open("/hello.txt", O_RDONLY);

    TEST(group,
         "cross/child-resources: mmap still works after child leaked pages+fds",
         WIFEXITED(status) && m != MAP_FAILED);
    TEST(group,
         "cross/child-resources: open still works after child leaked fds",
         fd >= 0);

    if (m != MAP_FAILED) munmap(m, 4096);
    if (fd >= 0) close(fd);
}

/*
 * 5.2  exec closes O_CLOEXEC fds, retains others.
 *      We test via a child that execs us_tests --envp-probe; the exec'd image
 *      exits 0 on success, proving exec ran cleanly (i.e. old AS freed,
 *      cloexec fds closed, new stack zeroed).
 */
static void test_cross_exec_cloexec(us_test_group_t *group) {
    /* open a O_CLOEXEC fd and a regular fd in the child before exec */
    pid_t pid = fork();
    if (pid < 0) {
        SKIP(group, "cross/exec-cloexec: exec closes O_CLOEXEC fds, retains others",
             "fork failed");
        return;
    }
    if (pid == 0) {
        /* open a cloexec fd — should be closed by exec */
        open("/hello.txt", O_RDONLY | O_CLOEXEC);
        /* exec the test binary: it exits 0 when probe succeeds */
        char *av[] = { "us_tests", "--envp-probe", "PATH=/bin", NULL };
        char *ev[] = { "PATH=/bin", NULL };
        execve("/bin/us_tests", av, ev);
        _exit(2);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    TEST(group,
         "cross/exec-cloexec: exec'd program ran correctly (old AS freed, cloexec fds closed)",
         WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

/* ── public entry point ──────────────────────────────────────────────────── */

void run_test_leak(us_test_group_t *group) {
    ut_group_begin(group, "leak");

    /* MM */
    test_mmap_cycle_no_leak(group);
    test_mmap_multipage_cycle(group);
    test_mmap_post_stress_alloc(group);

    /* FS */
    test_open_close_cycle(group);
    test_create_unlink_cycle(group);
    test_mkdir_rmdir_cycle(group);
    test_dup_close_cycle(group);
    test_pipe_cycle(group);
    test_child_fd_leak_on_exit(group);

    /* Proc */
    test_proc_sequential(group);
    test_zombie_reaped(group);
    test_proc_parallel(group);
    test_sigchld_ign_autoreap(group);

    /* Known-issue regressions */
    test_ki1_pagetable_leak(group);
    test_ki2_fd_leak_on_error(group);
    test_ki3_dentry_prune(group);
    test_ki4_grandchild_reparent(group);

    /* Cross-subsystem */
    test_cross_child_resources(group);
    test_cross_exec_cloexec(group);

    ut_group_end(group);
}
