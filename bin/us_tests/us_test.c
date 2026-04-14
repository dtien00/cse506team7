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
#include "us_test_common.h"

void run_test_printf(us_test_group_t *group);
void run_test_string(us_test_group_t *group);
void run_test_ctype(us_test_group_t *group);
void run_test_stdlib(us_test_group_t *group);
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
void run_test_stack_growth(us_test_group_t *group);
void run_test_dirent(us_test_group_t *group);
void run_test_getrusage(us_test_group_t *group);
void run_test_nice(us_test_group_t *group);
void run_test_nice_extended(us_test_group_t *group);
void run_test_yield(us_test_group_t *group);
void run_test_pause(us_test_group_t *group);
void run_test_procinfo(us_test_group_t *group);
void run_test_gettimeofday(us_test_group_t *group);
void run_test_time(us_test_group_t *group);
void run_test_termios_tty(us_test_group_t *group);

/* comprehensive libc conformance suites (each fills multiple groups) */
void run_test_libc_string(us_test_group_t groups[], int *idx);
void run_test_libc_ctype(us_test_group_t groups[], int *idx);
void run_test_libc_stdio(us_test_group_t groups[], int *idx);
void run_test_libc_stdlib(us_test_group_t groups[], int *idx);

/* base groups + 6 fs_persist sub-groups + libc conformance suites */
#define MAX_GROUPS 200

int main(void) {
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

    run_test_printf(&groups[idx++]);
    run_test_string(&groups[idx++]);
    run_test_ctype(&groups[idx++]);
    run_test_stdlib(&groups[idx++]);
    run_test_malloc(&groups[idx++]);
    run_test_file_io(&groups[idx++]);
    run_test_process(&groups[idx++]);
    run_test_mv_cp(&groups[idx++]);
    run_test_write(&groups[idx++]);
    run_test_exec_bin(&groups[idx++]);
    run_test_fs_persist(groups, &idx);
    run_test_syscall(&groups[idx++]);
    run_test_cow(&groups[idx++]);
    run_test_sched(&groups[idx++]);
    run_test_signal(&groups[idx++]);
    run_test_dirent(&groups[idx++]);
    run_test_termios_tty(&groups[idx++]);
    run_test_stack_growth(&groups[idx++]);
    run_test_getrusage(&groups[idx++]);
    run_test_nice(&groups[idx++]);
    run_test_nice_extended(&groups[idx++]);
    run_test_yield(&groups[idx++]);
    run_test_pause(&groups[idx++]);
    run_test_procinfo(&groups[idx++]);
    run_test_gettimeofday(&groups[idx++]);
    run_test_time(&groups[idx++]);

    /* comprehensive libc conformance tests */
    run_test_libc_string(groups, &idx);
    run_test_libc_ctype(groups, &idx);
    run_test_libc_stdio(groups, &idx);
    run_test_libc_stdlib(groups, &idx);

    return ut_suite_end(groups, idx);
}
