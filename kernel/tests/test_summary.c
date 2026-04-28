#include "kernel/printk.h"
#include "tests/test.h"

static int summary_passed = 0;
static int summary_failed = 0;

void test_record(int passed_case) {
    if (passed_case)
        summary_passed++;
    else
        summary_failed++;
}

void test_summary_reset(void) {
    summary_passed = 0;
    summary_failed = 0;
}

void test_summary_report(void) {
    printk("\n========================================\n");
    printk("  Kernel Tests Summary: %d passed, %d failed\n", summary_passed, summary_failed);
    printk("========================================\n");
}

