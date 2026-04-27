/*
 * test_demand_heap.c — Demand-paging heap stress test.
 *
 * Acceptance criterion for lazy sys_brk (MM-team B1):
 *   Before B1: child is killed OOM (physical frames exhausted).
 *   After  B1: child exits 0   (COW pages allocated on demand per write).
 *
 * Steps:
 *   1. Grow heap by HEAP_MB via sbrk.
 *   2. Touch every page from the parent (force eager mappings pre-fork).
 *   3. Fork.
 *   4. Child writes to every inherited page (triggers one COW fault per page).
 *   5. Parent waits; reports child exit status.
 */

#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include "us_test_common.h"

#define HEAP_MB   16
#define PAGE_SIZE 4096

static void test_cow_heap_stress(us_test_group_t *group) {
    size_t total = (size_t)HEAP_MB * 1024 * 1024;

    char *heap = (char *)sbrk(0);
    int sbrk_ok = (sbrk((intptr_t)total) != (void *)-1);
    TEST(group, "demand-heap: sbrk 16 MB succeeded", sbrk_ok);
    if (!sbrk_ok) return;

    /* Touch every page from the parent to force pre-fork mappings. */
    for (size_t i = 0; i < total; i += PAGE_SIZE)
        heap[i] = (char)(i & 0xFF);

    pid_t pid = fork();
    if (pid == 0) {
        /* Child: write to every page — each write is a COW fault. */
        for (size_t i = 0; i < total; i += PAGE_SIZE)
            heap[i] = ~heap[i];
        _exit(0);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    TEST(group, "demand-heap: child wrote 16 MB of COW heap and exited 0",
         WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

void run_test_demand_heap(us_test_group_t *group) {
    ut_group_begin(group, "demand_heap");
    test_cow_heap_stress(group);
    ut_group_end(group);
}
