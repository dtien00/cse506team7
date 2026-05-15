#ifndef _GETOPT_H
#define _GETOPT_H

/* getopt() and the opt* globals live in <unistd.h> per POSIX */
#include "unistd.h"

/* has_arg values for struct option */
#define no_argument       0
#define required_argument 1
#define optional_argument 2

struct option {
    const char *name;
    int         has_arg;
    int        *flag;
    int         val;
};

/* GNU extensions — not in POSIX */
int getopt_long(int, char * const [], const char *,
                const struct option *, int *);
int getopt_long_only(int, char * const [], const char *,
                     const struct option *, int *);

#endif /* _GETOPT_H */