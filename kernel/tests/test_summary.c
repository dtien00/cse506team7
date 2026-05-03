#include "kernel/printk.h"
#include "tests/test.h"

static int summary_passed = 0;
static int summary_failed = 0;

struct failure_entry {
    const char *name;
    const char *file;
    int line;
};

static struct failure_entry failures[TEST_MAX_FAILURES];
static int failure_count = 0;

void test_record(int passed_case) {
    if (passed_case)
        summary_passed++;
    else
        summary_failed++;
}

void test_record_failure(const char *name, const char *file, int line) {
    summary_failed++;
    if (failure_count < TEST_MAX_FAILURES) {
        failures[failure_count].name = name;
        failures[failure_count].file = file;
        failures[failure_count].line = line;
        failure_count++;
    }
}

void test_summary_reset(void) {
    summary_passed = 0;
    summary_failed = 0;
    failure_count = 0;
}

void test_summary_report(void) {
    printk("\n========================================\n");
    printk("  Kernel Tests Summary: %d passed, %d failed\n", summary_passed, summary_failed);
    if (failure_count > 0) {
        printk("  Failed cases:\n");
        for (int i = 0; i < failure_count; i++) {
            printk("    [%d] %s  (%s:%d)\n", i + 1,
                   failures[i].name, failures[i].file, failures[i].line);
        }
        if (summary_failed > failure_count) {
            printk("    ... and %d more (limit %d shown)\n",
                   summary_failed - failure_count, TEST_MAX_FAILURES);
        }
    }
    printk("========================================\n");
}
