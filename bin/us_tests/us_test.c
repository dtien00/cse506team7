/*
 * libc_test.c — Userspace libc correctness tests
 *
 * Runs entirely in userspace via syscalls.  Each test group is independent
 * and reports [PASS]/[FAIL] to stdout so results are visible on the console.
 *
 * Tests are written only for functionality that the current kernel supports:
 *   - printf / putchar / puts / sprintf / snprintf
 *   - string.h  (no kernel dependency)
 *   - ctype.h   (no kernel dependency)
 *   - stdlib.h  (atoi, strtol, qsort, etc.)
 *   - malloc / free / calloc / realloc  (via mmap syscall)
 *   - open / read / close               (tarfs, read-only)
 *   - write to stdout (fd 1)
 *   - fork / wait / getpid
 *   - exit
 *   - persistent fs (create/write/read/mkdir/lseek/sync on /mnt)
 *
 * Tests that depend on unimplemented syscalls (stat, getdents64, etc.)
 * are clearly marked SKIP so it's easy to enable them later.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "us_test_common.h"

void run_test_printf(us_test_group_t *group);
void run_test_string(us_test_group_t *group);
void run_test_ctype(us_test_group_t *group);
void run_test_stdlib(us_test_group_t *group);
void run_test_sbrk_brk(us_test_group_t *group);
void run_test_malloc(us_test_group_t *group);
void run_test_file_io(us_test_group_t *group);
void run_test_process(us_test_group_t *group);
void run_test_mv_cp(us_test_group_t *group);
void run_test_write(us_test_group_t *group);
void run_test_exec_bin(us_test_group_t *group);
/* fs_persist fills multiple groups (one per sub-test) */
void run_test_fs_persist(us_test_group_t groups[], int *count);
void run_test_syscall(us_test_group_t *group);
void run_test_cow(us_test_group_t *group);
void run_test_sched(us_test_group_t *group);
void run_test_signal(us_test_group_t *group);
void run_test_signal_faults(us_test_group_t *group);
void run_test_signals2(us_test_group_t *group);
void run_test_stack_growth(us_test_group_t *group);
void run_test_dirent(us_test_group_t *group);
void run_test_getrusage(us_test_group_t *group);
void run_test_nice(us_test_group_t *group);
void run_test_nice_extended(us_test_group_t *group);
void run_test_yield(us_test_group_t *group);
void run_test_pause(us_test_group_t *group);
void run_test_procinfo(us_test_group_t *group);
void run_test_procfs(us_test_group_t *group);
void run_test_gettimeofday(us_test_group_t *group);
void run_test_alarm(us_test_group_t *group);
void run_test_sigaction(us_test_group_t *group);
void run_test_sigsuspend(us_test_group_t *group);
void run_test_time(us_test_group_t *group);
void run_test_termios_tty(us_test_group_t *group);
void run_test_pgrp(us_test_group_t *group);
void run_test_jobctl(us_test_group_t *group);
void run_test_links(us_test_group_t groups[], int *idx);
void run_test_pipe_sigpipe(us_test_group_t *group);
void run_test_nanosleep(us_test_group_t *group);
void run_test_xv6_compat(us_test_group_t *group);
void run_test_fd_limits(us_test_group_t *group);
void run_test_bigfile(us_test_group_t *group);
void run_test_forkstress(us_test_group_t *group);
void run_test_dirops(us_test_group_t *group);
void run_test_lazy_alloc(us_test_group_t *group);
void run_test_demand_heap(us_test_group_t *group);
void run_test_inode_openunlink(us_test_group_t *group);
void run_test_syscall_robustness(us_test_group_t *group);
void run_test_fd_offset(us_test_group_t *group);
void run_test_forkforkfork(us_test_group_t *group);
void run_test_writebig(us_test_group_t *group);
void run_test_uaccess(us_test_group_t *group);
void run_test_mmap_file(us_test_group_t *group);
void run_test_errno(us_test_group_t *group);
void run_test_cloexec(us_test_group_t *group);

/* comprehensive libc conformance suites (each fills multiple groups) */
void run_test_libc_string(us_test_group_t groups[], int *idx);
void run_test_libc_ctype(us_test_group_t groups[], int *idx);
void run_test_libc_stdio(us_test_group_t groups[], int *idx);
void run_test_libc_stdlib(us_test_group_t groups[], int *idx);
void run_test_exec_envp(us_test_group_t groups[], int *idx);
void run_test_libc_time(us_test_group_t groups[], int *idx);

/* base groups + 6 fs_persist sub-groups + libc conformance suites */
#define MAX_GROUPS 250

/*
 * ── Physical-memory / fork-pressure toggles ──────────────────────────────────
 *
 * Set either flag to 0 to skip the corresponding class of tests.  Useful when
 * other tests under development also allocate heavily and exhaust physical pages
 * before the test of interest is reached.
 *
 * RUN_ALL_TESTS   – run every test group, including heavy/stress tests that are
 *                   normally commented out.
 *
 * RUN_FORK_HEAVY  – tests that spawn many child processes (each fork duplicates
 *                   COW page-table entries and can trigger page allocation):
 *                   cow, process, exec_bin, exec_envp, sched, signal,
 *                   signal_faults, signals2, getrusage, pgrp, jobctl
 *
 * RUN_MEMORY_HEAVY – tests that make large heap / anonymous-mmap allocations:
 *                   malloc, lazy_alloc (8 MB mmap, 4 MB sbrk regions)
 */
static const int RUN_ALL_TESTS   = 1;
static const int RUN_FORK_HEAVY  = RUN_ALL_TESTS && 0;
static const int RUN_MEMORY_HEAVY = RUN_ALL_TESTS && 0;
static const int RUN_STRESS_TESTS = RUN_ALL_TESTS || 1;

int main(int argc, char **argv, char **envp) {
    /*
     * Probe modes for execve(envp) tests.  Must run before any test
     * infrastructure so the child process exits cleanly with a simple code.
     *
     *   --envp-probe KEY=VALUE   exit 0 if getenv(KEY) == VALUE
     *   --envp-empty             exit 0 if getenv("SBX_SENTINEL") == NULL
     */
    if (argc >= 2 && strcmp(argv[1], "--envp-probe") == 0) {
        if (argc < 3) return 2;
        /* argv[2] is "KEY=VALUE"; split at '=' to get key and expected value */
        char *eq = strchr(argv[2], '=');
        if (!eq) return 2;
        *eq = '\0';
        const char *key = argv[2];
        const char *expected = eq + 1;
        char *got = getenv(key);
        return (got && strcmp(got, expected) == 0) ? 0 : 3;
    }
    if (argc >= 2 && strcmp(argv[1], "--envp-empty") == 0) {
        return (getenv("SBX_SENTINEL") == NULL) ? 0 : 4;
    }
    (void)envp; /* not used in normal test mode; environ is the live copy */

    us_test_group_t groups[MAX_GROUPS];
    int i;
    for (i = 0; i < MAX_GROUPS; i++) {
        groups[i].name    = 0;
        groups[i].passed  = 0;
        groups[i].failed  = 0;
        groups[i].skipped = 0;
    }

    ut_suite_begin("SBUnix Userspace Test Suite");
    int idx = 0;

    /* ── Lightweight / no-fork tests (always run) ─────────────────────────── */

    if (RUN_ALL_TESTS) {
        run_test_printf(&groups[idx++]);
        run_test_string(&groups[idx++]);
        run_test_ctype(&groups[idx++]);
        run_test_stdlib(&groups[idx++]);
        run_test_sbrk_brk(&groups[idx++]);
        run_test_file_io(&groups[idx++]);
        run_test_mv_cp(&groups[idx++]);
        run_test_write(&groups[idx++]);
        run_test_fs_persist(groups, &idx);
        run_test_syscall(&groups[idx++]);
        run_test_errno(&groups[idx++]);
        run_test_dirent(&groups[idx++]);
        run_test_termios_tty(&groups[idx++]);
        run_test_stack_growth(&groups[idx++]);
        run_test_yield(&groups[idx++]);
        run_test_procinfo(&groups[idx++]);
        run_test_procfs(&groups[idx++]);
        run_test_gettimeofday(&groups[idx++]);
        run_test_pipe_sigpipe(&groups[idx++]);
        run_test_time(&groups[idx++]);
        run_test_nanosleep(&groups[idx++]);
    }

    /* ── RUN_FORK_HEAVY: tests that spawn multiple child processes ─────────── */
    if (RUN_FORK_HEAVY) {
        run_test_process(&groups[idx++]);
        run_test_exec_bin(&groups[idx++]);
        run_test_cow(&groups[idx++]);
        run_test_sched(&groups[idx++]);
        run_test_signal(&groups[idx++]);
        run_test_signal_faults(&groups[idx++]);
        run_test_signals2(&groups[idx++]);
        run_test_getrusage(&groups[idx++]);
        run_test_nice(&groups[idx++]);
        run_test_nice_extended(&groups[idx++]);
        run_test_pause(&groups[idx++]);
        run_test_alarm(&groups[idx++]);
        run_test_sigaction(&groups[idx++]);
        run_test_sigsuspend(&groups[idx++]);
        run_test_pgrp(&groups[idx++]);
        run_test_jobctl(&groups[idx++]);
        run_test_links(groups, &idx);
        run_test_exec_envp(groups, &idx);
        run_test_mmap_file(&groups[idx++]);
        run_test_cloexec(&groups[idx++]);
    }

    /* ── RUN_MEMORY_HEAVY: tests with large heap / mmap allocations ────────── */
    if (RUN_MEMORY_HEAVY) {
        run_test_malloc(&groups[idx++]);
        run_test_lazy_alloc(&groups[idx++]);
        run_test_demand_heap(&groups[idx++]);
    }

    /* ── Disabled stress / destructive tests (uncomment individually) ───────── */
    if (RUN_STRESS_TESTS) {
        run_test_xv6_compat(&groups[idx++]);
        run_test_fd_limits(&groups[idx++]);
        // run_test_bigfile(&groups[idx++]);
        // run_test_forkstress(&groups[idx++]);
        // run_test_dirops(&groups[idx++]);
        // run_test_inode_openunlink(&groups[idx++]);
        // run_test_syscall_robustness(&groups[idx++]);
        // run_test_fd_offset(&groups[idx++]);
        // run_test_forkforkfork(&groups[idx++]);
        // run_test_writebig(&groups[idx++]); /* fills disk */
    }

    /* ── Comprehensive libc conformance tests (always run, no forks) ────────── */
    if (RUN_ALL_TESTS) {
        run_test_libc_string(groups, &idx);
        run_test_libc_ctype(groups, &idx);
        run_test_libc_stdio(groups, &idx);
        run_test_libc_stdlib(groups, &idx);
        run_test_libc_time(groups, &idx);
    }

    return ut_suite_end(groups, idx);
}
