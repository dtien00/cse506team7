#include "sys/wait.h"
#include "unistd.h"
#include "stddef.h"

/* wait() is provided by the assembly wrapper in syscall.S (SYS_wait = 7).
   The kernel's sys_wait takes a single argument: int *wstatus in a0. */
pid_t wait(int *wstatus);

/* The kernel only supports wait-for-any-child, so waitpid always calls wait(). */
pid_t waitpid(pid_t pid, int *wstatus, int options) {
    (void)pid;
    (void)options;
    return wait(wstatus);
}
