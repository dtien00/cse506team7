/*
 * mm_vma_test.c — Kernel-level VMA split/merge/lookup correctness tests
 *
 * Exercises the VMA layer directly without needing a running process.
 * Based on xv6 usertests: copyin/copyout address validation, kernmem,
 * sbrkbasic, and nowrite — adapted to kernel-level primitives.
 *
 * Requires: pmm_init, vmm_init, kmalloc_init already run.
 */

#include "kernel/printk.h"
#include "kernel/types.h"
#include "mm/vma.h"
#include "mm/kmalloc.h"
#include "arch/riscv.h"
#include "tests/test.h"
#include "errno.h"

static int passed = 0;
static int failed = 0;

/* ── §1  vma_find: lookup by address ─────────────────────────────────────── */

static void test_vma_find(void) {
    printk("=== VMA 1: vma_find ===\n");

    struct vm_area *list = NULL;

    struct vm_area *a = vma_create(0x1000, 0x3000,
                                   VMA_PROT_READ | VMA_PROT_WRITE,
                                   VMA_PRIVATE, VMA_TYPE_ANON);
    TEST("vma_find: create VMA A", a != NULL);
    if (!a) return;
    vma_insert(&list, a);

    struct vm_area *b = vma_create(0x5000, 0x8000,
                                   VMA_PROT_READ,
                                   VMA_PRIVATE, VMA_TYPE_ANON);
    TEST("vma_find: create VMA B", b != NULL);
    if (!b) { vma_free_all(list); return; }
    vma_insert(&list, b);

    /* address inside A */
    TEST("vma_find: addr 0x1000 finds A", vma_find(list, 0x1000) == a);
    TEST("vma_find: addr 0x2FFF finds A", vma_find(list, 0x2FFF) == a);

    /* address inside B */
    TEST("vma_find: addr 0x5000 finds B", vma_find(list, 0x5000) == b);
    TEST("vma_find: addr 0x7FFF finds B", vma_find(list, 0x7FFF) == b);

    /* address in gap between A and B */
    TEST("vma_find: addr 0x3000 returns NULL (exclusive end)", vma_find(list, 0x3000) == NULL);
    TEST("vma_find: addr 0x4000 returns NULL (gap)",           vma_find(list, 0x4000) == NULL);

    /* address before A */
    TEST("vma_find: addr 0x0FFF returns NULL (before A)",      vma_find(list, 0x0FFF) == NULL);

    /* address after B */
    TEST("vma_find: addr 0x8000 returns NULL (exclusive end)", vma_find(list, 0x8000) == NULL);

    vma_free_all(list);
}

/* ── §2  vma_split: split a VMA in two ───────────────────────────────────── */

static void test_vma_split(void) {
    printk("=== VMA 2: vma_split ===\n");

    struct vm_area *list = NULL;

    struct vm_area *v = vma_create(0x1000, 0x5000,
                                   VMA_PROT_READ | VMA_PROT_WRITE,
                                   VMA_PRIVATE, VMA_TYPE_ANON);
    TEST("vma_split: create VMA [0x1000, 0x5000)", v != NULL);
    if (!v) return;
    vma_insert(&list, v);

    /* split at 0x3000 → [0x1000, 0x3000) + [0x3000, 0x5000) */
    int r = vma_split(&list, v, 0x3000);
    TEST("vma_split: returns 0", r == 0);

    /* both halves must now be findable */
    struct vm_area *lo = vma_find(list, 0x1000);
    struct vm_area *hi = vma_find(list, 0x3000);

    TEST("vma_split: low half found at 0x1000", lo != NULL);
    TEST("vma_split: high half found at 0x3000", hi != NULL);
    TEST("vma_split: low half ends at 0x3000", lo && lo->end == 0x3000);
    TEST("vma_split: high half starts at 0x3000", hi && hi->start == 0x3000);
    TEST("vma_split: high half ends at 0x5000", hi && hi->end == 0x5000);
    TEST("vma_split: halves are distinct objects", lo != hi);

    /* prot preserved in both halves */
    TEST("vma_split: prot preserved in low half",
         lo && (lo->prot & (VMA_PROT_READ | VMA_PROT_WRITE)));
    TEST("vma_split: prot preserved in high half",
         hi && (hi->prot & (VMA_PROT_READ | VMA_PROT_WRITE)));

    /* gap test: nothing at 0x0FFF or 0x5000 */
    TEST("vma_split: no VMA at 0x5000 (exclusive)", vma_find(list, 0x5000) == NULL);

    vma_free_all(list);
}

/* ── §3  vma_merge: adjacent same-prot VMAs collapse ─────────────────────── */

static void test_vma_merge(void) {
    printk("=== VMA 3: vma_merge ===\n");

    struct vm_area *list = NULL;

    /* two adjacent VMAs with identical prot/flags/type */
    struct vm_area *a = vma_create(0x1000, 0x3000,
                                   VMA_PROT_READ | VMA_PROT_WRITE,
                                   VMA_PRIVATE, VMA_TYPE_ANON);
    struct vm_area *b = vma_create(0x3000, 0x5000,
                                   VMA_PROT_READ | VMA_PROT_WRITE,
                                   VMA_PRIVATE, VMA_TYPE_ANON);
    TEST("vma_merge: create A", a != NULL);
    TEST("vma_merge: create B", b != NULL);
    if (!a || !b) { if (a) vma_free(a); if (b) vma_free(b); return; }

    vma_insert(&list, a);
    vma_insert(&list, b);

    /* before merge: two distinct nodes */
    TEST("vma_merge: before: find 0x1000", vma_find(list, 0x1000) != NULL);
    TEST("vma_merge: before: find 0x3000", vma_find(list, 0x3000) != NULL);

    vma_merge(&list);

    /* after merge: single node covering the full span */
    struct vm_area *m = vma_find(list, 0x1000);
    TEST("vma_merge: merged node found at 0x1000", m != NULL);
    TEST("vma_merge: merged node starts at 0x1000", m && m->start == 0x1000);
    TEST("vma_merge: merged node ends at 0x5000",   m && m->end == 0x5000);
    TEST("vma_merge: 0x3000 still inside merged node",
         vma_find(list, 0x3000) == m);
    TEST("vma_merge: 0x4FFF still inside merged node",
         vma_find(list, 0x4FFF) == m);

    vma_free_all(list);
}

/* ── §4  split then merge: round-trip invariant ──────────────────────────── */

static void test_vma_split_merge_roundtrip(void) {
    printk("=== VMA 4: split-then-merge round-trip ===\n");

    struct vm_area *list = NULL;

    struct vm_area *v = vma_create(0x2000, 0x6000,
                                   VMA_PROT_READ,
                                   VMA_PRIVATE, VMA_TYPE_ANON);
    TEST("split-merge: create VMA", v != NULL);
    if (!v) return;
    vma_insert(&list, v);

    /* split at 0x4000 */
    int r = vma_split(&list, v, 0x4000);
    TEST("split-merge: split at 0x4000 succeeds", r == 0);

    struct vm_area *lo = vma_find(list, 0x2000);
    struct vm_area *hi = vma_find(list, 0x4000);
    TEST("split-merge: two distinct halves after split", lo != NULL && hi != NULL && lo != hi);

    /* re-merge: back to one node */
    vma_merge(&list);

    struct vm_area *merged = vma_find(list, 0x2000);
    TEST("split-merge: merged back to single node", merged != NULL);
    TEST("split-merge: merged span is [0x2000, 0x6000)",
         merged && merged->start == 0x2000 && merged->end == 0x6000);

    vma_free_all(list);
}

/* ── §5  vma_range_accessible: kernel address guard (mirrors xv6 kernmem) ── */

static void test_vma_range_accessible(void) {
    printk("=== VMA 5: vma_range_accessible (kernel-addr guard) ===\n");

    struct vm_area *list = NULL;

    /* typical user mapping: 0x1000–0x3000 read+write */
    struct vm_area *u = vma_create(0x1000, 0x3000,
                                   VMA_PROT_READ | VMA_PROT_WRITE,
                                   VMA_PRIVATE, VMA_TYPE_ANON);
    TEST("range_accessible: create user VMA", u != NULL);
    if (!u) return;
    vma_insert(&list, u);

    /* range fully inside the user VMA: accessible */
    TEST("range_accessible: [0x1000, 8) is accessible",
         vma_range_accessible(list, 0x1000, 8, VMA_PROT_READ) == 1);
    TEST("range_accessible: [0x1800, 0x100) is accessible",
         vma_range_accessible(list, 0x1800, 0x100, VMA_PROT_WRITE) == 1);

    /* range straddling end of VMA: not fully accessible */
    TEST("range_accessible: [0x2FF0, 0x20) crosses end — not accessible",
         vma_range_accessible(list, 0x2FF0, 0x20, VMA_PROT_READ) == 0);

    /* range entirely outside: not accessible */
    TEST("range_accessible: [0x4000, 8) outside VMA — not accessible",
         vma_range_accessible(list, 0x4000, 8, VMA_PROT_READ) == 0);

    /* kernel VA (>= KERNBASE = 0x80000000) must NOT be accessible via user VMA */
    TEST("range_accessible: kernel VA 0x80000000 not in any user VMA",
         vma_range_accessible(list, (uintptr_t)KERNBASE, 8, VMA_PROT_READ) == 0);
    TEST("range_accessible: kernel VA 0xFFFFFFFF not in any user VMA",
         vma_range_accessible(list, 0xFFFFFFFFUL, 8, VMA_PROT_READ) == 0);

    /* prot mismatch: VMA is RW, request exec — must fail */
    TEST("range_accessible: exec on RW VMA — not accessible",
         vma_range_accessible(list, 0x1000, 8, VMA_PROT_EXEC) == 0);

    vma_free_all(list);
}

/* ── §6  vma_find_overlap: overlap detection ─────────────────────────────── */

static void test_vma_find_overlap(void) {
    printk("=== VMA 6: vma_find_overlap ===\n");

    struct vm_area *list = NULL;

    struct vm_area *a = vma_create(0x1000, 0x3000,
                                   VMA_PROT_READ,
                                   VMA_PRIVATE, VMA_TYPE_ANON);
    struct vm_area *b = vma_create(0x5000, 0x8000,
                                   VMA_PROT_READ,
                                   VMA_PRIVATE, VMA_TYPE_ANON);
    if (!a || !b) { if (a) vma_free(a); if (b) vma_free(b); return; }
    vma_insert(&list, a);
    vma_insert(&list, b);

    /* exact overlap with A */
    TEST("find_overlap: [0x1000, 0x3000) overlaps A",
         vma_find_overlap(list, 0x1000, 0x3000) == a);

    /* partial overlap with A from below */
    TEST("find_overlap: [0x0800, 0x1800) overlaps A",
         vma_find_overlap(list, 0x0800, 0x1800) == a);

    /* partial overlap with A from above */
    TEST("find_overlap: [0x2800, 0x4000) overlaps A",
         vma_find_overlap(list, 0x2800, 0x4000) == a);

    /* gap between A and B: no overlap */
    TEST("find_overlap: [0x3000, 0x5000) in gap — NULL",
         vma_find_overlap(list, 0x3000, 0x5000) == NULL);

    /* span covering both A and B: detects first overlap */
    struct vm_area *got = vma_find_overlap(list, 0x0000, 0x9000);
    TEST("find_overlap: [0x0000, 0x9000) overlaps something (A or B)",
         got == a || got == b);

    vma_free_all(list);
}

/* ── §7  vma_find_free_region: allocator avoids existing VMAs ─────────────── */

static void test_vma_find_free_region(void) {
    printk("=== VMA 7: vma_find_free_region ===\n");

    struct vm_area *list = NULL;

    /* fill [0x1000, 0x5000) */
    struct vm_area *v = vma_create(0x1000, 0x5000,
                                   VMA_PROT_READ,
                                   VMA_PRIVATE, VMA_TYPE_ANON);
    if (!v) return;
    vma_insert(&list, v);

    /* request a page after the existing mapping */
    uintptr_t addr = vma_find_free_region(list, 0x1000, 0x1000,
                                          0x1000, 0x10000);
    TEST("find_free_region: returned address non-zero", addr != 0);
    TEST("find_free_region: returned address >= 0x5000 (after existing VMA)",
         addr >= 0x5000);
    TEST("find_free_region: returned address < 0x10000 (within bounds)",
         addr < 0x10000);

    /* request in a region fully blocked: must fail (return 0) */
    uintptr_t no = vma_find_free_region(list, 0x2000, 0x1000,
                                        0x1000, 0x4000);
    /* The entire [0x1000, 0x4000) window is occupied; no 0x1000-byte gap → 0 */
    TEST("find_free_region: fully-blocked window returns 0", no == 0);

    vma_free_all(list);
}

/* ── §8  vma_copy_all: fork path — all VMAs copied correctly ──────────────── */

static void test_vma_copy_all(void) {
    printk("=== VMA 8: vma_copy_all (fork VMA copy) ===\n");

    struct vm_area *src = NULL;

    struct vm_area *a = vma_create(0x1000, 0x3000,
                                   VMA_PROT_READ | VMA_PROT_WRITE,
                                   VMA_PRIVATE, VMA_TYPE_ANON);
    struct vm_area *b = vma_create(0x5000, 0x7000,
                                   VMA_PROT_READ | VMA_PROT_EXEC,
                                   VMA_PRIVATE, VMA_TYPE_ANON);
    if (!a || !b) { if (a) vma_free(a); if (b) vma_free(b); return; }
    vma_insert(&src, a);
    vma_insert(&src, b);

    struct vm_area *dst = NULL;
    int r = vma_copy_all(&dst, src);
    TEST("vma_copy_all: returns 0", r == 0);
    TEST("vma_copy_all: dst is non-NULL", dst != NULL);

    /* both VMAs present in copy */
    struct vm_area *ca = vma_find(dst, 0x1000);
    struct vm_area *cb = vma_find(dst, 0x5000);
    TEST("vma_copy_all: copy contains A at 0x1000", ca != NULL);
    TEST("vma_copy_all: copy contains B at 0x5000", cb != NULL);

    /* copies are deep (distinct pointers) */
    TEST("vma_copy_all: A copy is distinct pointer", ca != a);
    TEST("vma_copy_all: B copy is distinct pointer", cb != b);

    /* metadata preserved */
    TEST("vma_copy_all: A copy start/end preserved",
         ca && ca->start == 0x1000 && ca->end == 0x3000);
    TEST("vma_copy_all: B copy prot preserved",
         cb && (cb->prot & VMA_PROT_EXEC));

    vma_free_all(src);
    vma_free_all(dst);
}

/* ── entry point ──────────────────────────────────────────────────────────── */

void mm_vma_test_run(void) {
    printk("\n========================================\n");
    printk("  VMA Correctness Tests\n");
    printk("========================================\n\n");

    test_vma_find();
    test_vma_split();
    test_vma_merge();
    test_vma_split_merge_roundtrip();
    test_vma_range_accessible();
    test_vma_find_overlap();
    test_vma_find_free_region();
    test_vma_copy_all();

    printk("\nVMA tests: %d passed, %d failed\n\n", passed, failed);
}
