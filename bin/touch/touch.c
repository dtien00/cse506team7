#include <fcntl.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        write(STDERR_FILENO, "touch: missing operand\n", 23);
        return 1;
    }
    
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_CREAT | O_WRONLY, 0644);
        if (fd < 0) {
            write(STDERR_FILENO, "touch: cannot create file\n", 26);
            return 1;
        }
        close(fd);
    }
    
    return 0;
}
