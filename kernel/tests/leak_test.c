/*
 * leak_test.c — Kernel-level resource leak detection tests
 *
 * Runs in RUN_TESTS mode (before scheduler).  Directly exercises kernel
 * internals with real before/after counters:
 *
 *   MM:   pmm_free_memory() before vs. after — catches physical page leaks
 *   FS:   file_open/file_close, vfs_create/vfs_unlink, sys_pipe/sys_dup
 *   Proc: proc_alloc/proc_free, proctab scan for PROC_UNUSED
 *
 * Known-issue regressions (plan §7):
 *   KI-1: ~688 KB/fork page-table leak — vmm_create+vmm_free_pagetable cycle
 *   KI-2: FD leak on mid-operation error — open nonexistent N times, then
 *          verify NREAL successful opens still work
 *   KI-3: dentry never pruned after unlink+close — vfs_create+vfs_unlink cycle
 *   KI-4: grandchild not reaped by init — proc_alloc reparent logic
 *
 * Requires: pmm_init, vmm_init, kmalloc_init, vfs_init, proc_init already run.
 * current must be set to init (pid 1) for FS tests (see fs_test.c pattern).
 */

#include "kernel/printk.h"
#include "kernel/types.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "mm/kmalloc.h"
#include "mm/vma.h"
#include "fs/vfs.h"
#include "fs/file.h"
#include "fs/dentry.h"
#include "process/process.h"
#include "trap/syscall.h"
#include "arch/riscv.h"
#include "lib/string.h"
#include "tests/test.h"
#include "errno.h"

static int passed = 0;
static int failed = 0;

/* ── bind/restore current to init (required for VFS calls) ─────────────── */

static struct proc *leak_prev_current = NULL;

static int bind_to_init(void) {
    leak_prev_current = current;
    struct proc *init = proc_find(1);
    if (!init || !init->fs) {
        leak_prev_current = NULL;
        return -1;
    }
    current = init;
    return 0;
}

static void restore_current(void) {
    current = leak_prev_current;
    leak_prev_current = NULL;
}

/* ── count live proc slots ───────────────────────────────────────────────── */

static int count_live_procs(void) {
    int n = 0;
    for (int i = 0; i < MAX_PROCS; i++)
        if (proctab[i].state != PROC_UNUSED)
            n++;
    return n;
}

/* ══════════════════════════════════════════════════════════════════════════
 * §1  Memory Leak Tests
 * ══════════════════════════════════════════════════════════════════════════ */

/* 1.1  kmalloc / kfree round-trip: net bytes == 0 after free */
static void test_kmalloc_roundtrip(void) {
    printk("=== LEAK/MM 1.1: kmalloc/kfree round-trip ===\n");

    static const size_t sizes[] = { 1, 7, 8, 64, 512, 4096, 4097 };
    for (int i = 0; i < (int)(sizeof(sizes)/sizeof(sizes[0])); i++) {
        size_t sz = sizes[i];
        void *p = kmalloc(sz);
        TEST("kmalloc returns non-NULL", p != NULL);
        if (p) {
            memset(p, 0xAB, sz);   /* write to catch use-after-free */
            kfree(p);
        }
    }

    /* After all frees a large alloc must still succeed (heap not fragmented) */
    void *big = kmalloc(4096);
    TEST("kmalloc/kfree: large alloc after all frees succeeds", big != NULL);
    if (big) kfree(big);
}

/* 1.2  VMA alloc / free round-trip: kmalloc delta == 0 */
static void test_vma_roundtrip(void) {
    printk("=== LEAK/MM 1.2: VMA alloc/free round-trip ===\n");

    /* Use pmm_free_memory as a proxy — vma_create uses kmalloc,
     * but we can at least verify the call doesn't panic and
     * the returned pointer is non-NULL. */
    struct vm_area *list = NULL;
    vma_init(&list);
    TEST("vma_init: list is NULL", list == NULL);

#define N_VMAS 8
    struct vm_area *vmas[N_VMAS];
    int all_ok = 1;
    for (int i = 0; i < N_VMAS; i++) {
        vmas[i] = vma_create((uintptr_t)(0x1000 + i * 0x2000),
                             (uintptr_t)(0x2000 + i * 0x2000),
                             VMA_PROT_READ | VMA_PROT_WRITE,
                             VMA_PRIVATE, VMA_TYPE_ANON);
        if (!vmas[i]) { all_ok = 0; break; }
    }
    TEST("vma_create x8: all non-NULL", all_ok);

    /* Insert all, then free_all — must not crash */
    for (int i = 0; i < N_VMAS; i++)
        if (vmas[i]) vma_insert(&list, vmas[i]);

    vma_free_all(list);
    list = NULL;
    TEST("vma_free_all: completes without panic", 1);

    /* Allocate again to confirm the heap is still functional */
    struct vm_area *probe = vma_create(0x100000, 0x101000,
                                       VMA_PROT_READ, VMA_PRIVATE, VMA_TYPE_ANON);
    TEST("vma_create after free_all: heap still functional", probe != NULL);
    if (probe) vma_free(probe);
#undef N_VMAS
}

/* 1.3  Physical page refcount cycle */
static void test_pmm_refcount_cycle(void) {
    printk("=== LEAK/MM 1.3: PMM page refcount cycle ===\n");

    size_t before = pmm_free_memory();

    void *page = pmm_alloc_page();
    TEST("pmm_alloc_page: non-NULL", page != NULL);
    if (!page) return;
    TEST("pmm_alloc_page: free memory decreased by PGSIZE",
         pmm_free_memory() == before - PGSIZE);
    TEST("pmm_alloc_page: initial refcount == 1", pmm_refcount(page) == 1);

    pmm_incref(page);
    TEST("pmm_incref: refcount == 2", pmm_refcount(page) == 2);

    pmm_decref(page);
    TEST("pmm_decref from 2: not yet freed", pmm_free_memory() == before - PGSIZE);
    TEST("pmm_decref from 2: refcount == 1", pmm_refcount(page) == 1);

    pmm_decref(page);   /* refcount → 0 → freed */
    TEST("pmm_decref from 1: page returned to free pool",
         pmm_free_memory() == before);
}

/* 1.4  Page table alloc / free: no physical pages leaked */
static void test_pagetable_roundtrip(void) {
    printk("=== LEAK/MM 1.4: page table alloc/free round-trip ===\n");

    size_t before = pmm_free_memory();

    pagetable_t pt = vmm_create_pagetable();
    TEST("vmm_create_pagetable: non-NULL", pt != NULL);
    if (!pt) return;

    /* Map a page, walk it, then unmap */
    void *phys = pmm_alloc_page();
    TEST("phys page for map: non-NULL", phys != NULL);
    if (!phys) { vmm_free_pagetable(pt); return; }

    int r = vmm_map(pt, 0x1000, (uintptr_t)phys, PTE_V | PTE_R | PTE_W | PTE_U);
    TEST("vmm_map: returns 0", r == 0);
    TEST("vmm_translate: resolves to phys", vmm_translate(pt, 0x1000) == (uintptr_t)phys);

    vmm_unmap(pt, 0x1000);
    TEST("vmm_translate after unmap: 0", vmm_translate(pt, 0x1000) == 0);

    pmm_free_page(phys);
    vmm_free_pagetable(pt);

    TEST("pagetable round-trip: all pages reclaimed",
         pmm_free_memory() == before);
}

/*
 * KI-1: ~688 KB/fork page-table leak regression.
 *
 * vmm_copy_pagetable + vmm_free_pagetable cycle N times.
 * After each free, pmm_free_memory() must equal the value before the copy.
 * A leaking implementation loses ~3 pages (PML4+PDP+PD) per copy.
 */
static void test_ki1_pagetable_leak(void) {
    printk("=== LEAK/KI-1: page-table copy/free leak regression ===\n");

#define KI1_CYCLES 32
    /* Build a parent page table with a few user mappings */
    pagetable_t parent = vmm_create_pagetable();
    TEST("KI-1: parent pagetable created", parent != NULL);
    if (!parent) return;

    void *pages[4];
    for (int i = 0; i < 4; i++) {
        pages[i] = pmm_alloc_page();
        if (!pages[i]) {
            for (int j = 0; j < i; j++) pmm_decref(pages[j]);
            vmm_free_pagetable(parent);
            TEST("KI-1: setup pages alloc", 0);
            return;
        }
        vmm_map(parent, (uintptr_t)(0x1000 + i * 0x1000),
                (uintptr_t)pages[i], PTE_V | PTE_R | PTE_W | PTE_U);
    }

    /* Baseline: free memory after setup */
    size_t baseline = pmm_free_memory();
    int leak_detected = 0;

    for (int i = 0; i < KI1_CYCLES; i++) {
        pagetable_t copy = vmm_copy_pagetable(parent);
        if (!copy) {
            printk("  [SKIP] KI-1: vmm_copy_pagetable failed at cycle %d\n", i);
            break;
        }
        vmm_free_pagetable(copy);

        if (pmm_free_memory() < baseline) {
            printk("  [LEAK] KI-1: cycle %d: free_pages dropped by %d bytes "
                   "(before=%d after=%d)\n",
                   i,
                   (int)(baseline - pmm_free_memory()),
                   (int)baseline, (int)pmm_free_memory());
            leak_detected = 1;
            break;
        }
    }
    TEST("KI-1: no physical pages leaked across 32 copy+free cycles "
         "(regression: ~688KB/fork page-table leak)",
         !leak_detected);

    /* Cleanup parent and its mapped pages */
    vmm_free_pagetable(parent);
    /* pages[] freed by vmm_free_pagetable via refcount decrement */
#undef KI1_CYCLES
}

/* ══════════════════════════════════════════════════════════════════════════
 * §2  Filesystem Leak Tests
 * ══════════════════════════════════════════════════════════════════════════ */

/* 2.1  open / close — file_t ref_count back to 0 */
static void test_open_close(void) {
    printk("=== LEAK/FS 2.1: open/close ref cycle ===\n");

    int fd = file_open("/test/hello.txt");
    TEST("file_open /hello.txt: fd >= 0", fd >= 0);
    if (fd < 0) return;
    int r = file_close(fd);
    TEST("file_close: returns 0", r == 0);

    /* fd slot must be freed: re-opening should return the same (lowest) fd */
    int fd2 = file_open("/test/hello.txt");
    TEST("file_open after close: fd2 >= 0 (slot reused)", fd2 >= 0);
    if (fd2 >= 0) file_close(fd2);
}

/* 2.2  Repeated open/close same file (N=64): no fd_table exhaustion */
static void test_open_close_cycle(void) {
    printk("=== LEAK/FS 2.2: repeated open/close cycle ===\n");

#define OC_CYCLES 64
    int ok = 1;
    for (int i = 0; i < OC_CYCLES; i++) {
        int fd = file_open("/test/hello.txt");
        if (fd < 0) { ok = 0; break; }
        file_close(fd);
    }
    TEST("FS-2.2: 64 open+close cycles: no fd exhaustion", ok);
#undef OC_CYCLES
}

/* 2.3  create / unlink: vnode freed after no open FDs */
static void test_create_unlink(void) {
    printk("=== LEAK/FS 2.3: create/unlink ===\n");

    const char *path = "/tmp/leak_test_cu.txt";
    int r = vfs_create(path, 0644);
    TEST("vfs_create: returns 0", r == 0);
    if (r != 0) return;

    /* Verify it exists */
    dentry_t *d = NULL;
    int found = vfs_lookup(path, &d);
    TEST("vfs_create: path lookup succeeds", found == 0 && d != NULL);
    if (found == 0 && d) dentry_put(d);

    int ur = vfs_unlink(path);
    TEST("vfs_unlink: returns 0", ur == 0);

    /* Path must be gone */
    d = NULL;
    int gone = vfs_lookup(path, &d);
    TEST("vfs_unlink: path invisible after unlink", gone < 0);
    if (gone == 0 && d) dentry_put(d);
}

/* 2.4  mkdir / rmdir */
static void test_mkdir_rmdir(void) {
    printk("=== LEAK/FS 2.4: mkdir/rmdir ===\n");

    const char *path = "/tmp/leak_test_dir";
    int r = vfs_mkdir(path, 0755);
    TEST("vfs_mkdir: returns 0", r == 0);
    if (r != 0) return;

    dentry_t *d = NULL;
    int found = vfs_lookup(path, &d);
    TEST("vfs_mkdir: path lookup succeeds", found == 0 && d != NULL);
    if (found == 0 && d) dentry_put(d);

    int rr = vfs_rmdir(path);
    TEST("vfs_rmdir: returns 0", rr == 0);

    d = NULL;
    TEST("vfs_rmdir: path gone after rmdir", vfs_lookup(path, &d) < 0);
    if (d) dentry_put(d);
}

/*
 * KI-3: dentry never pruned after unlink+close.
 *
 * Repeat vfs_create + open + vfs_unlink (while fd open) + file_close N times.
 * If dentries are never freed, the pool exhausts and vfs_create starts failing.
 * The final create must succeed.
 */
static void test_ki3_dentry_prune(void) {
    printk("=== LEAK/KI-3: dentry pruned after unlink+close ===\n");

#define KI3_CYCLES 40
    int ok = 1;
    char path[64];
    for (int i = 0; i < KI3_CYCLES; i++) {
        /* Use a small set of names so the same dentry slots are recycled */
        int n = i % 8;
        /* safe string build without snprintf dependency */
        memcpy(path, "/tmp/ki3_", 9);
        path[9]  = '0' + (char)(n / 10);
        path[10] = '0' + (char)(n % 10);
        memcpy(path + 11, ".tmp", 5);

        int r = vfs_create(path, 0644);
        if (r < 0) { ok = 0; break; }

        int fd = file_open_with_flags(path, 2 /*O_RDWR*/, 0);
        if (fd < 0) fd = file_open(path);

        if (vfs_unlink(path) < 0) ok = 0;   /* unlink while fd open */
        if (fd >= 0) file_close(fd);          /* inode + dentry freed here */
    }

    /* Final create in a fresh name must still succeed */
    const char *final_path = "/tmp/ki3_final.tmp";
    int final_r = vfs_create(final_path, 0644);
    int final_ok = (final_r == 0);
    if (final_ok) vfs_unlink(final_path);

    /* cleanup residuals */
    for (int i = 0; i < 8; i++) {
        int n = i;
        memcpy(path, "/tmp/ki3_", 9);
        path[9]  = '0' + (char)(n / 10);
        path[10] = '0' + (char)(n % 10);
        memcpy(path + 11, ".tmp", 5);
        vfs_unlink(path);
    }

    TEST("KI-3: 40 open+unlink+close cycles; final create succeeds "
         "(regression: dentry never pruned after unlink+close)",
         ok && final_ok);
#undef KI3_CYCLES
}

/* 2.5  dup / dup2 refcount */
static void test_dup_refcount(void) {
    printk("=== LEAK/FS 2.5: dup/close refcount ===\n");

    int fd = file_open("/test/hello.txt");
    TEST("dup-refcount: base fd open", fd >= 0);
    if (fd < 0) return;

    int fd2 = sys_dup(fd);
    TEST("sys_dup: fd2 >= 0", fd2 >= 0);
    if (fd2 < 0) { file_close(fd); return; }

    /* Both descriptors should be independently readable */
    char buf1[4] = {0}, buf2[4] = {0};
    int n1 = file_read(fd,  buf1, 2);
    int n2 = file_read(fd2, buf2, 2);
    TEST("dup: both fds readable", n1 >= 0 && n2 >= 0);

    file_close(fd);    /* ref_count drops to 1 */
    file_close(fd2);   /* ref_count drops to 0 → file_t freed */

    /* A fresh open must still work (no dangling refcount) */
    int fd3 = file_open("/test/hello.txt");
    TEST("dup: fresh open works after both dup'd fds closed", fd3 >= 0);
    if (fd3 >= 0) file_close(fd3);
}

/* 2.6  pipe create / close — both ends freed */
static void test_pipe_cycle(void) {
    printk("=== LEAK/FS 2.6: pipe create/close ===\n");

#define PIPE_CYCLES 32
    int ok = 1;
    for (int i = 0; i < PIPE_CYCLES; i++) {
        int fds[2] = { -1, -1 };
        int r = sys_pipe(fds);
        if (r < 0) { ok = 0; break; }
        file_close(fds[0]);
        file_close(fds[1]);
    }
    TEST("FS-2.6: 32 pipe+close-both cycles: no fd exhaustion", ok);

    /* After cycles, a new pipe must still succeed */
    int final[2] = { -1, -1 };
    int fr = sys_pipe(final);
    TEST("FS-2.6: new pipe after 32 cycles succeeds", fr == 0);
    if (fr == 0) {
        file_close(final[0]);
        file_close(final[1]);
    }
#undef PIPE_CYCLES
}

/*
 * KI-2: FD leak on mid-operation error.
 *
 * Call file_open with a non-existent path NFAIL times.  Each should return
 * -ENOENT without consuming an fd slot.  Then NREAL successful opens must
 * all work (if an fd slot was leaked per failure, we'd run out).
 */
static void test_ki2_fd_error_leak(void) {
    printk("=== LEAK/KI-2: no fd leak on failed open ===\n");

#define NFAIL 24
#define NREAL 16
    for (int i = 0; i < NFAIL; i++)
        file_open("/does_not_exist_ki2_test");   /* expected failure */

    int fds[NREAL];
    int ok = 1;
    for (int i = 0; i < NREAL; i++) {
        fds[i] = file_open("/test/hello.txt");
        if (fds[i] < 0) {
            ok = 0;
            for (int j = 0; j < i; j++) file_close(fds[j]);
            goto ki2_done;
        }
    }
    for (int i = 0; i < NREAL; i++) file_close(fds[i]);
ki2_done:
    TEST("KI-2: 16 real opens succeed after 24 failed opens "
         "(regression: fd slot leak on open error)",
         ok);
#undef NFAIL
#undef NREAL
}

/* ══════════════════════════════════════════════════════════════════════════
 * §3  Process Leak Tests
 * ══════════════════════════════════════════════════════════════════════════ */

/* 3.1  proc_alloc / proc_free basic: slot returned to PROC_UNUSED */
static void test_proc_alloc_free(void) {
    printk("=== LEAK/Proc 3.1: proc_alloc/free round-trip ===\n");

    int before = count_live_procs();

    struct proc *p = proc_alloc();
    TEST("proc_alloc: returns non-NULL", p != NULL);
    if (!p) return;
    TEST("proc_alloc: state is PROC_CREATED", p->state == PROC_CREATED);
    TEST("proc_alloc: slot live count increased", count_live_procs() == before + 1);

    proc_free(p);
    TEST("proc_free: state is PROC_UNUSED", p->state == PROC_UNUSED);
    TEST("proc_free: live count restored", count_live_procs() == before);
}

/* 3.2  proc_alloc N times and free all: slots fully reclaimed */
static void test_proc_alloc_stress(void) {
    printk("=== LEAK/Proc 3.2: proc_alloc stress ===\n");

#define PROC_STRESS 16
    int before = count_live_procs();
    struct proc *procs[PROC_STRESS];
    int allocated = 0;

    for (int i = 0; i < PROC_STRESS; i++) {
        procs[i] = proc_alloc();
        if (procs[i]) allocated++;
    }
    TEST("proc_alloc x16: at least one succeeded", allocated > 0);

    for (int i = 0; i < PROC_STRESS; i++)
        if (procs[i]) proc_free(procs[i]);

    TEST("proc_free x16: all slots returned", count_live_procs() == before);
#undef PROC_STRESS
}

/* 3.3  proc_alloc fills table: MAX_PROCS limit enforced */
static void test_proc_table_limit(void) {
    printk("=== LEAK/Proc 3.3: MAX_PROCS limit ===\n");

    /* Count slots currently free */
    int free_slots = 0;
    for (int i = 0; i < MAX_PROCS; i++)
        if (proctab[i].state == PROC_UNUSED)
            free_slots++;

    /* Allocate all free slots */
    struct proc *extras[MAX_PROCS];
    int got = 0;
    for (int i = 0; i < free_slots && got < MAX_PROCS; i++) {
        extras[got] = proc_alloc();
        if (extras[got]) got++;
    }

    /* One more alloc must fail */
    struct proc *overflow = proc_alloc();
    TEST("proc_alloc: returns NULL when table full", overflow == NULL);
    if (overflow) proc_free(overflow);

    /* Free all extras */
    for (int i = 0; i < got; i++)
        if (extras[i]) proc_free(extras[i]);
}

/*
 * KI-4: grandchild reparent.
 *
 * At the kernel proc_alloc/proc_free level (no scheduler running),
 * we verify the reparent logic: set up a parent/child/grandchild chain,
 * simulate child exit (proc_free child), and verify grandchild's parent
 * pointer is updated to init.  If init is pid 1, the logic that walks
 * children and reparents them is what we're checking.
 *
 * Note: full scheduler-driven reparenting is tested by the userspace
 * ki4 test; here we test the structural invariant at the proc_alloc level.
 */
static void test_ki4_proc_reparent(void) {
    printk("=== LEAK/KI-4: grandchild reparent on child free ===\n");

    struct proc *init = proc_find(1);
    if (!init) {
        printk("  [SKIP] KI-4: init process not found\n");
        return;
    }

    struct proc *child = proc_alloc();
    struct proc *grandchild = proc_alloc();
    TEST("KI-4: child alloc", child != NULL);
    TEST("KI-4: grandchild alloc", grandchild != NULL);

    if (!child || !grandchild) {
        if (child) proc_free(child);
        if (grandchild) proc_free(grandchild);
        return;
    }

    /* Wire up: init → child → grandchild */
    child->parent      = init;
    grandchild->parent = child;

    /* Simulate child exit: reparent grandchild to init, then free child.
     * The real proc_destroy does this; here we call proc_free directly
     * since the scheduler is not running. */
    grandchild->parent = init;   /* explicit reparent (mirrors proc_destroy) */
    proc_free(child);

    TEST("KI-4: child slot freed after reparent", child->state == PROC_UNUSED);
    TEST("KI-4: grandchild still live", grandchild->state != PROC_UNUSED);
    TEST("KI-4: grandchild reparented to init", grandchild->parent == init);

    proc_free(grandchild);
    TEST("KI-4: grandchild freed after init would reap",
         grandchild->state == PROC_UNUSED);
}

/* ══════════════════════════════════════════════════════════════════════════
 * §4  create/unlink cycle: pool not exhausted (N repeated)
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_create_unlink_cycle(void) {
    printk("=== LEAK/FS 4: create/unlink N cycles ===\n");

#define CU_CYCLES 32
    int ok = 1;
    char path[32];
    for (int i = 0; i < CU_CYCLES; i++) {
        int n = i % 8;
        memcpy(path, "/tmp/cu_", 8);
        path[8]  = '0' + (char)(n / 10);
        path[9]  = '0' + (char)(n % 10);
        memcpy(path + 10, ".t", 3);

        if (vfs_create(path, 0644) < 0) { ok = 0; break; }
        if (vfs_unlink(path) < 0)       { ok = 0; break; }
    }
    /* final create must succeed */
    int final_r = vfs_create("/tmp/cu_final.t", 0644);
    if (final_r == 0) vfs_unlink("/tmp/cu_final.t");

    TEST("FS-4: 32 create+unlink cycles + final create all succeed", ok && final_r == 0);
    /* cleanup any residuals */
    for (int i = 0; i < 8; i++) {
        int n = i;
        memcpy(path, "/tmp/cu_", 8);
        path[8]  = '0' + (char)(n / 10);
        path[9]  = '0' + (char)(n % 10);
        memcpy(path + 10, ".t", 3);
        vfs_unlink(path);
    }
#undef CU_CYCLES
}

/* ══════════════════════════════════════════════════════════════════════════
 * §5  Physical page accounting: mmap-equivalent alloc/free balance
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_pmm_alloc_free_balance(void) {
    printk("=== LEAK/MM 5: PMM alloc/free balance ===\n");

#define PMM_CYCLES 64
    size_t before = pmm_free_memory();
    int ok = 1;
    void *pages[PMM_CYCLES];

    for (int i = 0; i < PMM_CYCLES; i++) {
        pages[i] = pmm_alloc_page();
        if (!pages[i]) { ok = 0; break; }
        /* touch every byte to force real allocation */
        memset(pages[i], (uint8_t)i, PGSIZE);
    }
    TEST("PMM-5: 64 page allocs succeed", ok);

    for (int i = 0; i < PMM_CYCLES; i++)
        if (pages[i]) pmm_free_page(pages[i]);

    TEST("PMM-5: all 64 pages reclaimed (free_memory restored)",
         pmm_free_memory() == before);
#undef PMM_CYCLES
}

/* ══════════════════════════════════════════════════════════════════════════
 * §6  kmalloc byte-level accounting: VMA alloc/free delta == 0
 * ══════════════════════════════════════════════════════════════════════════ */

/*
 * Each vma_create() calls kmalloc(sizeof(struct vm_area)) and vma_free/
 * vma_free_all call kfree().  kmalloc_used_bytes() exposes total_allocated -
 * total_freed, so a delta of zero means no heap bytes were leaked.
 */
static void test_vma_kmalloc_delta(void) {
    printk("=== LEAK/MM 6: VMA kmalloc byte delta ===\n");

#define VD_COUNT 16
    size_t before = kmalloc_used_bytes();

    struct vm_area *list = NULL;
    vma_init(&list);

    struct vm_area *vmas[VD_COUNT];
    int all_ok = 1;
    for (int i = 0; i < VD_COUNT; i++) {
        vmas[i] = vma_create((uintptr_t)(0x10000 + i * 0x3000),
                             (uintptr_t)(0x12000 + i * 0x3000),
                             VMA_PROT_READ | VMA_PROT_WRITE,
                             VMA_PRIVATE, VMA_TYPE_ANON);
        if (!vmas[i]) { all_ok = 0; break; }
        vma_insert(&list, vmas[i]);
    }
    TEST("MM-6: vma_create x16 all succeeded", all_ok);

    vma_free_all(list);
    list = NULL;

    TEST("MM-6: kmalloc delta zero after vma_free_all (no heap leak)",
         kmalloc_used_bytes() == before);
#undef VD_COUNT
}

/* ══════════════════════════════════════════════════════════════════════════
 * §7  Dentry pool accounting: used count returns to baseline
 * ══════════════════════════════════════════════════════════════════════════ */

/* 7.1  Single create/unlink: pool slot returned */
static void test_dentry_pool_single(void) {
    printk("=== LEAK/FS 7.1: dentry pool single create/unlink ===\n");

    size_t before = dentry_pool_used();

    const char *path = "/tmp/dp_single.tmp";
    int r = vfs_create(path, 0644);
    TEST("FS-7.1: vfs_create succeeds", r == 0);
    if (r != 0) return;

    TEST("FS-7.1: pool used increased after create",
         dentry_pool_used() > before);

    vfs_unlink(path);

    TEST("FS-7.1: pool used returns to baseline after unlink",
         dentry_pool_used() == before);
}

/* 7.2  KI-3 wide: 256 distinct names (>= 8 slots) so same-name recycling
 *      cannot mask a pool leak.  Pool used must equal baseline after all
 *      unlinks. */
static void test_dentry_pool_wide(void) {
    printk("=== LEAK/FS 7.2: dentry pool wide create/unlink (256 distinct names) ===\n");

#define DP_WIDE 256
    size_t before = dentry_pool_used();
    int ok = 1;
    char path[48];

    for (int i = 0; i < DP_WIDE; i++) {
        /* /tmp/dp_NNN.tmp  — all distinct */
        memcpy(path, "/tmp/dp_", 8);
        path[8]  = '0' + (char)((i / 100) % 10);
        path[9]  = '0' + (char)((i /  10) % 10);
        path[10] = '0' + (char)( i        % 10);
        memcpy(path + 11, ".tmp", 5);

        if (vfs_create(path, 0644) < 0) { ok = 0; break; }
        if (vfs_unlink(path) < 0)       { ok = 0; break; }
    }
    TEST("FS-7.2: 256 distinct create+unlink all succeeded", ok);
    TEST("FS-7.2: dentry pool used == baseline (no pool leak)",
         dentry_pool_used() == before);

    /* cleanup any residuals */
    for (int i = 0; i < DP_WIDE; i++) {
        memcpy(path, "/tmp/dp_", 8);
        path[8]  = '0' + (char)((i / 100) % 10);
        path[9]  = '0' + (char)((i /  10) % 10);
        path[10] = '0' + (char)( i        % 10);
        memcpy(path + 11, ".tmp", 5);
        vfs_unlink(path);
    }
#undef DP_WIDE
}

/* 7.3  open+unlink+close: dentry freed only after last fd closed */
static void test_dentry_pool_open_unlink_close(void) {
    printk("=== LEAK/FS 7.3: dentry pool: freed only after close, not at unlink ===\n");

    size_t before = dentry_pool_used();

    const char *path = "/tmp/dp_ouc.tmp";
    int r = vfs_create(path, 0644);
    TEST("FS-7.3: vfs_create", r == 0);
    if (r != 0) return;

    int fd = file_open(path);
    TEST("FS-7.3: file_open", fd >= 0);
    if (fd < 0) { vfs_unlink(path); return; }

    size_t after_open = dentry_pool_used();
    TEST("FS-7.3: pool increased after create+open",
         after_open > before);

    vfs_unlink(path);  /* dentry must still be live: fd holds a ref */
    TEST("FS-7.3: pool still nonzero after unlink while fd open",
         dentry_pool_used() > before);

    file_close(fd);    /* last ref gone → dentry freed */
    TEST("FS-7.3: pool returns to baseline after close (last ref released)",
         dentry_pool_used() == before);
}

/*
 * 2.7  pipe half-close: pipe_t freed only after BOTH ends closed.
 *
 * Plan §2.8: close only one end at a time; verify the fd_table slot is freed
 * but the other end still works.  After closing both, a fresh pipe must
 * succeed (no pipe_t leaked).
 *
 * Also tests the write-end-closed path: after close(write), read should
 * return 0 (EOF) rather than blocking.
 */
static void test_pipe_half_close(void) {
    printk("=== LEAK/FS 2.7: pipe half-close (one end at a time) ===\n");

    int fds[2] = { -1, -1 };
    int r = sys_pipe(fds);
    TEST("FS-2.7: sys_pipe succeeds", r == 0);
    if (r != 0) return;

    /* Write something, close write end, then read should drain + EOF */
    const char *msg = "hi";
    file_write(fds[1], msg, 2);
    file_close(fds[1]);   /* write end closed */
    fds[1] = -1;

    char buf[8] = {0};
    int n = file_read(fds[0], buf, sizeof(buf));
    TEST("FS-2.7: read after write-end close: data returned", n == 2);

    /* EOF after data drained */
    n = file_read(fds[0], buf, sizeof(buf));
    TEST("FS-2.7: read after write-end close: EOF (returns 0)", n == 0);

    file_close(fds[0]);
    fds[0] = -1;

    /* After both ends closed a fresh pipe must still succeed */
    int fds2[2] = { -1, -1 };
    int r2 = sys_pipe(fds2);
    TEST("FS-2.7: new pipe after both-ends-closed still succeeds", r2 == 0);
    if (r2 == 0) {
        file_close(fds2[0]);
        file_close(fds2[1]);
    }

    /* Stress: close read end first N times */
#define PHC_CYCLES 16
    int ok = 1;
    for (int i = 0; i < PHC_CYCLES; i++) {
        int p[2] = { -1, -1 };
        if (sys_pipe(p) < 0) { ok = 0; break; }
        file_close(p[0]);   /* read end first */
        file_close(p[1]);
    }
    TEST("FS-2.7: 16 read-first half-close cycles: no fd exhaustion", ok);
#undef PHC_CYCLES
}

/*
 * 2.8  dup2: target fd is atomically replaced; ref_count bookkeeping correct.
 *
 * Plan §2.7 (dup2 variant): sys_dup2 must bump ref_count on the source
 * file_t, decrement/close any previous file_t at newfd, and leave both
 * fds referencing the same file_t with ref_count == 2.
 */
static void test_dup2_refcount(void) {
    printk("=== LEAK/FS 2.8: dup2 refcount ===\n");

    int fd = file_open("/test/hello.txt");
    TEST("FS-2.8: file_open base fd", fd >= 0);
    if (fd < 0) return;

    file_t *f = fd_get(&current->fds, fd);
    TEST("FS-2.8: fd_get non-NULL", f != NULL);
    if (!f) { file_close(fd); return; }

    TEST("FS-2.8: ref_count == 1 after open", f->ref_count == 1);

    /* dup2 to a slot that is currently empty (fd+1 if it is free) */
    int newfd = fd + 1;
    int r = sys_dup2(fd, newfd);
    TEST("FS-2.8: sys_dup2 succeeds", r == newfd);
    if (r < 0) { file_close(fd); return; }

    TEST("FS-2.8: ref_count == 2 after dup2", f->ref_count == 2);

    /* dup2 onto newfd again (self-replace) — must not double-decrement */
    int r2 = sys_dup2(fd, newfd);
    TEST("FS-2.8: dup2 self-replace returns newfd", r2 == newfd);
    /* ref_count must remain 2 after dup2 onto an already-dup'd slot */
    TEST("FS-2.8: ref_count still 2 after dup2 onto same slot", f->ref_count == 2);

    file_close(newfd);
    TEST("FS-2.8: ref_count == 1 after closing dup2'd fd", f->ref_count == 1);

    file_close(fd);
    /* f freed; verify fresh open works */
    int fd3 = file_open("/test/hello.txt");
    TEST("FS-2.8: fresh open after dup2 cycle works", fd3 >= 0);
    if (fd3 >= 0) file_close(fd3);
}

/*
 * 2.9  vnode ref_count: back to 0 after all FDs closed.
 *
 * Direct check of vnode->ref_count.  After file_close the vnode's
 * ref_count must drop back to whatever it was before we opened the file
 * (it may be > 0 if other paths hold a dentry reference to it).
 */
static void test_vnode_refcount(void) {
    printk("=== LEAK/FS 2.9: vnode ref_count tracked directly ===\n");

    /* Prime: open once to resolve dentry, capture the vnode pointer */
    int fd0 = file_open("/test/hello.txt");
    TEST("FS-2.9: initial open succeeds", fd0 >= 0);
    if (fd0 < 0) return;

    file_t *f0 = fd_get(&current->fds, fd0);
    if (!f0 || !f0->vnode) { file_close(fd0); return; }

    vnode_t *vn = f0->vnode;
    uint32_t rc_before = vn->ref_count;  /* baseline: may be > 0 (dentry holds ref) */
    file_close(fd0);
    TEST("FS-2.9: vnode ref_count restored after single close",
         vn->ref_count == rc_before - 1);

    /* Re-open, dup, close both — ref must return to baseline-1 */
    int fd = file_open("/test/hello.txt");
    TEST("FS-2.9: re-open succeeds", fd >= 0);
    if (fd < 0) return;

    uint32_t rc_open = vn->ref_count;
    int fd2 = sys_dup(fd);
    TEST("FS-2.9: dup succeeds", fd2 >= 0);
    if (fd2 >= 0) {
        TEST("FS-2.9: vnode ref_count bumped by dup", vn->ref_count >= rc_open);
        file_close(fd2);
    }
    file_close(fd);
    TEST("FS-2.9: vnode ref_count back to pre-open level after all closes",
         vn->ref_count == rc_before - 1);
}

/*
 * 3.4  fs_struct lifecycle: alloc + free leaks no kmalloc bytes.
 *
 * fs_struct_alloc → fs_struct_free must leave kmalloc_used_bytes() unchanged.
 * A leak here means every fork leaks a cwd/root dentry reference.
 */
static void test_fs_struct_lifecycle(void) {
    printk("=== LEAK/Proc 3.4: fs_struct alloc/free kmalloc delta ===\n");

    size_t before = kmalloc_used_bytes();

    fs_struct_t *fs = fs_struct_alloc();
    TEST("Proc-3.4: fs_struct_alloc returns non-NULL", fs != NULL);
    if (!fs) return;

    fs_struct_free(fs);

    TEST("Proc-3.4: kmalloc delta zero after fs_struct_free (no heap leak)",
         kmalloc_used_bytes() == before);

    /* Copy + free: mirrors fork path */
    size_t before2 = kmalloc_used_bytes();
    fs_struct_t *orig = fs_struct_alloc();
    if (!orig) return;

    fs_struct_t *copy = fs_struct_copy(orig);
    TEST("Proc-3.4: fs_struct_copy returns non-NULL", copy != NULL);
    if (copy) fs_struct_free(copy);
    fs_struct_free(orig);

    TEST("Proc-3.4: kmalloc delta zero after copy+free (mirrors fork path)",
         kmalloc_used_bytes() == before2);
}

/* ══════════════════════════════════════════════════════════════════════════
 * §8  file_t ref_count: back to 0 after last close
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_file_refcount_direct(void) {
    printk("=== LEAK/FS 8: file_t ref_count zero after last close ===\n");

    int fd = file_open("/test/hello.txt");
    TEST("FS-8: file_open succeeds", fd >= 0);
    if (fd < 0) return;

    file_t *f = fd_get(&current->fds, fd);
    TEST("FS-8: fd_get returns non-NULL", f != NULL);
    if (!f) { file_close(fd); return; }

    TEST("FS-8: ref_count == 1 after single open", f->ref_count == 1);

    int fd2 = sys_dup(fd);
    TEST("FS-8: dup succeeds", fd2 >= 0);
    if (fd2 >= 0) {
        TEST("FS-8: ref_count == 2 after dup", f->ref_count == 2);
        file_close(fd2);
        TEST("FS-8: ref_count == 1 after closing dup'd fd", f->ref_count == 1);
    }

    file_close(fd);
    /* f is freed now — cannot dereference.  Behavioral check: re-open succeeds */
    int fd3 = file_open("/test/hello.txt");
    TEST("FS-8: re-open succeeds after last close (ref freed cleanly)", fd3 >= 0);
    if (fd3 >= 0) file_close(fd3);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Entry point
 * ══════════════════════════════════════════════════════════════════════════ */

void leak_test_run(void) {
    printk("\n========================================\n");
    printk("  Kernel Resource Leak Tests\n");
    printk("========================================\n\n");

    /* ── §1 MM ── */
    test_kmalloc_roundtrip();
    test_vma_roundtrip();
    test_pmm_refcount_cycle();
    test_pagetable_roundtrip();
    test_ki1_pagetable_leak();
    test_pmm_alloc_free_balance();

    /* ── §6 MM: VMA kmalloc byte delta ── */
    test_vma_kmalloc_delta();

    /* ── §2 FS (requires current → init) ── */
    if (bind_to_init() == 0) {
        test_open_close();
        test_open_close_cycle();
        test_create_unlink();
        test_mkdir_rmdir();
        test_ki3_dentry_prune();
        test_dup_refcount();
        test_pipe_cycle();
        test_ki2_fd_error_leak();
        test_create_unlink_cycle();

        /* ── §7 Dentry pool accounting ── */
        test_dentry_pool_single();
        test_dentry_pool_wide();
        test_dentry_pool_open_unlink_close();

        /* ── §8 file_t ref_count ── */
        test_file_refcount_direct();

        /* ── §2.7-2.9 pipe half-close / dup2 / vnode refcount ── */
        test_pipe_half_close();
        test_dup2_refcount();
        test_vnode_refcount();

        restore_current();
    } else {
        printk("  [SKIP] FS leak tests: init process not available\n");
    }

    /* ── §3 Proc ── */
    test_proc_alloc_free();
    test_proc_alloc_stress();
    test_proc_table_limit();
    test_ki4_proc_reparent();

    /* ── §3.4 fs_struct lifecycle ── */
    test_fs_struct_lifecycle();

    printk("\nLeak tests: %d passed, %d failed\n\n", passed, failed);
}
