#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#define BUF_SIZE 4096

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("usage: cp <source> <dest>\n");
        return 1;
    }
    
    int src_fd = open(argv[1], O_RDONLY);
    if (src_fd < 0) {
        printf("cp: cannot open %s\n", argv[1]);
        return 1;
    }
    
    int dst_fd = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst_fd < 0) {
        printf("cp: cannot create %s\n", argv[2]);
        close(src_fd);
        return 1;
    }
    
    char buf[BUF_SIZE];
    ssize_t n;
    while ((n = read(src_fd, buf, BUF_SIZE)) > 0) {
        if (write(dst_fd, buf, n) != n) {
            printf("cp: write error\n");
            close(src_fd);
            close(dst_fd);
            return 1;
        }
    }
    
    close(src_fd);
    close(dst_fd);
    return 0;
}
