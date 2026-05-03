#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int copy_and_delete(const char *src, const char *dst) {
    int src_fd = open(src, O_RDONLY);
    if (src_fd < 0) {
        write(STDERR_FILENO, "mv: cannot open source\n", 23);
        return 1;
    }

    int dst_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
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
                unlink(dst);
                write(STDERR_FILENO, "mv: write error\n", 16);
                return 1;
            }
            written += w;
        }
    }

    close(src_fd);
    close(dst_fd);

    if (n < 0) {
        unlink(dst);
        write(STDERR_FILENO, "mv: read error\n", 15);
        return 1;
    }

    if (unlink(src) < 0) {
        write(STDERR_FILENO, "mv: cannot remove source\n", 25);
        return 1;
    }

    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        write(STDERR_FILENO, "mv: usage: mv src dest\n", 23);
        return 1;
    }

    if (strcmp(argv[1], argv[2]) == 0)
        return 0;

    /* Try atomic rename first */
    if (rename(argv[1], argv[2]) == 0)
        return 0;

    /* EXDEV = cross-device: fall back to copy+delete (files only) */
    if (errno == EXDEV) {
        struct stat st;
        if (stat(argv[1], &st) == 0 && S_ISDIR(st.st_mode)) {
            write(STDERR_FILENO, "mv: cannot move directory across devices\n", 41);
            return 1;
        }
        return copy_and_delete(argv[1], argv[2]);
    }

    printf("mv: cannot rename '%s': error %d\n", argv[1], errno);
    return 1;
}
