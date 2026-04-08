/**
 * stack_growth_test.c — Kernel unit tests for VMA_GROWSDOWN / stack growth
 *
 * Tests vma_find_growsdown() in isolation using a synthetic VMA list.
 * No real page faults are triggered — this is purely a logic test that
 * runs at boot time before the scheduler starts.
 *
 * Design note: vma_find_growsdown() only checks whether addr is below a
 * GROWSDOWN VMA's current start.  The maximum-stack-size (USTACK_SIZE)
 * overflow guard lives in page_fault_handler() in trap.c, not here.
 *
 * Call stack_growth_test_run() from run_all_tests() in kernel/core/kernel.c.
 */

#include "kernel/printk.h"
#include "kernel/types.h"
#include "mm/vma.h"
#include "arch/riscv.h"   /* KERNBASE, PGSIZE */
#include "tests/test.h"

static int passed = 0;
static int failed = 0;

void stack_growth_test_run(void) {
    printk("\n=== Stack Growth (VMA_GROWSDOWN) Tests ===\n");

    /* Build a minimal VMA list that mirrors what exec() creates after the
     * Feature 3 change: one stack VMA, one page tall, with GROWSDOWN set. */
    struct vm_area *list = NULL;
    struct vm_area *sv = vma_create(KERNBASE - PGSIZE, KERNBASE,
                                    VMA_PROT_READ | VMA_PROT_WRITE,
                                    VMA_PRIVATE | VMA_GROWSDOWN,
                                    VMA_TYPE_STACK);
    if (!sv) {
        printk("  [SKIP] vma_create returned NULL — skipping tests\n");
        return;
    }
    vma_insert(&list, sv);

    /* ── T1: Address just below the current VMA start should match ────────── */
    {
        uintptr_t fault = KERNBASE - 2 * PGSIZE;  /* one page below bottom */
        struct vm_area *res = vma_find_growsdown(list, fault);
        TEST("growsdown: addr just below VMA start matches", res == sv);
    }

    /* ── T2: Address inside the current VMA should NOT match ─────────────── *
     * vma_find() handles those; vma_find_growsdown() should ignore them.    */
    {
        uintptr_t fault = KERNBASE - PGSIZE + 4;  /* 4 bytes into bottom page */
        struct vm_area *res = vma_find_growsdown(list, fault);
        TEST("growsdown: addr inside VMA does not match", res == NULL);
    }

    /* ── T3: Address far below VMA start still matches ───────────────────── *
     * The overflow floor check (USTACK_SIZE) lives in trap.c, not here.    */
    {
        uintptr_t fault = KERNBASE - 128 * PGSIZE;  /* 128 pages below top */
        struct vm_area *res = vma_find_growsdown(list, fault);
        TEST("growsdown: addr far below VMA start matches (no floor in vma.c)",
             res == sv);
    }

    /* ── T4: Non-GROWSDOWN VMA is ignored even if addr is below it ────────── *
     * Use a separate list with ONLY a heap VMA (no GROWSDOWN flag) so that  *
     * the stack VMA (sv) in the main list does not interfere with the test.  *
     * (0xF000 < sv->start = KERNBASE-PGSIZE, so sv would match if it were   *
     *  in the same list — that is the bug this isolation avoids.)            */
    {
        struct vm_area *heap_list = NULL;
        struct vm_area *heap = vma_create(0x10000, 0x20000,
                                          VMA_PROT_READ | VMA_PROT_WRITE,
                                          VMA_PRIVATE,
                                          VMA_TYPE_HEAP);
        if (heap) {
            vma_insert(&heap_list, heap);
            struct vm_area *res = vma_find_growsdown(heap_list, 0xF000);
            TEST("growsdown: non-GROWSDOWN VMA is ignored", res == NULL);
            vma_free_all(heap_list);
        }
    }

    /* ── T5: After simulating VMA growth, the new bottom still matches ─────── *
     * Mimic what page_fault_handler() does: extend stack_vma->start down.  */
    {
        uintptr_t original_start = sv->start;
        sv->start -= PGSIZE;  /* simulate one growth step */
        uintptr_t fault = sv->start - PGSIZE;  /* fault one page below new bottom */
        struct vm_area *res = vma_find_growsdown(list, fault);
        TEST("growsdown: after growth, new bottom also extends further", res == sv);
        sv->start = original_start;  /* restore */
    }

    /* ── T6: NULL list returns NULL safely ───────────────────────────────── */
    {
        struct vm_area *res = vma_find_growsdown(NULL, KERNBASE - 2 * PGSIZE);
        TEST("growsdown: NULL vma_list returns NULL", res == NULL);
    }

    /* ── T7: Address equal to VMA start does NOT match (not below start) ─── */
    {
        uintptr_t fault = sv->start;  /* exactly at current bottom, not below */
        struct vm_area *res = vma_find_growsdown(list, fault);
        TEST("growsdown: addr equal to VMA start does not match", res == NULL);
    }

    vma_free_all(list);

    printk("Stack Growth Tests: %d passed, %d failed\n", passed, failed);
}
