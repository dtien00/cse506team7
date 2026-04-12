#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "us_test_common.h"

static int write_file(const char *path, const char *data) {
    int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) return -1;
    ssize_t n = write(fd, data, strlen(data));
    close(fd);
    return (n == (ssize_t)strlen(data)) ? 0 : -1;
}

static int read_file(const char *path, char *buf, int bufsz) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;

    ssize_t n = read(fd, buf, bufsz - 1);
    close(fd);
    if (n < 0) return -1;

    buf[n] = '\0';
    return (int)n;
}

static void run_copy_test(us_test_group_t *group) {
    ut_group_begin(group, "mv_cp.cp");

    const char *src = "/tmp/cp_src.txt";
    const char *dst = "/tmp/cp_dst.txt";
    const char *same = "/tmp/cp_src.txt";

    unlink(src);
    unlink(dst);

    TEST(group, "cp: create source file succeeds", write_file(src, "copy payload\n") == 0);

    {
        pid_t pid = fork();
        TEST(group, "cp: fork succeeds", pid >= 0);
        if (pid == 0) {
            char *argv[] = { "cp", (char *)src, (char *)dst, NULL };
            execve("/bin/cp", argv, NULL);
            _exit(127);
        }
        if (pid > 0) {
            int status = 0;
            TEST(group, "cp: child reaped", waitpid(pid, &status, 0) == pid);
            TEST(group, "cp: exits successfully",
                 WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
    }

    {
        char buf[128];
        int n = read_file(dst, buf, sizeof(buf));
        TEST(group, "cp: destination readable", n > 0);
        TEST(group, "cp: destination content matches", strcmp(buf, "copy payload\n") == 0);
    }

    {
        pid_t pid = fork();
        TEST(group, "cp: same-file fork succeeds", pid >= 0);
        if (pid == 0) {
            char *argv[] = { "cp", (char *)same, (char *)same, NULL };
            execve("/bin/cp", argv, NULL);
            _exit(127);
        }
        if (pid > 0) {
            int status = 0;
            TEST(group, "cp: same-file child reaped", waitpid(pid, &status, 0) == pid);
            TEST(group, "cp: same-file copy fails",
                 WIFEXITED(status) && WEXITSTATUS(status) != 0);
        }
    }

    ut_group_end(group);
}

static void run_move_test(us_test_group_t *group) {
    ut_group_begin(group, "mv_cp.mv");

    const char *src = "/tmp/mv_src.txt";
    const char *dst = "/tmp/mv_dst.txt";
    unlink(src);
    unlink(dst);

    TEST(group, "mv: create source file succeeds", write_file(src, "move payload\n") == 0);

    {
        pid_t pid = fork();
        TEST(group, "mv: fork succeeds", pid >= 0);
        if (pid == 0) {
            char *argv[] = { "mv", (char *)src, (char *)dst, NULL };
            execve("/bin/mv", argv, NULL);
            _exit(127);
        }
        if (pid > 0) {
            int status = 0;
            TEST(group, "mv: child reaped", waitpid(pid, &status, 0) == pid);
            TEST(group, "mv: exits successfully",
                 WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
    }

    {
        char buf[128];
        int n = read_file(dst, buf, sizeof(buf));
        TEST(group, "mv: destination readable", n > 0);
        TEST(group, "mv: destination content matches", strcmp(buf, "move payload\n") == 0);
    }

    {
        int fd = open(src, O_RDONLY);
        TEST(group, "mv: source no longer exists", fd < 0);
        if (fd >= 0) close(fd);
    }

    ut_group_end(group);
}

void run_test_mv_cp(us_test_group_t *group) {
    run_copy_test(group);
    run_move_test(group);
}
