#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

static void head_file(int fd, int lines) {
    char buf[4096];
    int n, count = 0;
    
    while ((n = read(fd, buf, sizeof(buf))) > 0 && count < lines) {
        for (int i = 0; i < n && count < lines; i++) {
            write(STDOUT_FILENO, &buf[i], 1);
            if (buf[i] == '\n') count++;
        }
    }
}

int main(int argc, char *argv[]) {
    int lines = 10;
    int start_idx = 1;
    
    if (argc > 1 && argv[1][0] == '-') {
        lines = atoi(argv[1] + 1);
        start_idx = 2;
    }
    
    if (argc <= start_idx) {
        head_file(STDIN_FILENO, lines);
    } else {
        for (int i = start_idx; i < argc; i++) {
            int fd = open(argv[i], O_RDONLY);
            if (fd < 0) {
                printf("head: cannot open '%s'\n", argv[i]);
                continue;
            }
            if (argc - start_idx > 1) {
                printf("==> %s <==\n", argv[i]);
            }
            head_file(fd, lines);
            close(fd);
        }
    }
    return 0;
}
