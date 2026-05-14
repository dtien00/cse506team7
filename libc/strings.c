#include "strings.h"
#include "ctype.h"

int strcasecmp(const char *a, const char *b) {
    unsigned char ca, cb;
    do {
        ca = (unsigned char) tolower((unsigned char) *a++);
        cb = (unsigned char) tolower((unsigned char) *b++);
    } while (ca && ca == cb);
    return ca - cb;
}

int strncasecmp(const char *a, const char *b, size_t n) {
    if (!n) return 0;
    unsigned char ca, cb;
    do {
        ca = (unsigned char) tolower((unsigned char) *a++);
        cb = (unsigned char) tolower((unsigned char) *b++);
        if (!ca) break;
    } while (--n && ca == cb);
    return ca - cb;
}

int strcasecmp_l(const char *a, const char *b, locale_t loc) {
    (void) loc;
    return strcasecmp(a, b);
}

int strncasecmp_l(const char *a, const char *b, size_t n, locale_t loc) {
    (void) loc;
    return strncasecmp(a, b, n);
}
