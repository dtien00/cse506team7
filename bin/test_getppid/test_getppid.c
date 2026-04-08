#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>

/*
 * test_getppid — verifies getppid() correctness across a fork.
 *
 * Expected output (PIDs will vary):
 *   before fork: pid=3 ppid=1
 *   child:  pid=4 ppid=3
 *   parent: pid=3 ppid=1 child_was=4
 *
 * The key invariant: child's ppid must equal the parent's pid.
 */
int main(void) {
    printf("before fork: pid=%d ppid=%d\n", (int)getpid(), (int)getppid());

    pid_t child = fork();
    if (child < 0) {
        printf("fork failed\n");
        return 1;
    }

    if (child == 0) {
        /* child: getppid() must equal parent's getpid() */
        printf("child:  pid=%d ppid=%d\n", (int)getpid(), (int)getppid());
        return 0;
    }

    /* parent: wait for child, then print own ids */
    int status;
    waitpid(child, &status, 0);
    printf("parent: pid=%d ppid=%d child_was=%d\n",
           (int)getpid(), (int)getppid(), (int)child);
    return 0;
}
