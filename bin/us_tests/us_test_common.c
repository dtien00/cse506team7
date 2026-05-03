#include "us_test_common.h"

static int g_total_passed = 0;
static int g_total_failed = 0;
static int g_total_skipped = 0;

#define MAX_FAIL_LOG 256
typedef struct { const char *group; const char *test; const char *file; int line; } fail_entry_t;
static fail_entry_t g_fail_log[MAX_FAIL_LOG];
static int g_fail_count = 0;

void ut_suite_begin(const char *suite_name) {
    printf("\n========================================\n");
    printf("  %s\n", suite_name);
    printf("========================================\n");
}

void ut_group_begin(us_test_group_t *group, const char *name) {
    group->name = name;
    group->passed = 0;
    group->failed = 0;
    group->skipped = 0;
    printf("\n[%s]\n", name);
}

void ut_group_end(const us_test_group_t *group) {
    printf("[%s] passed=%d failed=%d skipped=%d\n",
           group->name,
           group->passed,
           group->failed,
           group->skipped);
}

void ut_check(us_test_group_t *group, int cond, const char *name, const char *file, int line) {
    if (cond) {
        group->passed++;
        g_total_passed++;
        printf("  [PASS] %s\n", name);
    } else {
        group->failed++;
        g_total_failed++;
        printf("  [FAIL] %s: %s (%s:%d)\n", group->name ? group->name : "(unnamed)", name, file, line);
        if (g_fail_count < MAX_FAIL_LOG) {
            g_fail_log[g_fail_count].group = group->name ? group->name : "(unnamed)";
            g_fail_log[g_fail_count].test  = name;
            g_fail_log[g_fail_count].file  = file;
            g_fail_log[g_fail_count].line  = line;
            g_fail_count++;
        }
    }
}

void ut_skip(us_test_group_t *group, const char *name, const char *reason) {
    group->skipped++;
    g_total_skipped++;
    printf("  [SKIP] %s: %s\n", name, reason);
}

int ut_suite_end(const us_test_group_t *groups, int count) {
    int i;

    printf("\n---------------- per-group summary ----------------\n");
    for (i = 0; i < count; i++) {
        printf("  %-16s pass=%d fail=%d skip=%d\n",
               groups[i].name ? groups[i].name : "(unnamed)",
               groups[i].passed,
               groups[i].failed,
               groups[i].skipped);
    }

    printf("---------------------------------------------------\n");
    printf("TOTAL: pass=%d fail=%d skip=%d\n", g_total_passed, g_total_failed, g_total_skipped);

    if (g_fail_count > 0) {
        int show = g_fail_count < 25 ? g_fail_count : 25;
        printf("\n---------------- failed tests (showing %d of %d) ----\n", show, g_fail_count);
        for (i = 0; i < show; i++)
            printf("  [FAIL] %s: %s (%s:%d)\n",
                   g_fail_log[i].group, g_fail_log[i].test,
                   g_fail_log[i].file, g_fail_log[i].line);
        if (g_fail_count > 25)
            printf("  ... and %d more\n", g_fail_count - 25);
        printf("---------------------------------------------------\n");
    }

    printf("===================================================\n\n");

    return (g_total_failed == 0) ? 0 : 1;
}

