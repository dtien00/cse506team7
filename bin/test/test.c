#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <fcntl.h>

int main(void) {
    // Test basic I/O
    printf("Testing basic functions...\n");
    
    // Test getpid
    pid_t pid = getpid();
    printf("PID: %d\n", pid);
    
    // Test write
    write(1, "Direct write test\n", 18);
    
    // Test fork
    pid_t child = fork();
    if (child == 0) {
        printf("Child process: %d\n", getpid());
        exit(0);
    } else if (child > 0) {
        int status;
        wait(&status);
        printf("Parent waited for child\n");
    }
    
    // Test string functions
    char buf[64];
    strcpy(buf, "hello");
    strcat(buf, " world");
    printf("String: %s (len=%zu)\n", buf, strlen(buf));
    
    // Test file operations
    int fd = open("/hello.txt", O_RDONLY);
    if (fd >= 0) {
        char data[128];
        ssize_t n = read(fd, data, sizeof(data) - 1);
        if (n > 0) {
            data[n] = '\0';
            printf("Read from file: %s\n", data);
        }
        close(fd);
    }
    
    return 0;
}
