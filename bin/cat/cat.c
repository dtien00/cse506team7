#include "fcntl.h"
#include "unistd.h"

int main(int argc, char *argv[]) {
    char buf[512];
    int fd, n;

    if (argc < 2) {
        while ((n = read(STDIN_FILENO, buf, sizeof(buf))) > 0)
            write(STDOUT_FILENO, buf, n);
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            write(STDERR_FILENO, "cat: cannot open file\n", 22);
            continue;
        }
        while ((n = read(fd, buf, sizeof(buf))) > 0)
            write(STDOUT_FILENO, buf, n);
        close(fd);
    }
    return 0;
}
