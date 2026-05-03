#pragma once

#define TEST_MAX_FAILURES 25

void test_record(int passed_case);
void test_record_failure(const char *name, const char *file, int line);
void test_summary_reset(void);
void test_summary_report(void);

// PASSED AND FAILED MUST BE DEFINED IN THE TEST FILES THEMSELVES, NOT HERE, WHEN TEST IS USED
#define TEST(name, cond)                                    \
    do {                                                    \
        if (cond) {                                         \
            printk("  [PASS] %s\n", name);                \
            passed++;                                       \
            test_record(1);                                 \
        } else {                                            \
            printk("  [FAIL] %s  (%s:%d)\n", name,       \
                    __FILE__, __LINE__);                    \
            failed++;                                       \
            test_record_failure(name, __FILE__, __LINE__); \
        }                                                   \
    } while (0)

void mem_test_run(void);
void fs_test_run(void);
void proc_test_run(void);
void proc_waitpid_test_run(void);
void procinfo_test_run(void);
void procfs_test_run(void);
void sched_test_run(void);
void sched_timeslice_test_run(void);
void sched_priority_test_run(void);
void sched_fairness_test_run(void);
void sched_idle_test_run(void);
void sched_nice_test_run(void);
void libc_syscall_test_run(void);
void timer_test_run(void);
void kill_test_run(void);
void pgrp_test_run(void);
void sigaction_test_run(void);
void sigpending_test_run(void);
void cow_test_run(void);
void stack_growth_test_run(void);
void ext2_populate_test_run(void);
void accounting_test_run(void);
void rtc_test_run(void);
void jobctl_test_run(void);
void sig_flag_test_run(void);
void kstack_overflow_test_run(void);
void leak_test_run(void);
void mm_vma_test_run(void);
