#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_LINE 1024
#define MAX_ARGS 64
#define MAX_CMDS 16

typedef struct shell_command {
    char *argv[MAX_ARGS];
    char *input_path;
    char *output_path;
} shell_command_t;

static int is_space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n';
}

static int is_special(char ch) {
    return ch == '|' || ch == '<' || ch == '>';
}

static char *next_token(char **cursor) {
    static char special_token[2];
    char *p = *cursor;
    char *start;

    while (*p && is_space(*p)) {
        p++;
    }
    if (*p == '\0') {
        *cursor = p;
        return NULL;
    }

    if (is_special(*p)) {
        special_token[0] = *p++;
        special_token[1] = '\0';
        *cursor = p;
        return special_token;
    }

    start = p;
    while (*p && !is_space(*p) && !is_special(*p)) {
        p++;
    }
    if (*p) {
        *p++ = '\0';
    }
    *cursor = p;
    return start;
}

static int is_token(const char *token, char expected) {
    return token != NULL && token[0] == expected && token[1] == '\0';
}

static int parse_line(char *line, shell_command_t *cmdv) {
    char *cursor = line;
    char *token;
    int cmdc = 0;
    int argc = 0;

    memset(cmdv, 0, sizeof(shell_command_t) * MAX_CMDS);

    while ((token = next_token(&cursor)) != NULL) {
        if (cmdc >= MAX_CMDS) {
            return -1;
        }

        if (is_token(token, '|')) {
            if (argc == 0) {
                return -1;
            }
            cmdv[cmdc].argv[argc] = NULL;
            cmdc++;
            argc = 0;
            continue;
        }

        if (is_token(token, '<') || is_token(token, '>')) {
            char *path = next_token(&cursor);
            if (path == NULL || is_token(path, '|') || is_token(path, '<') || is_token(path, '>')) {
                return -1;
            }

            if (cmdc >= MAX_CMDS) {
                return -1;
            }

            if (argc == 0 && cmdv[cmdc].argv[0] == NULL) {
                return -1;
            }

            if (token[0] == '<') {
                if (cmdv[cmdc].input_path != NULL) {
                    return -1;
                }
                cmdv[cmdc].input_path = path;
            } else {
                if (cmdv[cmdc].output_path != NULL) {
                    return -1;
                }
                cmdv[cmdc].output_path = path;
            }
            continue;
        }

        if (argc >= MAX_ARGS - 1) {
            return -1;
        }
        cmdv[cmdc].argv[argc++] = token;
    }

    if (argc == 0) {
        return cmdc == 0 ? 0 : -1;
    }

    cmdv[cmdc].argv[argc] = NULL;
    return cmdc + 1;
}

static void build_exec_path(const char *command, char *path, size_t path_size) {
    if (command[0] != '/') {
        snprintf(path, path_size, "/bin/%s", command);
    } else {
        strncpy(path, command, path_size - 1);
        path[path_size - 1] = '\0';
    }
}

static int redirect_fd(int from_fd, int to_fd) {
    if (from_fd < 0) {
        return -1;
    }
    if (from_fd == to_fd) {
        return 0;
    }
    if (dup2(from_fd, to_fd) < 0) {
        return -1;
    }
    close(from_fd);
    return 0;
}

static int apply_redirections(const shell_command_t *cmd) {
    int fd;

    if (cmd->input_path) {
        fd = open(cmd->input_path, O_RDONLY, 0);
        if (fd < 0) {
            dprintf(STDERR_FILENO, "sh: cannot open '%s'\n", cmd->input_path);
            return -1;
        }
        if (redirect_fd(fd, STDIN_FILENO) < 0) {
            dprintf(STDERR_FILENO, "sh: dup2 failed for '%s'\n", cmd->input_path);
            close(fd);
            return -1;
        }
    }

    if (cmd->output_path) {
        fd = open(cmd->output_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            dprintf(STDERR_FILENO, "sh: cannot open '%s'\n", cmd->output_path);
            return -1;
        }
        if (redirect_fd(fd, STDOUT_FILENO) < 0) {
            dprintf(STDERR_FILENO, "sh: dup2 failed for '%s'\n", cmd->output_path);
            close(fd);
            return -1;
        }
    }

    return 0;
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

static int is_builtin(char **argv) {
    if (!argv[0]) {
        return 0;
    }
    return strcmp(argv[0], "cd") == 0 || strcmp(argv[0], "exit") == 0;
}

static int run_builtin(char **argv) {
    if (strcmp(argv[0], "cd") == 0) {
        return builtin_cd(argv);
    }
    if (strcmp(argv[0], "exit") == 0) {
        return builtin_exit(argv);
    }
    return -1;
}

static int execute_single(shell_command_t *cmd) {
    int builtin_status;
    int saved_stdin = -1;
    int saved_stdout = -1;
    pid_t pid;
    int status;

    if (!cmd->argv[0]) {
        return 0;
    }

    if (is_builtin(cmd->argv) && !cmd->input_path && !cmd->output_path) {
        return run_builtin(cmd->argv);
    }

    if (is_builtin(cmd->argv)) {
        if (cmd->input_path) {
            saved_stdin = dup(STDIN_FILENO);
            if (saved_stdin < 0) {
                dprintf(STDERR_FILENO, "sh: dup failed\n");
                return 1;
            }
        }
        if (cmd->output_path) {
            saved_stdout = dup(STDOUT_FILENO);
            if (saved_stdout < 0) {
                dprintf(STDERR_FILENO, "sh: dup failed\n");
                if (saved_stdin >= 0) close(saved_stdin);
                return 1;
            }
        }

        if (apply_redirections(cmd) < 0) {
            if (saved_stdin >= 0) {
                dup2(saved_stdin, STDIN_FILENO);
                close(saved_stdin);
            }
            if (saved_stdout >= 0) {
                dup2(saved_stdout, STDOUT_FILENO);
                close(saved_stdout);
            }
            return 1;
        }

        builtin_status = run_builtin(cmd->argv);

        if (saved_stdin >= 0) {
            dup2(saved_stdin, STDIN_FILENO);
            close(saved_stdin);
        }
        if (saved_stdout >= 0) {
            dup2(saved_stdout, STDOUT_FILENO);
            close(saved_stdout);
        }
        return builtin_status;
    }

    pid = fork();
    if (pid < 0) {
        write(STDERR_FILENO, "fork failed\n", 12);
        return 1;
    }

    if (pid == 0) {
        if (apply_redirections(cmd) < 0) {
            _exit(1);
        }
        exec_command(cmd->argv);
    }

    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

static int execute_pipeline(char *line) {
    shell_command_t cmdv[MAX_CMDS];
    pid_t pids[MAX_CMDS];
    int cmdc;
    int prev_read = -1;
    int status = 0;
    int last_status = 0;

    cmdc = parse_line(line, cmdv);
    if (cmdc == 0) return 0;
    if (cmdc < 0) {
        dprintf(STDERR_FILENO, "sh: invalid syntax\n");
        return 1;
    }

    if (cmdc == 1) {
        return execute_single(&cmdv[0]);
    }

    for (int i = 0; i < cmdc; i++) {
        int pipefd[2] = {-1, -1};

        if (!cmdv[i].argv[0]) {
            dprintf(STDERR_FILENO, "sh: invalid syntax\n");
            if (prev_read >= 0) close(prev_read);
            return 1;
        }

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
            if (prev_read >= 0 && redirect_fd(prev_read, STDIN_FILENO) < 0) {
                _exit(1);
            }
            if (pipefd[1] >= 0 && redirect_fd(pipefd[1], STDOUT_FILENO) < 0) {
                _exit(1);
            }

            if (pipefd[0] >= 0) close(pipefd[0]);
            if (apply_redirections(&cmdv[i]) < 0) {
                _exit(1);
            }

            exec_command(cmdv[i].argv);
        }

        if (prev_read >= 0) close(prev_read);
        if (pipefd[1] >= 0) close(pipefd[1]);
        prev_read = pipefd[0];
    }

    if (prev_read >= 0) close(prev_read);

    for (int i = 0; i < cmdc; i++) {
        waitpid(pids[i], &status, 0);
        last_status = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
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
