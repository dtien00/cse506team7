/*
 * Minimal libc test - just the bare essentials
 * Tests only getpid, write, and fork as mentioned in the assignment
 */

#include <unistd.h>
#include <string.h>

int main(void) {
    const char *msg1 = "Testing basic syscalls...\n";
    write(STDOUT_FILENO, msg1, strlen(msg1));
    
    pid_t pid = getpid();
    (void)pid;  // silence unused variable warning
    const char *msg2 = "Got PID\n";
    write(STDOUT_FILENO, msg2, strlen(msg2));
    
    pid_t child = fork();
    if (child == 0) {
        const char *msg3 = "Child process running\n";
        write(STDOUT_FILENO, msg3, strlen(msg3));
        _exit(0);
    } else if (child > 0) {
        const char *msg4 = "Parent process - fork succeeded\n";
        write(STDOUT_FILENO, msg4, strlen(msg4));
    }
    
    return 0;
}
