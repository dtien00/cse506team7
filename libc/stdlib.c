#include "stdlib.h"
#include "ctype.h"
#include "string.h"

void _Exit(int status) {
    exit(status);
}

void abort(void) {
    _Exit(1);
}

int atoi(const char* nptr) {
    return (int)strtol(nptr, NULL, 10);
}

long atol(const char* nptr) {
    return strtol(nptr, NULL, 10);
}

long long atoll(const char* nptr) {
    return strtoll(nptr, NULL, 10);
}

static int char_to_digit(char c, int base) {
    int digit;
    if (c >= '0' && c <= '9') {
        digit = c - '0';
    } else if (c >= 'a' && c <= 'z') {
        digit = c - 'a' + 10;
    } else if (c >= 'A' && c <= 'Z') {
        digit = c - 'A' + 10;
    } else {
        return -1;
    }
    return (digit < base) ? digit : -1;
}

long strtol(const char* nptr, char** endptr, int base) {
    const char* s = nptr;
    long result = 0;
    int negative = 0;
    
    while (isspace(*s)) s++;
    
    if (*s == '-') {
        negative = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    
    if (base == 0) {
        if (*s == '0') {
            if (s[1] == 'x' || s[1] == 'X') {
                base = 16;
                s += 2;
            } else {
                base = 8;
                s++;
            }
        } else {
            base = 10;
        }
    } else if (base == 16 && *s == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    
    const char* start = s;
    while (*s) {
        int digit = char_to_digit(*s, base);
        if (digit < 0) break;
        result = result * base + digit;
        s++;
    }
    
    if (endptr) {
        *endptr = (char*)(s > start ? s : nptr);
    }
    
    return negative ? -result : result;
}

unsigned long strtoul(const char* nptr, char** endptr, int base) {
    const char* s = nptr;
    unsigned long result = 0;
    
    while (isspace(*s)) s++;
    
    if (*s == '+') {
        s++;
    }
    
    if (base == 0) {
        if (*s == '0') {
            if (s[1] == 'x' || s[1] == 'X') {
                base = 16;
                s += 2;
            } else {
                base = 8;
                s++;
            }
        } else {
            base = 10;
        }
    } else if (base == 16 && *s == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    
    const char* start = s;
    while (*s) {
        int digit = char_to_digit(*s, base);
        if (digit < 0) break;
        result = result * base + digit;
        s++;
    }
    
    if (endptr) {
        *endptr = (char*)(s > start ? s : nptr);
    }
    
    return result;
}

long long strtoll(const char* nptr, char** endptr, int base) {
    const char* s = nptr;
    long long result = 0;
    int negative = 0;
    
    while (isspace(*s)) s++;
    
    if (*s == '-') {
        negative = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    
    if (base == 0) {
        if (*s == '0') {
            if (s[1] == 'x' || s[1] == 'X') {
                base = 16;
                s += 2;
            } else {
                base = 8;
                s++;
            }
        } else {
            base = 10;
        }
    } else if (base == 16 && *s == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    
    const char* start = s;
    while (*s) {
        int digit = char_to_digit(*s, base);
        if (digit < 0) break;
        result = result * base + digit;
        s++;
    }
    
    if (endptr) {
        *endptr = (char*)(s > start ? s : nptr);
    }
    
    return negative ? -result : result;
}

unsigned long long strtoull(const char* nptr, char** endptr, int base) {
    const char* s = nptr;
    unsigned long long result = 0;
    
    while (isspace(*s)) s++;
    
    if (*s == '+') {
        s++;
    }
    
    if (base == 0) {
        if (*s == '0') {
            if (s[1] == 'x' || s[1] == 'X') {
                base = 16;
                s += 2;
            } else {
                base = 8;
                s++;
            }
        } else {
            base = 10;
        }
    } else if (base == 16 && *s == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    
    const char* start = s;
    while (*s) {
        int digit = char_to_digit(*s, base);
        if (digit < 0) break;
        result = result * base + digit;
        s++;
    }
    
    if (endptr) {
        *endptr = (char*)(s > start ? s : nptr);
    }
    
    return result;
}

int abs(int j) {
    return j < 0 ? -j : j;
}

long labs(long j) {
    return j < 0 ? -j : j;
}

long long llabs(long long j) {
    return j < 0 ? -j : j;
}

div_t div(int numer, int denom) {
    div_t result;
    result.quot = numer / denom;
    result.rem = numer % denom;
    return result;
}

ldiv_t ldiv(long numer, long denom) {
    ldiv_t result;
    result.quot = numer / denom;
    result.rem = numer % denom;
    return result;
}

lldiv_t lldiv(long long numer, long long denom) {
    lldiv_t result;
    result.quot = numer / denom;
    result.rem = numer % denom;
    return result;
}

static unsigned long next_rand = 1;

int rand(void) {
    next_rand = next_rand * 1103515245 + 12345;
    return (int)((next_rand / 65536) & 0x7fffffff);
}

void srand(unsigned int seed) {
    next_rand = seed;
}

char* getenv(const char* name) {
    (void)name;
    return NULL;
}

int setenv(const char* name, const char* value, int overwrite) {
    (void)name;
    (void)value;
    (void)overwrite;
    return -1;
}

int unsetenv(const char* name) {
    (void)name;
    return -1;
}

static void (*atexit_funcs[32])(void);
static int atexit_count = 0;

int atexit(void (*func)(void)) {
    if (atexit_count >= 32) {
        return -1;
    }
    atexit_funcs[atexit_count++] = func;
    return 0;
}

static void swap_bytes(char *a, char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char tmp = a[i];
        a[i] = b[i];
        b[i] = tmp;
    }
}

void qsort(void* base, size_t nmemb, size_t size,
           int (*compar)(const void*, const void*)) {
    if (nmemb <= 1) return;

    char* arr = (char*)base;
    size_t pivot_idx = nmemb / 2;

    swap_bytes(arr + pivot_idx * size, arr + (nmemb - 1) * size, size);

    char* pivot = arr + (nmemb - 1) * size;
    size_t store_idx = 0;

    for (size_t i = 0; i < nmemb - 1; i++) {
        if (compar(arr + i * size, pivot) < 0) {
            if (i != store_idx) {
                swap_bytes(arr + i * size, arr + store_idx * size, size);
            }
            store_idx++;
        }
    }
    
    swap_bytes(arr + store_idx * size, arr + (nmemb - 1) * size, size);

    if (store_idx > 0) {
        qsort(arr, store_idx, size, compar);
    }
    if (store_idx + 1 < nmemb) {
        qsort(arr + (store_idx + 1) * size, nmemb - store_idx - 1, size, compar);
    }
}

void* bsearch(const void* key, const void* base, size_t nmemb, size_t size,
              int (*compar)(const void*, const void*)) {
    const char* arr = (const char*)base;
    size_t low = 0, high = nmemb;
    
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        const void* mid_elem = arr + mid * size;
        int cmp = compar(key, mid_elem);
        
        if (cmp == 0) {
            return (void*)mid_elem;
        } else if (cmp < 0) {
            high = mid;
        } else {
            low = mid + 1;
        }
    }
    
    return NULL;
}