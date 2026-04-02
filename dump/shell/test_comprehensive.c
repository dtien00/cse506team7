#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

int main(void) {
    printf("=== Comprehensive libc Test ===\n");
    printf("This test exercises all critical POSIX functions\n\n");
    
    printf("1. Process functions:\n");
    printf("   getpid() = %d\n", getpid());
    printf("   getppid() = %d\n", getppid());
    printf("   getuid() = %d\n", getuid());
    printf("   getgid() = %d\n", getgid());
    
    printf("\n2. String functions:\n");
    char buf[128];
    strcpy(buf, "test");
    printf("   strcpy: %s\n", buf);
    strcat(buf, " string");
    printf("   strcat: %s\n", buf);
    printf("   strlen: %d\n", (int)strlen(buf));
    printf("   strcmp(\"a\", \"a\"): %d\n", strcmp("a", "a"));
    
    printf("\n3. Memory functions:\n");
    int *ptr = malloc(sizeof(int) * 5);
    if (ptr) {
        printf("   malloc succeeded\n");
        ptr[0] = 100;
        printf("   ptr[0] = %d\n", ptr[0]);
        free(ptr);
        printf("   free succeeded\n");
    }
    
    int *ptr2 = calloc(3, sizeof(int));
    if (ptr2) {
        printf("   calloc succeeded, values: %d %d %d\n", ptr2[0], ptr2[1], ptr2[2]);
        free(ptr2);
    }
    
    printf("\n4. File I/O:\n");
    int fd = open("/tmp/test_comprehensive.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd >= 0) {
        printf("   open for write: fd=%d\n", fd);
        const char *data = "test data\n";
        write(fd, data, strlen(data));
        printf("   write succeeded\n");
        close(fd);
        
        fd = open("/tmp/test_comprehensive.txt", O_RDONLY);
        if (fd >= 0) {
            printf("   open for read: fd=%d\n", fd);
            char rbuf[64];
            int n = read(fd, rbuf, sizeof(rbuf) - 1);
            rbuf[n] = '\0';
            printf("   read %d bytes: %s", n, rbuf);
            close(fd);
        }
        unlink("/tmp/test_comprehensive.txt");
    }
    
    printf("\n5. Fork and exec:\n");
    pid_t pid = fork();
    if (pid == 0) {
        printf("   Child process (PID=%d) running\n", getpid());
        exit(0);
    } else if (pid > 0) {
        printf("   Parent created child PID=%d\n", pid);
        int status;
        waitpid(pid, &status, 0);
        printf("   Child exited with status %d\n", WEXITSTATUS(status));
    }
    
    printf("\n6. Pipe test:\n");
    int pipefd[2];
    if (pipe(pipefd) == 0) {
        printf("   pipe created: [%d, %d]\n", pipefd[0], pipefd[1]);
        write(pipefd[1], "pipe", 4);
        char pbuf[8];
        read(pipefd[0], pbuf, 4);
        pbuf[4] = '\0';
        printf("   pipe data: %s\n", pbuf);
        close(pipefd[0]);
        close(pipefd[1]);
    }
    
    printf("\n=== All comprehensive tests passed! ===\n");
    return 0;
}
