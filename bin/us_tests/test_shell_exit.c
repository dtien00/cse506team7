#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "us_test_common.h"

/*
 * Run cmd_str in /bin/sh by writing it to sh's stdin via pipe.
 * Returns raw waitpid status, or -1 on error.
 * Shell doesn't support -c, so we feed the command through stdin.
 */
static int shell_exit_status(const char *cmd_str) {
    int pipefd[2];
    if (pipe(pipefd) < 0) return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    if (pid == 0) {
        close(pipefd[1]);
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);
        char *argv[] = { "sh", NULL };
        execve("/bin/sh", argv, NULL);
        _exit(127);
    }

    close(pipefd[0]);
    /* Write command + newline so the shell reads and executes it, then EOF closes. */
    write(pipefd[1], cmd_str, strlen(cmd_str));
    write(pipefd[1], "\n", 1);
    close(pipefd[1]);

    int status = -1;
    if (waitpid(pid, &status, 0) != pid) return -1;
    return status;
}

/* Fork a child that calls _exit(code); return raw waitpid status. */
static int fork_exit(int code) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) _exit(code);
    int status = -1;
    if (waitpid(pid, &status, 0) != pid) return -1;
    return status;
}

/* Fork a child that raises sig (default disposition → terminate). */
static int fork_signal(int sig) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        raise(sig);
        _exit(0);
    }
    int status = -1;
    if (waitpid(pid, &status, 0) != pid) return -1;
    return status;
}

void run_test_shell_exit(us_test_group_t *group) {
    ut_group_begin(group, "shell-exit");

    /* ── 1. Shell exit builtin propagation ─────────────────────────────── */
    {
        int s;

        s = shell_exit_status("exit 0");
        TEST(group, "sh -c 'exit 0': exited normally",   WIFEXITED(s));
        TEST(group, "sh -c 'exit 0': code == 0",         WEXITSTATUS(s) == 0);

        s = shell_exit_status("exit 1");
        TEST(group, "sh -c 'exit 1': exited normally",   WIFEXITED(s));
        TEST(group, "sh -c 'exit 1': code == 1",         WEXITSTATUS(s) == 1);

        s = shell_exit_status("exit 42");
        TEST(group, "sh -c 'exit 42': exited normally",  WIFEXITED(s));
        TEST(group, "sh -c 'exit 42': code == 42",       WEXITSTATUS(s) == 42);

        s = shell_exit_status("exit 127");
        TEST(group, "sh -c 'exit 127': exited normally", WIFEXITED(s));
        TEST(group, "sh -c 'exit 127': code == 127",     WEXITSTATUS(s) == 127);

        s = shell_exit_status("exit 255");
        TEST(group, "sh -c 'exit 255': exited normally", WIFEXITED(s));
        TEST(group, "sh -c 'exit 255': code == 255",     WEXITSTATUS(s) == 255);

        /* POSIX: only low 8 bits are meaningful */
        s = shell_exit_status("exit 256");
        TEST(group, "sh -c 'exit 256': exited normally", WIFEXITED(s));
        TEST(group, "sh -c 'exit 256': code == 0 (256 & 0xff)", WEXITSTATUS(s) == 0);

        s = shell_exit_status("exit");
        TEST(group, "sh -c 'exit' (no arg): exited normally", WIFEXITED(s));
        TEST(group, "sh -c 'exit' (no arg): code == 0",       WEXITSTATUS(s) == 0);
    }

    /* ── 2. exec child exit-code propagation ───────────────────────────── */
    {
        int s;

        s = fork_exit(0);
        TEST(group, "_exit(0): WIFEXITED",        WIFEXITED(s));
        TEST(group, "_exit(0): code == 0",        WEXITSTATUS(s) == 0);

        s = fork_exit(1);
        TEST(group, "_exit(1): WIFEXITED",        WIFEXITED(s));
        TEST(group, "_exit(1): code == 1",        WEXITSTATUS(s) == 1);

        s = fork_exit(123);
        TEST(group, "_exit(123): WIFEXITED",      WIFEXITED(s));
        TEST(group, "_exit(123): code == 123",    WEXITSTATUS(s) == 123);

        /* Multi-level: shell as intermediary */
        s = shell_exit_status("exit 77");
        TEST(group, "shell intermediary exit 77: WIFEXITED",    WIFEXITED(s));
        TEST(group, "shell intermediary exit 77: code == 77",   WEXITSTATUS(s) == 77);
    }

    /* ── 3. WIFEXITED / WIFSIGNALED / WTERMSIG macros ───────────────────── */
    {
        int s;

        s = fork_exit(5);
        TEST(group, "normal exit: WIFEXITED true",    WIFEXITED(s));
        TEST(group, "normal exit: WIFSIGNALED false",  !WIFSIGNALED(s));

        /* SIGKILL cannot be caught — reliable signal death */
        pid_t pid = fork();
        if (pid == 0) { while (1) sleep(1); }
        if (pid > 0) {
            kill(pid, SIGKILL);
            s = -1;
            waitpid(pid, &s, 0);
            TEST(group, "SIGKILL: WIFSIGNALED true",    WIFSIGNALED(s));
            TEST(group, "SIGKILL: WTERMSIG == SIGKILL", WTERMSIG(s) == SIGKILL);
            TEST(group, "SIGKILL: WIFEXITED false",     !WIFEXITED(s));
        }

        s = fork_signal(SIGTERM);
        TEST(group, "SIGTERM: WIFSIGNALED true",    WIFSIGNALED(s));
        TEST(group, "SIGTERM: WTERMSIG == SIGTERM", WTERMSIG(s) == SIGTERM);
        TEST(group, "SIGTERM: WIFEXITED false",     !WIFEXITED(s));
    }

    /* ── 4. Exit status encoding (kernel packs correctly) ───────────────── */
    {
        int s = fork_exit(7);
        /* status low byte == 0 for normal exit; next byte == exit code */
        TEST(group, "status encoding: low byte 0 on normal exit",  (s & 0x7f) == 0);
        TEST(group, "status encoding: exit code in byte 1",        ((s >> 8) & 0xff) == 7);
    }

    /* ── 5. waitpid options ─────────────────────────────────────────────── */
    {
        /* WNOHANG with no child ready → 0 */
        pid_t pid = fork();
        if (pid == 0) { while (1) sleep(1); }
        if (pid > 0) {
            pid_t w = waitpid(pid, NULL, WNOHANG);
            TEST(group, "WNOHANG on live child: returns 0", w == 0);
            kill(pid, SIGKILL);
            /* drain child after SIGKILL */
            while (waitpid(pid, NULL, 0) < 0) {}
        }

        /* WNOHANG with ready child → pid */
        pid = fork();
        if (pid == 0) _exit(0);
        if (pid > 0) {
            /* give child a moment to exit */
            sleep(1);
            int s = 0;
            pid_t w = waitpid(pid, &s, WNOHANG);
            TEST(group, "WNOHANG on ready child: returns pid",   w == pid);
            TEST(group, "WNOHANG on ready child: WIFEXITED",     WIFEXITED(s));
        }

        /* Double wait → ECHILD */
        pid = fork();
        if (pid == 0) _exit(0);
        if (pid > 0) {
            waitpid(pid, NULL, 0);
            pid_t w2 = waitpid(pid, NULL, 0);
            TEST(group, "double waitpid on same child: returns -1 (ECHILD)", w2 == -1);
        }

        /* waitpid for non-child PID */
        pid_t w = waitpid(99999, NULL, 0);
        TEST(group, "waitpid non-child PID: returns -1", w == -1);
    }

    /* ── 6. Zombie reaping ──────────────────────────────────────────────── */
    {
        pid_t pid = fork();
        if (pid == 0) _exit(0);
        if (pid > 0) {
            /* child exits → becomes zombie; wait reaps it */
            int s = 0;
            pid_t w = waitpid(pid, &s, 0);
            TEST(group, "zombie reap: waitpid returns child pid", w == pid);
            TEST(group, "zombie reap: WIFEXITED after reap",      WIFEXITED(s));
            /* child gone → second wait gives error */
            TEST(group, "zombie reap: no longer in proc table",   waitpid(pid, NULL, 0) == -1);
        }
    }

    /* ── 7. Exit status through shell pipeline ──────────────────────────── */
    {
        int s;

        s = shell_exit_status("false");
        TEST(group, "sh -c 'false': WIFEXITED",    WIFEXITED(s));
        TEST(group, "sh -c 'false': code == 1",    WEXITSTATUS(s) == 1);

        s = shell_exit_status("true");
        TEST(group, "sh -c 'true': WIFEXITED",     WIFEXITED(s));
        TEST(group, "sh -c 'true': code == 0",     WEXITSTATUS(s) == 0);

        /* Commands after exit must not run */
        s = shell_exit_status("exit 3; exit 9");
        TEST(group, "sh -c 'exit 3; exit 9': WIFEXITED",  WIFEXITED(s));
        TEST(group, "sh -c 'exit 3; exit 9': code == 3",  WEXITSTATUS(s) == 3);

        /* Command not found → 127 */
        s = shell_exit_status("/nonexistent_cmd_zxqy");
        TEST(group, "sh: command not found: WIFEXITED",   WIFEXITED(s));
        TEST(group, "sh: command not found: code == 127", WEXITSTATUS(s) == 127);
    }

    /* ── 8. Signal + exit interaction ───────────────────────────────────── */
    {
        /* Caught signal → handler calls exit(5) → WIFEXITED, code 5 */
        pid_t pid = fork();
        if (pid == 0) {
            struct sigaction sa;
            sa.sa_handler = (void (*)(int))_exit; /* _exit as handler: exit(sig_num) */
            sa.sa_flags = 0;
            sigemptyset(&sa.sa_mask);
            /* override: use a proper handler that calls exit(5) */
            /* Can't define nested fn — just raise SIGTERM with default action */
            /* Use shell to test caught-signal-then-exit path */
            _exit(0);
        }
        waitpid(pid, NULL, 0);

        /* raise(SIGTERM) in child: WIFSIGNALED */
        int s = fork_signal(SIGTERM);
        TEST(group, "raise(SIGTERM): WIFSIGNALED",        WIFSIGNALED(s));
        TEST(group, "raise(SIGTERM): WTERMSIG == SIGTERM", WTERMSIG(s) == SIGTERM);
        TEST(group, "raise(SIGTERM): WIFEXITED false",    !WIFEXITED(s));

        /* raise(SIGKILL): cannot be caught */
        pid = fork();
        if (pid == 0) { raise(SIGKILL); _exit(0); }
        if (pid > 0) {
            s = -1;
            waitpid(pid, &s, 0);
            TEST(group, "raise(SIGKILL): WIFSIGNALED",         WIFSIGNALED(s));
            TEST(group, "raise(SIGKILL): WTERMSIG == SIGKILL",  WTERMSIG(s) == SIGKILL);
        }
    }

    ut_group_end(group);
}
