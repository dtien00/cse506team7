#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "us_test_common.h"

/*
 * Fork a child that attempts execve; the child exits with errno on failure.
 * The parent asserts that WEXITSTATUS matches expected_errno.
 */
static void check_exec_errno(us_test_group_t *group, const char *desc,
                              const char *path, char *const argv[],
                              int expected_errno) {
    pid_t pid = fork();
    if (pid < 0) {
        SKIP(group, desc, "fork failed");
        return;
    }
    if (pid == 0) {
        execve(path, argv, NULL);
        _exit(errno);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    TEST(group, desc,
         WIFEXITED(status) && WEXITSTATUS(status) == expected_errno);
}

/* ENOENT — binary does not exist */
static void test_exec_enoent(us_test_group_t *tg) {
    char *argv[] = { "missing", NULL };
    check_exec_errno(tg, "execve(missing path): errno == ENOENT",
                     "/no_such_exec_errno_binary", argv, ENOENT);
}

/* EACCES — file exists but has no execute permission */
static void test_exec_eacces(us_test_group_t *tg) {
    const char *path = "/mnt/exec_errno_noexec.elf";
    /* Write a placeholder file with no execute bit */
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        SKIP(tg, "execve(no exec bit): errno == EACCES", "could not create file");
        return;
    }
    write(fd, "\x7f""ELF", 4);
    close(fd);
    chmod(path, 0644);   /* rw-r--r--: no execute bit */

    char *argv[] = { "noexec", NULL };
    check_exec_errno(tg, "execve(no exec bit): errno == EACCES",
                     path, argv, EACCES);
    unlink(path);
}

/* ENOEXEC — file is not an ELF and has no shebang */
static void test_exec_enoexec(us_test_group_t *tg) {
    const char *path = "/mnt/exec_errno_plain.txt";
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (fd < 0) {
        SKIP(tg, "execve(plain text): errno == ENOEXEC", "could not create file");
        return;
    }
    const char *body = "just a text file, not an ELF\n";
    write(fd, body, strlen(body));
    close(fd);

    char *argv[] = { "plain", NULL };
    check_exec_errno(tg, "execve(plain text, no shebang): errno == ENOEXEC",
                     path, argv, ENOEXEC);
    unlink(path);
}

/* E2BIG — too many arguments (> MAX_ARGS-1 = 63) */
static void test_exec_e2big_argc(us_test_group_t *tg) {
    pid_t pid = fork();
    if (pid < 0) {
        SKIP(tg, "execve(argc overflow): errno == E2BIG", "fork failed");
        return;
    }
    if (pid == 0) {
        /* 65 valid argv entries + NULL terminator; limit is 63 */
        char *big_argv[67];
        for (int i = 0; i < 66; i++) big_argv[i] = "x";
        big_argv[66] = NULL;
        execve("/bin/true", big_argv, NULL);
        _exit(errno);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    TEST(tg, "execve(argc overflow): errno == E2BIG",
         WIFEXITED(status) && WEXITSTATUS(status) == E2BIG);
}

/* E2BIG — a single argument string >= KARG_MAX (256) bytes */
static void test_exec_e2big_strlen(us_test_group_t *tg) {
    pid_t pid = fork();
    if (pid < 0) {
        SKIP(tg, "execve(arg string too long): errno == E2BIG", "fork failed");
        return;
    }
    if (pid == 0) {
        /* 256 'a' chars + null — one byte over the 255-char limit */
        char long_arg[257];
        memset(long_arg, 'a', 256);
        long_arg[256] = '\0';
        char *argv[] = { long_arg, NULL };
        execve("/bin/true", argv, NULL);
        _exit(errno);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    TEST(tg, "execve(arg string too long): errno == E2BIG",
         WIFEXITED(status) && WEXITSTATUS(status) == E2BIG);
}

/* Boundary: argument of exactly 254 chars must NOT trigger E2BIG */
static void test_exec_e2big_strlen_ok(us_test_group_t *tg) {
    pid_t pid = fork();
    if (pid < 0) {
        SKIP(tg, "execve(arg string at limit-1): succeeds without E2BIG", "fork failed");
        return;
    }
    if (pid == 0) {
        /* 254 chars + null — should fit without E2BIG */
        char ok_arg[255];
        memset(ok_arg, 'b', 254);
        ok_arg[254] = '\0';
        char *argv[] = { ok_arg, NULL };
        execve("/bin/true", argv, NULL);
        /* If E2BIG was mistakenly returned, exit(E2BIG); otherwise exit(errno) */
        _exit(errno);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    /* /bin/true exits 0; we expect exec to succeed → child exits 0 */
    TEST(tg, "execve(arg string 254 chars): no false E2BIG — exec succeeds",
         WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

void run_test_exec_errno(us_test_group_t *group) {
    ut_group_begin(group, "exec_errno");
    test_exec_enoent(group);
    test_exec_eacces(group);
    test_exec_enoexec(group);
    test_exec_e2big_argc(group);
    test_exec_e2big_strlen(group);
    test_exec_e2big_strlen_ok(group);
    ut_group_end(group);
}
