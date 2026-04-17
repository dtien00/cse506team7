/*
 * test_libc_time.c — tests for localtime, gmtime, mktime, asctime, ctime,
 *                     strftime, and clock_gettime validation.
 *
 * These verify the real implementations that replaced the old stubs.
 */

#include <time.h>
#include <string.h>
#include <sys/time.h>
#include "us_test_common.h"

/* Known epoch values for verification:
 *   0           = Thu Jan  1 00:00:00 UTC 1970
 *   1000000000  = Sun Sep  9 01:46:40 UTC 2001
 *   1713388800  = Thu Apr 18 00:00:00 UTC 2024
 */

void run_test_libc_time(us_test_group_t groups[], int *idx) {
    /* ---- group 1: localtime / gmtime ---- */
    {
        us_test_group_t *g = &groups[(*idx)++];
        ut_group_begin(g, "libc_time/localtime_gmtime");

        /* epoch 0 = 1970-01-01 00:00:00 Thu */
        {
            time_t t = 0;
            struct tm *tm = localtime(&t);
            TEST(g, "epoch0: tm != NULL",     tm != (void*)0);
            TEST(g, "epoch0: year == 70",     tm->tm_year == 70);
            TEST(g, "epoch0: mon  == 0",      tm->tm_mon  == 0);
            TEST(g, "epoch0: mday == 1",      tm->tm_mday == 1);
            TEST(g, "epoch0: hour == 0",      tm->tm_hour == 0);
            TEST(g, "epoch0: min  == 0",      tm->tm_min  == 0);
            TEST(g, "epoch0: sec  == 0",      tm->tm_sec  == 0);
            TEST(g, "epoch0: wday == 4 (Thu)",tm->tm_wday == 4);
            TEST(g, "epoch0: yday == 0",      tm->tm_yday == 0);
        }

        /* 1000000000 = 2001-09-09 01:46:40 Sun */
        {
            time_t t = 1000000000L;
            struct tm *tm = gmtime(&t);
            TEST(g, "1e9: year == 101",       tm->tm_year == 101);
            TEST(g, "1e9: mon  == 8 (Sep)",   tm->tm_mon  == 8);
            TEST(g, "1e9: mday == 9",         tm->tm_mday == 9);
            TEST(g, "1e9: hour == 1",         tm->tm_hour == 1);
            TEST(g, "1e9: min  == 46",        tm->tm_min  == 46);
            TEST(g, "1e9: sec  == 40",        tm->tm_sec  == 40);
            TEST(g, "1e9: wday == 0 (Sun)",   tm->tm_wday == 0);
        }

        /* current time should produce year >= 2025 */
        {
            time_t now = time(NULL);
            struct tm *tm = localtime(&now);
            TEST(g, "now: year >= 125 (2025+)", tm->tm_year >= 125);
            TEST(g, "now: mon in [0,11]",       tm->tm_mon >= 0 && tm->tm_mon <= 11);
            TEST(g, "now: mday in [1,31]",      tm->tm_mday >= 1 && tm->tm_mday <= 31);
            TEST(g, "now: hour in [0,23]",      tm->tm_hour >= 0 && tm->tm_hour <= 23);
        }

        /* localtime(NULL) returns NULL */
        {
            struct tm *tm = localtime((void*)0);
            TEST(g, "localtime(NULL) == NULL", tm == (void*)0);
        }

        ut_group_end(g);
    }

    /* ---- group 2: mktime ---- */
    {
        us_test_group_t *g = &groups[(*idx)++];
        ut_group_begin(g, "libc_time/mktime");

        /* roundtrip: localtime(t) -> mktime -> should give back t */
        {
            time_t t = 1000000000L;
            struct tm tm;
            struct tm *orig = localtime(&t);
            /* copy since localtime uses static buffer */
            memcpy(&tm, orig, sizeof(tm));
            time_t result = mktime(&tm);
            TEST(g, "mktime roundtrip 1e9", result == t);
        }

        /* epoch 0 roundtrip */
        {
            time_t t = 0;
            struct tm tm;
            memcpy(&tm, localtime(&t), sizeof(tm));
            time_t result = mktime(&tm);
            TEST(g, "mktime roundtrip epoch0", result == 0);
        }

        /* mktime(NULL) returns -1 */
        {
            time_t result = mktime((void*)0);
            TEST(g, "mktime(NULL) == -1", result == (time_t)-1);
        }

        ut_group_end(g);
    }

    /* ---- group 3: asctime / ctime ---- */
    {
        us_test_group_t *g = &groups[(*idx)++];
        ut_group_begin(g, "libc_time/asctime_ctime");

        /* asctime of epoch 0 should contain "Thu Jan  1" and "1970" */
        {
            time_t t = 0;
            struct tm *tm = localtime(&t);
            char *s = asctime(tm);
            TEST(g, "asctime(epoch0) not empty", s[0] != '\0');
            TEST(g, "asctime contains 'Thu'",    strstr(s, "Thu") != (void*)0);
            TEST(g, "asctime contains 'Jan'",    strstr(s, "Jan") != (void*)0);
            TEST(g, "asctime contains '1970'",   strstr(s, "1970") != (void*)0);
        }

        /* ctime should produce same result */
        {
            time_t t = 0;
            char *s = ctime(&t);
            TEST(g, "ctime(epoch0) contains '1970'", strstr(s, "1970") != (void*)0);
        }

        /* ctime of current time should contain current year */
        {
            time_t now = time(NULL);
            char *s = ctime(&now);
            TEST(g, "ctime(now) not empty", s[0] != '\0');
            /* at minimum should have a 4-digit year >= 2025 */
            TEST(g, "ctime(now) contains '202'", strstr(s, "202") != (void*)0);
        }

        ut_group_end(g);
    }

    /* ---- group 4: strftime ---- */
    {
        us_test_group_t *g = &groups[(*idx)++];
        ut_group_begin(g, "libc_time/strftime");

        /* format epoch 0 */
        {
            time_t t = 0;
            struct tm *tm = localtime(&t);
            char buf[64];

            size_t n = strftime(buf, sizeof(buf), "%Y-%m-%d", tm);
            TEST(g, "strftime %%Y-%%m-%%d len > 0", n > 0);
            TEST(g, "strftime epoch0 == '1970-01-01'",
                 strcmp(buf, "1970-01-01") == 0);
        }

        /* format with time fields */
        {
            time_t t = 1000000000L;
            struct tm *tm = localtime(&t);
            char buf[64];

            strftime(buf, sizeof(buf), "%H:%M:%S", tm);
            TEST(g, "strftime 1e9 time == '01:46:40'",
                 strcmp(buf, "01:46:40") == 0);
        }

        /* %a %b day-of-week and month abbreviations */
        {
            time_t t = 0;
            struct tm *tm = localtime(&t);
            char buf[64];

            strftime(buf, sizeof(buf), "%a %b", tm);
            TEST(g, "strftime epoch0 %%a %%b == 'Thu Jan'",
                 strcmp(buf, "Thu Jan") == 0);
        }

        /* %Z should produce UTC */
        {
            time_t t = 0;
            struct tm *tm = localtime(&t);
            char buf[16];
            strftime(buf, sizeof(buf), "%Z", tm);
            TEST(g, "strftime %%Z == 'UTC'", strcmp(buf, "UTC") == 0);
        }

        /* edge: zero-length buffer */
        {
            time_t t = 0;
            struct tm *tm = localtime(&t);
            char buf[1] = {'X'};
            size_t n = strftime(buf, 0, "%Y", tm);
            TEST(g, "strftime max=0 returns 0", n == 0);
        }

        /* edge: NULL format */
        {
            time_t t = 0;
            struct tm *tm = localtime(&t);
            char buf[16];
            size_t n = strftime(buf, sizeof(buf), (void*)0, tm);
            TEST(g, "strftime NULL fmt returns 0", n == 0);
        }

        ut_group_end(g);
    }

    /* ---- group 5: clock_gettime validation ---- */
    {
        us_test_group_t *g = &groups[(*idx)++];
        ut_group_begin(g, "libc_time/clock_gettime_validation");

        /* valid clock IDs should succeed */
        {
            struct timespec ts;
            int rc0 = clock_gettime(CLOCK_REALTIME, &ts);
            TEST(g, "CLOCK_REALTIME returns 0", rc0 == 0);

            int rc1 = clock_gettime(CLOCK_MONOTONIC, &ts);
            TEST(g, "CLOCK_MONOTONIC returns 0", rc1 == 0);

            int rc2 = clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
            TEST(g, "CLOCK_PROCESS_CPUTIME_ID returns 0", rc2 == 0);

            int rc3 = clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
            TEST(g, "CLOCK_THREAD_CPUTIME_ID returns 0", rc3 == 0);
        }

        /* invalid clock ID should fail */
        {
            struct timespec ts;
            int rc = clock_gettime(999, &ts);
            TEST(g, "clock_gettime(999) returns -1", rc == -1);
        }

        /* negative clock ID should fail */
        {
            struct timespec ts;
            int rc = clock_gettime(-1, &ts);
            TEST(g, "clock_gettime(-1) returns -1", rc == -1);
        }

        ut_group_end(g);
    }
}
