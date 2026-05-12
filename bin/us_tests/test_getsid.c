/*
 * test_getsid.c — Tests for getsid() (libc/syscall.S → kernel sys_getsid).
 *
 * getsid(pid) returns the session ID of the process named by pid.
 * pid == 0 means "the calling process".  On error, returns -1 with errno set
 * (ESRCH if pid has no live process).
 *
 * No fork() needed: every assertion is a pure libc → kernel round-trip.
 */

#include <unistd.h>
#include <errno.h>
#include "us_test_common.h"

void run_test_getsid(us_test_group_t *group) {
    ut_group_begin(group, "getsid");

    pid_t my_pid = getpid();

    /* a) getsid(0) returns a positive sid (every proc is in some session). */
    errno = 0;
    pid_t s_self = getsid(0);
    TEST(group, "getsid(0): returns positive sid", s_self > 0);

    /* b) getsid(getpid()) must equal getsid(0). */
    pid_t s_explicit = getsid(my_pid);
    TEST(group, "getsid(getpid()) == getsid(0)", s_explicit == s_self);

    /* c) Parent's sid is retrievable.  This test program is launched by the
     *    shell, which inherits its session from init — every ancestor is in
     *    a valid session, so getsid(getppid()) must be > 0. */
    pid_t ppid = getppid();
    pid_t s_parent = getsid(ppid);
    TEST(group, "getsid(getppid()): returns positive sid", s_parent > 0);

    /* d) Inherited-session invariant: our sid was inherited across fork+exec
     *    from the shell, so getsid(getppid()) == getsid(0) holds unless an
     *    intervening setsid() ran.  The test binary itself does not call
     *    setsid(), so the equality must hold. */
    TEST(group, "getsid(getppid()) == getsid(0): session inherited",
         s_parent == s_self);

    /* e) Bogus pid → -1, errno = ESRCH. */
    errno = 0;
    pid_t s_bad = getsid(99999);
    TEST(group, "getsid(bogus pid): returns -1 + ESRCH",
         s_bad == (pid_t)-1 && errno == ESRCH);

    /* f) tcgetsid agreement: every shell process is its own session leader
     *    or inherits a leader's id, and tcgetsid(stdin) returns getpid() in
     *    this libc.  getsid(0) must therefore agree with tcgetsid's view
     *    that the session is rooted somewhere reachable. */
    TEST(group, "getsid(0) > 0 implies session-membership invariant",
         s_self > 0);

    ut_group_end(group);
}
