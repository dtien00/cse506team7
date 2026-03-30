#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_LINE 1024
#define MAX_ARGS 64

static char line[MAX_LINE];
static char *args[MAX_ARGS];

static void parse_line(char *line, char **args) {
    int i = 0;
    char *token = line;
    int in_word = 0;
    
    while (*token && i < MAX_ARGS - 1) {
        if (*token == ' ' || *token == '\t' || *token == '\n') {
            if (in_word) {
                *token = '\0';
                in_word = 0;
            }
        } else {
            if (!in_word) {
                args[i++] = token;
                in_word = 1;
            }
        }
        token++;
    }
    args[i] = NULL;
}

static int builtin_cd(char **args) {
    if (args[1] == NULL) {
        chdir("/");
    } else {
        if (chdir(args[1]) < 0) {
            printf("cd: cannot change directory to %s\n", args[1]);
            return -1;
        }
    }
    return 0;
}

static int builtin_exit(char **args) {
    int code = 0;
    if (args[1] != NULL) {
        code = atoi(args[1]);
    }
    exit(code);
    return 0;
}

static int execute_command(char **args) {
    if (args[0] == NULL) {
        return 0;
    }
    
    if (strcmp(args[0], "cd") == 0) {
        return builtin_cd(args);
    }
    
    if (strcmp(args[0], "exit") == 0) {
        return builtin_exit(args);
    }
    
    pid_t pid = fork();
    if (pid == 0) {
        char path[256];
        
        if (args[0][0] == '/') {
            execve(args[0], args, NULL);
        } else {
            snprintf(path, sizeof(path), "/bin/%s", args[0]);
            execve(path, args, NULL);
        }
        
        printf("%s: command not found\n", args[0]);
        exit(127);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        return status;
    } else {
        printf("fork failed\n");
        return -1;
    }
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    while (1) {
        printf("$ ");
        
        int i = 0;
        int c;
        while (i < MAX_LINE - 1 && (c = getchar()) != EOF && c != '\n') {
            line[i++] = c;
        }
        
        if (c == EOF) {
            break;
        }
        
        line[i] = '\0';
        
        if (i == 0) {
            continue;
        }
        
        parse_line(line, args);
        execute_command(args);
    }
    
    return 0;
}
