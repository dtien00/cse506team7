/*
 * sigaction_helper — helper binary for the exec + SIG_IGN survival test.
 *
 * The test (6 in test_sigaction.c) does:
 *   1. fork() a child
 *   2. child sets signal(SIGTERM, SIG_IGN)
 *   3. child execve()s THIS binary
 *   4. parent sleeps 1 s, then sends SIGTERM
 *   5. parent sleeps 1 s, then checks with WNOHANG — child must still be alive
 *   6. parent sends SIGKILL to clean up
 *
 * Per POSIX: "Signals set to SIG_IGN shall remain set to SIG_IGN after a
 * successful call to exec()."
 *
 * Important: an ignored fatal signal wakes a sleeping process (proc_unblock),
 * the SIG_IGN filter in proc_sleep() clears the signal bit, and sleep()
 * returns early with ticks_left > 0.  The loop ensures we do not exit just
 * because sleep() returned early — we keep sleeping until SIGKILL arrives.
 */
#include <unistd.h>

int main(void) {
    /*
     * Loop on sleep so that an early-return caused by an ignored signal does
     * not make the process exit.  SIGKILL (sent by the test for cleanup) will
     * wake the process and kill it through the fatal-signal path in proc_sleep.
     */
    while (1)
        sleep(5);

    return 0; /* unreachable */
}
