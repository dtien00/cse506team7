#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("usage: rm <file>\n");
        return 1;
    }
    
    for (int i = 1; i < argc; i++) {
        if (unlink(argv[i]) < 0) {
            printf("rm: cannot remove %s\n", argv[i]);
            return 1;
        }
    }
    
    return 0;
}
