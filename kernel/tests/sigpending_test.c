/*
 * sigpending_test.c — Kernel unit tests for sys_sigpending().
 *
 * Runs in RUN_TESTS mode (before schedule()). proc_sleep() must NOT be called.
 * We manipulate proc structs directly using proc_alloc() and save/restore
 * `current` so each sub-test runs in isolation.
 *
 * sys_sigpending(set) is specified by POSIX as:
 *   "store in *set the signals that are blocked AND pending for the caller"
 *   i.e. *set = pending_signals & signal_mask
 *
 * Tests:
 *   1. NULL set pointer → returns -EINVAL, no fault
 *   2. No signals pending, no mask → *set == 0
 *   3. Signal pending AND masked → appears in *set
 *   4. Signal pending but NOT masked (unblocked) → does NOT appear in *set
 *   5. Multiple signals: only the blocked+pending subset appears
 *   6. All signals masked, none pending → *set == 0
 *   7. Returns 0 on success
 *
 * Note: sys_sigpending uses set_csr_bit(sstatus, SSTATUS_SUM) before writing
 * to the caller-supplied pointer, following the same convention as
 * sys_sigprocmask.  In RUN_TESTS mode `set` is a kernel stack address, so SUM
 * is a no-op but does not cause faults — the tests are still valid.
 */

#include "trap/syscall.h"
#include "process/process.h"
#include "kernel/printk.h"
#include "tests/test.h"

static int passed = 0;
static int failed = 0;

void sigpending_test_run(void) {
    printk("\n========================================\n");
    printk("        sigpending Tests\n");
    printk("========================================\n\n");

    struct proc *saved_current = current;

    /* ── 1. NULL set pointer → -EINVAL ──────────────────────────────────────
     * sys_sigpending must guard against a NULL output pointer.
     */
    printk("=== 1. NULL set pointer returns -EINVAL ===\n");
    {
        struct proc *p = proc_alloc();
        TEST("null-set: proc_alloc", p != NULL);
        if (p) {
            current = p;
            int r = sys_sigpending(NULL);
            TEST("null-set: returns -EINVAL", r < 0);
            proc_free(p);
        }
        current = saved_current;
    }

    /* ── 2. No signals pending, no mask → *set == 0 ─────────────────────────
     * Fresh proc: pending_signals = 0, signal_mask = 0.
     * sigpending must return 0 and write 0 into *set.
     */
    printk("=== 2. No pending signals, no mask → 0 ===\n");
    {
        struct proc *p = proc_alloc();
        TEST("empty: proc_alloc", p != NULL);
        if (p) {
            current = p;
            p->pending_signals = 0;
            p->signal_mask     = 0;

            uint32_t out = 0xdeadbeef;
            int r = sys_sigpending(&out);
            TEST("empty: returns 0", r == 0);
            TEST("empty: *set == 0", out == 0);
            proc_free(p);
        }
        current = saved_current;
    }

    /* ── 3. Signal pending AND masked → appears in *set ─────────────────────
     * SIGTERM (15) is pending and blocked.  sigpending must report it.
     */
    printk("=== 3. Pending AND masked signal appears in *set ===\n");
    {
        struct proc *p = proc_alloc();
        TEST("pending-masked: proc_alloc", p != NULL);
        if (p) {
            current = p;
            p->pending_signals = (1u << KERNEL_SIGTERM);
            p->signal_mask     = (1u << KERNEL_SIGTERM);   /* mask == block */

            uint32_t out = 0;
            int r = sys_sigpending(&out);
            TEST("pending-masked: returns 0", r == 0);
            TEST("pending-masked: SIGTERM bit set in *set",
                 out & (1u << KERNEL_SIGTERM));
            proc_free(p);
        }
        current = saved_current;
    }

    /* ── 4. Signal pending but NOT masked → does NOT appear in *set ─────────
     * POSIX: sigpending reports only signals blocked from delivery.
     * An unmasked pending signal is deliverable immediately — it is not in the
     * "pending and blocked" set reported by sigpending.
     */
    printk("=== 4. Pending but unmasked signal does NOT appear in *set ===\n");
    {
        struct proc *p = proc_alloc();
        TEST("pending-unmasked: proc_alloc", p != NULL);
        if (p) {
            current = p;
            p->pending_signals = (1u << KERNEL_SIGTERM);
            p->signal_mask     = 0;   /* SIGTERM is NOT blocked */

            uint32_t out = 0xffffffff;
            int r = sys_sigpending(&out);
            TEST("pending-unmasked: returns 0", r == 0);
            TEST("pending-unmasked: SIGTERM bit NOT set in *set",
                 !(out & (1u << KERNEL_SIGTERM)));
            proc_free(p);
        }
        current = saved_current;
    }

    /* ── 5. Mixed: only blocked+pending subset appears ──────────────────────
     * SIGTERM (15): pending + masked  → should appear
     * SIGINT  (2):  pending + unmasked → should NOT appear
     * SIGKILL (9):  masked but NOT pending → should NOT appear
     */
    printk("=== 5. Mixed signals: only blocked+pending subset in *set ===\n");
    {
        struct proc *p = proc_alloc();
        TEST("mixed: proc_alloc", p != NULL);
        if (p) {
            current = p;
            p->pending_signals = (1u << KERNEL_SIGTERM) | (1u << KERNEL_SIGINT);
            /* Mask SIGTERM and SIGKILL, but not SIGINT */
            p->signal_mask     = (1u << KERNEL_SIGTERM) | (1u << KERNEL_SIGKILL);

            uint32_t out = 0;
            int r = sys_sigpending(&out);
            TEST("mixed: returns 0", r == 0);
            TEST("mixed: SIGTERM (pending+masked) appears",
                 out & (1u << KERNEL_SIGTERM));
            TEST("mixed: SIGINT (pending but unmasked) absent",
                 !(out & (1u << KERNEL_SIGINT)));
            TEST("mixed: SIGKILL (masked but not pending) absent",
                 !(out & (1u << KERNEL_SIGKILL)));
            proc_free(p);
        }
        current = saved_current;
    }

    /* ── 6. All signals masked, none pending → *set == 0 ────────────────────
     */
    printk("=== 6. All masked, none pending → 0 ===\n");
    {
        struct proc *p = proc_alloc();
        TEST("all-masked-none-pending: proc_alloc", p != NULL);
        if (p) {
            current = p;
            p->pending_signals = 0;
            p->signal_mask     = 0xffffffff;

            uint32_t out = 0xdeadbeef;
            int r = sys_sigpending(&out);
            TEST("all-masked-none-pending: returns 0", r == 0);
            TEST("all-masked-none-pending: *set == 0", out == 0);
            proc_free(p);
        }
        current = saved_current;
    }

    /* ── 7. Return value is 0 on success ─────────────────────────────────── */
    printk("=== 7. Return value 0 on success ===\n");
    {
        struct proc *p = proc_alloc();
        TEST("retval: proc_alloc", p != NULL);
        if (p) {
            current = p;
            p->pending_signals = (1u << KERNEL_SIGTERM);
            p->signal_mask     = (1u << KERNEL_SIGTERM);

            uint32_t out = 0;
            int r = sys_sigpending(&out);
            TEST("retval: sys_sigpending returns 0", r == 0);
            proc_free(p);
        }
        current = saved_current;
    }

    printk("\n========================================\n");
    printk("  Results: %d passed, %d failed\n", passed, failed);
    printk("========================================\n\n");
}
