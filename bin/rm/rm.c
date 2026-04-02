#include <stdio.h>
#include <unistd.h>

int unlink(const char *pathname);

int main(int argc, char *argv[]) {
    if (argc < 2) {
        write(STDERR_FILENO, "rm: missing operand\n", 20);
        return 1;
    }
    
    for (int i = 1; i < argc; i++) {
        if (unlink(argv[i]) < 0) {
            printf("rm: cannot remove '%s'\n", argv[i]);
            return 1;
        }
    }
    
    return 0;
}
