#ifndef _SYS_WAIT_H
#define _SYS_WAIT_H

#include "types.h"

#define WNOHANG    1
#define WUNTRACED  2
#define WCONTINUED 8

#define WEXITSTATUS(s)   (((s) >> 8) & 0xff)
#define WTERMSIG(s)      ((s) & 0x7f)
#define WSTOPSIG(s)      WEXITSTATUS(s)
#define WIFEXITED(s)     (WTERMSIG(s) == 0)
#define WIFSIGNALED(s)   (((s) & 0x7f) > 0 && ((s) & 0x7f) < 0x7f)
#define WIFSTOPPED(s)    (((s) & 0xff) == 0x7f)
#define WIFCONTINUED(s)  ((s) == 0xffff)

pid_t wait(int *wstatus);
pid_t waitpid(pid_t pid, int *wstatus, int options);

#endif
