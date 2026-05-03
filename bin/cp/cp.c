#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

int main(int argc, char *argv[]) {
    char buf[4096];
    int n;
    struct stat src_stat;
    struct stat dst_stat;

    if (argc != 3) {
        write(STDERR_FILENO, "cp: usage: cp src dest\n", 23);
        return 1;
    }

    if (stat(argv[1], &src_stat) < 0) {
        write(STDERR_FILENO, "cp: cannot open source\n", 23);
        return 1;
    }

    if (S_ISDIR(src_stat.st_mode)) {
        write(STDERR_FILENO, "cp: cannot copy directories\n", 28);
        return 1;
    }

    if (stat(argv[2], &dst_stat) == 0) {
        if (src_stat.st_dev == dst_stat.st_dev && src_stat.st_ino == dst_stat.st_ino) {
            return 0;
        }
    }

    int src_fd = open(argv[1], O_RDONLY);
    if (src_fd < 0) {
        write(STDERR_FILENO, "cp: cannot open source\n", 23);
        return 1;
    }

    int dst_fd = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst_fd < 0) {
        close(src_fd);
        write(STDERR_FILENO, "cp: cannot create destination\n", 30);
        return 1;
    }
    
    while ((n = read(src_fd, buf, sizeof(buf))) > 0) {
        if (write(dst_fd, buf, n) != n) {
            close(src_fd);
            close(dst_fd);
            write(STDERR_FILENO, "cp: write error\n", 16);
            return 1;
        }
    }
    
    close(src_fd);
    close(dst_fd);
    return 0;
}
