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
 *
 * Tests that depend on unimplemented syscalls (lseek, stat, getdents64, etc.)
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
void run_test_write(us_test_group_t *group);
void run_test_syscall(us_test_group_t *group);
void run_test_cow(us_test_group_t *group);
void run_test_sched(us_test_group_t *group);
void run_test_signal(us_test_group_t *group);
void run_test_stack_growth(us_test_group_t *group);
void run_test_getrusage(us_test_group_t *group);
void run_test_nice(us_test_group_t *group);
void run_test_nice_extended(us_test_group_t *group);
void run_test_yield(us_test_group_t *group);
void run_test_pause(us_test_group_t *group);


int main(void) {
    us_test_group_t groups[] = {
        {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}
    };

    ut_suite_begin("SBUnix Userspace Test Suite");

    run_test_printf(&groups[0]);
    run_test_string(&groups[1]);
    run_test_ctype(&groups[2]);
    run_test_stdlib(&groups[3]);
    run_test_malloc(&groups[4]);
    run_test_file_io(&groups[5]);
    run_test_process(&groups[6]);
    run_test_write(&groups[7]);
    run_test_syscall(&groups[8]);
    run_test_cow(&groups[9]);
    run_test_sched(&groups[10]);
    run_test_signal(&groups[11]);
    run_test_stack_growth(&groups[12]);
    run_test_getrusage(&groups[13]);
    run_test_nice(&groups[14]);
    run_test_nice_extended(&groups[15]);
    run_test_yield(&groups[16]);
    run_test_pause(&groups[17]);

    return ut_suite_end(groups, (int)(sizeof(groups) / sizeof(groups[0])));
}
