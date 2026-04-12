#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (argc != 3) {
        write(STDERR_FILENO, "mv: usage: mv src dest\n", 24);
        return 1;
    }

    if (rename(argv[1], argv[2]) < 0) {
        printf("mv: cannot move '%s' to '%s'\n", argv[1], argv[2]);
        return 1;
    }

    return 0;
}
