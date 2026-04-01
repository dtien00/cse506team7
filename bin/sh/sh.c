#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_LINE 1024
#define MAX_ARGS 64

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

static int builtin_cd(char **argv) {
    if (!argv[1]) {
        return 0;
    }
    if (chdir(argv[1]) < 0) {
        printf("cd: cannot change directory to '%s'\n", argv[1]);
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
        printf("fork failed\n");
        return 1;
    }

    if (pid == 0) {
        char path[256];
        if (argv[0][0] != '/') {
            strcpy(path, "/bin/");
            strcat(path, argv[0]);
        } else {
            strcpy(path, argv[0]);
        }

        execve(path, argv, NULL);
        printf("sh: %s: command not found\n", argv[0]);
        exit(127);
    }

    int status;
    waitpid(pid, &status, 0);
    return WEXITSTATUS(status);
}

int main(void) {
    char line[MAX_LINE];
    char *argv[MAX_ARGS];
    int n;

    while (1) {
        write(STDOUT_FILENO, "$ ", 2);

        n = read(STDIN_FILENO, line, sizeof(line) - 1);

        line[n] = '\0';

        if (line[0] == '\n' || line[0] == '\0') continue;

        parse_args(line, argv);
        execute(argv);
    }

    return 0;
}
