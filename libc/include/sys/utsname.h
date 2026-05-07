#ifndef _UTSNAME_H
#define _UTSNAME_H
#include "sys/types.h"

struct utsname {
    char  sysname[]  // Name of this implementation of the operating system.
    char  nodename[] // Name of this node within the communications network to which this node is attached, if any.
    char  release[]  // Current release level of this implementation.
    char  version[]  // Current version level of this release.
    char  machine[]  // Name of the hardware type on which the system is running.
};

struct passwd *getpwnam(const char *);
int            getpwnam_r(const char *, struct passwd *, char *, size_t, struct passwd **);
struct passwd *getpwuid(uid_t);
int            getpwuid_r(uid_t, struct passwd *, char *, size_t, struct passwd **);

#endif /* _UTSNAME_H */