#ifndef _SYS_WAIT_H
#define _SYS_WAIT_H

#include "sys/types.h"
#include "signal.h"

/* waitpid() options */
#define WNOHANG    1   /* don't block if no child status available */
#define WUNTRACED  2   /* report stopped children */
#define WCONTINUED 8   /* report continued children (XSI) */

/* waitid() options */
#define WEXITED    4          /* wait for processes that have exited */
#define WSTOPPED   2          /* wait for stopped children (= WUNTRACED) */
#define WNOWAIT    0x01000000 /* leave child in waitable state (peek) */

/* status inspection macros */
#define WEXITSTATUS(s)  (((s) >> 8) & 0xff)
#define WTERMSIG(s)     ((s) & 0x7f)
#define WSTOPSIG(s)     WEXITSTATUS(s)
#define WIFEXITED(s)    (WTERMSIG(s) == 0)
#define WIFSIGNALED(s)  (((s) & 0x7f) > 0 && ((s) & 0x7f) < 0x7f)
#define WIFSTOPPED(s)   (((s) & 0xff) == 0x7f)
#define WIFCONTINUED(s) ((s) == 0xffff)
#define WCOREDUMP(s)    (WIFSIGNALED(s) && ((s) & 0x80))

/* idtype_t for waitid() — POSIX.1-2024 §<sys/wait.h> */
typedef enum { P_ALL = 0, P_PID = 1, P_PGID = 2 } idtype_t;

pid_t wait(int *wstatus);
pid_t waitpid(pid_t pid, int *wstatus, int options);
int   waitid(idtype_t idtype, id_t id, siginfo_t *infop, int options);

#endif /* _SYS_WAIT_H */
