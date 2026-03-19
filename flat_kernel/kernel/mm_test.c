/**
 * mem_test.c — Memory subsystem integration tests
 *
 * Tests PMM, kmalloc, and VMM in dependency order.
 * Drop this in kernel/test/ and call mem_test_run() from your init code.
 *
 * Assumes:
 *   - pmm_init() has already been called with your memory map
 *   - vmm_init() has already been called
 *   - A basic printk / panic are available
 */

#include "printk.h"
#include "types.h"
#include "pmm.h"
#include "kmalloc.h"
#include "vmm.h"
#include "test.h"
#include "riscv.h"

// #define TEST_VA 0x0000000040000000UL

/* ── helpers ─────────────────────────────────────────────────────────────── */

// Adjust these to match your actual kernel print / assert primitives
extern void printk(const char *fmt, ...);
extern void panic(const char *msg);

int passed = 0;
int failed = 0;

static void test_pmm(void) {
    printk("\n=== PMM Tests ===\n");

    size_t total  = pmm_total_memory();
    size_t before = pmm_free_memory();

    // 1. Sanity: reported totals make sense
    TEST("total memory > 0",        total > 0);
    TEST("free <= total",           before <= total);

    // 2. Single page allocation
    void *p1 = pmm_alloc_page();
    TEST("alloc_page returns non-NULL",   p1 != NULL);
    TEST("free memory decreased by 4KB", pmm_free_memory() == before - PGSIZE);

    // 3. Page is 4KB-aligned (bitmap allocators always should be)
    TEST("page is 4KB-aligned", ((uintptr_t)p1 & (PGSIZE - 1)) == 0);

    // 4. Two consecutive allocs return distinct pages
    void *p2 = pmm_alloc_page();
    TEST("two allocs return distinct pages", p1 != p2);

    // 5. Free and verify memory is reclaimed
    pmm_free_page(p1);
    pmm_free_page(p2);
    TEST("free reclaims memory", pmm_free_memory() == before);

    // 6. Multi-page contiguous allocation
    void *mp = pmm_alloc_pages(4);
    TEST("alloc_pages(4) non-NULL",        mp != NULL);
    TEST("alloc_pages(4) is 4KB-aligned",  ((uintptr_t)mp & (PGSIZE - 1)) == 0);
    TEST("free memory decreased by 16KB",  pmm_free_memory() == before - 4 * PGSIZE);

    pmm_free_pages(mp, 4);
    TEST("free_pages reclaims memory", pmm_free_memory() == before);

    // 7. Allocate every free page, then expect NULL
    size_t n_pages = before / PGSIZE;
    void **pages = (void **)pmm_alloc_pages(n_pages); // borrow a big slab for ptrs
    // If that giant alloc fails (fragmentation) just skip exhaustion test
    if (pages != NULL) {
        // Re-free it; we just wanted to confirm the allocator can handle it
        pmm_free_pages(pages, n_pages);
    }
    void *should_fail = pmm_alloc_pages(n_pages + 1); // one more than available
    TEST("over-alloc returns NULL", should_fail == NULL);
}

/* ── kmalloc tests ───────────────────────────────────────────────────────── */

static void test_kmalloc(void) {
    printk("\n=== kmalloc Tests ===\n");

    // 1. Basic alloc / free
    void *a = kmalloc(64);
    TEST("kmalloc(64) non-NULL", a != NULL);
    kfree(a);

    // 2. kzalloc zeroes memory
    uint8_t *z = (uint8_t *)kzalloc(128);
    TEST("kzalloc non-NULL", z != NULL);
    int all_zero = 1;
    for (int i = 0; i < 128; i++) {
        if (z[i] != 0) { all_zero = 0; break; }
    }
    TEST("kzalloc memory is zeroed", all_zero);
    kfree(z);

    // 3. Distinct allocations don't overlap
    uint8_t *b1 = (uint8_t *)kmalloc(256);
    uint8_t *b2 = (uint8_t *)kmalloc(256);
    TEST("two allocs non-NULL",       b1 != NULL && b2 != NULL);
    TEST("two allocs don't overlap",  b2 >= b1 + 256 || b1 >= b2 + 256);

    // 4. Write then read back (basic heap corruption check)
    for (int i = 0; i < 256; i++) b1[i] = (uint8_t)i;
    int intact = 1;
    for (int i = 0; i < 256; i++) {
        if (b1[i] != (uint8_t)i) { intact = 0; break; }
    }
    TEST("heap write/read intact after second alloc", intact);
    kfree(b1);
    kfree(b2);

    // 5. krealloc grows a buffer
    uint8_t *r = (uint8_t *)kmalloc(32);
    TEST("kmalloc(32) non-NULL", r != NULL);
    for (int i = 0; i < 32; i++) r[i] = (uint8_t)i;

    r = (uint8_t *)krealloc(r, 128);
    TEST("krealloc(128) non-NULL", r != NULL);
    int realloc_ok = 1;
    for (int i = 0; i < 32; i++) {           // first 32 bytes must survive
        if (r[i] != (uint8_t)i) { realloc_ok = 0; break; }
    }
    TEST("krealloc preserves existing data", realloc_ok);
    kfree(r);

    // 6. Many small allocs (stress the free list)
#define N_STRESS 64
    void *ptrs[N_STRESS];
    int alloc_ok = 1;
    for (int i = 0; i < N_STRESS; i++) {
        ptrs[i] = kmalloc(32 + i * 8);
        if (!ptrs[i]) { alloc_ok = 0; break; }
    }
    TEST("64 small allocs succeed", alloc_ok);
    for (int i = 0; i < N_STRESS; i++) if (ptrs[i]) kfree(ptrs[i]);

    // After freeing everything the allocator should be able to serve a
    // fresh large-ish request (basic fragmentation / coalescing check)
    void *big = kmalloc(PGSIZE);
    TEST("large alloc after mass-free succeeds (coalescing)", big != NULL);
    kfree(big);
#undef N_STRESS
}

/* ── VMM tests ───────────────────────────────────────────────────────────── */

// RISC-V Sv39 PTE permission bits — adjust if you define them elsewhere
#define PTE_V (1UL << 0)   // Valid
#define PTE_R (1UL << 1)   // Read
#define PTE_W (1UL << 2)   // Write
#define PTE_X (1UL << 3)   // Execute
#define PTE_U (1UL << 4)   // User

static void test_vmm(void) {
    printk("\n=== VMM Tests ===\n");

    // 1. Kernel page table is valid
    pagetable_t kpt = vmm_kernel_pagetable();
    TEST("kernel pagetable non-NULL", kpt != NULL);

    // 2. Create a fresh user page table
    pagetable_t upt = vmm_create_pagetable();
    TEST("create_pagetable non-NULL", upt != NULL);

    // 3. Allocate a physical page and map it at an arbitrary user VA
    void *phys = pmm_alloc_page();
    TEST("phys page for mapping non-NULL", phys != NULL);

    uintptr_t va   = 0x1000;   // first user page
    uintptr_t pa   = (uintptr_t)phys;
    uint64_t  perm = PTE_V | PTE_R | PTE_W | PTE_U;

    int map_ret = vmm_map(upt, va, pa, perm);
    TEST("vmm_map returns 0", map_ret == 0);

    // 4. Translation should resolve back to the physical address
    uintptr_t translated = vmm_translate(upt, va);
    TEST("vmm_translate returns correct PA", translated == pa);

    // 5. Walk should find a valid PTE
    uint64_t *pte = vmm_walk(upt, va, /*alloc=*/0);
    TEST("vmm_walk returns non-NULL PTE", pte != NULL);
    TEST("PTE valid bit set",             pte != NULL && (*pte & PTE_V));

    // 6. Unmapping clears the translation
    vmm_unmap(upt, va);
    uintptr_t after_unmap = vmm_translate(upt, va);
    TEST("vmm_translate returns 0 after unmap", after_unmap == 0);

    // 7. Range mapping
    void *phys2 = pmm_alloc_pages(4);
    TEST("phys pages for range map non-NULL", phys2 != NULL);

    uintptr_t range_va = 0x10000;
    int range_ret = vmm_map_range(upt, range_va, (uintptr_t)phys2,
                                  4 * PGSIZE, perm);
    TEST("vmm_map_range returns 0", range_ret == 0);

    // Each page in the range should translate correctly
    int range_ok = 1;
    for (int i = 0; i < 4; i++) {
        uintptr_t t = vmm_translate(upt, range_va + i * PGSIZE);
        if (t != (uintptr_t)phys2 + i * PGSIZE) { range_ok = 0; break; }
    }
    TEST("range: all 4 pages translate correctly", range_ok);

    vmm_unmap_range(upt, range_va, 4 * PGSIZE);

    // 8. Tear down — free the page table and physical pages
    pmm_free_page(phys);
    pmm_free_pages(phys2, 4);
    vmm_free_pagetable(upt);
    TEST("vmm_free_pagetable completes without panic", 1); // structural only
}

/**
 * mem_test_extra.c — Deeper memory correctness tests
 *
 * Complements mem_test.c with:
 *   1. PMM page isolation   — verifies two allocations never share physical memory
 *   2. PMM full write/read  — verifies every byte of an allocated page is writable
 *   3. PMM free/realloc     — verifies a freed page is reusable and still writable
 *   4. VMM hardware mapping — actually switches to the page table and dereferences a VA
 *
 * Drop in kernel/test/ alongside mem_test.c.
 * Call mem_test_extra_run() after pmm_init() and vmm_init().
 *
 * Requires:
 *   - pmm_init(), vmm_init() already called
 *   - printk(), panic() available
 */




/* Arbitrary user VA that doesn't conflict with kernel mappings */


/*  Page isolation ────────────────────────────────────────────────────
 *
 * Allocate two pages, write distinct patterns to each, then cross-check.
 * A bitmap off-by-one that hands out overlapping pages will fail here
 * immediately — the second write clobbers the first.
 */
static void test_pmm_isolation(void)
{
    printk("\n--- PMM isolation test ---\n");

    void *p1 = pmm_alloc_page();
    void *p2 = pmm_alloc_page();

    TEST("isolation: both pages non-NULL", p1 != NULL && p2 != NULL);
    if (!p1 || !p2) {
        if (p1) pmm_free_page(p1);
        if (p2) pmm_free_page(p2);
        return;
    }

    /* Write 0xAA to every byte of p1, 0xBB to every byte of p2. */
    uint8_t *b1 = (uint8_t *)p1;
    uint8_t *b2 = (uint8_t *)p2;

    for (size_t i = 0; i < PGSIZE; i++) b1[i] = 0xAA;
    for (size_t i = 0; i < PGSIZE; i++) b2[i] = 0xBB;

    /*
     * If p1 and p2 overlap at all, the 0xBB pass overwrote part of p1.
     * Any byte of p1 that isn't 0xAA reveals the overlap.
     */
    int p1_intact = 1;
    for (size_t i = 0; i < PGSIZE; i++) {
        if (b1[i] != 0xAA) { p1_intact = 0; break; }
    }
    TEST("isolation: p1 unaffected by write to p2", p1_intact);

    /* Symmetric check: p2 must be all 0xBB. */
    int p2_intact = 1;
    for (size_t i = 0; i < PGSIZE; i++) {
        if (b2[i] != 0xBB) { p2_intact = 0; break; }
    }
    TEST("isolation: p2 unaffected by write to p1", p2_intact);

    pmm_free_page(p1);
    pmm_free_page(p2);
}

/* Full page write / read-back ──────────────────────────────────────
 *
 * Write a unique value to every byte of a page, then verify each one.
 * Catches partial mappings, read-only physical regions, and hardware
 * write-protection that the allocator doesn't know about.
 *
 * Pattern: byte[i] = (i ^ 0xC3) & 0xFF  — avoids all-zeros / all-ones
 * so it's easy to distinguish "never written" from "written then zeroed".
 */
static void test_pmm_full_readback(void)
{
    printk("\n--- PMM full page read-back test ---\n");

    void *p = pmm_alloc_page();
    TEST("readback: page non-NULL", p != NULL);
    if (!p) return;

    uint8_t *b = (uint8_t *)p;

    for (size_t i = 0; i < PGSIZE; i++)
        b[i] = (uint8_t)((i ^ 0xC3) & 0xFF);

    int ok = 1;
    size_t first_bad = 0;
    for (size_t i = 0; i < PGSIZE; i++) {
        if (b[i] != (uint8_t)((i ^ 0xC3) & 0xFF)) {
            ok = 0;
            first_bad = i;
            break;
        }
    }

    TEST("readback: all 4096 bytes correct", ok);
    if (!ok)
        printk("  first mismatch at byte %zu: got 0x%02x expected 0x%02x\n",
               first_bad, b[first_bad], (uint8_t)((first_bad ^ 0xC3) & 0xFF));

    pmm_free_page(p);
}

/*
 * VMM targeted tests
 *
 * 1. test_vmm_hw_walk  — proves vmm_switch() + CPU hardware TLB walk works
 * 2. test_vmm_free     — proves vmm_free_pagetable() doesn't double-free
 *                        kernel page table pages
 *
 * Call both from mem_test_run() after vmm_init().
 */

/* -------------------------------------------------------------------------
 * Sv39 user-half VA that vmm_init() never touches.
 *
 * L2 index = VA >> 30. vmm_create_pagetable() only copies L2 entries
 * 256-511 (kernel half, VA >> 30 >= 256).  Anything with VA >> 30 < 256
 * is guaranteed empty in a freshly created page table.
 *
 * We pick just below MAXVA (1<<38) but in the user half:
 *   (1UL << 38) - 3*PGSIZE  =>  L2 index = ((1<<38)-3*4096) >> 30 = 3
 * That's index 3 — deep user space, never mapped by the kernel.
 * ------------------------------------------------------------------------- */
#define VMM_TEST_VA  ((1UL << 38) - 3 * PGSIZE)

/* -------------------------------------------------------------------------
 * hardware TLB walk
 *
 * Steps:
 *   a. Allocate a physical page, write a sentinel pattern.
 *   b. Create a user page table (inherits kernel mappings).
 *   c. Map TEST_VA -> phys in the user page table.
 *   d. vmm_switch(upt)  — CPU now uses the new page table.
 *   e. Read through TEST_VA — hardware does the PTE walk.
 *   f. Write through TEST_VA — verify it lands at the physical page.
 *   g. vmm_switch(kpt)  — restore kernel page table.
 *   h. Confirm the write is visible at the physical address.
 *   i. Clean up cleanly.
 * ------------------------------------------------------------------------- */
static void test_vmm_hw_walk(void)
{
    printk("\n--- VMM hardware TLB walk test ---\n");

    /* (a) Physical page + sentinel */
    void *phys = pmm_alloc_page();
    TEST("hw-walk: phys page non-NULL", phys != NULL);
    if (!phys) return;

    volatile uint8_t *pb = (volatile uint8_t *)phys;
    pb[0] = 0xDE; pb[1] = 0xAD; pb[2] = 0xBE; pb[3] = 0xEF;

    /* (b) Fresh user page table — kernel half copied automatically */
    pagetable_t upt = vmm_create_pagetable();
    TEST("hw-walk: upt created", upt != NULL);
    if (!upt) { pmm_free_page(phys); return; }

    /* (c) Map */
    int ret = vmm_map(upt, VMM_TEST_VA, (uintptr_t)phys, PTE_V | PTE_R | PTE_W);
    // int ret = vmm_map(upt, VMM_TEST_VA, (uintptr_t)phys, PTE_V | PTE_R | PTE_W | PTE_U);
    TEST("hw-walk: vmm_map succeeded", ret == 0);
    if (ret != 0) { vmm_free_pagetable(upt); pmm_free_page(phys); return; }

    /* Software sanity check before involving the CPU */
    uintptr_t sw_pa = vmm_translate(upt, VMM_TEST_VA);
    TEST("hw-walk: sw translate correct", sw_pa == (uintptr_t)phys);

    /* (d-e) Switch + read via hardware walk */
    vmm_switch(upt);

    volatile uint8_t *vb = (volatile uint8_t *)VMM_TEST_VA;
    int read_ok = (vb[0] == 0xDE && vb[1] == 0xAD &&
                   vb[2] == 0xBE && vb[3] == 0xEF);
    TEST("hw-walk: hardware read of sentinel correct", read_ok);

    /* (f) Write through VA */
    vb[0] = 0x12; vb[1] = 0x34; vb[2] = 0x56; vb[3] = 0x78;

    /* (g) Restore kernel page table */
    vmm_switch(vmm_kernel_pagetable());

    /* (h) Write must be visible at physical address */
    TEST("hw-walk: byte 0 visible at PA", pb[0] == 0x12);
    TEST("hw-walk: byte 1 visible at PA", pb[1] == 0x34);
    TEST("hw-walk: byte 2 visible at PA", pb[2] == 0x56);
    TEST("hw-walk: byte 3 visible at PA", pb[3] == 0x78);

    /* (i) Clean up — unmap before freeing so vmm_free_pagetable
     *     doesn't try to free the physical page a second time */
    vmm_unmap(upt, VMM_TEST_VA);

    /* Now safe to free: upt has no leaf PTEs pointing at our page */
    vmm_free_pagetable(upt);
    pmm_free_page(phys);

    TEST("hw-walk: teardown without fault", 1);
}

/* VMM hardware activation ──────────────────────────────────────────
 *
 * Your existing VMM tests walk the page table in *software*.  This test
 * goes one step further: it switches the CPU to the new page table, then
 * dereferences the virtual address.  If the hardware PTE walk disagrees
 * with vmm_translate() you'll get a store/load fault here rather than a
 * silent wrong answer.
 *
 * Safety:
 *   - We map a single page at TEST_VA (0x80000) in a fresh user page table.
 *   - After the test we switch back to the kernel page table.
 *   - We free the physical page and destroy the user page table.
 *
 * If your VMM implementation isn't ready for vmm_switch() yet, comment
 * out this function and the call below — the first three tests are still
 * valuable on their own.
 */
static void test_vmm_hw_mapping(void)
{
    printk("\n--- VMM hardware mapping test ---\n");

    void *phys = pmm_alloc_page();
    TEST("hw-map: physical page non-NULL", phys != NULL);
    if (!phys) return;

    volatile uint8_t *pb = (volatile uint8_t *)phys;
    pb[0] = 0xDE; pb[1] = 0xAD; pb[2] = 0xBE; pb[3] = 0xEF;

    pagetable_t upt = vmm_create_pagetable();  // now correctly copies all kernel entries
    TEST("hw-map: user page table created", upt != NULL);
    if (!upt) { pmm_free_page(phys); return; }

    // Removed: manual kernel PTE copy loop — vmm_create_pagetable handles this now

    uint64_t perm = PTE_V | PTE_R | PTE_W;    // no PTE_U, supervisor access
    int ret = vmm_map(upt, VMM_TEST_VA, (uintptr_t)phys, perm);
    TEST("hw-map: vmm_map returned 0", ret == 0);
    if (ret != 0) { vmm_free_pagetable(upt); pmm_free_page(phys); return; }

    vmm_switch(upt);

    volatile uint8_t *vb = (volatile uint8_t *)VMM_TEST_VA;
    int sentinel_ok = (vb[0] == 0xDE && vb[1] == 0xAD &&
                       vb[2] == 0xBE && vb[3] == 0xEF);
    TEST("hw-map: VA dereference reads sentinel bytes correctly", sentinel_ok);

    vb[0] = 0x12;
    vb[1] = 0x34;

    vmm_switch(vmm_kernel_pagetable());

    TEST("hw-map: write via VA visible at PA (byte 0)", pb[0] == 0x12);
    TEST("hw-map: write via VA visible at PA (byte 1)", pb[1] == 0x34);

    vmm_unmap(upt, VMM_TEST_VA);
    vmm_free_pagetable(upt);
    pmm_free_page(phys);

    TEST("hw-map: teardown completed without fault", 1);
}

/* ── entry point ─────────────────────────────────────────────────────────── */

void mem_test_run(void) {
    printk("\n========================================\n");
    printk("  sbunix memory subsystem tests\n");
    printk("========================================\n");

    test_pmm();
    test_kmalloc();
    test_vmm();

    test_pmm_isolation();
    test_pmm_full_readback();
    test_vmm_hw_walk();
    test_vmm_hw_mapping();

    printk("\n========================================\n");
    printk("  Results: %d passed, %d failed\n", passed, failed);
    printk("========================================\n");

    if (failed > 0) {
        printk("WARN: %d test(s) failed — check output above\n", failed);
    }
}