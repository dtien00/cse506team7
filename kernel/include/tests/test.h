#pragma once

// PASSED AND FAILED MUST BE DEFINED IN THE TEST FILES THEMSELVES, NOT HERE, WHEN TEST IS USED
#define TEST(name, cond)                                    \
    do {                                                    \
        if (cond) {                                         \
            printk("  [PASS] %s\n", name);                \
            passed++;                                       \
        } else {                                            \
            printk("  [FAIL] %s  (%s:%d)\n", name,       \
                    __FILE__, __LINE__);                    \
            failed++;                                       \
        }                                                   \
    } while (0)

void mem_test_run(void);
void fs_test_run(void);
void proc_test_run(void);
void proc_waitpid_test_run(void);
void procinfo_test_run(void);
void sched_test_run(void);
void sched_timeslice_test_run(void);
void sched_priority_test_run(void);
void sched_fairness_test_run(void);
void sched_idle_test_run(void);
void sched_nice_test_run(void);
void libc_syscall_test_run(void);
void timer_test_run(void);
void kill_test_run(void);
void sigaction_test_run(void);
void cow_test_run(void);
void stack_growth_test_run(void);
void ext2_populate_test_run(void);
void accounting_test_run(void);
void rtc_test_run(void);
