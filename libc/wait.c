#include "sys/wait.h"
#include "unistd.h"
#include "stddef.h"

pid_t wait4(pid_t pid, int *wstatus, int options, void *rusage);

pid_t wait(int *wstatus) {
    return wait4(-1, wstatus, 0, NULL);
}

pid_t waitpid(pid_t pid, int *wstatus, int options) {
    return wait4(pid, wstatus, options, NULL);
}
