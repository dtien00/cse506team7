#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "usage: chmod <mode> <file> [file...]\n");
        return 1;
    }
    char *end;
    mode_t mode = (mode_t)strtol(argv[1], &end, 8);
    if (*end != '\0') {
        fprintf(stderr, "chmod: invalid mode: %s\n", argv[1]);
        return 1;
    }
    int rc = 0;
    for (int i = 2; i < argc; i++) {
        if (chmod(argv[i], mode) < 0) {
            fprintf(stderr, "chmod: cannot change '%s'\n", argv[i]);
            rc = 1;
        }
    }
    return rc;
}
