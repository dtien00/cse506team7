#ifndef _SYS_SELECT_H
#define _SYS_SELECT_H

#include "types.h"
#include "time.h"

typedef struct {
    unsigned long fds_bits[16];
} fd_set;

#define FD_SETSIZE 1024
#define FD_SET(fd, set)   ((set)->fds_bits[(fd)/64] |= (1UL << ((fd) % 64)))
#define FD_CLR(fd, set)   ((set)->fds_bits[(fd)/64] &= ~(1UL << ((fd) % 64)))
#define FD_ISSET(fd, set) ((set)->fds_bits[(fd)/64] & (1UL << ((fd) % 64)))
#define FD_ZERO(set)      do { for(int i=0; i<16; i++) (set)->fds_bits[i]=0; } while(0)

int select(int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds, struct timeval* timeout);

#endif
