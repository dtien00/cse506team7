#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    struct stat src_stat;
    struct stat dst_stat;

    if (argc != 3) {
        write(STDERR_FILENO, "mv: usage: mv src dest\n", 24);
        return 1;
    }

    if (strcmp(argv[1], argv[2]) == 0)
        return 0;

    if (stat(argv[1], &src_stat) == 0 && stat(argv[2], &dst_stat) == 0) {
        if (src_stat.st_dev == dst_stat.st_dev && src_stat.st_ino == dst_stat.st_ino)
            return 0;
    }

    int src_fd = open(argv[1], O_RDONLY);
    if (src_fd < 0) {
        write(STDERR_FILENO, "mv: cannot open source\n", 24);
        return 1;
    }

    int dst_fd = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst_fd < 0) {
        close(src_fd);
        write(STDERR_FILENO, "mv: cannot create destination\n", 30);
        return 1;
    }

    char buf[4096];
    ssize_t n;
    while ((n = read(src_fd, buf, sizeof(buf))) > 0) {
        ssize_t written = 0;
        while (written < n) {
            ssize_t w = write(dst_fd, buf + written, n - written);
            if (w <= 0) {
                close(src_fd);
                close(dst_fd);
                unlink(argv[2]);
                write(STDERR_FILENO, "mv: write error\n", 16);
                return 1;
            }
            written += w;
        }
    }

    if (n < 0) {
        close(src_fd);
        close(dst_fd);
        unlink(argv[2]);
        write(STDERR_FILENO, "mv: read error\n", 15);
        return 1;
    }

    close(src_fd);
    close(dst_fd);

    if (unlink(argv[1]) < 0) {
        write(STDERR_FILENO, "mv: cannot remove source\n", 26);
        return 1;
    }

    return 0;
}
