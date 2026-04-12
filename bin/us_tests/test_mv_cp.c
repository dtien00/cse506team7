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
    ut_group_begin(group, "mv/cp.cp");

    const char *src = "/tmp/cp_src.txt";
    const char *dst = "/tmp/cp_dst.txt";
    const char *same = "/tmp/cp_src.txt";
    const char *dst_exist = "/tmp/cp_dst_exist.txt";
    const char *src_nonexist = "/tmp/cp_src_nonexist.txt";
    const char *dst_bad = "/nonexistent/cp_dst.txt";
    const char *src_empty = "/tmp/cp_src_empty.txt";
    const char *dst_empty = "/tmp/cp_dst_empty.txt";
    const char *src_large = "/tmp/cp_src_large.txt";
    const char *dst_large = "/tmp/cp_dst_large.txt";

    // Cleanup
    unlink(src);
    unlink(dst);
    unlink(same);
    unlink(dst_exist);
    unlink(src_nonexist);
    unlink(dst_bad);
    unlink(src_empty);
    unlink(dst_empty);
    unlink(src_large);
    unlink(dst_large);

    // Basic copy test
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

    // Test copying to existing file (overwrite)
    TEST(group, "cp: create existing destination", write_file(dst_exist, "old content\n") == 0);
    {
        pid_t pid = fork();
        TEST(group, "cp: overwrite fork succeeds", pid >= 0);
        if (pid == 0) {
            char *argv[] = { "cp", (char *)src, (char *)dst_exist, NULL };
            execve("/bin/cp", argv, NULL);
            _exit(127);
        }
        if (pid > 0) {
            int status = 0;
            TEST(group, "cp: overwrite child reaped", waitpid(pid, &status, 0) == pid);
            TEST(group, "cp: overwrite exits successfully",
                 WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
    }
    {
        char buf[128];
        int n = read_file(dst_exist, buf, sizeof(buf));
        TEST(group, "cp: overwrite destination readable", n > 0);
        TEST(group, "cp: overwrite destination content matches", strcmp(buf, "copy payload\n") == 0);
    }

    // Test copying non-existent source
    {
        pid_t pid = fork();
        TEST(group, "cp: nonexist fork succeeds", pid >= 0);
        if (pid == 0) {
            char *argv[] = { "cp", (char *)src_nonexist, (char *)dst, NULL };
            execve("/bin/cp", argv, NULL);
            _exit(127);
        }
        if (pid > 0) {
            int status = 0;
            TEST(group, "cp: nonexist child reaped", waitpid(pid, &status, 0) == pid);
            TEST(group, "cp: nonexist copy fails",
                 WIFEXITED(status) && WEXITSTATUS(status) != 0);
        }
    }

    // Test copying to bad destination path
    {
        pid_t pid = fork();
        TEST(group, "cp: bad dst fork succeeds", pid >= 0);
        if (pid == 0) {
            char *argv[] = { "cp", (char *)src, (char *)dst_bad, NULL };
            execve("/bin/cp", argv, NULL);
            _exit(127);
        }
        if (pid > 0) {
            int status = 0;
            TEST(group, "cp: bad dst child reaped", waitpid(pid, &status, 0) == pid);
            TEST(group, "cp: bad dst copy fails",
                 WIFEXITED(status) && WEXITSTATUS(status) != 0);
        }
    }

    // Test copying empty file
    TEST(group, "cp: create empty source", write_file(src_empty, "") == 0);
    {
        pid_t pid = fork();
        TEST(group, "cp: empty fork succeeds", pid >= 0);
        if (pid == 0) {
            char *argv[] = { "cp", (char *)src_empty, (char *)dst_empty, NULL };
            execve("/bin/cp", argv, NULL);
            _exit(127);
        }
        if (pid > 0) {
            int status = 0;
            TEST(group, "cp: empty child reaped", waitpid(pid, &status, 0) == pid);
            TEST(group, "cp: empty copy succeeds",
                 WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
    }
    {
        char buf[128];
        int n = read_file(dst_empty, buf, sizeof(buf));
        TEST(group, "cp: empty destination readable", n == 0);
        TEST(group, "cp: empty destination is empty", strcmp(buf, "") == 0);
    }

    // Test copying large file
    char large_data[2048];
    memset(large_data, 'A', sizeof(large_data) - 1);
    large_data[sizeof(large_data) - 1] = '\0';
    TEST(group, "cp: create large source", write_file(src_large, large_data) == 0);
    {
        pid_t pid = fork();
        TEST(group, "cp: large fork succeeds", pid >= 0);
        if (pid == 0) {
            char *argv[] = { "cp", (char *)src_large, (char *)dst_large, NULL };
            execve("/bin/cp", argv, NULL);
            _exit(127);
        }
        if (pid > 0) {
            int status = 0;
            TEST(group, "cp: large child reaped", waitpid(pid, &status, 0) == pid);
            TEST(group, "cp: large copy succeeds",
                 WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
    }
    {
        char buf[2048];
        int n = read_file(dst_large, buf, sizeof(buf));
        TEST(group, "cp: large destination readable", n == (int)strlen(large_data));
        TEST(group, "cp: large destination content matches", strcmp(buf, large_data) == 0);
    }

    // Test same-file copy (should fail)
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
            TEST(group, "cp: same-file copy succeeds",
                 WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
    }
    {
        char buf[128];
        int n = read_file(same, buf, sizeof(buf));
        TEST(group, "cp: same-file no-op preserves content", n > 0 && strcmp(buf, "copy payload\n") == 0);
    }

    ut_group_end(group);
}

static void run_move_test(us_test_group_t *group) {
    ut_group_begin(group, "mv/cp.mv");

    const char *src = "/tmp/mv_src.txt";
    const char *dst = "/tmp/mv_dst.txt";
    const char *dst_exist = "/tmp/mv_dst_exist.txt";
    const char *src_nonexist = "/tmp/mv_src_nonexist.txt";
    const char *dst_bad = "/nonexistent/mv_dst.txt";
    const char *src_empty = "/tmp/mv_src_empty.txt";
    const char *dst_empty = "/tmp/mv_dst_empty.txt";
    const char *src_large = "/tmp/mv_src_large.txt";
    const char *dst_large = "/tmp/mv_dst_large.txt";
    const char *src_same = "/tmp/mv_src_same.txt";

    // Cleanup
    unlink(src);
    unlink(dst);
    unlink(dst_exist);
    unlink(src_nonexist);
    unlink(dst_bad);
    unlink(src_empty);
    unlink(dst_empty);
    unlink(src_large);
    unlink(dst_large);
    unlink(src_same);

    // Basic move test
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

    // Test moving to existing file (overwrite)
    TEST(group, "mv: create source for overwrite", write_file(src, "new move payload\n") == 0);
    TEST(group, "mv: create existing destination", write_file(dst_exist, "old content\n") == 0);
    {
        pid_t pid = fork();
        TEST(group, "mv: overwrite fork succeeds", pid >= 0);
        if (pid == 0) {
            char *argv[] = { "mv", (char *)src, (char *)dst_exist, NULL };
            execve("/bin/mv", argv, NULL);
            _exit(127);
        }
        if (pid > 0) {
            int status = 0;
            TEST(group, "mv: overwrite child reaped", waitpid(pid, &status, 0) == pid);
            TEST(group, "mv: overwrite exits successfully",
                 WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
    }
    {
        char buf[128];
        int n = read_file(dst_exist, buf, sizeof(buf));
        TEST(group, "mv: overwrite destination readable", n > 0);
        TEST(group, "mv: overwrite destination content matches", strcmp(buf, "new move payload\n") == 0);
    }
    {
        int fd = open(src, O_RDONLY);
        TEST(group, "mv: overwrite source no longer exists", fd < 0);
        if (fd >= 0) close(fd);
    }

    // Test moving non-existent source
    {
        pid_t pid = fork();
        TEST(group, "mv: nonexist fork succeeds", pid >= 0);
        if (pid == 0) {
            char *argv[] = { "mv", (char *)src_nonexist, (char *)dst, NULL };
            execve("/bin/mv", argv, NULL);
            _exit(127);
        }
        if (pid > 0) {
            int status = 0;
            TEST(group, "mv: nonexist child reaped", waitpid(pid, &status, 0) == pid);
            TEST(group, "mv: nonexist move fails",
                 WIFEXITED(status) && WEXITSTATUS(status) != 0);
        }
    }

    // Test moving to bad destination path
    TEST(group, "mv: create source for bad dst", write_file(src, "bad dst payload\n") == 0);
    {
        pid_t pid = fork();
        TEST(group, "mv: bad dst fork succeeds", pid >= 0);
        if (pid == 0) {
            char *argv[] = { "mv", (char *)src, (char *)dst_bad, NULL };
            execve("/bin/mv", argv, NULL);
            _exit(127);
        }
        if (pid > 0) {
            int status = 0;
            TEST(group, "mv: bad dst child reaped", waitpid(pid, &status, 0) == pid);
            TEST(group, "mv: bad dst move fails",
                 WIFEXITED(status) && WEXITSTATUS(status) != 0);
        }
    }

    // Test moving empty file
    TEST(group, "mv: create empty source", write_file(src_empty, "") == 0);
    {
        pid_t pid = fork();
        TEST(group, "mv: empty fork succeeds", pid >= 0);
        if (pid == 0) {
            char *argv[] = { "mv", (char *)src_empty, (char *)dst_empty, NULL };
            execve("/bin/mv", argv, NULL);
            _exit(127);
        }
        if (pid > 0) {
            int status = 0;
            TEST(group, "mv: empty child reaped", waitpid(pid, &status, 0) == pid);
            TEST(group, "mv: empty move succeeds",
                 WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
    }
    {
        char buf[128];
        int n = read_file(dst_empty, buf, sizeof(buf));
        TEST(group, "mv: empty destination readable", n == 0);
        TEST(group, "mv: empty destination is empty", strcmp(buf, "") == 0);
    }
    {
        int fd = open(src_empty, O_RDONLY);
        TEST(group, "mv: empty source no longer exists", fd < 0);
        if (fd >= 0) close(fd);
    }

    // Test moving large file
    char large_data[2048];
    memset(large_data, 'B', sizeof(large_data) - 1);
    large_data[sizeof(large_data) - 1] = '\0';
    TEST(group, "mv: create large source", write_file(src_large, large_data) == 0);
    {
        pid_t pid = fork();
        TEST(group, "mv: large fork succeeds", pid >= 0);
        if (pid == 0) {
            char *argv[] = { "mv", (char *)src_large, (char *)dst_large, NULL };
            execve("/bin/mv", argv, NULL);
            _exit(127);
        }
        if (pid > 0) {
            int status = 0;
            TEST(group, "mv: large child reaped", waitpid(pid, &status, 0) == pid);
            TEST(group, "mv: large move succeeds",
                 WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
    }
    {
        char buf[2048];
        int n = read_file(dst_large, buf, sizeof(buf));
        TEST(group, "mv: large destination readable", n == (int)strlen(large_data));
        TEST(group, "mv: large destination content matches", strcmp(buf, large_data) == 0);
    }
    {
        int fd = open(src_large, O_RDONLY);
        TEST(group, "mv: large source no longer exists", fd < 0);
        if (fd >= 0) close(fd);
    }

    // Test moving to same file (no-op)
    TEST(group, "mv: create source for same", write_file(src_same, "same payload\n") == 0);
    {
        pid_t pid = fork();
        TEST(group, "mv: same-file fork succeeds", pid >= 0);
        if (pid == 0) {
            char *argv[] = { "mv", (char *)src_same, (char *)src_same, NULL };
            execve("/bin/mv", argv, NULL);
            _exit(127);
        }
        if (pid > 0) {
            int status = 0;
            TEST(group, "mv: same-file child reaped", waitpid(pid, &status, 0) == pid);
            TEST(group, "mv: same-file move succeeds",
                 WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
    }
    {
        char buf[128];
        int n = read_file(src_same, buf, sizeof(buf));
        TEST(group, "mv: same-file source preserved", n > 0 && strcmp(buf, "same payload\n") == 0);
    }

    ut_group_end(group);
}

void run_test_mv_cp(us_test_group_t *group) {
    printf("\n=== mv/cp tests ===\n");
    run_copy_test(group);
    run_move_test(group);
}
