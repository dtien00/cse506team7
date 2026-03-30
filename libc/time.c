#include "time.h"
#include "unistd.h"

#define SYS_uptime   12
#define SYS_sleep    11

time_t time(time_t *tloc) {
    register long a7 asm("a7") = SYS_uptime;
    register long ret asm("a0");
    asm volatile("ecall" : "=r"(ret) : "r"(a7) : "memory");
    time_t t = (time_t)ret;
    if (tloc) {
        *tloc = t;
    }
    return t;
}

int clock_gettime(int clk_id, struct timespec *tp) {
    if (!tp) return -1;
    time_t t = time(NULL);
    tp->tv_sec = t;
    tp->tv_nsec = 0;
    (void)clk_id;
    return 0;
}

static struct tm global_tm;

struct tm* localtime(const time_t *timep) {
    if (!timep) return NULL;
    time_t t = *timep;
    
    global_tm.tm_sec = (int)(t % 60);
    t /= 60;
    global_tm.tm_min = (int)(t % 60);
    t /= 60;
    global_tm.tm_hour = (int)(t % 24);
    t /= 24;
    
    global_tm.tm_mday = 1;
    global_tm.tm_mon = 0;
    global_tm.tm_year = 70;
    global_tm.tm_wday = 0;
    global_tm.tm_yday = 0;
    global_tm.tm_isdst = 0;
    
    return &global_tm;
}

struct tm* gmtime(const time_t *timep) {
    return localtime(timep);
}

time_t mktime(struct tm *tm) {
    if (!tm) return -1;
    return 0;
}

static char asctime_buf[26];

char* asctime(const struct tm *tm) {
    if (!tm) return NULL;
    
    static const char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    
    int wday = tm->tm_wday;
    int mon = tm->tm_mon;
    if (wday < 0 || wday > 6) wday = 0;
    if (mon < 0 || mon > 11) mon = 0;
    
    char *p = asctime_buf;
    const char *day = days[wday];
    const char *month = months[mon];
    
    *p++ = day[0]; *p++ = day[1]; *p++ = day[2]; *p++ = ' ';
    *p++ = month[0]; *p++ = month[1]; *p++ = month[2]; *p++ = ' ';
    
    int mday = tm->tm_mday;
    if (mday < 10) *p++ = ' ';
    else *p++ = '0' + (mday / 10);
    *p++ = '0' + (mday % 10);
    *p++ = ' ';
    
    int hour = tm->tm_hour;
    *p++ = '0' + (hour / 10);
    *p++ = '0' + (hour % 10);
    *p++ = ':';
    
    int min = tm->tm_min;
    *p++ = '0' + (min / 10);
    *p++ = '0' + (min % 10);
    *p++ = ':';
    
    int sec = tm->tm_sec;
    *p++ = '0' + (sec / 10);
    *p++ = '0' + (sec % 10);
    *p++ = ' ';
    
    int year = 1900 + tm->tm_year;
    *p++ = '0' + (year / 1000);
    *p++ = '0' + ((year / 100) % 10);
    *p++ = '0' + ((year / 10) % 10);
    *p++ = '0' + (year % 10);
    *p++ = '\n';
    *p++ = '\0';
    
    return asctime_buf;
}

char* ctime(const time_t *timep) {
    return asctime(localtime(timep));
}

size_t strftime(char *s, size_t max, const char *format, const struct tm *tm) {
    if (!s || !format || !tm || max == 0) return 0;
    
    size_t written = 0;
    const char *p = format;
    
    while (*p && written < max - 1) {
        if (*p == '%' && p[1]) {
            p++;
            switch (*p) {
                case 'Y': {
                    int year = 1900 + tm->tm_year;
                    if (written + 4 >= max) return 0;
                    s[written++] = '0' + (year / 1000);
                    s[written++] = '0' + ((year / 100) % 10);
                    s[written++] = '0' + ((year / 10) % 10);
                    s[written++] = '0' + (year % 10);
                    break;
                }
                case 'm': {
                    int mon = tm->tm_mon + 1;
                    if (written + 2 >= max) return 0;
                    s[written++] = '0' + (mon / 10);
                    s[written++] = '0' + (mon % 10);
                    break;
                }
                case 'd': {
                    int mday = tm->tm_mday;
                    if (written + 2 >= max) return 0;
                    s[written++] = '0' + (mday / 10);
                    s[written++] = '0' + (mday % 10);
                    break;
                }
                case 'H': {
                    int hour = tm->tm_hour;
                    if (written + 2 >= max) return 0;
                    s[written++] = '0' + (hour / 10);
                    s[written++] = '0' + (hour % 10);
                    break;
                }
                case 'M': {
                    int min = tm->tm_min;
                    if (written + 2 >= max) return 0;
                    s[written++] = '0' + (min / 10);
                    s[written++] = '0' + (min % 10);
                    break;
                }
                case 'S': {
                    int sec = tm->tm_sec;
                    if (written + 2 >= max) return 0;
                    s[written++] = '0' + (sec / 10);
                    s[written++] = '0' + (sec % 10);
                    break;
                }
                case '%':
                    s[written++] = '%';
                    break;
                default:
                    s[written++] = '%';
                    if (written < max - 1) {
                        s[written++] = *p;
                    }
                    break;
            }
        } else {
            s[written++] = *p;
        }
        p++;
    }
    
    s[written] = '\0';
    return written;
}

int nanosleep(const struct timespec *req, struct timespec *rem) {
    if (!req) return -1;
    
    unsigned int seconds = (unsigned int)req->tv_sec;
    if (req->tv_nsec >= 500000000) {
        seconds++;
    }
    
    if (seconds > 0) {
        sleep(seconds);
    }
    
    if (rem) {
        rem->tv_sec = 0;
        rem->tv_nsec = 0;
    }
    
    return 0;
}
