#include "stdlib.h"
#include "ctype.h"
#include "string.h"
#include "signal.h"

void _Exit(int status) {
    exit(status);
}

void abort(void) {
    raise(SIGABRT);
    // Unreachable: SIGABRT is in KERNEL_FATAL_SIGS and unmaskable (sys_sigprocmask strips it).
    // _Exit(134) was the pre-SIGS_2 fallback (128 + SIGABRT shell convention).
    _Exit(134);
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

static const char* parse_unsigned_prefix(const char *s, int *base, int *consumed_zero) {
    *consumed_zero = 0;

    if (*base == 0) {
        if (*s == '0') {
            if (s[1] == 'x' || s[1] == 'X') {
                *base = 16;
                return s + 2;
            }
            *base = 8;
            *consumed_zero = 1;
            return s + 1;
        }
        *base = 10;
    } else if (*base == 16 && *s == '0' && (s[1] == 'x' || s[1] == 'X')) {
        return s + 2;
    }

    return s;
}

long strtol(const char* nptr, char** endptr, int base) {
    const char* s = nptr;
    long result = 0;
    int negative = 0;
    int consumed_zero = 0;
    
    while (isspace(*s)) s++;
    
    if (*s == '-') {
        negative = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    
    s = parse_unsigned_prefix(s, &base, &consumed_zero);
    
    const char* start = s;
    while (*s) {
        int digit = char_to_digit(*s, base);
        if (digit < 0) break;
        result = result * base + digit;
        s++;
    }
    
    if (endptr) {
        if (s > start || consumed_zero) {
            *endptr = (char*)s;
        } else {
            *endptr = (char*)nptr;
        }
    }
    
    return negative ? -result : result;
}

unsigned long strtoul(const char* nptr, char** endptr, int base) {
    const char* s = nptr;
    unsigned long result = 0;
    int consumed_zero = 0;
    
    while (isspace(*s)) s++;
    
    if (*s == '+') {
        s++;
    }
    
    s = parse_unsigned_prefix(s, &base, &consumed_zero);
    
    const char* start = s;
    while (*s) {
        int digit = char_to_digit(*s, base);
        if (digit < 0) break;
        result = result * base + digit;
        s++;
    }
    
    if (endptr) {
        if (s > start || consumed_zero) {
            *endptr = (char*)s;
        } else {
            *endptr = (char*)nptr;
        }
    }
    
    return result;
}

long long strtoll(const char* nptr, char** endptr, int base) {
    const char* s = nptr;
    long long result = 0;
    int negative = 0;
    int consumed_zero = 0;
    
    while (isspace(*s)) s++;
    
    if (*s == '-') {
        negative = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    
    s = parse_unsigned_prefix(s, &base, &consumed_zero);
    
    const char* start = s;
    while (*s) {
        int digit = char_to_digit(*s, base);
        if (digit < 0) break;
        result = result * base + digit;
        s++;
    }
    
    if (endptr) {
        if (s > start || consumed_zero) {
            *endptr = (char*)s;
        } else {
            *endptr = (char*)nptr;
        }
    }
    
    return negative ? -result : result;
}

unsigned long long strtoull(const char* nptr, char** endptr, int base) {
    const char* s = nptr;
    unsigned long long result = 0;
    int consumed_zero = 0;
    
    while (isspace(*s)) s++;
    
    if (*s == '+') {
        s++;
    }
    
    s = parse_unsigned_prefix(s, &base, &consumed_zero);
    
    const char* start = s;
    while (*s) {
        int digit = char_to_digit(*s, base);
        if (digit < 0) break;
        result = result * base + digit;
        s++;
    }
    
    if (endptr) {
        if (s > start || consumed_zero) {
            *endptr = (char*)s;
        } else {
            *endptr = (char*)nptr;
        }
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

/* Simple in-process environment store: array of "KEY=VALUE" strings. */
#define ENV_MAX 64
static char *_env_store[ENV_MAX];  /* NULL-terminated pointers */
static int   _env_count = 0;

/* Find the index of name in _env_store, or -1 if not found. */
static int env_find(const char *name) {
    size_t nlen = strlen(name);
    for (int i = 0; i < _env_count; i++) {
        if (_env_store[i] &&
            strncmp(_env_store[i], name, nlen) == 0 &&
            _env_store[i][nlen] == '=')
            return i;
    }
    return -1;
}

char* getenv(const char* name) {
    int idx = env_find(name);
    if (idx < 0) return NULL;
    return _env_store[idx] + strlen(name) + 1; /* skip "NAME=" */
}

int setenv(const char* name, const char* value, int overwrite) {
    int idx = env_find(name);
    if (idx >= 0 && !overwrite)
        return 0; /* exists, keep old value */

    size_t nlen = strlen(name);
    size_t vlen = strlen(value);
    char *entry = malloc(nlen + 1 + vlen + 1); /* "NAME=VALUE\0" */
    if (!entry) return -1;
    memcpy(entry, name, nlen);
    entry[nlen] = '=';
    memcpy(entry + nlen + 1, value, vlen + 1);

    if (idx >= 0) {
        free(_env_store[idx]);
        _env_store[idx] = entry;
    } else {
        if (_env_count >= ENV_MAX) { free(entry); return -1; }
        _env_store[_env_count++] = entry;
    }
    return 0;
}

int unsetenv(const char* name) {
    int idx = env_find(name);
    if (idx < 0) return 0; /* not present — success per POSIX */
    free(_env_store[idx]);
    /* Compact the array */
    _env_store[idx] = _env_store[--_env_count];
    _env_store[_env_count] = NULL;
    return 0;
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
