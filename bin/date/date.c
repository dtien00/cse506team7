/*
 * date — print current UTC date and time.
 *
 * Uses gettimeofday() (backed by the Goldfish RTC) to obtain the Unix
 * timestamp, then converts it to a Gregorian calendar representation.
 * libc's gmtime()/localtime() are stubs, so the conversion is done here.
 *
 * Output format:
 *   Www Mmm DD HH:MM:SS UTC YYYY
 * e.g.
 *   Sat Apr 12 15:30:45 UTC 2026
 */

#include <stdio.h>
#include <sys/time.h>

static int is_leap(int y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static void epoch_to_fields(long ts,
                             int *year, int *mon, int *mday,
                             int *hour, int *min, int *sec,
                             int *wday) {
    /* Time of day */
    int rem = (int)(ts % 86400);
    *sec  = rem % 60; rem /= 60;
    *min  = rem % 60;
    *hour = rem / 60;

    /* Days since epoch (Jan 1 1970 = day 0) */
    int days = (int)(ts / 86400);

    /* Day of week: Jan 1 1970 was a Thursday (4); 0=Sun … 6=Sat */
    *wday = (days + 4) % 7;

    /* Year */
    int y = 1970;
    while (1) {
        int dy = is_leap(y) ? 366 : 365;
        if (days < dy) break;
        days -= dy;
        y++;
    }
    *year = y;

    /* Month */
    static const int dim[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int m = 0;
    while (m < 12) {
        int d = dim[m];
        if (m == 1 && is_leap(y)) d = 29;
        if (days < d) break;
        days -= d;
        m++;
    }
    *mon  = m;           /* 0-based */
    *mday = days + 1;
}

int main(void) {
    static const char *wday_name[7] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    static const char *mon_name[12] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) {
        fprintf(stderr, "date: gettimeofday failed\n");
        return 1;
    }

    int year, mon, mday, hour, min, sec, wday;
    epoch_to_fields((long)tv.tv_sec,
                    &year, &mon, &mday,
                    &hour, &min, &sec, &wday);

    printf("%s %s %2d %02d:%02d:%02d UTC %d\n",
           wday_name[wday], mon_name[mon], mday,
           hour, min, sec, year);

    return 0;
}
