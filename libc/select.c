#include "sys/select.h"
#include "stddef.h"

/* select() is implemented as a kernel syscall (SYS_select = 104) via
 * the assembly stub in libc/syscall.S.  No C wrapper is needed. */

int pselect(int nfds, fd_set *restrict rfds, fd_set *restrict wfds,
            fd_set *restrict efds, const struct timespec *restrict timeout,
            const sigset_t *restrict sigmask)
{
    (void)nfds; (void)rfds; (void)wfds; (void)efds;
    (void)timeout; (void)sigmask;
    return -1;
}
