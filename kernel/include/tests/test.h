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
