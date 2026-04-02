/*
 * Minimal libc test program
 * Tests basic POSIX functions to verify libc implementation completeness
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>

#define TEST_FILE "/tmp/test.txt"

void test_basic_io(void) {
    const char *msg = "Testing basic I/O\n";
    write(STDOUT_FILENO, msg, strlen(msg));
}

void test_process_info(void) {
    pid_t pid = getpid();
    pid_t ppid = getppid();
    uid_t uid = getuid();
    gid_t gid = getgid();
    
    printf("Process info: PID=%d PPID=%d UID=%d GID=%d\n", pid, ppid, uid, gid);
}

void test_fork_wait(void) {
    pid_t pid = fork();
    
    if (pid < 0) {
        printf("Fork failed\n");
        return;
    }
    
    if (pid == 0) {
        printf("Child process (PID=%d)\n", getpid());
        _exit(42);
    } else {
        int status;
        pid_t waited = waitpid(pid, &status, 0);
        printf("Parent: child %d exited with status %d\n", waited, WEXITSTATUS(status));
    }
}

void test_file_operations(void) {
    int fd = open(TEST_FILE, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        printf("Failed to open file for writing\n");
        return;
    }
    
    const char *data = "Hello from libc test\n";
    ssize_t written = write(fd, data, strlen(data));
    printf("Wrote %ld bytes to %s\n", written, TEST_FILE);
    close(fd);
    
    fd = open(TEST_FILE, O_RDONLY);
    if (fd < 0) {
        printf("Failed to open file for reading\n");
        return;
    }
    
    char buf[128];
    ssize_t nread = read(fd, buf, sizeof(buf) - 1);
    if (nread > 0) {
        buf[nread] = '\0';
        printf("Read back: %s", buf);
    }
    close(fd);
    
    unlink(TEST_FILE);
}

void test_directory_operations(void) {
    const char *testdir = "/tmp/testdir";
    
    if (mkdir(testdir, 0755) == 0) {
        printf("Created directory: %s\n", testdir);
    }
    
    DIR *dir = opendir("/tmp");
    if (dir) {
        printf("Opened /tmp directory\n");
        struct dirent *entry;
        int count = 0;
        while ((entry = readdir(dir)) != NULL && count < 5) {
            printf("  Entry: %s\n", entry->d_name);
            count++;
        }
        closedir(dir);
    }
}

void test_string_functions(void) {
    char buf1[64] = "Hello";
    char buf2[64];
    
    strcpy(buf2, buf1);
    strcat(buf1, " World");
    
    printf("String test: '%s' vs '%s', strcmp=%d\n", 
           buf1, buf2, strcmp(buf1, buf2));
    
    printf("strlen('%s') = %lu\n", buf1, strlen(buf1));
    
    char *found = strchr(buf1, 'W');
    if (found) {
        printf("strchr found: '%s'\n", found);
    }
}

void test_memory_functions(void) {
    void *ptr1 = malloc(100);
    void *ptr2 = malloc(200);
    
    if (ptr1 && ptr2) {
        printf("malloc test: allocated %p and %p\n", ptr1, ptr2);
        memset(ptr1, 0xAB, 100);
        free(ptr1);
        free(ptr2);
        printf("malloc test: freed successfully\n");
    }
}

void test_pipe(void) {
    int pipefd[2];
    
    if (pipe(pipefd) < 0) {
        printf("Pipe creation failed\n");
        return;
    }
    
    pid_t pid = fork();
    if (pid == 0) {
        close(pipefd[0]);
        const char *msg = "pipe message";
        write(pipefd[1], msg, strlen(msg));
        close(pipefd[1]);
        _exit(0);
    } else if (pid > 0) {
        close(pipefd[1]);
        char buf[64];
        ssize_t n = read(pipefd[0], buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            printf("Pipe test: received '%s'\n", buf);
        }
        close(pipefd[0]);
        waitpid(pid, NULL, 0);
    }
}

void test_cwd(void) {
    char cwd[256];
    if (getcwd(cwd, sizeof(cwd))) {
        printf("Current directory: %s\n", cwd);
    }
    
    if (chdir("/tmp") == 0) {
        if (getcwd(cwd, sizeof(cwd))) {
            printf("Changed to: %s\n", cwd);
        }
    }
}

int main(int argc, char *argv[]) {
    printf("\n=== SBUNIX Libc Test Suite ===\n\n");
    
    printf("1. Basic I/O test:\n");
    test_basic_io();
    
    printf("\n2. Process info test:\n");
    test_process_info();
    
    printf("\n3. String functions test:\n");
    test_string_functions();
    
    printf("\n4. Memory functions test:\n");
    test_memory_functions();
    
    printf("\n5. File operations test:\n");
    test_file_operations();
    
    printf("\n6. Directory operations test:\n");
    test_directory_operations();
    
    printf("\n7. Current directory test:\n");
    test_cwd();
    
    printf("\n8. Pipe test:\n");
    test_pipe();
    
    printf("\n9. Fork and wait test:\n");
    test_fork_wait();
    
    printf("\n=== All tests completed ===\n");
    return 0;
}
