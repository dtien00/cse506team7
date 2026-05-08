#include "unistd.h"
#include "sys/wait.h"
#include "stdlib.h"
#include "us_test_common.h"

void run_test_cow(us_test_group_t *group) {
    ut_group_begin(group, "cow_fork");

    /* ── Test 1: child write does not affect parent ──────────────────────── */
    {
        int *shared = malloc(sizeof(int));
        *shared = 42;

        pid_t pid = fork();
        TEST(group, "cow: fork succeeds", pid >= 0);

        if (pid == 0) {
            /* Child: overwrite the COW page */
            *shared = 99;
            _exit(*shared == 99 ? 0 : 1);
        }

        int status = 0;
        waitpid(pid, &status, 0);
        TEST(group, "cow: child exits cleanly",
             WIFEXITED(status) && WEXITSTATUS(status) == 0);
        TEST(group, "cow: parent value unchanged after child write",
             *shared == 42);

        free(shared);
    }

    /* ── Test 2: parent write does not affect child ──────────────────────── */
    {
        int *shared = malloc(sizeof(int));
        *shared = 10;

        pid_t pid = fork();
        TEST(group, "cow: fork succeeds (test 2)", pid >= 0);

        if (pid == 0) {
            /* Child: sleep briefly then verify it still sees 10, not 20 */
            /* (busy-wait: sleep syscall may not be available in all test builds) */
            volatile long spin = 0;
            while (spin < 500000) spin++;
            _exit(*shared == 10 ? 0 : 1);
        }

        /* Parent writes after fork */
        *shared = 20;

        int status = 0;
        waitpid(pid, &status, 0);
        TEST(group, "cow: child saw original value before parent write",
             WIFEXITED(status) && WEXITSTATUS(status) == 0);
        TEST(group, "cow: parent value updated correctly", *shared == 20);

        free(shared);
    }

    /* ── Test 3: multiple forks each get independent copies ─────────────── */
    {
#define NFORKS 4
        int *val = malloc(sizeof(int));
        *val = 0;

        pid_t pids[NFORKS];
        for (int i = 0; i < NFORKS; i++) {
            pids[i] = fork();
            if (pids[i] == 0) {
                /* Each child writes its own index and exits with it */
                *val = i + 1;
                _exit(*val);
            }
        }

        int all_ok = 1;
        for (int i = 0; i < NFORKS; i++) {
            int status = 0;
            waitpid(pids[i], &status, 0);
            if (!WIFEXITED(status) || WEXITSTATUS(status) != i + 1)
                all_ok = 0;
        }
        TEST(group, "cow: each of 4 fork children wrote and exited correctly",
             all_ok);
        TEST(group, "cow: parent value unchanged after 4 children wrote",
             *val == 0);

        free(val);
#undef NFORKS
    }

    /* ── Test 4: COW on stack-allocated variable ─────────────────────────── */
    /* The shell's first action after fork is writing to its own stack (setting
     * up the frame for the execve call).  This test verifies that a COW fault
     * on a stack page is handled correctly: child gets a private copy, parent
     * retains the original value. */
    {
        int local = 100;
        pid_t pid = fork();
        TEST(group, "cow: stack-var fork succeeded", pid >= 0);
        if (pid == 0) {
            local = 200;   /* triggers COW fault on the stack page */
            _exit(local == 200 ? 0 : 1);
        }
        if (pid > 0) {
            int status = 0;
            waitpid(pid, &status, 0);
            TEST(group, "cow: child stack write isolated from parent",
                 WIFEXITED(status) && WEXITSTATUS(status) == 0);
            TEST(group, "cow: parent stack variable unchanged after child write",
                 local == 100);
        }
    }

    /* ── Test 5: multi-generation COW — grandchild writes to stack ──────── */
    /* Feedback scenario: A forks B, B forks C, C writes to a stack variable.
     *
     * After A→B fork, shared pages get PTE_COW set (PTE_W cleared).  When B
     * then forks C, vmm_copy_pagetable must preserve the existing PTE_COW on
     * pages that are already COW-marked (pte & ~PTE_W keeps bit 8 intact).
     * If PTE_COW is accidentally dropped, C's stack ends up read-only with no
     * COW designation; C's first stack write triggers a protection fault and
     * the kernel kills C instead of giving it a private copy.
     *
     * Verifies: grandchild survives its own stack writes and parent/child
     * stack variables remain independent across all three generations. */
    {
        int a_val = 1;

        pid_t pid_b = fork();   /* A → B */
        TEST(group, "cow-multigen: first fork (A->B) succeeded", pid_b >= 0);
        if (pid_b < 0) goto multigen_done;

        if (pid_b == 0) {
            /* ── We are B ── */
            int b_val = 2;

            pid_t pid_c = fork();   /* B → C */
            if (pid_c < 0) _exit(1);

            if (pid_c == 0) {
                /* ── We are C ──
                 * Write to multiple stack variables to guarantee at least one
                 * COW fault fires on a page that was already COW-marked from
                 * the A→B fork and had its COW flag preserved through B→C. */
                b_val = 10;         /* write to B's former COW stack page     */
                int c_local = 20;   /* write to C's own stack frame           */
                c_local += b_val;   /* use both so compiler keeps them alive  */
                _exit(c_local == 30 ? 0 : 1);
            }

            /* B waits for C */
            int st = 0;
            waitpid(pid_c, &st, 0);
            /* b_val must still be 2 — C's write got its own private copy */
            _exit((WIFEXITED(st) && WEXITSTATUS(st) == 0 && b_val == 2) ? 0 : 1);
        }

        /* A waits for B */
        {
            int st = 0;
            waitpid(pid_b, &st, 0);
            TEST(group, "cow-multigen: grandchild (C) writes to stack and exits cleanly",
                 WIFEXITED(st) && WEXITSTATUS(st) == 0);
            TEST(group, "cow-multigen: grandparent (A) stack variable unchanged",
                 a_val == 1);
        }
    }
multigen_done:;

    ut_group_end(group);
}
