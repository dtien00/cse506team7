#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

static int grep_file(const char *pattern, int fd, const char *filename) {
    char buf[4096];
    char line[1024];
    int line_pos = 0;
    int n, found = 0;
    
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                line[line_pos] = '\0';
                if (strstr(line, pattern)) {
                    if (filename) {
                        printf("%s:", filename);
                    }
                    printf("%s\n", line);
                    found = 1;
                }
                line_pos = 0;
            } else if (line_pos < sizeof(line) - 1) {
                line[line_pos++] = buf[i];
            }
        }
    }
    
    if (line_pos > 0) {
        line[line_pos] = '\0';
        if (strstr(line, pattern)) {
            if (filename) {
                printf("%s:", filename);
            }
            printf("%s\n", line);
            found = 1;
        }
    }
    
    return found;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        write(STDERR_FILENO, "grep: usage: grep pattern [file...]\n", 36);
        return 1;
    }
    
    const char *pattern = argv[1];
    int found = 0;
    
    if (argc == 2) {
        found = grep_file(pattern, STDIN_FILENO, NULL);
    } else {
        for (int i = 2; i < argc; i++) {
            int fd = open(argv[i], O_RDONLY);
            if (fd < 0) {
                printf("grep: cannot open '%s'\n", argv[i]);
                continue;
            }
            if (grep_file(pattern, fd, argc > 3 ? argv[i] : NULL)) {
                found = 1;
            }
            close(fd);
        }
    }
    
    return found ? 0 : 1;
}
