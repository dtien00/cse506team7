#ifndef _SYS_MMAN_H
#define _SYS_MMAN_H

#include "sys/types.h"

/* Memory protection flags — PROT_* (POSIX.1-2024 §sys/mman.h, base) */
#define PROT_NONE  0x0  /* page cannot be accessed */
#define PROT_READ  0x1  /* page can be read */
#define PROT_WRITE 0x2  /* page can be written */
#define PROT_EXEC  0x4  /* page can be executed */

/* Mapping flags — MAP_* (POSIX.1-2024 §sys/mman.h, base) */
#define MAP_SHARED    0x01  /* share changes */
#define MAP_PRIVATE   0x02  /* changes are private */
#define MAP_FIXED     0x10  /* interpret addr exactly */
#define MAP_ANONYMOUS 0x20  /* map anonymous memory (not backed by a file) */
#define MAP_ANON      MAP_ANONYMOUS  /* synonym for MAP_ANONYMOUS */

#define MAP_FAILED ((void *) -1)  /* mmap() failure return value */

/* POSIX memory advice constants — POSIX_MADV_* [ADV] (POSIX.1-2024 §sys/mman.h) */
#define POSIX_MADV_NORMAL     0  /* no specific advice; default behavior */
#define POSIX_MADV_RANDOM     1  /* expect random access order */
#define POSIX_MADV_SEQUENTIAL 2  /* expect sequential access order */
#define POSIX_MADV_WILLNEED   3  /* will access range soon */
#define POSIX_MADV_DONTNEED   4  /* will not access range soon */

/* SBUnix extensions: Linux-compatible madvise constants (non-POSIX names) */
#define MADV_NORMAL     POSIX_MADV_NORMAL
#define MADV_RANDOM     POSIX_MADV_RANDOM
#define MADV_SEQUENTIAL POSIX_MADV_SEQUENTIAL
#define MADV_WILLNEED   POSIX_MADV_WILLNEED
#define MADV_DONTNEED   POSIX_MADV_DONTNEED

/* Base POSIX function declarations */
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int   munmap(void *addr, size_t length);
int   mprotect(void *addr, size_t len, int prot);

/* posix_madvise — [ADV] (POSIX.1-2024 §sys/mman.h) */
int posix_madvise(void *addr, size_t len, int advice);

/* SBUnix extension: Linux-compatible madvise (non-POSIX name) */
int madvise(void *addr, size_t length, int advice);

#endif /* _SYS_MMAN_H */
