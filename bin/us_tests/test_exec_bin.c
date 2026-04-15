#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "us_test_common.h"

static void check_exec_exit(us_test_group_t *group,
                            const char *name,
                            const char *path,
                            char *const argv[],
                            int expected_exit) {
    pid_t pid = fork();
    TEST(group, name, pid >= 0);
    if (pid == 0) {
        execve(path, argv, NULL);
        _exit(127);
    }
    if (pid > 0) {
        int status = 0;
        pid_t w = waitpid(pid, &status, 0);
        TEST(group, "exec child reaped", w == pid);
        TEST(group, "exec child exit status matches expected",
             WIFEXITED(status) && WEXITSTATUS(status) == expected_exit);
    }
}

static ssize_t read_all_fd(int fd, char *buf, size_t size) {
    ssize_t total = 0;

    if (size == 0) {
        return 0;
    }

    while (total < (ssize_t)(size - 1)) {
        ssize_t n = read(fd, buf + total, size - 1 - (size_t)total);
        if (n <= 0) {
            break;
        }
        total += n;
    }

    buf[total] = '\0';
    return total;
}

static int read_file_text(const char *path, char *buf, size_t size) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        if (size > 0) {
            buf[0] = '\0';
        }
        return -1;
    }

    (void)read_all_fd(fd, buf, size);
    close(fd);
    return 0;
}

static int write_file_text(const char *path, const char *text) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    size_t len = strlen(text);

    if (fd < 0) {
        return -1;
    }
    if (write(fd, text, len) != (ssize_t)len) {
        close(fd);
        return -1;
    }
    return close(fd);
}

static int run_shell_script(const char *script,
                            char *stdout_buf,
                            size_t stdout_size,
                            char *stderr_buf,
                            size_t stderr_size) {
    int stdin_pipe[2] = {-1, -1};
    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};
    pid_t pid;
    int status = -1;

    if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0 || pipe(stderr_pipe) < 0) {
        if (stdin_pipe[0] >= 0) close(stdin_pipe[0]);
        if (stdin_pipe[1] >= 0) close(stdin_pipe[1]);
        if (stdout_pipe[0] >= 0) close(stdout_pipe[0]);
        if (stdout_pipe[1] >= 0) close(stdout_pipe[1]);
        if (stderr_pipe[0] >= 0) close(stderr_pipe[0]);
        if (stderr_pipe[1] >= 0) close(stderr_pipe[1]);
        return -1;
    }

    pid = fork();
    if (pid < 0) {
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        return -1;
    }

    if (pid == 0) {
        char *argv[] = { "sh", NULL };

        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);

        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);

        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        execve("/bin/sh", argv, NULL);
        _exit(127);
    }

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    if (script) {
        size_t len = strlen(script);
        if (write(stdin_pipe[1], script, len) != (ssize_t)len) {
            close(stdin_pipe[1]);
            close(stdout_pipe[0]);
            close(stderr_pipe[0]);
            waitpid(pid, &status, 0);
            return -1;
        }
    }
    close(stdin_pipe[1]);

    if (stdout_buf && stdout_size > 0) {
        (void)read_all_fd(stdout_pipe[0], stdout_buf, stdout_size);
    } else {
        char discard[64];
        (void)read_all_fd(stdout_pipe[0], discard, sizeof(discard));
    }

    if (stderr_buf && stderr_size > 0) {
        (void)read_all_fd(stderr_pipe[0], stderr_buf, stderr_size);
    } else {
        char discard[64];
        (void)read_all_fd(stderr_pipe[0], discard, sizeof(discard));
    }

    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    if (waitpid(pid, &status, 0) != pid) {
        return -1;
    }

    return status;
}

void run_test_exec_bin(us_test_group_t *group) {
    ut_group_begin(group, "exec+bin");

    {
        char *argv[] = { "true", NULL };
        check_exec_exit(group, "exec /bin/true succeeds", "/bin/true", argv, 0);
    }

    {
        char *argv[] = { "false", NULL };
        check_exec_exit(group, "exec /bin/false returns failure exit code", "/bin/false", argv, 1);
    }

    {
        int pipefd[2];
        int rc = pipe(pipefd);
        TEST(group, "echo capture pipe created", rc == 0);
        if (rc == 0) {
            pid_t pid = fork();
            TEST(group, "exec /bin/echo fork succeeds", pid >= 0);
            if (pid == 0) {
                close(pipefd[0]);
                dup2(pipefd[1], STDOUT_FILENO);
                close(pipefd[1]);
                {
                    char *argv[] = { "echo", "hello", "world", NULL };
                    execve("/bin/echo", argv, NULL);
                }
                _exit(127);
            }
            if (pid > 0) {
                char buf[32];
                int status = 0;
                ssize_t total;

                close(pipefd[1]);
                total = read_all_fd(pipefd[0], buf, sizeof(buf));
                close(pipefd[0]);

                TEST(group, "exec /bin/echo writes expected stdout",
                     total > 0 && strcmp(buf, "hello world\n") == 0);

                TEST(group, "exec /bin/echo child reaped", waitpid(pid, &status, 0) == pid);
                TEST(group, "exec /bin/echo exits successfully",
                     WIFEXITED(status) && WEXITSTATUS(status) == 0);
            }
        }
    }

    {
        pid_t pid = fork();
        TEST(group, "missing binary fork succeeds", pid >= 0);
        if (pid == 0) {
            char *argv[] = { "missing", NULL };
            _exit(execve("/bin/definitely_missing", argv, NULL) < 0 ? 0 : 1);
        }
        if (pid > 0) {
            int status = 0;
            TEST(group, "missing binary exec fails without replacing child",
                 waitpid(pid, &status, 0) == pid &&
                 WIFEXITED(status) &&
                 WEXITSTATUS(status) == 0);
        }
    }

    {
        pid_t pid = fork();
        TEST(group, "non-ELF target fork succeeds", pid >= 0);
        if (pid == 0) {
            char *argv[] = { "hello.txt", NULL };
            _exit(execve("/hello.txt", argv, NULL) < 0 ? 0 : 1);
        }
        if (pid > 0) {
            int status = 0;
            TEST(group, "execing plain text file fails cleanly",
                 waitpid(pid, &status, 0) == pid &&
                 WIFEXITED(status) &&
                 WEXITSTATUS(status) == 0);
        }
    }

    {
        char stdout_buf[64];
        char stderr_buf[128];
        char file_buf[128];
        const char *path = "/mnt/sh_redirect_basic.txt";
        int status;

        unlink(path);
        status = run_shell_script("echo hello > /mnt/sh_redirect_basic.txt\n",
                                  stdout_buf, sizeof(stdout_buf),
                                  stderr_buf, sizeof(stderr_buf));
        TEST(group, "shell redirect: /bin/sh executes", status >= 0);
        TEST(group, "shell redirect: shell exits successfully",
             status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0);
        TEST(group, "shell redirect: no stderr", stderr_buf[0] == '\0');
        TEST(group, "shell redirect: output file created",
             read_file_text(path, file_buf, sizeof(file_buf)) == 0);
        TEST(group, "shell redirect: stdout lands in file",
             strcmp(file_buf, "hello\n") == 0);
    }

    {
        char stderr_buf[128];
        char file_buf[128];
        const char *path = "/mnt/sh_redirect_nospace.txt";
        int status;

        unlink(path);
        status = run_shell_script("echo compact>/mnt/sh_redirect_nospace.txt\n",
                                  NULL, 0, stderr_buf, sizeof(stderr_buf));
        TEST(group, "shell redirect(no spaces): /bin/sh executes", status >= 0);
        TEST(group, "shell redirect(no spaces): exits successfully",
             status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0);
        TEST(group, "shell redirect(no spaces): no stderr", stderr_buf[0] == '\0');
        TEST(group, "shell redirect(no spaces): file matches expected payload",
             read_file_text(path, file_buf, sizeof(file_buf)) == 0 &&
             strcmp(file_buf, "compact\n") == 0);
    }

    {
        char source_buf[256];
        char redirected_buf[256];
        char stderr_buf[128];
        const char *path = "/mnt/sh_redirect_input.txt";
        int status;

        unlink(path);
        status = run_shell_script("cat</hello.txt>/mnt/sh_redirect_input.txt\n",
                                  NULL, 0, stderr_buf, sizeof(stderr_buf));
        TEST(group, "shell input redirect: /bin/sh executes", status >= 0);
        TEST(group, "shell input redirect: exits successfully",
             status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0);
        TEST(group, "shell input redirect: no stderr", stderr_buf[0] == '\0');
        TEST(group, "shell input redirect: source file readable",
             read_file_text("/hello.txt", source_buf, sizeof(source_buf)) == 0);
        TEST(group, "shell input redirect: redirected file readable",
             read_file_text(path, redirected_buf, sizeof(redirected_buf)) == 0);
        TEST(group, "shell input redirect: copied file matches source",
             strcmp(source_buf, redirected_buf) == 0);
    }

    {
        char file_buf[128];
        char stderr_buf[128];
        const char *path = "/mnt/sh_redirect_truncate.txt";
        int status;

        TEST(group, "shell redirect truncate: seed file written",
             write_file_text(path, "this should be replaced entirely\n") == 0);
        status = run_shell_script("echo short > /mnt/sh_redirect_truncate.txt\n",
                                  NULL, 0, stderr_buf, sizeof(stderr_buf));
        TEST(group, "shell redirect truncate: /bin/sh executes", status >= 0);
        TEST(group, "shell redirect truncate: exits successfully",
             status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0);
        TEST(group, "shell redirect truncate: no stderr", stderr_buf[0] == '\0');
        TEST(group, "shell redirect truncate: file was truncated and rewritten",
             read_file_text(path, file_buf, sizeof(file_buf)) == 0 &&
             strcmp(file_buf, "short\n") == 0);
    }

    {
        char stderr_buf[128];
        int status = run_shell_script("echo broken >\n", NULL, 0, stderr_buf, sizeof(stderr_buf));

        TEST(group, "shell redirect syntax error: /bin/sh executes", status >= 0);
        TEST(group, "shell redirect syntax error: shell stays alive and exits cleanly",
             status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0);
        TEST(group, "shell redirect syntax error: emits parser error",
             strstr(stderr_buf, "invalid syntax") != NULL);
    }

    {
        char stderr_buf[128];
        char file_buf[128];
        const char *path = "/mnt/sh_pipe_basic.txt";
        int status;

        unlink(path);
        status = run_shell_script("echo piped text | cat > /mnt/sh_pipe_basic.txt\n",
                                  NULL, 0, stderr_buf, sizeof(stderr_buf));
        TEST(group, "shell pipe: /bin/sh executes", status >= 0);
        TEST(group, "shell pipe: exits successfully",
             status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0);
        TEST(group, "shell pipe: no stderr", stderr_buf[0] == '\0');
        TEST(group, "shell pipe: final output redirected to file",
             read_file_text(path, file_buf, sizeof(file_buf)) == 0 &&
             strcmp(file_buf, "piped text\n") == 0);
    }

    {
        char stderr_buf[128];
        char file_buf[128];
        const char *path = "/mnt/sh_pipe_chain.txt";
        int status;

        unlink(path);
        status = run_shell_script("echo chain|cat|cat>/mnt/sh_pipe_chain.txt\n",
                                  NULL, 0, stderr_buf, sizeof(stderr_buf));
        TEST(group, "shell multi-stage pipe(no spaces): /bin/sh executes", status >= 0);
        TEST(group, "shell multi-stage pipe(no spaces): exits successfully",
             status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0);
        TEST(group, "shell multi-stage pipe(no spaces): no stderr", stderr_buf[0] == '\0');
        TEST(group, "shell multi-stage pipe(no spaces): data survives pipeline",
             read_file_text(path, file_buf, sizeof(file_buf)) == 0 &&
             strcmp(file_buf, "chain\n") == 0);
    }

    {
        char source_buf[256];
        char file_buf[256];
        char stderr_buf[128];
        const char *path = "/mnt/sh_pipe_input.txt";
        int status;

        unlink(path);
        status = run_shell_script("cat</hello.txt|cat>/mnt/sh_pipe_input.txt\n",
                                  NULL, 0, stderr_buf, sizeof(stderr_buf));
        TEST(group, "shell pipe with redirects: /bin/sh executes", status >= 0);
        TEST(group, "shell pipe with redirects: exits successfully",
             status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0);
        TEST(group, "shell pipe with redirects: no stderr", stderr_buf[0] == '\0');
        TEST(group, "shell pipe with redirects: source file readable",
             read_file_text("/hello.txt", source_buf, sizeof(source_buf)) == 0);
        TEST(group, "shell pipe with redirects: result file readable",
             read_file_text(path, file_buf, sizeof(file_buf)) == 0);
        TEST(group, "shell pipe with redirects: pipeline preserves redirected input",
             strcmp(source_buf, file_buf) == 0);
    }

    ut_group_end(group);
}
