#include "sys/wait.h"
#include "unistd.h"
#include "stddef.h"

/* wait() is provided by the assembly wrapper in syscall.S (SYS_wait = 7).
   The kernel's sys_wait takes a single argument: int *wstatus in a0. */
pid_t wait(int *wstatus);

/* waitpid() uses SYS_waitpid (17): a0=pid, a1=wstatus. */
pid_t waitpid(pid_t pid, int *wstatus, int options) {
    (void)options;
    register long _a0 asm("a0") = (long)pid;
    register long _a1 asm("a1") = (long)wstatus;
    register long _a7 asm("a7") = 17; /* SYS_waitpid */
    asm volatile("ecall" : "+r"(_a0) : "r"(_a1), "r"(_a7) : "memory");
    return (pid_t)_a0;
}
