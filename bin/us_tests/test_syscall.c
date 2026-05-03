#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include "us_test_common.h"

#define PAGE_SIZE 4096

void run_test_syscall(us_test_group_t *group) {
    ut_group_begin(group, "syscall");

    TEST(group, "sys/syscall.h: SYS_getpid matches ABI", SYS_getpid == 9);
    TEST(group, "sys/syscall.h: SYS_execve alias matches exec syscall", SYS_execve == SYS_exec);

    {
        pid_t mmap_child = fork();
        if (mmap_child < 0) {
            SKIP(group, "mmap-fixed: device overlap rejected, safe page zero-filled",
                 "fork failed");
        } else if (mmap_child == 0) {
            char *dev = mmap((void *)0x40000000UL, PAGE_SIZE,
                             PROT_READ | PROT_WRITE,
                             MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED, -1, 0);
            if (dev != MAP_FAILED)
                _exit(1);

            char *p = mmap((void *)0x50000000UL, PAGE_SIZE,
                           PROT_READ | PROT_WRITE,
                           MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED, -1, 0);
            if (p == MAP_FAILED)
                _exit(2);

            for (int i = 0; i < PAGE_SIZE; i++)
                if (p[i] != 0)
                    _exit(3);

            p[0] = 0x5a;
            p[PAGE_SIZE - 1] = 0x33;
            if (p[0] != 0x5a || p[PAGE_SIZE - 1] != 0x33)
                _exit(4);

            munmap(p, PAGE_SIZE);
            _exit(0);
        } else {
            int mmap_status = 0;
            waitpid(mmap_child, &mmap_status, 0);
            TEST(group, "mmap-fixed: device overlap rejected, safe page zero-filled",
                 WIFEXITED(mmap_status) && WEXITSTATUS(mmap_status) == 0);
        }
    }

    /* ── uptime ─────────────────────────────────────────────────────── */
    int t1 = uptime();
    TEST(group, "uptime: returns non-negative value", t1 >= 0);

    int t2 = uptime();
    TEST(group, "uptime: non-decreasing across two calls", t2 >= t1);

    /* ── sleep ──────────────────────────────────────────────────────── */
    int before = uptime();
    sleep(1);
    int after  = uptime();
    int delta  = after - before;

    TEST(group, "sleep(1): uptime advances by at least 1 tick", delta >= 1);
    /*
     * QEMU virt timer fires at ~10 Hz (TIMER_INTERVAL=1,000,000 at 10 MHz).
     * Accept >= 8 ticks to tolerate scheduling jitter.
     */
    TEST(group, "sleep(1): uptime advances by >= 8 ticks (~1 second)", delta >= 8);

    /* ── kill: SIGKILL terminates a sleeping child ───────────────────── */
    pid_t child = fork();
    if (child < 0) {
        SKIP(group, "kill(child,SIGKILL): returns 0",        "fork failed");
        SKIP(group, "kill: wait returns killed child's pid", "fork failed");
        SKIP(group, "kill: child reaped quickly",            "fork failed");
    } else if (child == 0) {
        sleep(30);   /* parent kills us before we ever wake */
        _exit(0);
    } else {
        int t_before = uptime();
        int kr = kill(child, SIGKILL);
        TEST(group, "kill(child,SIGKILL): returns 0", kr == 0);

        int status = 0;
        pid_t reaped = wait(&status);
        int t_after  = uptime();

        TEST(group, "kill: wait returns killed child's pid", reaped == child);

        int elapsed = t_after - t_before;
        TEST(group, "kill: child reaped quickly (< 250 ticks, not 30 s)", elapsed < 250);
    }

    /* ── kill edge cases ────────────────────────────────────────────── */
    /* kill(-1, SIGKILL) would broadcast to all processes except init and
     * caller.  When run from a shell, this kills the shell and other
     * bystanders — which is destructive and causes cascading failures.
     * Test the broadcast API with sig=0 (validity check) which probes
     * reachability without actually sending any signal. */
    {
        pid_t iso = fork();
        if (iso < 0) {
            SKIP(group, "kill(-1, 0) from lone child returns -1", "fork failed");
        } else if (iso == 0) {
            /* Become a new session + group so the parent is the only
             * "other" eligible process.  Wait for parent to be ready
             * (in waitpid) before probing — this ensures at least one
             * eligible target exists and kill(-1, 0) returns 0. */
            setsid();
            /* sig=0: merely checks for eligible targets, sends nothing */
            int r2 = kill(-1, 0);
            /* With at least our parent alive, r2 should be 0. */
            _exit(r2 == 0 ? 0 : 1);
        } else {
            int kst = 0;
            waitpid(iso, &kst, 0);
            TEST(group, "kill(-1, 0) from child: eligible targets found (parent exists)",
                 WIFEXITED(kst) && WEXITSTATUS(kst) == 0);
        }
    }

    int r = kill(getpid(), 0);
    TEST(group, "kill(getpid(), 0): returns 0",   r == 0);

    /* ── fcntl / nonblocking pipe ──────────────────────────────────── */
    {
        int pfd[2];
        if (pipe(pfd) < 0) {
            SKIP(group, "fcntl(F_GETFL): returns pipe status flags", "pipe failed");
            SKIP(group, "fcntl(F_SETFL,O_NONBLOCK): updates pipe status flags", "pipe failed");
            SKIP(group, "nonblocking pipe read: empty pipe returns -1 (errno=EAGAIN)", "pipe failed");
            SKIP(group, "fcntl(F_DUPFD): duplicates fd at or above minimum", "pipe failed");
        } else {
            errno = 0;
            int fl = fcntl(pfd[0], F_GETFL);
            TEST(group, "fcntl(F_GETFL): returns pipe status flags", fl >= 0);

            int setfl = fcntl(pfd[0], F_SETFL, fl | O_NONBLOCK);
            int nfl = fcntl(pfd[0], F_GETFL);
            TEST(group, "fcntl(F_SETFL,O_NONBLOCK): updates pipe status flags",
                 setfl == 0 && (nfl & O_NONBLOCK));

            char ch;
            errno = 0;
            int nr = read(pfd[0], &ch, 1);
            TEST(group, "nonblocking pipe read: empty pipe returns -1 (errno=EAGAIN)",
                 nr == -1 && errno == EAGAIN);

            int dupfd = fcntl(pfd[0], F_DUPFD, 5);
            TEST(group, "fcntl(F_DUPFD): duplicates fd at or above minimum", dupfd >= 5);
            if (dupfd >= 0) close(dupfd);
            close(pfd[0]);
            close(pfd[1]);
        }
    }

    /* ── termios ─────────────────────────────────────────────────────── */
    struct termios oldt;
    int tr = tcgetattr(STDIN_FILENO, &oldt);
    TEST(group, "tcgetattr(stdin): returns 0", tr == 0);

    if (tr == 0) {
        struct termios newt = oldt;
        newt.c_lflag ^= ECHO;

        int sw = tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        TEST(group, "tcsetattr(stdin,TCSANOW): returns 0", sw == 0);

        struct termios verify;
        int gv = tcgetattr(STDIN_FILENO, &verify);
        TEST(group, "tcgetattr(stdin) after set: returns 0", gv == 0);
        TEST(group, "tcsetattr updates c_lflag", gv == 0 && verify.c_lflag == newt.c_lflag);

        int restore = tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        TEST(group, "tcsetattr restore old termios: returns 0", restore == 0);

        errno = 0;
        int bad_action = tcsetattr(STDIN_FILENO, 99, &oldt);
        TEST(group, "tcsetattr invalid action: returns -1 (errno=EINVAL)",
             bad_action == -1 && errno == EINVAL);
    } else {
        SKIP(group, "tcsetattr(stdin,TCSANOW): returns 0", "tcgetattr(stdin) failed");
        SKIP(group, "tcgetattr(stdin) after set: returns 0", "tcgetattr(stdin) failed");
        SKIP(group, "tcsetattr updates c_lflag", "tcgetattr(stdin) failed");
        SKIP(group, "tcsetattr restore old termios: returns 0", "tcgetattr(stdin) failed");
        SKIP(group, "tcsetattr invalid action: returns -1 (errno=EINVAL)", "tcgetattr(stdin) failed");
    }

    int regfd = open("/hello.txt", O_RDONLY);
    if (regfd < 0) {
        SKIP(group, "tcgetattr(regular file): returns -1 (errno=ENOTTY)", "open(/hello.txt) failed");
    } else {
        struct termios t;
        errno = 0;
        int notty = tcgetattr(regfd, &t);
        TEST(group, "tcgetattr(regular file): returns -1 (errno=ENOTTY)",
             notty == -1 && errno == ENOTTY);
        close(regfd);
    }

    ut_group_end(group);
}
