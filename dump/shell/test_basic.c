#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>

int main(void) {
    printf("=== Basic POSIX Function Test ===\n");
    
    printf("Testing getpid()...\n");
    pid_t my_pid = getpid();
    printf("  My PID: %d\n", my_pid);
    
    printf("\nTesting getppid()...\n");
    pid_t parent_pid = getppid();
    printf("  Parent PID: %d\n", parent_pid);
    
    printf("\nTesting write()...\n");
    write(STDOUT_FILENO, "  Direct write to stdout\n", 25);
    
    printf("\nTesting fork()...\n");
    pid_t pid = fork();
    
    if (pid < 0) {
        printf("  Fork failed!\n");
        return 1;
    } else if (pid == 0) {
        printf("  Child process: PID=%d, Parent=%d\n", getpid(), getppid());
        return 42;
    } else {
        printf("  Parent process: created child with PID=%d\n", pid);
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) {
            printf("  Child exited with status: %d\n", WEXITSTATUS(status));
        }
    }
    
    printf("\n=== All tests passed! ===\n");
    return 0;
}
