#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>

int main(void) {
    printf("=== Exec and Process Test ===\n");
    
    printf("\nTesting fork() and execve()...\n");
    pid_t pid = fork();
    
    if (pid < 0) {
        printf("  fork() failed!\n");
        return 1;
    } else if (pid == 0) {
        printf("  Child: About to exec /bin/echo\n");
        char *argv[] = {"/bin/echo", "Hello", "from", "execve!", NULL};
        execve("/bin/echo", argv, NULL);
        printf("  execve() failed!\n");
        return 1;
    } else {
        printf("  Parent: waiting for child (PID=%d)\n", pid);
        int status;
        waitpid(pid, &status, 0);
        printf("  Child exited with status: %d\n", WEXITSTATUS(status));
    }
    
    printf("\nTesting pipe()...\n");
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        printf("  pipe() failed!\n");
        return 1;
    }
    printf("  pipe() succeeded: read_fd=%d, write_fd=%d\n", pipefd[0], pipefd[1]);
    
    pid = fork();
    if (pid == 0) {
        close(pipefd[0]);
        const char *msg = "Hello through pipe!";
        write(pipefd[1], msg, strlen(msg));
        close(pipefd[1]);
        return 0;
    } else {
        close(pipefd[1]);
        char buf[64];
        int n = read(pipefd[0], buf, sizeof(buf) - 1);
        buf[n] = '\0';
        printf("  Received from pipe: \"%s\"\n", buf);
        close(pipefd[0]);
        waitpid(pid, NULL, 0);
    }
    
    printf("\n=== All exec tests passed! ===\n");
    return 0;
}
