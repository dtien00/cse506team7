#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#define MAX_LINES 1000

static void tail_file(int fd, int lines) {
    char buf[4096];
    char *line_starts[MAX_LINES];
    int line_count = 0;
    int n;
    static char storage[65536];
    int storage_pos = 0;
    
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n; i++) {
            if (storage_pos >= sizeof(storage) - 1) break;
            
            if (buf[i] == '\n') {
                storage[storage_pos++] = '\n';
                if (line_count < MAX_LINES) {
                    line_starts[line_count++] = &storage[storage_pos];
                } else {
                    for (int j = 0; j < MAX_LINES - 1; j++) {
                        line_starts[j] = line_starts[j + 1];
                    }
                    line_starts[MAX_LINES - 1] = &storage[storage_pos];
                }
            } else {
                storage[storage_pos++] = buf[i];
            }
        }
    }
    storage[storage_pos] = '\0';
    
    int start = line_count > lines ? line_count - lines : 0;
    if (start == 0 && line_count > 0) {
        write(STDOUT_FILENO, storage, storage_pos);
    } else {
        for (int i = start; i < line_count; i++) {
            char *end = (i + 1 < line_count) ? line_starts[i + 1] : &storage[storage_pos];
            write(STDOUT_FILENO, line_starts[i], end - line_starts[i]);
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
        tail_file(STDIN_FILENO, lines);
    } else {
        for (int i = start_idx; i < argc; i++) {
            int fd = open(argv[i], O_RDONLY);
            if (fd < 0) {
                printf("tail: cannot open '%s'\n", argv[i]);
                continue;
            }
            if (argc - start_idx > 1) {
                printf("==> %s <==\n", argv[i]);
            }
            tail_file(fd, lines);
            close(fd);
        }
    }
    return 0;
}
