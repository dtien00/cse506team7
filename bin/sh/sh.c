#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_LINE 1024
#define MAX_ARGS 64
#define MAX_CMDS 16

static void parse_args(char *line, char **argv) {
    int argc = 0;
    char *p = line;

    while (*p && argc < MAX_ARGS - 1) {
        while (*p == ' ' || *p == '\t' || *p == '\n') p++;
        if (*p == '\0') break;

        argv[argc++] = p;

        while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
        if (*p) *p++ = '\0';
    }
    argv[argc] = NULL;
}

static int parse_pipeline(char *line, char **cmdv) {
    int argc = 0;
    char *p = line;
    int expect_command = 1;

    while (*p && argc < MAX_CMDS) {
        while (*p == ' ' || *p == '\t' || *p == '\n') p++;
        if (*p == '\0') break;

        expect_command = 0;
        cmdv[argc++] = p;

        while (*p && *p != '|') p++;
        if (*p == '|') {
            *p++ = '\0';
            expect_command = 1;
        }
    }

    if (expect_command && argc > 0) {
        return -1;
    }

    return argc;
}

static void build_exec_path(const char *command, char *path, size_t path_size) {
    if (command[0] != '/') {
        snprintf(path, path_size, "/bin/%s", command);
    } else {
        strncpy(path, command, path_size - 1);
        path[path_size - 1] = '\0';
    }
}

static void exec_command(char **argv) {
    char path[256];

    build_exec_path(argv[0], path, sizeof(path));
    execve(path, argv, NULL);
    dprintf(STDERR_FILENO, "sh: %s: command not found\n", argv[0]);
    exit(127);
}

static int builtin_cd(char **argv) {
    if (!argv[1]) {
        return 0;
    }
    if (chdir(argv[1]) < 0) {
        dprintf(STDERR_FILENO, "cd: cannot change directory to '%s'\n", argv[1]);
        return 1;
    }
    return 0;
}

static int builtin_exit(char **argv) {
    int code = 0;
    if (argv[1]) {
        code = atoi(argv[1]);
    }
    exit(code);
    return 0;
}

static int execute(char **argv) {
    if (!argv[0]) return 0;

    if (strcmp(argv[0], "cd") == 0) {
        return builtin_cd(argv);
    }
    if (strcmp(argv[0], "exit") == 0) {
        return builtin_exit(argv);
    }

    pid_t pid = fork();
    if (pid < 0) {
        write(STDERR_FILENO, "fork failed\n", 12);
        return 1;
    }

    if (pid == 0) {
        setpgid(0, 0);  /* child becomes its own process group leader */
        exec_command(argv);
    }

    int status;
    waitpid(pid, &status, 0);
    return WEXITSTATUS(status);
}

static int execute_pipeline(char *line) {
    char *cmdv[MAX_CMDS];
    char *argvv[MAX_CMDS][MAX_ARGS];
    pid_t pids[MAX_CMDS];
    int cmdc;
    int prev_read = -1;
    int status = 0;
    int last_status = 0;

    cmdc = parse_pipeline(line, cmdv);
    if (cmdc == 0) return 0;
    if (cmdc < 0) {
        dprintf(STDERR_FILENO, "sh: invalid null command\n");
        return 1;
    }

    for (int i = 0; i < cmdc; i++) {
        parse_args(cmdv[i], argvv[i]);
        if (!argvv[i][0]) {
            dprintf(STDERR_FILENO, "sh: invalid null command\n");
            if (prev_read >= 0) close(prev_read);
            return 1;
        }
    }

    if (cmdc == 1) {
        return execute(argvv[0]);
    }

    for (int i = 0; i < cmdc; i++) {
        int pipefd[2] = {-1, -1};

        if (i < cmdc - 1 && pipe(pipefd) < 0) {
            write(STDERR_FILENO, "pipe failed\n", 12);
            if (prev_read >= 0) close(prev_read);
            return 1;
        }

        pids[i] = fork();
        if (pids[i] < 0) {
            write(STDERR_FILENO, "fork failed\n", 12);
            if (prev_read >= 0) close(prev_read);
            if (pipefd[0] >= 0) close(pipefd[0]);
            if (pipefd[1] >= 0) close(pipefd[1]);
            return 1;
        }

        if (pids[i] == 0) {
            /* All pipeline members share the first child's pgid */
            pid_t pipeline_pgid = (i == 0) ? 0 : pids[0];
            setpgid(0, pipeline_pgid);

            if (prev_read >= 0) {
                dup2(prev_read, STDIN_FILENO);
            }
            if (pipefd[1] >= 0) {
                dup2(pipefd[1], STDOUT_FILENO);
            }

            if (prev_read >= 0) close(prev_read);
            if (pipefd[0] >= 0) close(pipefd[0]);
            if (pipefd[1] >= 0) close(pipefd[1]);

            exec_command(argvv[i]);
        }

        if (prev_read >= 0) close(prev_read);
        if (pipefd[1] >= 0) close(pipefd[1]);
        prev_read = pipefd[0];
    }

    if (prev_read >= 0) close(prev_read);

    for (int i = 0; i < cmdc; i++) {
        waitpid(pids[i], &status, 0);
        if (WIFEXITED(status)) {
            last_status = WEXITSTATUS(status);
        } else {
            last_status = 1;
        }
    }

    return last_status;
}

int main(void) {
    char line[MAX_LINE];
    int n;

    while (1) {
        write(STDOUT_FILENO, "$ ", 2);

        n = read(STDIN_FILENO, line, sizeof(line) - 1);
        if (n <= 0) {
            break;
        }

        line[n] = '\0';

        if (line[0] == '\n' || line[0] == '\0') continue;

        execute_pipeline(line);
    }

    return 0;
}
