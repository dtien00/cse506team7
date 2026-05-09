#ifndef _UTSNAME_H
#define _UTSNAME_H
#include "sys/types.h"

struct utsname {
    char  sysname[65];
    char  nodename[65];
    char  release[65];
    char  version[65];
    char  machine[65];
};

struct passwd *getpwnam(const char *);
int            getpwnam_r(const char *, struct passwd *, char *, size_t, struct passwd **);
struct passwd *getpwuid(uid_t);
int            getpwuid_r(uid_t, struct passwd *, char *, size_t, struct passwd **);

#endif /* _UTSNAME_H */