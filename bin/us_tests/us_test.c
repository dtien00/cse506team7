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
void run_test_write(us_test_group_t *group);
/* fs_persist fills multiple groups (one per sub-test) */
void run_test_fs_persist(us_test_group_t groups[], int *count);

/* base groups + 6 fs_persist sub-groups */
#define MAX_GROUPS 16

int main(void) {
    us_test_group_t groups[MAX_GROUPS];
    int i;
    for (i = 0; i < MAX_GROUPS; i++) {
        groups[i].name    = 0;
        groups[i].passed  = 0;
        groups[i].failed  = 0;
        groups[i].skipped = 0;
    }

    ut_suite_begin("libc Userspace Test Suite");

    int idx = 0;
    run_test_printf (&groups[idx++]);
    run_test_string (&groups[idx++]);
    run_test_ctype  (&groups[idx++]);
    run_test_stdlib (&groups[idx++]);
    run_test_malloc (&groups[idx++]);
    run_test_file_io(&groups[idx++]);
    run_test_process(&groups[idx++]);
    run_test_write  (&groups[idx++]);
    run_test_fs_persist(groups, &idx);   /* appends 6 groups */

    return ut_suite_end(groups, idx);
}
