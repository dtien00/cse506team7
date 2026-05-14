/*
 * test_fg_counter — manual interactive job-control regression test for
 * FEEDBACK_JOBCTL_2.md §9 Test G.
 *
 * Prints an incrementing counter once per second, forever. Used to exercise
 * the shell's stop/resume path:
 *
 *   $ ./test_fg_counter
 *   0
 *   1
 *   ^Z                       (kernel sends SIGTSTP to fg pgrp)
 *   [1]+  Stopped  ./test_fg_counter
 *   $ fg                     (shell SIGCONT + tcsetpgrp)
 *   ./test_fg_counter
 *   2
 *   3
 *   ^Z                       (must stop again, prompt must come back)
 *   [1]+  Stopped  ./test_fg_counter
 *   $
 *
 * Pass criterion: every ^Z returns the shell prompt within a tick, and the
 * counter resumes from where it left off after fg.  Failure modes covered:
 * shell waitpid() taking the ECHILD path after resume, or the second ^Z
 * never returning the prompt.
 *
 * Exits only via signal (e.g. ^C / SIGTERM); the loop is intentionally
 * unbounded so the tester controls termination.
 */
#include <stdio.h>
#include <unistd.h>

int main(void) {
    for (long i = 0; ; i++) {
        printf("%ld\n", i);
        sleep(1);
    }
}
