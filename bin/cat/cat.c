#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#define BUF_SIZE 4096

static void cat_file(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("cat: cannot open %s\n", path);
        return;
    }
    
    char buf[BUF_SIZE];
    ssize_t n;
    while ((n = read(fd, buf, BUF_SIZE)) > 0) {
        write(STDOUT_FILENO, buf, n);
    }
    
    close(fd);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        char buf[BUF_SIZE];
        ssize_t n;
        while ((n = read(STDIN_FILENO, buf, BUF_SIZE)) > 0) {
            write(STDOUT_FILENO, buf, n);
        }
    } else {
        for (int i = 1; i < argc; i++) {
            cat_file(argv[i]);
        }
    }
    return 0;
}
