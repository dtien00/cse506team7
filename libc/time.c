#include "time.h"
#include "stddef.h"
#include "unistd.h"
#include "string.h"
#include "stdio.h"

time_t time(time_t *tloc) {
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) {
        if (tloc) *tloc = (time_t)-1;
        return (time_t)-1;
    }
    if (tloc) *tloc = (time_t)tv.tv_sec;
    return (time_t)tv.tv_sec;
}

int clock_gettime(clockid_t clock_id, struct timespec *tp) {
    if (clock_id != CLOCK_REALTIME && clock_id != CLOCK_MONOTONIC &&
        clock_id != CLOCK_PROCESS_CPUTIME_ID && clock_id != CLOCK_THREAD_CPUTIME_ID)
        return -1;   /* EINVAL — unknown clock */
    if (!tp) return -1;
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) return -1;
    tp->tv_sec  = tv.tv_sec;
    tp->tv_nsec = tv.tv_usec * 1000L;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Gregorian calendar helpers                                        */
/* ------------------------------------------------------------------ */

static int is_leap(int y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static const int days_in_month[12] = {31,28,31,30,31,30,31,31,30,31,30,31};

static struct tm static_tm;

static struct tm *epoch_to_tm(time_t t, struct tm *out) {
    long ts = (long)t;
    int rem = (int)(ts % 86400);
    if (rem < 0) rem += 86400;          /* handle negative timestamps */

    out->tm_sec  = rem % 60; rem /= 60;
    out->tm_min  = rem % 60;
    out->tm_hour = rem / 60;

    int days = (int)(ts / 86400);
    if (ts < 0 && ts % 86400 != 0) days--;   /* floor division */

    out->tm_wday = ((days % 7) + 4 + 7) % 7; /* Jan 1 1970 = Thu(4) */

    int y = 1970;
    if (days >= 0) {
        while (1) {
            int dy = is_leap(y) ? 366 : 365;
            if (days < dy) break;
            days -= dy;
            y++;
        }
    } else {
        while (days < 0) {
            y--;
            days += is_leap(y) ? 366 : 365;
        }
    }
    out->tm_year = y - 1900;
    out->tm_yday = days;

    int m = 0;
    while (m < 12) {
        int d = days_in_month[m];
        if (m == 1 && is_leap(y)) d = 29;
        if (days < d) break;
        days -= d;
        m++;
    }
    out->tm_mon  = m;
    out->tm_mday = days + 1;
    out->tm_isdst = 0;
    return out;
}

struct tm* localtime(const time_t *timep) {
    if (!timep) return NULL;
    return epoch_to_tm(*timep, &static_tm);
}

struct tm* gmtime(const time_t *timep) {
    return localtime(timep);   /* no timezone support — always UTC */
}

time_t mktime(struct tm *tm) {
    if (!tm) return (time_t)-1;
    int y = tm->tm_year + 1900;
    int m = tm->tm_mon;
    long days = 0;

    /* years */
    if (y >= 1970) {
        for (int i = 1970; i < y; i++)
            days += is_leap(i) ? 366 : 365;
    } else {
        for (int i = y; i < 1970; i++)
            days -= is_leap(i) ? 366 : 365;
    }
    /* months */
    for (int i = 0; i < m; i++) {
        days += days_in_month[i];
        if (i == 1 && is_leap(y)) days++;
    }
    days += tm->tm_mday - 1;

    time_t t = (time_t)days * 86400
             + tm->tm_hour * 3600
             + tm->tm_min  * 60
             + tm->tm_sec;

    /* normalise the struct */
    epoch_to_tm(t, tm);
    return t;
}

/* ------------------------------------------------------------------ */
/*  Formatting helpers                                                */
/* ------------------------------------------------------------------ */

static const char *wday_short[7] = {
    "Sun","Mon","Tue","Wed","Thu","Fri","Sat"
};
static const char *mon_short[12] = {
    "Jan","Feb","Mar","Apr","May","Jun",
    "Jul","Aug","Sep","Oct","Nov","Dec"
};

static char asctime_buf[26];

char* asctime(const struct tm *tm) {
    if (!tm) { asctime_buf[0] = '\0'; return asctime_buf; }
    snprintf(asctime_buf, sizeof(asctime_buf),
             "%.3s %.3s %2d %02d:%02d:%02d %d\n",
             wday_short[tm->tm_wday], mon_short[tm->tm_mon],
             tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec,
             tm->tm_year + 1900);
    return asctime_buf;
}

char* ctime(const time_t *timep) {
    return asctime(localtime(timep));
}

/* Minimal strftime — supports the specifiers common in POSIX utilities. */
size_t strftime(char *s, size_t max, const char *format, const struct tm *tm) {
    if (!s || max == 0 || !format || !tm) return 0;

    size_t pos = 0;
    char tmp[32];

#define EMIT(str) do {                          \
    const char *_p = (str);                     \
    while (*_p && pos + 1 < max)                \
        s[pos++] = *_p++;                       \
} while (0)

    for (const char *f = format; *f; f++) {
        if (*f != '%') {
            if (pos + 1 < max) s[pos++] = *f;
            continue;
        }
        f++;
        if (!*f) break;
        switch (*f) {
        case 'a': EMIT(wday_short[tm->tm_wday]); break;
        case 'b': case 'h': EMIT(mon_short[tm->tm_mon]); break;
        case 'd': snprintf(tmp,sizeof(tmp),"%02d",tm->tm_mday); EMIT(tmp); break;
        case 'e': snprintf(tmp,sizeof(tmp),"%2d",tm->tm_mday); EMIT(tmp); break;
        case 'H': snprintf(tmp,sizeof(tmp),"%02d",tm->tm_hour); EMIT(tmp); break;
        case 'M': snprintf(tmp,sizeof(tmp),"%02d",tm->tm_min); EMIT(tmp); break;
        case 'S': snprintf(tmp,sizeof(tmp),"%02d",tm->tm_sec); EMIT(tmp); break;
        case 'Y': snprintf(tmp,sizeof(tmp),"%d",tm->tm_year+1900); EMIT(tmp); break;
        case 'y': snprintf(tmp,sizeof(tmp),"%02d",tm->tm_year%100); EMIT(tmp); break;
        case 'm': snprintf(tmp,sizeof(tmp),"%02d",tm->tm_mon+1); EMIT(tmp); break;
        case 'j': snprintf(tmp,sizeof(tmp),"%03d",tm->tm_yday+1); EMIT(tmp); break;
        case 'n': if (pos+1<max) s[pos++]='\n'; break;
        case 't': if (pos+1<max) s[pos++]='\t'; break;
        case '%': if (pos+1<max) s[pos++]='%'; break;
        case 'Z': EMIT("UTC"); break;
        case 'c':
            snprintf(tmp,sizeof(tmp),"%.3s %.3s %2d %02d:%02d:%02d %d",
                     wday_short[tm->tm_wday], mon_short[tm->tm_mon],
                     tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec,
                     tm->tm_year+1900);
            EMIT(tmp);
            break;
        default:  /* unsupported — emit literal */
            if (pos+1<max) s[pos++] = '%';
            if (pos+1<max) s[pos++] = *f;
            break;
        }
    }
#undef EMIT

    s[pos] = '\0';
    return pos;
}

