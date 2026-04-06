/*
 * cow_test.c — Kernel-level unit tests for Copy-on-Write fork.
 *
 * Runs in RUN_TESTS mode before the scheduler starts.  Tests three layers:
 *   1. PMM refcount primitives (incref / decref / refcount)
 *   2. vmm_copy_pagetable PTE flags (COW bit, write-protect, shared PA)
 *   3. vmm_free_pagetable refcount interaction (leaf pages decremented)
 */

#include "mm/pmm.h"
#include "mm/vmm.h"
#include "arch/riscv.h"
#include "kernel/printk.h"
#include "tests/test.h"
#include "lib/string.h"

static int passed = 0;
static int failed = 0;

/* ── 1. PMM refcount primitives ─────────────────────────────────────────── */

static void test_pmm_refcount(void) {
    printk("=== COW: PMM refcount primitives ===\n");

    void *page = pmm_alloc_page();
    TEST("pmm_alloc_page succeeds", page != NULL);
    if (!page) return;

    TEST("initial refcount == 1", pmm_refcount(page) == 1);

    pmm_incref(page);
    TEST("after incref refcount == 2", pmm_refcount(page) == 2);

    pmm_incref(page);
    TEST("after second incref refcount == 3", pmm_refcount(page) == 3);

    size_t free_before = pmm_free_memory();
    pmm_decref(page);
    TEST("decref from 3: page not freed yet", pmm_free_memory() == free_before);
    TEST("after decref refcount == 2", pmm_refcount(page) == 2);

    pmm_decref(page);
    TEST("decref from 2: page not freed yet", pmm_free_memory() == free_before);
    TEST("after second decref refcount == 1", pmm_refcount(page) == 1);

    pmm_decref(page);
    TEST("decref from 1: page returned to free pool",
         pmm_free_memory() == free_before + PGSIZE);
}

/* ── 2. vmm_copy_pagetable COW PTE flags ────────────────────────────────── */

static void test_cow_pte_flags(void) {
    printk("=== COW: PTE flags after vmm_copy_pagetable ===\n");

    /* Build a minimal parent page table with one writable user page. */
    pagetable_t parent = vmm_create_pagetable();
    TEST("parent pagetable created", parent != NULL);
    if (!parent) return;

    void *data = pmm_alloc_page();
    TEST("data page allocated", data != NULL);
    if (!data) { vmm_free_pagetable(parent); return; }

    /* Write a sentinel value so we can verify the child sees it. */
    memset(data, 0xAB, PGSIZE);

    uintptr_t test_va = 0x1000;
    uint64_t perm = PTE_V | PTE_R | PTE_W | PTE_U;
    int r = vmm_map(parent, test_va, (uintptr_t)data, perm);
    TEST("vmm_map writable user page succeeds", r == 0);
    if (r != 0) { pmm_free_page(data); vmm_free_pagetable(parent); return; }

    TEST("data page refcount == 1 before fork", pmm_refcount(data) == 1);

    /* Fork: copy parent to child. */
    pagetable_t child = vmm_copy_pagetable(parent);
    TEST("vmm_copy_pagetable succeeds", child != NULL);
    if (!child) { vmm_free_pagetable(parent); return; }

    /* After COW copy, refcount must be 2 (shared). */
    TEST("data page refcount == 2 after fork", pmm_refcount(data) == 2);

    /* Both parent and child must map to the same physical address. */
    uintptr_t parent_pa = vmm_translate(parent, test_va);
    uintptr_t child_pa  = vmm_translate(child,  test_va);
    TEST("parent maps to original physical page", parent_pa == (uintptr_t)data);
    TEST("child maps to same physical page (shared)", child_pa == (uintptr_t)data);

    /* Both PTEs must have PTE_COW set and PTE_W cleared. */
    uint64_t *parent_pte = vmm_walk(parent, test_va, 0);
    uint64_t *child_pte  = vmm_walk(child,  test_va, 0);
    TEST("parent_pte pointer valid", parent_pte != NULL);
    TEST("child_pte pointer valid",  child_pte  != NULL);

    if (parent_pte && child_pte) {
        TEST("parent PTE: PTE_W cleared",  !(*parent_pte & PTE_W));
        TEST("parent PTE: PTE_COW set",     (*parent_pte & PTE_COW));
        TEST("child  PTE: PTE_W cleared",  !(*child_pte  & PTE_W));
        TEST("child  PTE: PTE_COW set",     (*child_pte  & PTE_COW));
        TEST("parent and child PTEs identical", *parent_pte == *child_pte);
    }

    /* ── 3. vmm_free_pagetable decrements shared page refcount ───────────── */
    printk("=== COW: vmm_free_pagetable decrements leaf refcounts ===\n");

    /* Free the child page table; data page refcount must drop to 1. */
    vmm_free_pagetable(child);
    TEST("data page refcount == 1 after child freed", pmm_refcount(data) == 1);

    /* Free the parent page table; data page must be returned to free pool. */
    size_t free_before = pmm_free_memory();
    vmm_free_pagetable(parent);
    TEST("data page freed after parent freed",
         pmm_free_memory() >= free_before + PGSIZE);
}

/* ── entry point ─────────────────────────────────────────────────────────── */

void cow_test_run(void) {
    printk("\n========================================\n");
    printk("       Copy-on-Write Fork Tests\n");
    printk("========================================\n\n");

    test_pmm_refcount();
    test_cow_pte_flags();

    printk("\nCOW tests: %d passed, %d failed\n\n", passed, failed);
}
